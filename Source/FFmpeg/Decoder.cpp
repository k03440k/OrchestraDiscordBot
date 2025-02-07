#define NOMINMAX
#include "Decoder.hpp"

#include <string_view>
#include <GuelderConsoleLog.hpp>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include "../Utils.hpp"

namespace FSDB
{
    Decoder::Decoder(const std::string_view& url, const AVSampleFormat& outSampleFormat, const uint32_t& outSampleRate)
        : m_FormatContext(avformat_alloc_context(), FFmpegUniquePtrManager::FreeFormatContext),
        m_CodecContext(nullptr, FFmpegUniquePtrManager::FreeAVCodecContext),
        m_SwrContext(nullptr, FFmpegUniquePtrManager::FreeSwrContext),
        m_Packet(nullptr, FFmpegUniquePtrManager::FreeAVPacket),
        m_Frame(nullptr, FFmpegUniquePtrManager::FreeAVFrame),
        m_AudioStreamIndex(std::numeric_limits<uint32_t>::max()),
        m_SampleFormat(outSampleFormat),
        m_SampleRate(outSampleRate)
    {
        AVDictionary* options = nullptr;
        //av_dict_set(&options, "buffer_size", "10485760", 0); // 10 MB buffer
        //av_dict_set(&options, "rw_timeout", "50000000", 0);   // 5 seconds timeout
        av_dict_set(&options, "reconnect", "1", 0);
        av_dict_set(&options, "reconnect_streamed", "1", 0);
        av_dict_set(&options, "reconnect_delay_max", "4294", 0);
        av_dict_set(&options, "reconnect_max_retries", "9999", 0);
        av_dict_set(&options, "reconnect_on_network_error", "1", 0);
        av_dict_set(&options, "reconnect_on_http_error", "1", 0);
        av_dict_set(&options, "timeout", "2000000000", 0);

        auto ptr = m_FormatContext.get();
        GE_ASSERT(avformat_open_input(&ptr, url.data(), nullptr, &options) == 0, "Failed to open url: ", url);

        GE_ASSERT((avformat_find_stream_info(m_FormatContext.get(), nullptr)) >= 0, "Failed to retrieve stream info.");

        m_AudioStreamIndex = FindStreamIndex(AVMEDIA_TYPE_AUDIO);

        const AVCodecParameters* codecParameters = m_FormatContext->streams[m_AudioStreamIndex]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(codecParameters->codec_id);

        m_CodecContext = FFmpegUniquePtrManager::UniquePtrAVCodecContext(avcodec_alloc_context3(codec), FFmpegUniquePtrManager::FreeAVCodecContext);

        GE_ASSERT(avcodec_parameters_to_context(m_CodecContext.get(), codecParameters) >= 0, "Failed to copy codec parameters to codec context.");
        GE_ASSERT(avcodec_open2(m_CodecContext.get(), codec, nullptr) >= 0, "Failed to open codec through avcodec_open2.");

        m_SwrContext = FFmpegUniquePtrManager::UniquePtrSwrContext(swr_alloc(), FFmpegUniquePtrManager::FreeSwrContext);

        av_opt_set_chlayout(m_SwrContext.get(), "in_chlayout", &m_CodecContext->ch_layout, 0);
        av_opt_set_chlayout(m_SwrContext.get(), "out_chlayout", &m_CodecContext->ch_layout, 0);
        av_opt_set_int(m_SwrContext.get(), "in_sample_rate", m_CodecContext->sample_rate, 0);
        av_opt_set_int(m_SwrContext.get(), "out_sample_rate", m_SampleRate, 0);
        av_opt_set_sample_fmt(m_SwrContext.get(), "in_sample_fmt", m_CodecContext->sample_fmt, 0);
        av_opt_set_sample_fmt(m_SwrContext.get(), "out_sample_fmt", m_SampleFormat, 0);

        GE_ASSERT(swr_init(m_SwrContext.get()) >= 0, "Failed to initialize swrContext");

        m_Packet = FFmpegUniquePtrManager::UniquePtrAVPacket(av_packet_alloc(), FFmpegUniquePtrManager::FreeAVPacket);
        m_Frame = FFmpegUniquePtrManager::UniquePtrAVFrame(av_frame_alloc(), FFmpegUniquePtrManager::FreeAVFrame);
    }
    Decoder::Decoder(const Decoder& other)
        : m_FormatContext(CloneUniquePtr(other.m_FormatContext)),
        m_CodecContext(CloneUniquePtr(other.m_CodecContext)),
        m_SwrContext(DuplicateSwrContext(&*other.m_SwrContext), FFmpegUniquePtrManager::FreeSwrContext),
        m_Packet(CloneUniquePtr(other.m_Packet)),
        m_Frame(CloneUniquePtr(other.m_Frame)),
        m_AudioStreamIndex(other.m_AudioStreamIndex),
        m_SampleFormat(other.m_SampleFormat),
        m_SampleRate(other.m_SampleRate)
    {}
    Decoder& Decoder::operator=(const Decoder& other)
    {
        *m_FormatContext = *other.m_FormatContext;
        *m_CodecContext = *other.m_CodecContext;
        *m_Packet = *other.m_Packet;
        *m_Frame = *other.m_Frame;

        CopySwrParams(&*other.m_SwrContext, &*m_SwrContext);

        m_AudioStreamIndex = other.m_AudioStreamIndex;
        m_SampleFormat = other.m_SampleFormat;
        m_SampleRate = other.m_SampleRate;

        return *this;
    }

    std::vector<uint8_t> Decoder::DecodeAudioFrame() const
    {
        GE_ASSERT(avcodec_send_packet(m_CodecContext.get(), m_Packet.get()) >= 0, "Failed to send a packet to the decoder.");

        std::vector<uint8_t> out;
        out.reserve((GetMaxBufferSize() <= 0 ? 1024 : GetMaxBufferSize()));

        if(avcodec_receive_frame(m_CodecContext.get(), m_Frame.get()) == 0)
        {
            const int64_t outNumberOfSamples = av_rescale_rnd(swr_get_delay(m_SwrContext.get(), m_CodecContext->sample_rate) + m_Frame->nb_samples, m_SampleRate, m_CodecContext->sample_rate, AV_ROUND_UP);

            uint8_t* outputBuffer;
            /*int bufferSize = */av_samples_alloc(&outputBuffer, nullptr, m_CodecContext->ch_layout.nb_channels, outNumberOfSamples, m_SampleFormat, 1);

            int convertedSamples = 0;
            GE_ASSERT((convertedSamples = swr_convert(m_SwrContext.get(), &outputBuffer, outNumberOfSamples, const_cast<const uint8_t**>(m_Frame->data), m_Frame->nb_samples)) > 0, "Failed to convert samples.");

            const size_t convertedSize = static_cast<size_t>(convertedSamples) * m_CodecContext->ch_layout.nb_channels * av_get_bytes_per_sample(m_SampleFormat);

            out.insert(out.begin(), outputBuffer, outputBuffer + convertedSize);

            av_free(outputBuffer);
        }

        av_packet_unref(m_Packet.get());

        return out;
    }
    uint32_t Decoder::FindStreamIndex(const AVMediaType& mediaType) const
    {
        if(m_AudioStreamIndex == std::numeric_limits<uint32_t>::max())
            for(uint32_t i = 0; i < m_FormatContext->nb_streams; i++)
            {
                const AVCodecParameters* codecParameters = m_FormatContext->streams[i]->codecpar;

                if(codecParameters->codec_type == mediaType)
                    return i;
            }
        else
            return m_AudioStreamIndex;

        GE_THROW("Failed to find ", mediaType, " media type.");
    }
    void Decoder::SetSampleFormat(const AVSampleFormat& sampleFormat)
    {
        m_SampleFormat = sampleFormat;
    }
    void Decoder::SetSampleRate(const uint32_t& sampleRate)
    {
        m_SampleRate = sampleRate;
    }

    bool Decoder::AreThereFramesToProcess() const
    {
        return (av_read_frame(m_FormatContext.get(), m_Packet.get()) == 0);
    }
    //in seconds
    int64_t Decoder::GetDuration() const
    {
        return m_FormatContext->duration / AV_TIME_BASE;
    }
    int Decoder::GetMaxBufferSize() const
    {
        return av_samples_get_buffer_size(nullptr, m_CodecContext->ch_layout.nb_channels, m_CodecContext->frame_size, m_SampleFormat, 0);
    }
    void Decoder::CopySwrParams(SwrContext* from, SwrContext* to)
    {
        //copy swr
        AVChannelLayout inChannelLayout;
        AVChannelLayout outChannelLayout;
        int64_t inSampleRate = 0;
        int64_t outSampleRate = 0;
        AVSampleFormat inSampleFormat = AV_SAMPLE_FMT_NONE;
        AVSampleFormat outSampleFormat = AV_SAMPLE_FMT_NONE;

        // Retrieve parameters
        av_opt_get_chlayout(from, "in_chlayout", 0, &inChannelLayout);
        av_opt_get_chlayout(from, "out_chlayout", 0, &outChannelLayout);
        av_opt_get_int(from, "in_sample_rate", 0, &inSampleRate);
        av_opt_get_int(from, "out_sample_rate", 0, &outSampleRate);
        av_opt_get_sample_fmt(from, "in_sample_fmt", 0, &inSampleFormat);
        av_opt_get_sample_fmt(from, "out_sample_fmt", 0, &outSampleFormat);

        // Set parameters
        av_opt_set_chlayout(to, "in_chlayout", &inChannelLayout, 0);
        av_opt_set_chlayout(to, "out_chlayout", &outChannelLayout, 0);
        av_opt_set_int(to, "in_sample_rate", inSampleRate, 0);
        av_opt_set_int(to, "out_sample_rate", outSampleRate, 0);
        av_opt_set_sample_fmt(to, "in_sample_fmt", inSampleFormat, 0);
        av_opt_set_sample_fmt(to, "out_sample_fmt", outSampleFormat, 0);
    }
    SwrContext* Decoder::DuplicateSwrContext(SwrContext* from)
    {
        SwrContext* out = swr_alloc();

        //copy swr
        AVChannelLayout inChannelLayout;
        AVChannelLayout outChannelLayout;
        int64_t inSampleRate = 0;
        int64_t outSampleRate = 0;
        AVSampleFormat inSampleFormat = AV_SAMPLE_FMT_NONE;
        AVSampleFormat outSampleFormat = AV_SAMPLE_FMT_NONE;

        // Retrieve parameters
        av_opt_get_chlayout(from, "in_chlayout", 0, &inChannelLayout);
        av_opt_get_chlayout(from, "out_chlayout", 0, &outChannelLayout);
        av_opt_get_int(from, "in_sample_rate", 0, &inSampleRate);
        av_opt_get_int(from, "out_sample_rate", 0, &outSampleRate);
        av_opt_get_sample_fmt(from, "in_sample_fmt", 0, &inSampleFormat);
        av_opt_get_sample_fmt(from, "out_sample_fmt", 0, &outSampleFormat);

        // Set parameters
        av_opt_set_chlayout(out, "in_chlayout", &inChannelLayout, 0);
        av_opt_set_chlayout(out, "out_chlayout", &outChannelLayout, 0);
        av_opt_set_int(out, "in_sample_rate", inSampleRate, 0);
        av_opt_set_int(out, "out_sample_rate", outSampleRate, 0);
        av_opt_set_sample_fmt(out, "in_sample_fmt", inSampleFormat, 0);
        av_opt_set_sample_fmt(out, "out_sample_fmt", outSampleFormat, 0);

        GE_ASSERT(swr_init(out) >= 0, "Failed to initialize swrContext");

        return out;
    }
}