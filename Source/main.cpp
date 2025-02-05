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
#include <atomic>
#include <mutex>
#include <functional>
#include <future>
#include <chrono>

using namespace GuelderConsoleLog;
using namespace GuelderResourcesManager;

GE_DECLARE_LOG_CATEGORY_EXTERN(DPP, All, true, false, true);
GE_DEFINE_LOG_CATEGORY(DPP);

//Fucking Slave Discord Bot
GE_DECLARE_LOG_CATEGORY_EXTERN(FSDB, All, true, false, true);
GE_DEFINE_LOG_CATEGORY(FSDB);

#define LogInfo(...) GE_LOG(FSDB, Info, __VA_ARGS__)
#define LogWarning(...) GE_LOG(FSDB, Warning, __VA_ARGS__)
#define LogError(...) GE_LOG(FSDB, Error, __VA_ARGS__)

bool StringToBool(const std::string_view& str)
{
    if(str == "true")
        return true;
    else if(str == "false")
        return false;
    else
        GE_THROW("Failed to convert string to bool");
}
int StringToInt(const std::string_view& str)
{
    return std::atoi(str.data());
}

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
            const int64_t outNumberOfSamples = av_rescale_rnd(swr_get_delay(m_SwrContext.get(), m_CodecContext->sample_rate) + m_Frame->nb_samples, outSampleRate, m_CodecContext->sample_rate, AV_ROUND_UP);

            uint8_t* outputBuffer;
            /*int bufferSize = */av_samples_alloc(&outputBuffer, nullptr, m_CodecContext->ch_layout.nb_channels, outNumberOfSamples, outSampleFormat, 1);

            int convertedSamples = 0;
            GE_ASSERT((convertedSamples = swr_convert(m_SwrContext.get(), &outputBuffer, outNumberOfSamples, const_cast<const uint8_t**>(m_Frame->data), m_Frame->nb_samples)) > 0, "Failed to convert samples.");

            const size_t convertedSize = static_cast<size_t>(convertedSamples) * m_CodecContext->ch_layout.nb_channels * av_get_bytes_per_sample(outSampleFormat);

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
    //in seconds
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

template<class T>
struct Worker
{
private:
    static void DefaultDeleter() noexcept {}
public:
    Worker(const size_t& index, std::function<T()>&& work)
        :m_Index(index), m_HasWorkBeenStarted(false), m_Work(std::move(work)) {}
    Worker(Worker&& other) noexcept
        : m_Index(std::move(other.m_Index)), m_HasWorkBeenStarted(std::move(other.m_HasWorkBeenStarted)), m_Future(std::move(other.m_Future)), m_Work(std::move(other.m_Work)) {}

    Worker& operator=(Worker&& other) noexcept
    {
        m_Index = std::move(other.m_Index);
        m_Future = std::move(other.m_Future);
        m_Work = std::move(other.m_Work);
        m_HasWorkBeenStarted = std::move(other.m_HasWorkBeenStarted);

        return *this;
    }
    ~Worker()
    {
        if(m_Future.valid())
            m_Future.wait_for(std::chrono::milliseconds(0));
    }

    void Work()
    {
        m_HasWorkBeenStarted = true;
        m_Future = std::async(std::launch::async, m_Work);
    }

    //INDEX IS UNIQUE BUT CAN BE SHARED
    size_t GetIndex() const noexcept { return m_Index; }
    const std::future<T>& GetFuture() const { return m_Future; }
    T GetFutureResult() { return m_Future.get(); }
    //if true, the Worker is invalid and should be removed or moved
    bool HasWorkBeenStarted() const { return m_HasWorkBeenStarted; }

private:
    size_t m_Index;
    bool m_HasWorkBeenStarted;

    std::future<T> m_Future;
    std::function<T()> m_Work;
};
template<typename T>
class WorkersManager
{
public:
    WorkersManager() = default;
    explicit WorkersManager(const size_t& reserve)
        : m_WorkersCurrentIndex(0)
    {
        Reserve(reserve);
    }

    void Work()
    {
        std::lock_guard lock{ m_WorkersMutex };
        std::ranges::for_each(m_Workers, [](Worker<T>& w) { if(!w.HasWorkBeenStarted()) w.Work(); });
    }
    void Work(const size_t& index)
    {
        std::lock_guard lock{ m_WorkersMutex };
        const auto found = std::ranges::find_if(m_Workers, [&index](const Worker<T>& worker) { return worker.GetIndex() == index; });

        if(found != m_Workers.end())
            found->Work();
    }

    //returns workers id
    size_t AddWorker(const std::function<T()>& func, bool remove = false)
    {
        std::lock_guard lock{ m_WorkersMutex };

        m_Workers.emplace_back(
            m_WorkersCurrentIndex,
            [this, _func = func, index = m_WorkersCurrentIndex.load(), remove]
            {
                if constexpr(std::is_same_v<T, void>)
                {
                    _func();

                    if(remove)
                    {
                        std::thread removeThread([=] { RemoveWorker(index); });
                        removeThread.detach();
                    }
                }
                else
                {
                    auto result = _func();

                    if(remove)
                    {
                        std::thread removeThread([=] { RemoveWorker(index); });
                        removeThread.detach();
                    }

                    return result;
                }
            }
        );
        ++m_WorkersCurrentIndex;

        return m_WorkersCurrentIndex - 1;
    }
    //returns workers id
    size_t AddWorker(std::function<T()>&& func, bool remove = false)
    {
        std::lock_guard lock{ m_WorkersMutex };

        m_Workers.emplace_back(
            m_WorkersCurrentIndex,
            [this, _func = std::move(func), index = m_WorkersCurrentIndex.load(), remove]
            {
                if constexpr(std::is_same_v<T, void>)
                {
                    _func();

                    if(remove)
                    {
                        std::thread removeThread([=] { RemoveWorker(index); });
                        removeThread.detach();
                    }
                    LogInfo("ending ", index);
                }
                else
                {
                    auto result = _func();

                    if(remove)
                    {
                        std::thread removeThread([=] { RemoveWorker(index); });
                        removeThread.detach();
                    }

                    LogInfo("ending ", index);

                    return result;
                }
            }
        );
        ++m_WorkersCurrentIndex;

        return m_WorkersCurrentIndex - 1;
    }
    void RemoveWorker(const size_t& index)
    {
        std::lock_guard lock{ m_WorkersMutex };

        const auto found = std::ranges::find_if(m_Workers, [&index](const Worker<T>& worker) { return worker.GetIndex() == index; });

        if(found != m_Workers.end())
            m_Workers.erase(found);
        else
            GE_THROW("Failed to find worker with index ", index, '.');
    }

    void Reserve(const size_t& reserve)
    {
        std::lock_guard lock{ m_WorkersMutex };

        m_Workers.reserve(reserve);
    }

    //WARNING: it doesn't return the result momentally
    const std::vector<Worker<T>>& GetWorkers() const noexcept
    {
        std::lock_guard lock{ m_WorkersMutex };
        return m_Workers;
    }
    size_t GetCurrentWorkerIndex() const { return m_WorkersCurrentIndex.load(); }

    T GetWorkerFutureResult(const size_t& index)
    {
        std::lock_guard lock{ m_WorkersMutex };

        auto found = std::ranges::find_if(m_Workers, [&index](const Worker<T>& worker) { return worker.GetIndex() == index; });

        if(found != m_Workers.end())
            return found->GetFutureResult();
        else
            GE_THROW("Failed to find worker with index ", index, '.');
    }

private:
    std::atomic<size_t> m_WorkersCurrentIndex;
    std::vector<Worker<T>> m_Workers;
    mutable std::mutex m_WorkersMutex;
};

struct Command
{
public:
    Command(const std::string_view& name, const std::function<void(const dpp::message_create_t& message)>& func, const std::string_view& description = "")
        : func(func), name(name), description(description) {}
    Command(std::string&& name, std::function<void(const dpp::message_create_t& message)>&& func, std::string&& description = "")
        : func(std::move(func)), name(std::move(name)), description(std::move(description)) {}
    ~Command() = default;

    Command(const Command& other)
        : func(other.func), name(other.name), description(other.description) {}
    Command(Command&& other) noexcept
        : func(std::move(other.func)), name(std::move(other.name)), description(std::move(other.description)) {}

    void operator()(const dpp::message_create_t& message) const
    {
        func(message);
    }

    const std::function<void(const dpp::message_create_t& message)> func;
    const std::string name;
    const std::string description;
};

class DiscordBot : protected dpp::cluster
{
public:
    DiscordBot(const std::string_view& token, const std::string_view& prefix, uint32_t intents = dpp::i_all_intents)
        : dpp::cluster(token.data(), intents), m_Prefix(prefix) {}
    ~DiscordBot() override = default;

    void AddCommand(const Command& command)
    {
        m_Commands.push_back(command);
    }
    void AddCommand(Command&& command)
    {
        m_Commands.push_back(std::move(command));
    }
    void RegisterCommands()
    {
        m_WorkersManger.Reserve(m_Commands.size());

        on_message_create(
            [&](const dpp::message_create_t& message)
            {
                if(message.msg.author.id != me.id)
                {
                    const auto& content = message.msg.content;
                    for(auto& command : m_Commands)
                    {
                        const size_t found = content.find(command.name);
                        const size_t prefixSize = m_Prefix.size();

                        if(found != std::string::npos && found > 0 && found >= prefixSize && content.substr(found - prefixSize, prefixSize) == m_Prefix)
                        {
                            LogInfo("User with snowflake: ", message.msg.author.id, " has just called \"", command.name, "\" command.");
                            //use threads
                            //std::async(std::launch::async, [command, message]{command(message);});

                            const size_t id = m_WorkersManger.AddWorker([command, message] { command(message); }, true);

                            m_WorkersManger.Work(id);

                            //std::thread worker{[this]{}};

                            //std::thread commandThread{ [command, message] { command(message); } };
                            //commandThread.detach();
                        }
                    }
                }
            }
        );
    }

    void Run()
    {
        set_websocket_protocol(dpp::ws_etf);

        start(dpp::st_wait);
    }

protected:
    const std::string m_Prefix;
    std::vector<Command> m_Commands;

    WorkersManager<void> m_WorkersManger;
};

class FuckingSlaveDiscordBot : public DiscordBot
{
public:
    FuckingSlaveDiscordBot(const std::string_view& token, const std::string_view& prefix, const std::string& yt_dlpPath, uint32_t intents = dpp::i_all_intents)
        : DiscordBot(token, prefix, intents), m_EnableLogSentPackets(false), m_EnableLazyDecoding(true), m_SentPacketSize(150000)
    {
        this->on_voice_state_update(
            [this](const dpp::voice_state_update_t& voiceState)
            {
                if(voiceState.state.user_id == this->me.id)
                {
                    if(!voiceState.state.channel_id.empty())
                    {
                        m_IsJoined = true;
                        LogInfo("Has just joined to ", voiceState.state.channel_id, " channel in ", voiceState.state.guild_id, " guild.");
                    }
                    else
                    {
                        m_IsJoined = false;
                        m_IsPlaying = false;
                        LogInfo("Has just disconnected from voice channel.");
                    }

                    m_IsPaused = false;
                    m_JoinedCondition.notify_all();
                    m_PauseCondition.notify_all();
                }
            }
        );

        //help
        this->AddCommand({ "help",
            [this](const dpp::message_create_t& message)
            {
                std::stringstream outStream;

                std::ranges::for_each(this->m_Commands, [&outStream, this](const Command& command) { outStream << m_Prefix << command.name << " - " << command.description << '\n'; });

                message.reply(outStream.str());
            },
            "Prints out all available commands and also the command's description if it exists."
            });
        //play
        this->AddCommand({ "play",
            [this, yt_dlpPath](const dpp::message_create_t& message)
            {
                //join
                if(!m_IsJoined)
                {
                    if(!dpp::find_guild(message.msg.guild_id)->connect_member_voice(message.msg.author.id))
                        message.reply("You don't seem to be in a voice channel!");
                    //else
                        //message.reply("Joining your voice channel!");
                }

                const std::string inUrl = message.msg.content.substr(6);

                LogInfo("Received music url: ", inUrl);

                const std::string rawUrl = ResourcesManager::ExecuteCommand(Logger::Format(yt_dlpPath, " --flat-playlist -f bestaudio --get-url \"", inUrl, '\"'), 1)[0];
                //const std::string title = ResourcesManager::ExecuteCommand(Logger::Format(yt_dlpPath, " --print \"%(title)s\" \"", inUrl, '\"'))[0];

                //const auto _urlCount = ResourcesManager::ExecuteCommand(Logger::Format(yt_dlpPath, " --flat-playlist --print \"%(playlist_count)s\" \"", inUrl, '\"'), 1);
                //const size_t urlCount = (!_urlCount.empty() && !_urlCount[0].empty() && _urlCount[0] != "NA" ? std::atoi(_urlCount[0].data()) : 0);

                LogInfo("Received raw url to audio: ", rawUrl);

                std::unique_lock joinLock{m_JoinMutex};
                if(!m_JoinedCondition.wait_for(joinLock, std::chrono::seconds(10), [this] { return m_IsJoined == true; }))
                    return;

                dpp::voiceconn* v = this->get_shard(0)->get_voice(message.msg.guild_id);
                if(!v || !v->voiceclient || !v->voiceclient->is_ready())
                {
                    message.reply("There was an issue joining the voice channel. Please make sure I am in a channel.");
                    return;
                }

                std::lock_guard playbackLock{m_PlaybackMutex};

                m_IsPlaying = true;

                PlayAudio(v, rawUrl, m_SentPacketSize, m_EnableLogSentPackets, m_EnableLazyDecoding);
                /*for(size_t i = 1; i <= urlCount; ++i)
                {
                    const std::string currentUrl = ResourcesManager::ExecuteCommand(Logger::Format(yt_dlpPath, " --flat-playlist -f bestaudio --get-url --playlist-items", i, " \"", inUrl, '\"'))[0];
                    PlayAudio(v, rawUrl, bufferSize, logSentPackets);
                }*/
                m_IsPlaying = false;
                LogError("exiting playing audio");
            },
            "I should join your voice channel and play audio from youtube, soundcloud and all other stuff that yt-dlp supports."
            });
        //stop
        this->AddCommand({ "stop",
            [this](const dpp::message_create_t& message)
            {
                    dpp::voiceconn* v = this->get_shard(0)->get_voice(message.msg.guild_id);

                    if(!v)
                    {
                        LogError("this->get_shard(0)->get_voice(message.msg.guild_id) == nullptr for some reason");
                        return;
                    }

                    m_IsPlaying = false;
                    m_IsPaused = false;
                    v->voiceclient->pause_audio(m_IsPaused);

                    message.reply("Stopping all audio.");

                    v->voiceclient->stop_audio();
            },
            "Stops all audio"
            });
        //pause
        this->AddCommand({ "pause",
            [this](const dpp::message_create_t& message)
            {
                if(m_IsJoined)
                {
                    dpp::voiceconn* v = this->get_shard(0)->get_voice(message.msg.guild_id);

                    if(!v)
                    {
                        LogError("this->get_shard(0)->get_voice(message.msg.guild_id) == nullptr for some reason");
                        return;
                    }

                    m_IsPaused = !m_IsPaused;
                    m_PauseCondition.notify_all();
                    v->voiceclient->pause_audio(m_IsPaused);

                    message.reply(Logger::Format("Pause is ", (m_IsPaused ? "on" : "off"), '.'));
                }
                else
                    message.reply("Why should I pause?");
            },
            "Pauses the audio"
            });
        //leave
        this->AddCommand({ "leave",
            [this](const dpp::message_create_t& message)
            {
                dpp::voiceconn* voice = this->get_shard(0)->get_voice(message.msg.guild_id);

                if(voice && voice->voiceclient && voice->is_ready())
                {
                    m_IsPlaying = false;

                    //voice->voiceclient->stop_audio();
                    this->get_shard(0)->disconnect_voice(message.msg.guild_id);

                    message.reply(Logger::Format("Leaving."));
                }
                else
                    message.reply("I'm not in a voice channel");
            },
            "Disconnects from a voice channel if it is in it."
            });
        //terminate
        this->AddCommand({ "terminate",
            [this](const dpp::message_create_t& message)
            {
                if(message.msg.author.id == 465169363230523422)//k03440k
                {
                    //disconnect
                    dpp::voiceconn* voice = this->get_shard(0)->get_voice(message.msg.guild_id);

                    if(voice && voice->voiceclient && voice->is_ready())
                    {
                        this->get_shard(0)->disconnect_voice(message.msg.guild_id);
                    }

                    message.reply("Goodbye!");

                    //std::unique_lock lock{m_JoinMutex};
                    //m_JoinedCondition.wait_for(lock, std::chrono::milliseconds(500), [this] { return m_IsJoined == false; });

                    exit(0);
                    }
                }
            });
    }

    void AddLogger(const std::function<void(const dpp::log_t& log)>& logger) { this->on_log(logger); }

    //setters, getters
public:
    void SetEnableLogSentPackets(bool enable) { m_EnableLogSentPackets = enable; }
    void SetEnableLazyDecoding(bool enable) { m_EnableLazyDecoding = enable; }
    void SetSentPacketSize(const uint32_t& size) { m_SentPacketSize = size; }

    bool GetEnableLogSentPackets() const noexcept { return m_EnableLogSentPackets; }
    bool GetEnableLazyDecoding() const noexcept { return m_EnableLazyDecoding; }
    uint32_t GetSentPacketSize() const noexcept { return m_SentPacketSize; }

private:
    void PlayAudio(dpp::voiceconn* v, const std::string_view& url, uint32_t bufferSizeToSend, bool logSentPackets, bool lazyDecoding)
    {
        constexpr auto sampleFormat = AV_SAMPLE_FMT_S16;
        constexpr uint32_t sampleRate = 48000;

        Decoder decoder = Decoder(std::string(url));

        LogInfo("Total duration of audio: ", decoder.GetDuration(), "s.");

        decoder.PrepareForDecodingAudio(sampleFormat, sampleRate);

        std::vector<uint8_t> buffer;
        buffer.reserve(bufferSizeToSend);

        uint64_t totalReads = 0;
        uint64_t totalSentSize = 0;
        float totalDuration = 0;

        LogError(std::this_thread::get_id());

        while(decoder.AreThereFramesToProcess())
        {
            std::unique_lock pauseLock{ m_PauseMutex };
            m_PauseCondition.wait(pauseLock, [this] { return m_IsPaused == false; });

            if(m_IsPlaying)
            {
                auto out = decoder.DecodeAudioFrame(sampleFormat, sampleRate);

                buffer.insert(buffer.end(), out.begin(), out.end());

                if(buffer.size() >= bufferSizeToSend)
                {
                    if(lazyDecoding)
                    {
                        //so-called "lazy loading"
                        const auto startedTime = std::chrono::steady_clock::now();
                        const auto playingTime = std::chrono::seconds(static_cast<int>(v->voiceclient->get_secs_remaining() * .95f));

                        while(m_IsPlaying)
                        {
                            if(std::chrono::steady_clock::now() - startedTime >= playingTime)
                                break;

                            std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        }
                    }

                    v->voiceclient->send_audio_raw(reinterpret_cast<uint16_t*>(buffer.data()), buffer.size());

                    totalSentSize += buffer.size();
                    totalDuration += v->voiceclient->get_secs_remaining();

                    if(logSentPackets)
                        LogInfo("Sent ", buffer.size(), " bytes of data; totalNumberOfReadings: ", totalReads, " for; sent data lasts for ", v->voiceclient->get_secs_remaining(), "s.");

                    buffer.clear();
                }

                totalReads++;
            }
            else
                break;
        }

        if(m_IsPlaying && !buffer.empty())
        {
            if(lazyDecoding)
            {
                //so-called "lazy loading"
                const auto startedTime = std::chrono::steady_clock::now();
                const auto playingTime = std::chrono::seconds(static_cast<int>(v->voiceclient->get_secs_remaining() * .95f));

                while(m_IsPlaying)
                {
                    if(std::chrono::steady_clock::now() - startedTime >= playingTime)
                        break;

                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }

            v->voiceclient->send_audio_raw(reinterpret_cast<uint16_t*>(buffer.data()), buffer.size());

            totalSentSize += buffer.size();
            totalDuration += v->voiceclient->get_secs_remaining();

            if(logSentPackets)
                LogInfo("Sent ", buffer.size(), " last bytes of data that lasts for ", v->voiceclient->get_secs_remaining(), " seconds.");
        }

        LogWarning("Playback finished. Total number of reads: ", totalReads, " reads. Total size of sent data: ", totalSentSize, ". Total sent duration: ", totalDuration, '.');
    }

private:
    bool m_EnableLogSentPackets : 1;
    bool m_EnableLazyDecoding : 1;
    uint32_t m_SentPacketSize;

    std::atomic_bool m_IsJoined;
    std::condition_variable m_JoinedCondition;
    std::mutex m_JoinMutex;

    //this var is more likely should be named as m_IsDecodingAudio
    std::atomic_bool m_IsPlaying;
    std::mutex m_PlaybackMutex;

    std::atomic_bool m_IsPaused;
    std::condition_variable m_PauseCondition;
    std::mutex m_PauseMutex;
};

int main(int argc, char** argv)
{
    try
    {
        ResourcesManager resourcesManager{ argv[0] };

        const auto& botToken = resourcesManager.GetResourcesVariableContent("botToken");
        const auto& prefix = resourcesManager.GetResourcesVariableContent("prefix");
        const auto& yt_dlpPath = resourcesManager.GetResourcesVariableContent("yt_dlp");
        const auto logSentPackets = StringToBool(resourcesManager.GetResourcesVariableContent("logSentPackets"));
        const auto lazyPacketSend = StringToBool(resourcesManager.GetResourcesVariableContent("lazyPacketSend"));
        const auto sentPacketsSize = StringToInt(resourcesManager.GetResourcesVariableContent("maxPacketSize"));

        LogInfo("Found token: ", botToken);

        FuckingSlaveDiscordBot bot{ botToken, prefix, yt_dlpPath.data() };

        bot.SetEnableLogSentPackets(logSentPackets);
        bot.SetEnableLazyDecoding(lazyPacketSend);
        bot.SetSentPacketSize(sentPacketsSize);

        bot.AddLogger(BotLogger);

        bot.RegisterCommands();

        bot.Run();
    }
    catch(const std::exception& e)
    {
        LogError(e.what());
    }

#ifndef _DEBUG
    system("pause");
#endif

    return 0;
}

void BotLogger(const dpp::log_t& log)
{
    switch(log.severity)
    {
    case dpp::ll_debug:
    case dpp::ll_info:
        GE_LOG(DPP, Info, log.message);
        break;
    case dpp::ll_warning:
        GE_LOG(DPP, Warning, log.message);
        break;
    case dpp::ll_error:
    case dpp::ll_critical:
        GE_LOG(DPP, Error, log.message);
        break;
    default:
        break;
    }
}