//ONLY RELEASE WORKS
//Holy fucking shit, ONLY THIS ORDER OF INCLUDES
#if 1
#include <GuelderResourcesManager.hpp>
#include <dpp/dpp.h>
#include <GuelderConsoleLog.hpp>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <thread>
#include <filesystem>

GE_DECLARE_LOG_CATEGORY_EXTERN(DiscordBot, All, true, false, true);
GE_DEFINE_LOG_CATEGORY(DiscordBot);

void BotLogger(const dpp::log_t& log);

class Decoder
{
public:
    using UniquePtrAVFormatContext = std::unique_ptr<AVFormatContext, void(*)(AVFormatContext*)>;
    using UniquePtrAVCodecContext = std::unique_ptr<AVCodecContext, void(*)(AVCodecContext*)>;
    using UniquePtrSwrContext = std::unique_ptr<SwrContext, void(*)(SwrContext*)>;
    using UniquePtrAVPacket = std::unique_ptr<AVPacket, void(*)(AVPacket*)>;
    using UniquePtrAVFrame = std::unique_ptr<AVFrame, void(*)(AVFrame*)>;

public:
    Decoder(const std::string_view& url)
        : m_FormatContext(UniquePtrAVFormatContext(avformat_alloc_context(), FreeFormatContext)),
        m_CodecContext(nullptr, FreeAVCodecContext), m_SwrContext(nullptr, FreeSwrContext), m_Packet(nullptr, FreeAVPacket), m_Frame(nullptr, FreeAVFrame), m_AudioStreamIndex(std::numeric_limits<uint32_t>::max())
    {
        AVDictionary* options = nullptr;
        av_dict_set(&options, "buffer_size", "10485760", 0); // 10 MB buffer
        av_dict_set(&options, "rw_timeout", "5000000", 0);   // 5 seconds timeout

        auto ptr = m_FormatContext.get();
        GE_ASSERT(avformat_open_input(&ptr, url.data(), nullptr, &options) == 0, "Failed to open url: ", url);

        GE_ASSERT((avformat_find_stream_info(m_FormatContext.get(), nullptr)) >= 0, "Failed to retrieve stream info.");

        m_AudioStreamIndex = FindStreamIndex(AVMEDIA_TYPE_AUDIO);

        AVCodecParameters* codecParameters = m_FormatContext->streams[m_AudioStreamIndex]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(codecParameters->codec_id);

        m_CodecContext = UniquePtrAVCodecContext(avcodec_alloc_context3(codec), FreeAVCodecContext);

        GE_ASSERT(avcodec_parameters_to_context(m_CodecContext.get(), codecParameters) >= 0, "Failed to copy codec parameters to codec context.");
        GE_ASSERT(avcodec_open2(m_CodecContext.get(), codec, nullptr) >= 0, "Failed to open codec through avcodec_open2.");

        m_SwrContext = UniquePtrSwrContext(swr_alloc(), FreeSwrContext);

        m_Packet = UniquePtrAVPacket(av_packet_alloc(), FreeAVPacket);
        m_Frame = UniquePtrAVFrame(av_frame_alloc(), FreeAVFrame);
    }
    ~Decoder() = default;

    void PrepareForDecodingAudio(const AVSampleFormat& outSampleFormat, const uint32_t& outSampleRate) const
    {
        av_opt_set_chlayout(m_SwrContext.get(), "in_chlayout", &m_CodecContext->ch_layout, 0);
        av_opt_set_chlayout(m_SwrContext.get(), "out_chlayout", &m_CodecContext->ch_layout, 0);
        av_opt_set_int(m_SwrContext.get(), "in_sample_rate", m_CodecContext->sample_rate, 0);
        av_opt_set_int(m_SwrContext.get(), "out_sample_rate", outSampleRate, 0);
        av_opt_set_sample_fmt(m_SwrContext.get(), "in_sample_fmt", m_CodecContext->sample_fmt, 0);
        av_opt_set_sample_fmt(m_SwrContext.get(), "out_sample_fmt", outSampleFormat, 0);

        GE_ASSERT(swr_init(m_SwrContext.get()) >= 0, "Failed to initialize swrContext");
    }
    std::vector<uint8_t> DecodeAudioFrame(const AVSampleFormat& outSampleFormat, const uint32_t& outSampleRate) const
    {
        GE_ASSERT(avcodec_send_packet(m_CodecContext.get(), m_Packet.get()) >= 0, "Failed to send a packet to the decoder.");

        std::vector<uint8_t> out;
        out.reserve((GetMaxBufferSize(outSampleFormat) <= 0 ? 1024 : GetMaxBufferSize(outSampleFormat)));

        if(avcodec_receive_frame(m_CodecContext.get(), m_Frame.get()) == 0)
        {
            const int outNumberOfSamples = av_rescale_rnd(swr_get_delay(m_SwrContext.get(), m_CodecContext->sample_rate) + m_Frame->nb_samples, outSampleRate, m_CodecContext->sample_rate, AV_ROUND_UP);

            uint8_t* outputBuffer;
            /*int bufferSize = */av_samples_alloc(&outputBuffer, nullptr, m_CodecContext->ch_layout.nb_channels, outNumberOfSamples, outSampleFormat, 1);

            int convertedSamples = 0;
            GE_ASSERT((convertedSamples = swr_convert(m_SwrContext.get(), &outputBuffer, outNumberOfSamples, const_cast<const uint8_t**>(m_Frame->data), m_Frame->nb_samples)) > 0, "Failed to convert samples.");

            const size_t convertedSize = convertedSamples * m_CodecContext->ch_layout.nb_channels * av_get_bytes_per_sample(outSampleFormat);

            out.insert(out.begin(), outputBuffer, outputBuffer + convertedSize);

            av_free(outputBuffer);
        }

        av_packet_unref(m_Packet.get());

        return out;
    }
    uint32_t FindStreamIndex(const AVMediaType& mediaType) const
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

    bool AreThereFramesToProcess() const { return (av_read_frame(m_FormatContext.get(), m_Packet.get()) == 0); }
    int64_t GetDuration() const { return m_FormatContext->duration / AV_TIME_BASE; }
    int GetMaxBufferSize(const AVSampleFormat& sampleFormat) const
    {
        return av_samples_get_buffer_size(nullptr, m_CodecContext->ch_layout.nb_channels, m_CodecContext->frame_size, sampleFormat, 0);
    }
private:
    UniquePtrAVFormatContext m_FormatContext;
    UniquePtrAVCodecContext m_CodecContext;
    UniquePtrSwrContext m_SwrContext;
    UniquePtrAVPacket m_Packet;
    UniquePtrAVFrame m_Frame;

    uint32_t m_AudioStreamIndex;
private:
    static void FreeFormatContext(AVFormatContext* formatContext)
    {
        if(formatContext)
        {
            avformat_close_input(&formatContext);
            avformat_free_context(formatContext);
        }
    }
    static void FreeAVCodecContext(AVCodecContext* codecContext)
    {
        if(codecContext)
            avcodec_free_context(&codecContext);
    }
    static void FreeSwrContext(SwrContext* swrContext)
    {
        if(swrContext)
            swr_free(&swrContext);
    }
    static void FreeAVPacket(AVPacket* packet)
    {
        if(packet)
            av_packet_free(&packet);
    }
    static void FreeAVFrame(AVFrame* frame)
    {
        if(frame)
            av_frame_free(&frame);
    }
};

int main(int argc, char** argv)
{
    using namespace GuelderConsoleLog;
    using namespace GuelderResourcesManager;
    try
    {
        ResourcesManager resourcesManager{ argv[0] };

        const auto& t = resourcesManager.GetResourcesVariableContent("botToken");

        LogInfo("Found token: ", t);

        dpp::cluster bot{ t.data(), dpp::i_all_intents };

        bot.on_log(BotLogger);

        bot.on_message_create([&](const dpp::message_create_t& message)
            {
                if(message.msg.content == "!ping")
                    message.reply("pong!");
                if(message.msg.content == "!join")
                {
                    if(!dpp::find_guild(message.msg.guild_id)->connect_member_voice(message.msg.author.id))
                        message.reply("You don't seem to be in a voice channel!");
                    else
                        message.reply("Joining your voice channel!");
                }
                if(message.msg.content == "!leave")
                {
                    dpp::voiceconn* voice = bot.get_shard(0)->get_voice(message.msg.guild_id);

                    if(voice && voice->voiceclient && voice->is_ready())
                        bot.get_shard(0)->disconnect_voice(message.msg.guild_id);
                    else
                        message.reply("I'm not in a voice channel");
                }
                if(message.msg.content.find("!play") != std::string::npos)
                {
                    const std::string youtubeUrl = message.msg.content.substr(6);

                    const auto yt_dlpPath = resourcesManager.GetResourcesVariableContent("yt_dlp");

                    constexpr std::string_view tempFileName = "temp_audio_file.m4a";

                    if(std::filesystem::exists(tempFileName))
                        std::filesystem::remove(tempFileName);

                    const auto url = resourcesManager.ExecuteCommand(Logger::Format(yt_dlpPath, " -f bestaudio -o ", tempFileName, " \"", youtubeUrl, "\""));
                    //const auto url = ExecuteCommand(Logger::Format(yt_dlpPath, " -f bestaudio --get-url \"", youtubeUrl, '\"'));

                    dpp::voiceconn* v = bot.get_shard(0)->get_voice(message.msg.guild_id);
                    if(!v || !v->voiceclient || !v->voiceclient->is_ready())
                    {
                        LogWarning("There was an issue with getting the voice channel. Make sure I'm in a voice channel!");
                        //return;
                    }

                    const uint32_t bufferSizeToSend = atoi(resourcesManager.GetResourcesVariableContent("maxPacketSize").data());
                    constexpr auto sampleFormat = AV_SAMPLE_FMT_S16;
                    constexpr uint32_t sampleRate = 48000;

                    uint64_t totalNumberOfReadings = 0;

                    Decoder decoder = Decoder(tempFileName);
                    //Decoder decoder = Decoder(url);

                    decoder.PrepareForDecodingAudio(sampleFormat, sampleRate);

                    std::vector<uint8_t> buffer;
                    buffer.reserve(bufferSizeToSend);

                    while(decoder.AreThereFramesToProcess())
                    {
                        auto out = std::move(decoder.DecodeAudioFrame(sampleFormat, sampleRate));

                        buffer.insert(buffer.begin() + buffer.size(), out.begin(), out.end());

                        if(buffer.size() >= bufferSizeToSend)
                        {
                            LogInfo("Sending ", buffer.size(), " bytes of data; totalNumberOfReadings: ", totalNumberOfReadings);
                            v->voiceclient->send_audio_raw(reinterpret_cast<uint16_t*>(buffer.data()), buffer.size());
                            buffer.clear();
                        }

                        totalNumberOfReadings++;
                    }

                    if(!buffer.empty())
                    {
                        LogInfo("Sending ", buffer.size(), " last bytes of data.");
                        v->voiceclient->send_audio_raw(reinterpret_cast<uint16_t*>(buffer.data()), buffer.size());

                        totalNumberOfReadings++;
                    }

                    LogInfo("Sent all audio data with ", totalNumberOfReadings, " number of readings.");
                }
            });

        bot.set_websocket_protocol(dpp::ws_etf);

        bot.start(dpp::st_wait);
    }
    catch(const std::exception& e)
    {
        LogError(e.what());
    }

#ifndef GE_DEBUG
    system("pause");
#endif

    return 0;
    }

void BotLogger(const dpp::log_t& log)
{
    switch(log.severity)
    {
        //case dpp::ll_trace:
    case dpp::ll_debug:
    case dpp::ll_info:
        GE_LOG(DiscordBot, Info, log.message);
        break;
    case dpp::ll_warning:
        GE_LOG(DiscordBot, Warning, log.message);
        break;
    case dpp::ll_error:
    case dpp::ll_critical:
        GE_LOG(DiscordBot, Error, log.message);
        break;
    default:
        //GE_THROW("An unknown logging category");
        break;
    }
}
#endif