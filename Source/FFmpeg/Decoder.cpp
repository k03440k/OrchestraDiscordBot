#define NOMINMAX
#include "Decoder.hpp"

#include <string_view>
#include <GuelderConsoleLog.hpp>
#include <map>

#include "GuelderResourcesManager.hpp"

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
}

#include "../Utils.hpp"

//public
namespace Orchestra
{
    Decoder::Decoder()
        : m_FormatContext(nullptr, FFmpegUniquePtrManager::FreeFormatContext),
        m_CodecContext(nullptr, FFmpegUniquePtrManager::FreeAVCodecContext),
        m_SwrContext(nullptr, FFmpegUniquePtrManager::FreeSwrContext),
        m_Packet(nullptr, FFmpegUniquePtrManager::FreeAVPacket),
        m_Frame(nullptr, FFmpegUniquePtrManager::FreeAVFrame),
        m_FilterGraph(nullptr, FFmpegUniquePtrManager::FreeAVFilterGraph),
        m_MaxBufferSize(0),
        m_AudioStreamIndex(std::numeric_limits<uint32_t>::max()),
        m_OutSampleFormat(AV_SAMPLE_FMT_NONE),
        m_OutSampleRate(0) {}
    Decoder::Decoder(const std::string_view& url, int outSampleRate, AVSampleFormat outSampleFormat)
        : m_FormatContext(nullptr, FFmpegUniquePtrManager::FreeFormatContext),
        m_CodecContext(nullptr, FFmpegUniquePtrManager::FreeAVCodecContext),
        m_SwrContext(nullptr, FFmpegUniquePtrManager::FreeSwrContext),
        m_Packet(nullptr, FFmpegUniquePtrManager::FreeAVPacket),
        m_Frame(nullptr, FFmpegUniquePtrManager::FreeAVFrame),
        m_FilterGraph(nullptr, FFmpegUniquePtrManager::FreeAVFilterGraph),
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

        ResetGraph();
    }
    Decoder::Decoder(const Decoder& other)
        : m_FormatContext(CloneUniquePtr(other.m_FormatContext)),
        m_CodecContext(CloneUniquePtr(other.m_CodecContext)),
        m_SwrContext(DuplicateSwrContext(other.m_SwrContext.get()), FFmpegUniquePtrManager::FreeSwrContext),
        m_Packet(CloneUniquePtr(other.m_Packet)),
        m_Frame(CloneUniquePtr(other.m_Frame)),
        m_FilterGraph(CloneUniquePtr(other.m_FilterGraph)),
        m_Filters(other.m_Filters),
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
        *m_FilterGraph = *other.m_FilterGraph;
        m_Filters = other.m_Filters;

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
            FFmpegUniquePtrManager::UniquePtrAVFrame allocatedFrame = FFmpegUniquePtrManager::UniquePtrAVFrame(nullptr, FFmpegUniquePtrManager::FreeAVFrame);

            AVFrame* frame = nullptr;

            //if(m_FilterGraph)
            {
                O_ASSERT(av_buffersrc_add_frame(m_Filters.bufferSource, m_Frame.get()) >= 0, "Failed to apply filter to the frame.");

                allocatedFrame.reset(av_frame_alloc());

                frame = allocatedFrame.get();

                O_ASSERT(frame != nullptr, "Failed to initialize a frame, during filter process.");

                O_ASSERT(av_buffersink_get_frame(m_Filters.bufferSink, frame) >= 0, "Failed to receive a frame from filter sink.");
            }
            //else
                //frame = m_Frame.get();

            const int64_t outNumberOfSamples = av_rescale_rnd(swr_get_delay(m_SwrContext.get(), m_CodecContext->sample_rate) + frame->nb_samples, m_OutSampleRate, m_CodecContext->sample_rate, AV_ROUND_UP);

            uint8_t* outputBuffer;
            /*int bufferSize = */av_samples_alloc(&outputBuffer, nullptr, m_CodecContext->ch_layout.nb_channels, outNumberOfSamples, m_OutSampleFormat, 1);

            int convertedSamples = 0;
            O_ASSERT((convertedSamples = swr_convert(m_SwrContext.get(), &outputBuffer, outNumberOfSamples, const_cast<const uint8_t**>(frame->data), frame->nb_samples)) > 0, "Failed to convert samples.");

            const size_t convertedSize = static_cast<size_t>(convertedSamples) * m_CodecContext->ch_layout.nb_channels * av_get_bytes_per_sample(m_OutSampleFormat);

            out.insert(out.begin(), outputBuffer, outputBuffer + convertedSize);

            av_free(outputBuffer);
        }

        av_packet_unref(m_Packet.get());

        return out;
    }

    void Decoder::SkipToTimestamp(int64_t timestamp) const
    {
        O_ASSERT(timestamp >= 0 && (timestamp * GetTimestampToSecondsRatio() * AV_TIME_BASE) <= GetTotalDuration(), "The skipping timestamp ", timestamp, " is bigger or lesser than duration.");//TODO

        O_ASSERT(avformat_seek_file(m_FormatContext.get(), m_AudioStreamIndex, std::numeric_limits<int>::min(), timestamp, std::numeric_limits<int>::max(), AVSEEK_FLAG_BACKWARD) >= 0, "Failed to skip to timestamp ", timestamp);

        avcodec_flush_buffers(m_CodecContext.get());
    }
    void Decoder::SkipTimestamp(int64_t timestamp) const
    {
        SkipToTimestamp(timestamp + GetCurrentTimestamp());
    }
    void Decoder::SkipToSeconds(float seconds) const
    {
        SkipToTimestamp(seconds / GetTimestampToSecondsRatio());
    }
    void Decoder::SkipSeconds(float seconds) const
    {
        SkipTimestamp(seconds / GetTimestampToSecondsRatio());
    }

    void Decoder::ResetGraph()
    {
        m_FilterGraph.reset(avfilter_graph_alloc());

        //sourceBuffer
        std::string args;
        args = GuelderConsoleLog::Logger::Format("time_base=", m_FormatContext->streams[m_AudioStreamIndex]->time_base.num, '/', m_FormatContext->streams[m_AudioStreamIndex]->time_base.den, ":sample_rate=", m_CodecContext->sample_rate, ":sample_fmt=", av_get_sample_fmt_name(m_CodecContext->sample_fmt), ":channel_layout=", m_CodecContext->ch_layout.u.mask);

        m_Filters.bufferSource = CreateFilterContext("abuffer", nullptr, "in", args);
        m_Filters.bass = CreateFilterContext("bass", m_Filters.bufferSource);
        m_Filters.equalizer = CreateFilterContext("firequalizer", m_Filters.bass);
        //m_Filters.limiter = CreateFilterContext("alimiter", m_Filters.equalizer, "alimiter", "0.9");
        m_Filters.bufferSink = CreateFilterContext("abuffersink", m_Filters.equalizer, "out");

        O_ASSERT(avfilter_graph_config(m_FilterGraph.get(), nullptr) >= 0, "Failed to configure filter graph.");
    }

    uint32_t Decoder::FindStreamIndex(AVMediaType mediaType) const
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
    void Decoder::Reset()
    {
        m_FormatContext.reset();
        m_CodecContext.reset();
        m_SwrContext.reset();
        m_Packet.reset();
        m_Frame.reset();
        m_FilterGraph.reset();
        m_Filters.bufferSource = nullptr;
        m_Filters.bass = nullptr;
        m_Filters.bufferSink = nullptr;

        m_MaxBufferSize = 0;
        m_AudioStreamIndex = std::numeric_limits<uint32_t>::max();
        m_OutSampleFormat = AV_SAMPLE_FMT_NONE;
        m_OutSampleRate = 0;
    }
    bool Decoder::IsReady() const
    {
        return m_FormatContext && m_CodecContext && m_SwrContext && m_MaxBufferSize > 0 && m_AudioStreamIndex != std::numeric_limits<uint32_t>::max() && m_OutSampleFormat != AV_SAMPLE_FMT_NONE;
    }
}
//getters, setters
namespace Orchestra
{
    void Decoder::SetBassBoost(float decibelsBoost, float frequencyToAdjust, float bandwidth) const
    {
        using namespace GuelderConsoleLog;

        O_ASSERT(avfilter_graph_send_command(m_FilterGraph.get(), "bass", "g", Logger::Format(decibelsBoost).c_str(), nullptr, 0, 0) >= 0, "Failed to set \"g\" parameter to \"bass\" filter");
        O_ASSERT(avfilter_graph_send_command(m_FilterGraph.get(), "bass", "f", Logger::Format(frequencyToAdjust).c_str(), nullptr, 0, 0) >= 0, "Failed to set \"f\" parameter to \"bass\" filter");
        O_ASSERT(avfilter_graph_send_command(m_FilterGraph.get(), "bass", "w", Logger::Format(bandwidth).c_str(), nullptr, 0, 0) >= 0, "Failed to set \"w\" parameter to \"bass\" filter");
    }

    void Decoder::SetEqualizer(const std::string_view& args) const
    {
        O_ASSERT(avfilter_graph_send_command(m_FilterGraph.get(), "firequalizer", "gain_entry", args.data(), nullptr, 0, 0) >= 0, "Failed to set \"gain\" parameter to \"equalizer\" filter");
    }
    void Decoder::SetEqualizer(const std::map<float, float>& frequencies) const
    {
        using namespace GuelderConsoleLog;

        std::string args;

        if(frequencies.empty())
            args = '0';
        else
        {
            auto it = frequencies.begin();

            args += Logger::Format("entry(", it->first, ", ", it->second, ")");

            ++it;

            for(; it != frequencies.end(); ++it)
                args += Logger::Format(";entry(", it->first, ", ", it->second, ')');
        }

        SetEqualizer(args);
    }

    int Decoder::GetInitialSampleRate() const
    {
        return m_CodecContext->sample_rate;
    }
    AVSampleFormat Decoder::GetInitialSampleFormat() const
    {
        return m_CodecContext->sample_fmt;
    }

    void Decoder::SetOutSampleFormat(AVSampleFormat sampleFormat)
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
    void Decoder::SetOutSampleRate(int sampleRate)
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

    std::string Decoder::GetTitle() const
    {
        const AVDictionaryEntry* tag = av_dict_get(m_FormatContext->metadata, "title", nullptr, 0);

        if(tag)
            return tag->value;
        else
            return std::string{};
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

    AVFilterContext* Decoder::CreateFilterContext(const std::string_view& filterNameToFind, AVFilterContext* link, const std::string_view& customName, const std::string_view& args) const
    {
        const AVFilter* filter = avfilter_get_by_name(filterNameToFind.data());
        O_ASSERT(filter, "Failed to find filter with filterNameToFind ", filterNameToFind);
        AVFilterContext* filterContext = nullptr;

        O_ASSERT(avfilter_graph_create_filter(&filterContext, filter, customName.empty() ? filterNameToFind.data() : customName.data(), args.data(), nullptr, m_FilterGraph.get()) >= 0, "Failed to create filter with filterNameToFind", filterNameToFind);

        if(link)
            O_ASSERT(!avfilter_link(link, 0, filterContext, 0), "Failed to link filter with filterNameToFind ", filterNameToFind);

        return filterContext;
    }
}