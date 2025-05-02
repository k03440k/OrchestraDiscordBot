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
        //add support of playlists - DONE
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
                ParamProperties{"index", Type::Int, "The index of a playlist item. Used only if input music value is a playlist."}
            },
            "Joins your voice channel and play audio from youtube or soundcloud, or from raw url to audio, or admin's local files(note: a whole bunch of formats are not supported). Playlists are supported!"
            });
        //current
        this->AddCommand({ "current",
            std::bind(&OrchestraDiscordBot::CommandCurrentTrack, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {},
            "Prints info about current track, if it plays."
            });
        //stop
        this->AddCommand({ "stop",
            std::bind(&OrchestraDiscordBot::CommandStop, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {},
            "Stops all audio."
            });
        //pause
        this->AddCommand({ "pause",
            std::bind(&OrchestraDiscordBot::CommandPause, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {},
            "Pauses the audio."
            });
        //skip
        this->AddCommand({ "skip",
            std::bind(&OrchestraDiscordBot::CommandSkip, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{"secs", Type::Float, "Skips given amount of seconds."},
                ParamProperties{"tosecs", Type::Float, "If tosecs < 0: it will skip to the beginning of the audio. Skips up to the given time."}
            },
            "Skips current track."
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
    void OrchestraDiscordBot::CommandCurrentTrack(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value) const
    {
        if(!m_IsJoined && !m_Player.GetDecodersCount())
            message.reply("I'm not even playing anything!");

        std::wstring reply = Logger::Format(L"**", *(m_CurrentPlayingTrackTitle.load()), L"**\nCurrent timestamp: ", m_Player.GetCurrentDecodingDurationSeconds(), L" seconds. Total duration: ", m_Player.GetCurrentTotalDurationSeconds(), " seconds.");

        if(m_CurrentPlaylistTrackIndex)
            reply += Logger::Format(L"\nCurrent playlist item index: ", m_CurrentPlaylistTrackIndex, L". Playlist size: ", m_CurrentPLaylistSize, L'.');

            reply += Logger::Format(L"\nURL: ", *(m_CurrentPlayingURL.load()));

        message.reply(WStringToString(reply));
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

            bool isInputRaw = false;
            GetParamValue(params, "raw", isInputRaw);
            bool noInfo = false;
            GetParamValue(params, "noinfo", noInfo);

            //path also may contain non English letters
            std::wstring wYt_dlpPath = StringToWString(m_yt_dlpPath);

            GE_LOG(Orchestra, Info, L"Received music value: ", GuelderConsoleLog::StringToWString(value));

            std::string rawUrl;

            bool isPlaylist = false;
            int playlistIndex = 0;
            int playlistSize = 0;

            WJSON initialJSONDocument;
            WJSON::MemberIterator itEntries;

            if(!isInputRaw)
            {
                //as urls may contain non English letters
                std::wstring wValue = StringToWString(value);
                std::wstring pipeCommand;

                bool isUsingSearch = false;

                if(const int paramIndex = GetParamIndex(params, "search"); paramIndex != -1)
                    isUsingSearch = params[paramIndex].GetValue<bool>();
                else
                    isUsingSearch = !IsValidURL(value.data());

                //making a command for yt-dlp
                if(isUsingSearch)
                {
                    std::string searchEngine;
                    GetParamValue(params, "searchengine", searchEngine);

                    if(searchEngine.empty())
                        searchEngine = s_SupportedYt_DlpSearchingEngines[0];

                    if(std::ranges::find(s_SupportedYt_DlpSearchingEngines, searchEngine) == s_SupportedYt_DlpSearchingEngines.end())
                    {
                        LogWarning(searchEngine, " search engine is unsupported. Using ", s_SupportedYt_DlpSearchingEngines[0], " instead.");
                        searchEngine = s_SupportedYt_DlpSearchingEngines[0];
                    }

                    pipeCommand = Logger::Format(wYt_dlpPath, L' ', StringToWString(s_Yt_dlpParameters), L" \"", StringToWString(searchEngine), L"search:", wValue, L"\"");
                }
                else
                    pipeCommand = Logger::Format(wYt_dlpPath, L' ', StringToWString(s_Yt_dlpParameters), L" \"", wValue, L'\"');

                {
                    //receiving json from yt-dlp
                    std::vector<std::wstring> output = GuelderResourcesManager::ResourcesManager::ExecuteCommand<wchar_t>(pipeCommand, 1);

                    //yt-dlp failed to find a raw url
                    if(output.empty())
                    {
                        O_ASSERT(IsValidURL(rawUrl.data()), value, " is not a valid url.");

                        //because I used std::move(wValue). never mind
                        GE_LOG(Orchestra, Warning, L"Failed to get raw url from ", wValue, L". Assuming that it is an already raw url.");
                        rawUrl = value;
                    }
                    else
                    {
                        //this thing is huge
                        std::wstring& wOutputJSON = output[0];

                        initialJSONDocument.Parse(wOutputJSON.c_str());

                        O_ASSERT(!initialJSONDocument.HasParseError(), "Failed to parse json document from yt-dlp. Error offset: ", initialJSONDocument.GetErrorOffset(), '.');

                        itEntries = initialJSONDocument.FindMember(L"entries");
                        isPlaylist = itEntries != initialJSONDocument.MemberEnd() && itEntries->value.IsArray();

                        if(isPlaylist)
                        {
                            playlistSize = itEntries->value.GetArray().Size();
                            m_CurrentPLaylistSize = playlistSize;

                            //dummy
                            GetParamValue(params, "index", playlistIndex);
                            m_CurrentPlaylistTrackIndex = playlistIndex;

                            if(playlistIndex >= playlistSize || playlistIndex < 0)
                            {
                                message.reply(Logger::Format("The input index ", playlistIndex, " is bigger than the last member of the playlist with index ", playlistSize - 1, " or is less than 0. Setting index to 0."));
                                playlistIndex = 0;
                            }
                        }
                        else
                        {
                            rawUrl = GetRawAudioUrlFromJSON(initialJSONDocument);

                            m_CurrentPlayingURL.store(std::make_shared<std::wstring>(initialJSONDocument.FindMember(L"webpage_url")->value.GetString()));
                            m_CurrentPlayingTrackTitle.store(std::make_shared<std::wstring>(initialJSONDocument.FindMember(L"title")->value.GetString()));

                            GE_LOG(Orchestra, Info, "Received raw url to audio: ", rawUrl);

                            if(!noInfo)
                                ReplyWithInfoAboutTrack(message, initialJSONDocument, isUsingSearch);
                        }
                    }
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
                if(isPlaylist)
                {
                    WJSON rawUrlJSON;
                    for(int i = 0; i < repeat; ++i)
                        for(; playlistIndex < playlistSize && !m_IsStopped; ++playlistIndex)
                        {
                            rawUrlJSON.Parse(GetRawAudioJsonWStringFromPlaylistJson(itEntries->value.GetArray(), m_yt_dlpPath, playlistIndex).c_str());
                            rawUrl = GetRawAudioUrlFromJSON(rawUrlJSON);

                            m_CurrentPlaylistTrackIndex = playlistIndex;
                            m_CurrentPlayingURL.store(std::make_shared<std::wstring>(rawUrlJSON.FindMember(L"webpage_url")->value.GetString()));
                            m_CurrentPlayingTrackTitle.store(std::make_shared<std::wstring>(rawUrlJSON.FindMember(L"title")->value.GetString()));

                            GE_LOG(Orchestra, Info, "Received raw url to audio: ", rawUrl);

                            if(!noInfo)
                                ReplyWithInfoAboutTrack(message, rawUrlJSON);

                            m_Player.AddDecoderBack(rawUrl, Decoder::DEFAULT_SAMPLE_RATE * speed);
                            m_Player.DecodeAndSendAudio(v);

                            if(m_Player.GetDecodersCount())
                                m_Player.DeleteAudio();
                        }
                }
                else
                    for(int i = 0; i < repeat && !m_IsStopped; ++i)
                    {
                        m_Player.AddDecoderBack(rawUrl, Decoder::DEFAULT_SAMPLE_RATE * speed);
                        m_Player.DecodeAndSendAudio(v);

                        if(m_Player.GetDecodersCount())
                            m_Player.DeleteAudio();
                    }
                if(m_Player.GetDecodersCount())
                    m_Player.DeleteAllAudio();
            }

            m_CurrentPlayingURL.load().reset();
            m_CurrentPlaylistTrackIndex = 0;
            m_CurrentPLaylistSize = 0;
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
                m_Player.SkipSeconds(secs - v->voiceclient->get_secs_remaining(), 0);
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

    std::string OrchestraDiscordBot::GetRawAudioUrlFromJSON(const WJSON& jsonRawAudio)
    {
        const auto itFormats = jsonRawAudio.FindMember(L"formats");

        O_ASSERT(itFormats != jsonRawAudio.MemberEnd() && itFormats->value.IsArray(), "Failed to find \"formats\" or \"formats\" is not an array in json from yt-dlp.");

        for(const auto& format : itFormats->value.GetArray())
            if(format.IsObject())
            {
                const auto itFormat = format.FindMember(L"resolution");
                //there is such member               it is a string                the string and "audio only" are equal
                if(itFormat != format.MemberEnd() && itFormat->value.IsString() && std::wcscmp(itFormat->value.GetString(), L"audio only") == 0)
                {
                    if(auto itUrl = format.FindMember(L"url"); itUrl != format.MemberEnd() && itUrl->value.IsString())
                        //found!
                        return WStringToString(itUrl->value.GetString());
                }
            }

        O_THROW("Failed to find url for audio.");
    }
    std::wstring OrchestraDiscordBot::GetRawAudioJsonWStringFromPlaylistJson(const rapidjson::GenericValue<rapidjson::UTF16<>>::Array& playlistArray, const std::string& yt_dlpPath, const size_t& index)
    {
        const auto itPlaylistUrlByIndex = playlistArray[index].FindMember(L"url");

        O_ASSERT(itPlaylistUrlByIndex != playlistArray[index].MemberEnd() && itPlaylistUrlByIndex->value.IsString(), "Failed to find a playlist member url by index ", index, '.');

        const std::string playlistUrlCall = Logger::Format(yt_dlpPath, ' ', s_Yt_dlpParameters, " \"", WStringToString(itPlaylistUrlByIndex->value.GetString()));

        const std::vector<std::wstring> output = GuelderResourcesManager::ResourcesManager::ExecuteCommand<char, wchar_t>(playlistUrlCall, 1);

        O_ASSERT(!output.empty(), "Failed to get raw json from url.");

        return output[0];
    }
    void OrchestraDiscordBot::ReplyWithInfoAboutTrack(const dpp::message_create_t& message, const WJSON& jsonRawAudio, const bool& outputURL)
    {
        //description
        //we assume that all variables exist
        std::wstring description = Logger::Format(L"**", jsonRawAudio.FindMember(L"title")->value.GetString(), L"** is going to be played for **", jsonRawAudio.FindMember(L"duration")->value.GetFloat(), L"** seconds.");

        if(outputURL)
            description += Logger::Format(L"\nURL: ", jsonRawAudio.FindMember(L"webpage_url")->value.GetString());

        message.reply(WStringToString(description));
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