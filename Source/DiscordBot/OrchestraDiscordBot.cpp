#define NOMINMAX

#include "OrchestraDiscordBot.hpp"

#include <atomic>
#include <mutex>
#include <future>
#include <chrono>
#include <string_view>

#include <dpp/dpp.h>

#include <GuelderResourcesManager.hpp>

#include "../Utils.hpp"
#include "DiscordBot.hpp"
#include "Player.hpp"

namespace Orchestra
{
    using namespace GuelderConsoleLog;
    using namespace GuelderResourcesManager;

    OrchestraDiscordBot::OrchestraDiscordBot(const std::string_view& token, const std::string_view& yt_dlpPath, const std::string_view& prefix, const char& paramPrefix, uint32_t intents)
        : DiscordBot(token, prefix, paramPrefix, intents), m_Player(200000, true, false), m_AdminSnowflake(0), m_yt_dlpPath(yt_dlpPath)
    {
        this->on_voice_state_update(
            [this](const dpp::voice_state_update_t& voiceState)
            {
                if(voiceState.state.user_id == this->me.id)
                {
                    if(!voiceState.state.channel_id.empty())
                    {
                        m_IsJoined = true;
                        GE_LOG(Orchestra, Info, "Has just joined to ", voiceState.state.channel_id, " channel in ", voiceState.state.guild_id, " guild.");
                    }
                    else
                    {
                        m_IsJoined = false;
                        m_Player.Stop();
                        GE_LOG(Orchestra, Info, "Has just disconnected from voice channel.");
                    }

                    m_JoinedCondition.notify_all();
                    m_Player.Pause(false);
                }
            }
        );

        //TODO:
        //support of commands like: "!play -speed .4" so to finish the Param system - DONE
        //add support of playlists
        //add support of streaming audio from raw url, like from google drive - PROBABLY DONE
        //add support of skipping a certain time - DONE
        //add gui?
        //add support of looking for url depending on the name so that message contain only the name, for instance, "!play "Linkpark: Numb"" - DONE
        //make first decoding faster then next so to make play almost instantly
        //make beauty and try optimize all possible things - PARTIALLY
        //add possibility of making bass boost?
        //rename to Orchestra and find an avatar - DONE

        //help
        this->AddCommand({ "help",
            std::bind(&OrchestraDiscordBot::CommandHelp, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {},
            "Prints out all available commands and also the command's description if it exists."
            });
        //play
        this->AddCommand({ "play",
            std::bind(&OrchestraDiscordBot::CommandPlay, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{"speed", Type::Float, "Changes speed of audio. If speed < 1: audio plays faster. This param can be called while audio is playing."},
                ParamProperties{"repeat", Type::Int, Logger::Format("Repeats audio for certain number. If repeat < 0: the audio will be playing for ", std::numeric_limits<int>::max(), " times.")},
                //ParamProperties{"index", Type::Int},
                ParamProperties{"search", Type::Bool, "If search is explicitly set: it will search or not search via yt-dlp."},
                ParamProperties{"searchengine", Type::String, "A certain search engine that will be used to find url. Supported: yt - Youtube(default), sc - SoundCloud."},
                ParamProperties{"noinfo", Type::Bool, "If noinfo is true: the info(name, url(optional), duration) about track won't be sent."},
                ParamProperties{"raw", Type::Bool, "If raw is false: it won't use yt-dlp for finding a raw url to audio."},
            },
            "Joins your voice channel and play audio from youtube, soundcloud and all other stuff that yt-dlp supports."
            });
        //stop
        this->AddCommand({ "stop",
            std::bind(&OrchestraDiscordBot::CommandStop, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {},
            "Stops all audio"
            });
        //pause
        this->AddCommand({ "pause",
            std::bind(&OrchestraDiscordBot::CommandPause, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {},
            "Pauses the audio"
            });
        //skip
        this->AddCommand({ "skip",
            std::bind(&OrchestraDiscordBot::CommandSkip, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{"secs", Type::Float, "Skips given amount of seconds."},
                ParamProperties{"tosecs", Type::Float, "If tosecs < 0: it will skip to the beginning of the audio. Skips up to the given time."}
            },
            "Skips current track"
            });
        //leave
        this->AddCommand({ "leave",
            std::bind(&OrchestraDiscordBot::CommandLeave, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {},
            "Disconnects from a voice channel."
            });
        //terminate
        this->AddCommand({ "terminate",
            std::bind(&OrchestraDiscordBot::CommandTerminate, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {},
            "Terminates the bot. Only admin can use this command."
            });
    }

    void OrchestraDiscordBot::CommandHelp(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value) const
    {
        std::stringstream outStream;

        for(auto&& command : m_Commands)
        {
            outStream << m_Prefix << "**" << command.name << "**";

            if(!command.description.empty())
                outStream << " - " << command.description;

            outStream << '\n';

            if(!command.paramsProperties.empty())
            {
                outStream << "\tParams:\n";
                for(auto&& paramProperties : command.paramsProperties)
                {
                    outStream << "\t\t" << '[' << TypeToString(paramProperties.type) << "] " << "**" << paramProperties.name << "**";

                    if(!paramProperties.description.empty())
                        outStream << " - " << paramProperties.description;

                    outStream << '\n';
                }
            }
        }

        message.reply(outStream.str());
    }
    void OrchestraDiscordBot::CommandPlay(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        if(!value.empty())
        {
            //join
            if(!m_IsJoined)
                if(!dpp::find_guild(message.msg.guild_id)->connect_member_voice(message.msg.author.id))
                {
                    message.reply("You don't seem to be in a voice channel!");
                    return;
                }

            m_IsStopped = false;

            bool raw = false;
            GetParamValue(params, "raw", raw);

            GE_LOG(Orchestra, Info, L"Received music value: ", GuelderConsoleLog::StringToWString(value));

            std::string rawUrl;

            std::string inSearchEngine = "yt";
            GetParamValue(params, "searchengine", inSearchEngine);

            if(!raw)
            {
                std::vector<std::string> receivedUrls;

                bool usingSearch = !IsValidURL(value.data());
                GetParamValue(params, "search", usingSearch);

                if(inSearchEngine != "yt" && inSearchEngine != "sc")
                {
                    inSearchEngine = "yt";
                    message.reply(Logger::Format(inSearchEngine, " is not supported as a searching engine. Using yt as a searching engine."));
                }
                std::wstring wInSearchEngine = GuelderConsoleLog::StringToWString(inSearchEngine);

                //need rework
                {
                    std::string call;

                    if(usingSearch)
                    {
                        std::wstring wValue = GuelderConsoleLog::StringToWString(value);
                        std::wstring wYt_dlpPath = GuelderConsoleLog::StringToWString(m_yt_dlpPath);

                        //std::wstring wCall = Logger::Format(wYt_dlpPath, L" \"ytsearch:", wValue, L"\" --flat-playlist -f bestaudio --get-url");
                        std::wstring wCall = Logger::Format(std::move(wYt_dlpPath), L" \"", wInSearchEngine, L"search:", std::move(wValue), L"\" -f bestaudio --get-url");

                        receivedUrls = ResourcesManager::ExecuteCommand<wchar_t, char>(wCall, 1);

                        O_ASSERT(!receivedUrls.empty(), "Failed to find track with ", inSearchEngine, " search engine: ", value, '.');

                        //call = Logger::Format(m_yt_dlpPath, " --flat-playlist -f bestaudio --get-url \"", receivedUrls[0], '\"');
                    }
                    else
                    {
                        call = Logger::Format(m_yt_dlpPath, " -f bestaudio --get-url \"", value, '\"');
                        receivedUrls = ResourcesManager::ExecuteCommand(call, 1);
                    }
                }

                bool yt_dlpSuccess = !receivedUrls.empty();

                //O_ASSERT(yt_dlpSuccess, "Failed to get raw url from ", value, '.');
                if(!yt_dlpSuccess)
                    GE_LOG(Orchestra, Warning, "Failed to get raw url from ", value, ". Trying to use it as already a raw url.");

                rawUrl = !yt_dlpSuccess ? value : receivedUrls[0];

                GE_LOG(Orchestra, Info, "Received raw url to audio: ", rawUrl);

                bool noInfo = false;
                GetParamValue(params, "noinfo", noInfo);

                //TODO: rework: it is too slow
                if(yt_dlpSuccess && !noInfo)
                {
                    std::thread discordInfo(
                        [&, wInSearchEngine, this]
                        {
                            std::string ytUrl;
                            ytUrl.reserve(44);
                            if(usingSearch)
                            {
                                std::wstring wValue = GuelderConsoleLog::StringToWString(value);
                                std::wstring wYt_dlpPath = GuelderConsoleLog::StringToWString(m_yt_dlpPath);

                                //to get yt url
                                std::wstring wCall = Logger::Format(wYt_dlpPath, L" \"", wInSearchEngine, L"search:", wValue, L"\" --flat-playlist -f bestaudio --get-url");

                                ytUrl = ResourcesManager::ExecuteCommand<wchar_t, char>(wCall, 1)[0];

                                if(!m_Player.GetIsDecoding())
                                    return;
                            }
                            else
                                ytUrl = value;

                            std::wstring title = ResourcesManager::ExecuteCommand<char, wchar_t>(Logger::Format(m_yt_dlpPath, " --print \"%(title)s\" \"", ytUrl, '\"'))[0];

                            if(!m_Player.GetIsDecoding())
                                return;

                            std::string duration = ResourcesManager::ExecuteCommand(Logger::Format(m_yt_dlpPath, " --get-duration ", ytUrl))[0];

                            if(!m_Player.GetIsDecoding())
                                return;

                            auto m = Logger::Format(L"**", title, L"** is going to be played for ", GuelderConsoleLog::StringToWString(duration));

                            if(usingSearch)
                                m += Logger::Format(L"\nURL: ", GuelderConsoleLog::StringToWString(ytUrl));

                            message.reply(GuelderConsoleLog::WStringToString(m));
                        });
                    discordInfo.detach();
                }
            }
            else
            {
                rawUrl = value;
                if(!IsValidURL(rawUrl.data()) && message.msg.author.id != m_AdminSnowflake)
                {
                    message.reply("You don't have permission to access admin's local files.");
                    return;
                }
            }

            //joining stuff
            std::unique_lock joinLock{ m_JoinMutex };
            if(!m_JoinedCondition.wait_for(joinLock, std::chrono::seconds(2), [this] { return m_IsJoined == true; }))
                return;

            //waiting until connected
            {
                const auto startedTime = std::chrono::steady_clock::now();

                while(true)
                {
                    dpp::voiceconn* v = this->get_shard(0)->get_voice(message.msg.guild_id);
                    if((v && v->voiceclient && v->voiceclient->is_ready()) || std::chrono::steady_clock::now() - startedTime >= std::chrono::seconds(2))
                        break;

                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }

            dpp::voiceconn* v = this->get_shard(0)->get_voice(message.msg.guild_id);
            if(!v || !v->voiceclient || !v->voiceclient->is_ready())
            {
                message.reply("There was an issue joining the voice channel. Please make sure I am in a channel.");
                return;
            }

            float speed = 1.f;
            GetParamValue(params, "speed", speed);

            int repeat = 1;
            GetParamValue(params, "repeat", repeat);
            if(repeat < 0)
                repeat = std::numeric_limits<int>::max();

            std::lock_guard lock{ m_PlayMutex };

            if(!m_IsStopped)
            {
                for(size_t i = 0; i < repeat && !m_IsStopped; ++i)
                {
                    m_Player.AddDecoderBack(rawUrl, Decoder::DEFAULT_SAMPLE_RATE * speed);
                    m_Player.DecodeAndSendAudio(v);

                    if(m_Player.GetDecodersCount())
                        m_Player.DeleteAudio();
                }
                if(m_Player.GetDecodersCount())
                    m_Player.DeleteAllAudio();
            }
            //const std::wstring title = StringToWString(ResourcesManager::ExecuteCommand(Logger::Format(m_CallYt_dlpPath, " --print \"%(title)s\" \"", rawUrl, '\"'))[0]);
            //message.reply(WStringToString(Logger::Format(L"The url is going to be played: ", title)));
            //const auto _urlCount = ResourcesManager::ExecuteCommand(Logger::Format(yt_dlpPath, " --flat-playlist --print \"%(playlist_count)s\" \"", inUrl, '\"'), 1);
            //const size_t urlCount = (!_urlCount.empty() && !_urlCount[0].empty() && _urlCount[0] != "NA" ? std::atoi(_urlCount[0].data()) : 0);
            /*for(size_t i = 1; i <= urlCount; ++i)
            {
                const std::string currentUrl = ResourcesManager::ExecuteCommand(Logger::Format(yt_dlpPath, " --flat-playlist -f bestaudio --get-url --playlist-items", i, " \"", inUrl, '\"'))[0];
                DecodeAndSendAudio(v, rawUrl, bufferSize, logSentPackets);
            }*/
        }
        else
        {
            if(m_Player.GetDecodersCount())
            {
                float speed = 0.f;
                GetParamValue(params, "speed", speed);

                if(speed > 0.f)
                {
                    int index = 0;
                    //GetParamValue(params, "index", index);

                    //if(index < 0)
                    //    index = 0;

                    m_Player.SetAudioSampleRate(Decoder::DEFAULT_SAMPLE_RATE * speed, index);
                }
            }
        }
    }
    void OrchestraDiscordBot::CommandStop(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        dpp::voiceconn* v = this->get_shard(0)->get_voice(message.msg.guild_id);

        O_ASSERT(v && v->voiceclient && v->voiceclient->is_ready(), "Failed to stop.");

        m_Player.Stop();
        m_IsStopped = true;

        v->voiceclient->pause_audio(m_Player.GetIsPaused());
        v->voiceclient->stop_audio();
    }
    void OrchestraDiscordBot::CommandPause(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        if(m_IsJoined)
        {
            dpp::voiceconn* v = this->get_shard(0)->get_voice(message.msg.guild_id);

            O_ASSERT(v && v->voiceclient && v->voiceclient->is_ready(), "Failed to pause.");

            m_Player.Pause(!m_Player.GetIsPaused());
            v->voiceclient->pause_audio(m_Player.GetIsPaused());

            message.reply(Logger::Format("Pause is ", (m_Player.GetIsPaused() ? "on" : "off"), '.'));
        }
        else
            message.reply("Why should I pause?");
    }
    void OrchestraDiscordBot::CommandSkip(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        float tosecs = 0.f;
        GetParamValue(params, "tosecs", tosecs);
        float secs = 0.f;
        if(tosecs <= 0.f)
            GetParamValue(params, "secs", secs);

        dpp::voiceconn* v = this->get_shard(0)->get_voice(message.msg.guild_id);

        O_ASSERT(v && v->voiceclient && v->voiceclient->is_ready(), "Failed to skip.");

        if(m_Player.GetDecodersCount() == 0)
            return;

        if(tosecs == 0.f && secs == 0.f)
        {
            v->voiceclient->stop_audio();
            m_Player.Skip();
        }
        else
        {
            if(tosecs > 0.f)
                m_Player.SkipToSeconds(tosecs, 0);
            else
                m_Player.SkipSeconds(secs - v->voiceclient->get_secs_remaining(), 0);//DOESN'T work properly
        }
    }
    void OrchestraDiscordBot::CommandLeave(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        dpp::voiceconn* voice = this->get_shard(0)->get_voice(message.msg.guild_id);

        if(voice && voice->voiceclient && voice->is_ready())
        {
            m_Player.Stop();

            voice->voiceclient->stop_audio();
            this->get_shard(0)->disconnect_voice(message.msg.guild_id);

            message.reply(Logger::Format("Leaving."));
        }
        else
            message.reply("I'm not in a voice channel");
    }
    void OrchestraDiscordBot::CommandTerminate(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        if(m_AdminSnowflake != 0 && message.msg.author.id == m_AdminSnowflake)
        {
            //disconnect
            dpp::voiceconn* voice = this->get_shard(0)->get_voice(message.msg.guild_id);

            if(voice && voice->voiceclient && voice->is_ready())
                this->get_shard(0)->disconnect_voice(message.msg.guild_id);

            message.reply("Goodbye!");

            std::unique_lock lock{ m_JoinMutex };
            m_JoinedCondition.wait_for(lock, std::chrono::milliseconds(500), [this] { return m_IsJoined == false; });

            exit(0);
        }
        else
            message.reply("You are not an admin!");
    }

    void OrchestraDiscordBot::SetEnableLogSentPackets(const bool& enable)
    {
        m_Player.SetEnableLogSentPackets(enable);
    }
    void OrchestraDiscordBot::SetEnableLazyDecoding(const bool& enable)
    {
        m_Player.SetEnableLazyDecoding(enable);
    }
    void OrchestraDiscordBot::SetSentPacketSize(const uint32_t& size)
    {
        m_Player.SetSentPacketSize(size);
    }
    void OrchestraDiscordBot::SetAdminSnowflake(const dpp::snowflake& id)
    {
        m_AdminSnowflake = id;
    }

    bool OrchestraDiscordBot::GetEnableLogSentPackets() const noexcept
    {
        return m_Player.GetEnableLogSentPackets();
    }
    bool OrchestraDiscordBot::GetEnableLazyDecoding() const noexcept
    {
        return m_Player.GetEnableLazyDecoding();
    }
    uint32_t OrchestraDiscordBot::GetSentPacketSize() const noexcept
    {
        return m_Player.GetSentPacketSize();
    }
    dpp::snowflake OrchestraDiscordBot::GetAdminSnowflake() const noexcept
    {
        return m_AdminSnowflake;
    }
}