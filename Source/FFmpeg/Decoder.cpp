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

//public
namespace Orchestra
{
    Decoder::Decoder(const std::string_view& url, const AVSampleFormat& outSampleFormat, const uint32_t& outSampleRate)
        : m_FormatContext(nullptr/*avformat_alloc_context()*/, FFmpegUniquePtrManager::FreeFormatContext),
        m_CodecContext(nullptr, FFmpegUniquePtrManager::FreeAVCodecContext),
        m_SwrContext(nullptr, FFmpegUniquePtrManager::FreeSwrContext),
        m_Packet(nullptr, FFmpegUniquePtrManager::FreeAVPacket),
        m_Frame(nullptr, FFmpegUniquePtrManager::FreeAVFrame),
        m_AudioStreamIndex(std::numeric_limits<uint32_t>::max()),
        m_OutSampleFormat(outSampleFormat),
        m_OutSampleRate(outSampleRate)
    {
        av_log_set_level(AV_LOG_WARNING);

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

        AVFormatContext* f = avformat_alloc_context();

        O_ASSERT(f, "Failed to initialize format context.");

        //WTF?! why when I use m_FormatContext as ptr it crashes, but when a default ptr it works fine!!????
        //auto ptr = m_FormatContext.get();
        auto ptr = f;
        O_ASSERT(avformat_open_input(&ptr, url.data(), nullptr, &options) == 0, "Failed to open url: ", url);

        O_ASSERT((avformat_find_stream_info(ptr, nullptr)) >= 0, "Failed to retrieve stream info.");

        m_FormatContext.reset(f);
        f = nullptr;

        m_AudioStreamIndex = FindStreamIndex(AVMEDIA_TYPE_AUDIO);

        const AVCodecParameters* codecParameters = m_FormatContext->streams[m_AudioStreamIndex]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(codecParameters->codec_id);

        m_CodecContext = FFmpegUniquePtrManager::UniquePtrAVCodecContext(avcodec_alloc_context3(codec), FFmpegUniquePtrManager::FreeAVCodecContext);

        O_ASSERT(avcodec_parameters_to_context(m_CodecContext.get(), codecParameters) >= 0, "Failed to copy codec parameters to codec context.");
        O_ASSERT(avcodec_open2(m_CodecContext.get(), codec, nullptr) >= 0, "Failed to open codec through avcodec_open2.");

        m_SwrContext = FFmpegUniquePtrManager::UniquePtrSwrContext(swr_alloc(), FFmpegUniquePtrManager::FreeSwrContext);

        av_opt_set_chlayout(m_SwrContext.get(), "in_chlayout", &m_CodecContext->ch_layout, 0);
        av_opt_set_chlayout(m_SwrContext.get(), "out_chlayout", &m_CodecContext->ch_layout, 0);
        av_opt_set_int(m_SwrContext.get(), "in_sample_rate", m_CodecContext->sample_rate, 0);
        av_opt_set_int(m_SwrContext.get(), "out_sample_rate", m_OutSampleRate, 0);
        av_opt_set_sample_fmt(m_SwrContext.get(), "in_sample_fmt", m_CodecContext->sample_fmt, 0);
        av_opt_set_sample_fmt(m_SwrContext.get(), "out_sample_fmt", m_OutSampleFormat, 0);

        O_ASSERT(swr_init(m_SwrContext.get()) >= 0, "Failed to initialize swrContext");

        m_Packet = FFmpegUniquePtrManager::UniquePtrAVPacket(av_packet_alloc(), FFmpegUniquePtrManager::FreeAVPacket);
        m_Frame = FFmpegUniquePtrManager::UniquePtrAVFrame(av_frame_alloc(), FFmpegUniquePtrManager::FreeAVFrame);

        m_MaxBufferSize = av_samples_get_buffer_size(nullptr, m_CodecContext->ch_layout.nb_channels, m_CodecContext->frame_size, m_CodecContext->sample_fmt, 0);
        if(m_MaxBufferSize < 0)
            m_MaxBufferSize = 1024;
    }
    Decoder::Decoder(const Decoder& other)
        : m_FormatContext(CloneUniquePtr(other.m_FormatContext)),
        m_CodecContext(CloneUniquePtr(other.m_CodecContext)),
        m_SwrContext(DuplicateSwrContext(other.m_SwrContext.get()), FFmpegUniquePtrManager::FreeSwrContext),
        m_Packet(CloneUniquePtr(other.m_Packet)),
        m_Frame(CloneUniquePtr(other.m_Frame)),
        m_MaxBufferSize(other.m_MaxBufferSize),
        m_AudioStreamIndex(other.m_AudioStreamIndex),
        m_OutSampleFormat(other.m_OutSampleFormat),
        m_OutSampleRate(other.m_OutSampleRate)
    {}
    Decoder& Decoder::operator=(const Decoder& other)
    {
        *m_FormatContext = *other.m_FormatContext;
        *m_CodecContext = *other.m_CodecContext;
        *m_Packet = *other.m_Packet;
        *m_Frame = *other.m_Frame;

        CopySwrParams(other.m_SwrContext.get(), m_SwrContext.get());

        m_MaxBufferSize = other.m_MaxBufferSize;
        m_AudioStreamIndex = other.m_AudioStreamIndex;
        m_OutSampleFormat = other.m_OutSampleFormat;
        m_OutSampleRate = other.m_OutSampleRate;

        return *this;
    }

    std::vector<uint8_t> Decoder::DecodeAudioFrame() const
    {
        O_ASSERT(avcodec_send_packet(m_CodecContext.get(), m_Packet.get()) >= 0, "Failed to send a packet to the decoder.");

        std::vector<uint8_t> out;
        out.reserve(m_MaxBufferSize);

        if(avcodec_receive_frame(m_CodecContext.get(), m_Frame.get()) == 0)
        {
            const int64_t outNumberOfSamples = av_rescale_rnd(swr_get_delay(m_SwrContext.get(), m_CodecContext->sample_rate) + m_Frame->nb_samples, m_OutSampleRate, m_CodecContext->sample_rate, AV_ROUND_UP);

            uint8_t* outputBuffer;
            /*int bufferSize = */av_samples_alloc(&outputBuffer, nullptr, m_CodecContext->ch_layout.nb_channels, outNumberOfSamples, m_OutSampleFormat, 1);

            int convertedSamples = 0;
            O_ASSERT((convertedSamples = swr_convert(m_SwrContext.get(), &outputBuffer, outNumberOfSamples, const_cast<const uint8_t**>(m_Frame->data), m_Frame->nb_samples)) > 0, "Failed to convert samples.");

            const size_t convertedSize = static_cast<size_t>(convertedSamples) * m_CodecContext->ch_layout.nb_channels * av_get_bytes_per_sample(m_OutSampleFormat);

            out.insert(out.begin(), outputBuffer, outputBuffer + convertedSize);

            av_free(outputBuffer);
        }

        av_packet_unref(m_Packet.get());

        return out;
    }

    void Decoder::SkipToTimestamp(const int64_t& timestamp) const
    {
        O_ASSERT(timestamp >= 0 && (timestamp * GetTimestampToSecondsRatio() * AV_TIME_BASE) <= GetTotalDuration(), "The skipping timestamp ", timestamp, " is bigger or lesser than duration.");//TODO

        O_ASSERT(avformat_seek_file(m_FormatContext.get(), m_AudioStreamIndex, std::numeric_limits<int>::min(), timestamp, std::numeric_limits<int>::max(), AVSEEK_FLAG_BACKWARD) >= 0, "Failed to skip to timestamp ", timestamp);

        avcodec_flush_buffers(m_CodecContext.get());
    }
    void Decoder::SkipTimestamp(const int64_t& timestamp) const
    {
        SkipToTimestamp(timestamp + GetCurrentTimestamp());
    }
    void Decoder::SkipToSeconds(const float& seconds) const
    {
        /*O_ASSERT(seconds <= GetTotalDurationSeconds(), "The duration of audio is lesser than ", seconds, "s.");

        const int64_t seekTimestamp = av_rescale_q(seconds * AV_TIME_BASE, AV_TIME_BASE_Q, m_FormatContext->streams[m_AudioStreamIndex]->time_base);

        O_ASSERT(avformat_seek_file(m_FormatContext.get(), m_AudioStreamIndex, std::numeric_limits<int>::min(), seekTimestamp, std::numeric_limits<int>::max(), AVSEEK_FLAG_BACKWARD) >= 0, "Failed to skip ", seconds, "s.");

        avcodec_flush_buffers(m_CodecContext.get());*/
        SkipToTimestamp(seconds / GetTimestampToSecondsRatio());
    }
    void Decoder::SkipSeconds(const float& seconds) const
    {
        /*O_ASSERT(seconds <= GetTotalDurationSeconds(), "The duration of audio is lesser than ", seconds, "s.");

        const int64_t seekTimestamp = av_rescale_q(m_Frame->pts + seconds * AV_TIME_BASE, AV_TIME_BASE_Q, m_FormatContext->streams[m_AudioStreamIndex]->time_base);

        O_ASSERT(avformat_seek_file(m_FormatContext.get(), m_AudioStreamIndex, std::numeric_limits<int>::min(), seekTimestamp, std::numeric_limits<int>::max(), AVSEEK_FLAG_BACKWARD) >= 0, "Failed to skip ", seconds, "s.");

        avcodec_flush_buffers(m_CodecContext.get());*/
        SkipTimestamp(seconds / GetTimestampToSecondsRatio());
    }

    uint32_t Decoder::FindStreamIndex(const AVMediaType& mediaType) const
    {
        if(m_AudioStreamIndex == std::numeric_limits<uint32_t>::max())
            for(uint32_t i = 0; i < m_FormatContext->nb_streams; i++)
            {
                if(const AVCodecParameters* codecParameters = m_FormatContext->streams[i]->codecpar; codecParameters->codec_type == mediaType)
                    return i;
            }
        else
            return m_AudioStreamIndex;
    }

    bool Decoder::AreThereFramesToProcess() const
    {
        return (av_read_frame(m_FormatContext.get(), m_Packet.get()) == 0);
    }
}
//getters, setters
namespace Orchestra
{
    int Decoder::GetInitialSampleRate() const
    {
        return m_CodecContext->sample_rate;
    }
    AVSampleFormat Decoder::GetInitialSampleFormat() const
    {
        return m_CodecContext->sample_fmt;
    }

    void Decoder::SetOutSampleFormat(const AVSampleFormat& sampleFormat)
    {
        m_OutSampleFormat = sampleFormat;

        m_SwrContext.reset();

        m_SwrContext = FFmpegUniquePtrManager::UniquePtrSwrContext(swr_alloc(), FFmpegUniquePtrManager::FreeSwrContext);

        av_opt_set_chlayout(m_SwrContext.get(), "in_chlayout", &m_CodecContext->ch_layout, 0);
        av_opt_set_chlayout(m_SwrContext.get(), "out_chlayout", &m_CodecContext->ch_layout, 0);
        av_opt_set_int(m_SwrContext.get(), "in_sample_rate", m_CodecContext->sample_rate, 0);
        av_opt_set_int(m_SwrContext.get(), "out_sample_rate", m_OutSampleRate, 0);
        av_opt_set_sample_fmt(m_SwrContext.get(), "in_sample_fmt", m_CodecContext->sample_fmt, 0);
        av_opt_set_sample_fmt(m_SwrContext.get(), "out_sample_fmt", m_OutSampleFormat, 0);

        O_ASSERT(swr_init(m_SwrContext.get()) >= 0, "Failed to initialize swrContext");
    }
    void Decoder::SetOutSampleRate(const uint32_t& sampleRate)
    {
        m_OutSampleRate = sampleRate;

        m_SwrContext.reset();

        m_SwrContext = FFmpegUniquePtrManager::UniquePtrSwrContext(swr_alloc(), FFmpegUniquePtrManager::FreeSwrContext);

        av_opt_set_chlayout(m_SwrContext.get(), "in_chlayout", &m_CodecContext->ch_layout, 0);
        av_opt_set_chlayout(m_SwrContext.get(), "out_chlayout", &m_CodecContext->ch_layout, 0);
        av_opt_set_int(m_SwrContext.get(), "in_sample_rate", m_CodecContext->sample_rate, 0);
        av_opt_set_int(m_SwrContext.get(), "out_sample_rate", m_OutSampleRate, 0);
        av_opt_set_sample_fmt(m_SwrContext.get(), "in_sample_fmt", m_CodecContext->sample_fmt, 0);
        av_opt_set_sample_fmt(m_SwrContext.get(), "out_sample_fmt", m_OutSampleFormat, 0);

        O_ASSERT(swr_init(m_SwrContext.get()) >= 0, "Failed to initialize swrContext");
    }

    AVSampleFormat Decoder::GetOutSampleFormat() const
    {
        return m_OutSampleFormat;
    }
    int Decoder::GetOutSampleRate() const
    {
        return m_OutSampleRate;
    }

    int64_t Decoder::GetTotalDuration() const
    {
        return m_FormatContext->duration;
    }
    //in seconds
    float Decoder::GetTotalDurationSeconds() const
    {
        return static_cast<float>(GetTotalDuration()) / AV_TIME_BASE;
    }

    int Decoder::GetMaxBufferSize() const
    {
        return m_MaxBufferSize;
    }

    int64_t Decoder::GetCurrentTimestamp() const
    {
        return m_Frame->pts;
    }

    double Decoder::GetTimestampToSecondsRatio() const
    {
        return av_q2d(GetStream()->time_base);
    }
}
//private
namespace Orchestra
{
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

        CopySwrParams(from, out);

        O_ASSERT(swr_init(out) >= 0, "Failed to initialize swrContext");

        return out;
    }

    AVStream* Decoder::GetStream() const
    {
        return m_FormatContext->streams[FindStreamIndex(AVMEDIA_TYPE_AUDIO)];
    }
}