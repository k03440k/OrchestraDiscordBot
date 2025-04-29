#define NOMINMAX

#include "OrchestraDiscordBot.hpp"

#include <atomic>
#include <mutex>
#include <future>
#include <chrono>
#include <string_view>

#include <dpp/dpp.h>

#include <rapidjson/document.h>
#include <rapidjson/writer.h>

#include <GuelderResourcesManager.hpp>

#include "../Utils.hpp"
#include "DiscordBot.hpp"
#include "Player.hpp"

namespace Orchestra
{
    using namespace GuelderConsoleLog;

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
            "Joins your voice channel and play audio from youtube or soundcloud, or from raw url to audio, or admin's local files(note: a whole bunch of formats are not supported)."
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
        constexpr std::string_view DEFAULT_SEARCH_ENGINE = "yt";

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

            bool isInputRaw = false;
            GetParamValue(params, "raw", isInputRaw);

            GE_LOG(Orchestra, Info, L"Received music value: ", GuelderConsoleLog::StringToWString(value));

            std::string rawUrl;

            if(!isInputRaw)
            {
                //path also may contain non English letters
                std::wstring wYt_dlpPath = StringToWString(m_yt_dlpPath);
                //as urls may contain non English letters
                std::wstring wValue = StringToWString(value);
                std::wstring pipeCommand;

                bool isUsingSearch = false;

                if(const int index = GetParamIndex(params, "search"); index != -1)
                    isUsingSearch = params[index].GetValue<bool>();
                else
                    isUsingSearch = !IsValidURL(value.data());

                //making a command for yt-dlp
                if(isUsingSearch)
                {
                    std::string searchEngine;
                    GetParamValue(params, "searchengine", searchEngine);

                    if(searchEngine.empty())
                        searchEngine = DEFAULT_SEARCH_ENGINE;

                    if(searchEngine != "yt" && searchEngine != "sc")
                    {
                        LogWarning(searchEngine, " search engine is unsupported. Using ", DEFAULT_SEARCH_ENGINE, " instead.");
                        searchEngine = DEFAULT_SEARCH_ENGINE;
                    }

                    pipeCommand = Logger::Format(std::move(wYt_dlpPath), L" \"", StringToWString(searchEngine), L"search:", std::move(wValue), "\" --dump-json");
                }
                else
                    pipeCommand = Logger::Format(std::move(wYt_dlpPath), L" \"", std::move(wValue), "\" --dump-json");

                //receiving json from yt-dlp
                std::vector<std::wstring> output = GuelderResourcesManager::ResourcesManager::ExecuteCommand<wchar_t>(pipeCommand, 1);

                const bool yt_dlpSuccess = !output.empty();

                if(!yt_dlpSuccess)
                {
                    O_ASSERT(IsValidURL(rawUrl.data()), value, " is not a valid url.");

                    GE_LOG(Orchestra, Warning, L"Failed to get raw url from ", wValue, L". Assuming that it is an already raw url.");
                    rawUrl = value;
                }
                else
                {
                    //this thing is huge
                    std::wstring& wOutputJSON = output[0];

                    rapidjson::GenericDocument<rapidjson::UTF16<>> jsonDocument;
                    jsonDocument.Parse(wOutputJSON.c_str());

                    O_ASSERT(!jsonDocument.HasParseError(), "Failed to parse json document from yt-dlp. Error offset: ", jsonDocument.GetErrorOffset(), '.');

                    //GE_LOG(Orchestra, Info, wOutputJSON);

                    auto itFormats = jsonDocument.FindMember(L"formats");

                    O_ASSERT(itFormats != jsonDocument.MemberEnd() && itFormats->value.IsArray(), "Failed to find \"formats\" or \"formats\" is not an array in json from yt-dlp.");

                    for(const auto& format : itFormats->value.GetArray())
                        if(format.IsObject())
                        {
                            const auto itFormat = format.FindMember(L"resolution");
                            //there is such member               it is a string                the string and "audio only" are equal
                            if(itFormat != format.MemberEnd() && itFormat->value.IsString() && std::wcscmp(itFormat->value.GetString(), L"audio only") == 0)
                            {
                                auto itUrl = format.FindMember(L"url");

                                if(itUrl != format.MemberEnd() && itUrl->value.IsString())
                                    //found!
                                    rawUrl = WStringToString(itUrl->value.GetString());
                            }
                        }

                    O_ASSERT(!rawUrl.empty(), "Failed to find url for audio.");

                    //description
                    //we assume that all variables exist
                    std::wstring description = Logger::Format(L"**", jsonDocument.FindMember(L"title")->value.GetString(), L"**", L" is going to be played for **", jsonDocument.FindMember(L"duration")->value.GetFloat(), L"** seconds.");

                    if(isUsingSearch)
                        description += Logger::Format(L"\nURL: ", jsonDocument.FindMember(L"webpage_url")->value.GetString());

                    message.reply(WStringToString(description));
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

            GE_LOG(Orchestra, Info, "Received raw url to audio: ", rawUrl);

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