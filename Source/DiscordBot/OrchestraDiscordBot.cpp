#define NOMINMAX

#include "OrchestraDiscordBot.hpp"

#include <atomic>
#include <mutex>
#include <future>
#include <chrono>
#include <string>
#include <string_view>
#include <random>

#include <dpp/dpp.h>

#include <rapidjson/document.h>
#include <rapidjson/writer.h>

#include <GuelderResourcesManager.hpp>

#include "../Utils.hpp"
#include "DiscordBot.hpp"
#include "Player.hpp"
#include "Yt_DlpManager.hpp"

namespace Orchestra
{
    using namespace GuelderConsoleLog;

    OrchestraDiscordBot::OrchestraDiscordBot(const std::string_view& token, const std::wstring_view& yt_dlpPath, const std::string_view& prefix, const char& paramPrefix, uint32_t intents)
        : DiscordBot(token, prefix, paramPrefix, intents), m_Player(200000, true, false), m_AdminSnowflake(0), m_Yt_DlpManager(yt_dlpPath.data()), m_CurrentTrack{}
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
        //add support of streaming audio from raw URL, like from google drive - PROBABLY DONE
        //add support of skipping a certain time - DONE
        //add gui?
        //add support of looking for URL depending on the name so that message contain only the name, for instance, "!play "Linkpark: Numb"" - DONE
        //make first decoding faster then next so to make play almost instantly
        //make beauty and try optimize all possible things - PARTIALLY
        //add possibility of making bass boost?
        //rename to Orchestra and find an avatar - DONE
        //Playlists Roadmap:
        //1. Add shuffling
        //2. Add skipping multiple tracks
        //3. Add getting to a certain track(almost the same as the 2nd clause)

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
                ParamProperties{"searchengine", Type::String, "A certain search engine that will be used to find URL. Supported: yt - Youtube(default), sc - SoundCloud."},
                ParamProperties{"noinfo", Type::Bool, "If noinfo is true: the info(name, URL(optional), duration) about track won't be sent."},
                ParamProperties{"raw", Type::Bool, "If raw is false: it won't use yt-dlp for finding a raw URL to audio."},
                ParamProperties{"index", Type::Int, "The index of a playlist item. Used only if input music value is a playlist."},
                ParamProperties{"shuffle", Type::Bool, "Whether to shuffle tracks of a playlist."}
            },
            "Joins your voice channel and play audio from youtube or soundcloud, or from raw URL to audio, or admin's local files(note: a whole bunch of formats are not supported). Playlists are supported!"
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
                ParamProperties{"tosecs", Type::Float, "If tosecs < 0: it will skip to the beginning of the audio. Skips up to the given time."},
                ParamProperties{"inindex", Type::Float, "Skips in the given index in the current playing playlist."},
                ParamProperties{"toindex", Type::Float, "Skips to the given index in the current playing playlist."}
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

        std::unique_lock lock{ m_CurrentTrackMutex };

        std::wstring reply = Logger::Format(L"**", m_CurrentTrack.title, L"**\nCurrent timestamp: ", m_Player.GetCurrentDecodingDurationSeconds(), L" seconds. Total duration: ", m_Player.GetCurrentTotalDurationSeconds(), " seconds.");

        if(m_Yt_DlpManager.IsPlaylist())
            reply += Logger::Format(L"\nCurrent playlist item index: ", m_CurrentTrack.playlistIndex, L". Playlist size: ", m_Yt_DlpManager.GetPlaylistSize(), L'.');

        reply += Logger::Format(L"\nURL: ", StringToWString(m_CurrentTrack.URL));

        message.reply(WStringToString(reply));
    }
    void OrchestraDiscordBot::CommandPlay(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        if(!value.empty())
        {
            struct PlayParams
            {
                float speed = 1.f;
                int repeat = 1;
                bool doSearch = false;
                std::string searchEngine;
                bool noInfo = false;
                bool isRaw = false;
                int initialIndex = 0;
                bool doShuffle = false;
            };

            //join
            if(!m_IsJoined)
                ConnectToMemberVoice(message);

            m_IsStopped = false;

            //as urls may contain non English letters
            std::wstring wValue = StringToWString(value);

            GE_LOG(Orchestra, Info, L"Received music value: ", wValue);

            PlayParams playParams{};

            GetParamValue(params, "raw", playParams.isRaw);

            if(!playParams.isRaw)
            {
                GetParamValue(params, "searchengine", playParams.searchEngine);

                const bool foundSearchEngine = std::ranges::find(g_SupportedYt_DlpSearchingEngines, playParams.searchEngine) != g_SupportedYt_DlpSearchingEngines.end();
                bool isUsingSearch = false;

                //finding out whether search is being used
                if(!foundSearchEngine)
                {
                    if(const int paramIndex = GetParamIndex(params, "search"); paramIndex != -1)
                        isUsingSearch = params[paramIndex].GetValue<bool>();
                    else
                        isUsingSearch = !IsValidURL(value.data());
                }
                else
                    isUsingSearch = foundSearchEngine;

                //making a command for yt-dlp
                if(isUsingSearch)
                {
                    SearchEngine searchEngine;

                    if(!foundSearchEngine)
                        searchEngine = SearchEngine::YouTube;
                    else
                    {
                        playParams.doShuffle = true;
                        searchEngine = StringToSearchEngine(playParams.searchEngine);
                    }

                    m_Yt_DlpManager.FetchSearch(wValue, searchEngine);
                }
                else
                    m_Yt_DlpManager.FetchURL(wValue);
            }
            else
            {
                //taking the value as a raw url
                m_CurrentTrack.rawURL = value;
                if(!IsValidURL(m_CurrentTrack.rawURL) && message.msg.author.id != m_AdminSnowflake)
                {
                    message.reply("You don't have permission to access admin's local files.");
                    return;
                }
            }

            //joining stuff
            WaitUntilJoined(std::chrono::seconds(2));

            //waiting for voice connection to be established
            WaitUntil(
                [&]
                {
                    const dpp::voiceconn* v = this->get_shard(0)->get_voice(message.msg.guild_id);

                    return v && v->voiceclient && v->voiceclient->is_ready();
                },
                std::chrono::seconds(2));

            //checking if connection was successful
            const dpp::voiceconn* voice = IsVoiceConnectionReady(message.msg.guild_id);

            GetParamValue(params, "speed", playParams.speed);
            GetParamValue(params, "repeat", playParams.repeat);
            if(playParams.repeat < 0)
                playParams.repeat = std::numeric_limits<int>::max();

            std::lock_guard lock{ m_PlayMutex };

            if(!m_IsStopped)
            {
                GetParamValue(params, "noinfo", playParams.noInfo);

                if(m_Yt_DlpManager.IsPlaylist())
                {
                    GetParamValue(params, "index", playParams.initialIndex);
                    GetParamValue(params, "shuffle", playParams.doShuffle);

                    std::vector<int> tracksIndices;

                    int index = playParams.initialIndex;

                    if(playParams.doShuffle)
                        tracksIndices.resize(m_Yt_DlpManager.GetPlaylistSize());
                    else
                        O_ASSERT(playParams.initialIndex < m_Yt_DlpManager.GetPlaylistSize(), "The index ", playParams.initialIndex, " is bigger than the last index ", m_Yt_DlpManager.GetPlaylistSize()-1);

                    for(int i = 0; i < playParams.repeat; ++i)
                    {
                        if(playParams.doShuffle)
                        {
                            if(i > 0)
                                for(auto& trackIndex : tracksIndices)
                                    trackIndex = 0;

                            std::iota(tracksIndices.begin(), tracksIndices.end(), 0);

                            std::ranges::shuffle(tracksIndices, std::default_random_engine{});

                            index = tracksIndices[0];
                        }

                        for(size_t count = 0; count < m_Yt_DlpManager.GetPlaylistSize() && index < m_Yt_DlpManager.GetPlaylistSize() && !m_IsStopped; ++count, (playParams.doShuffle ? index = tracksIndices[count] : ++index))
                        {
                            m_CurrentTrackMutex.lock();

                            m_CurrentTrack = m_Yt_DlpManager.GetTrackInfo(index);
                            m_CurrentTrack.playlistIndex = index;

                            GE_LOG(Orchestra, Info, "Received raw URL to audio: ", m_CurrentTrack.rawURL);

                            if(!playParams.noInfo)
                                ReplyWithInfoAboutTrack(message, m_CurrentTrack);

                            m_Player.AddDecoderBack(m_CurrentTrack.rawURL, Decoder::DEFAULT_SAMPLE_RATE * playParams.speed);

                            m_CurrentTrackMutex.unlock();

                            //sending
                            m_Player.DecodeAndSendAudio(voice);

                            if(m_Player.GetDecodersCount())
                                m_Player.DeleteAudio();
                        }
                    }
                }
                else
                {
                    m_CurrentTrackMutex.lock();
                    m_CurrentTrack = m_Yt_DlpManager.GetTrackInfo(0, false);

                    GE_LOG(Orchestra, Info, "Received raw URL to audio: ", m_CurrentTrack.rawURL);
                    m_CurrentTrackMutex.unlock();

                    if(!playParams.noInfo)
                        ReplyWithInfoAboutTrack(message, m_CurrentTrack);

                    for(int i = 0; i < playParams.repeat && !m_IsStopped; ++i)
                    {
                        m_CurrentTrackMutex.lock();
                        m_Player.AddDecoderBack(m_CurrentTrack.rawURL, Decoder::DEFAULT_SAMPLE_RATE * playParams.speed);
                        m_CurrentTrackMutex.unlock();

                        m_Player.DecodeAndSendAudio(voice);

                        if(m_Player.GetDecodersCount())
                            m_Player.DeleteAudio();
                    }
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
        if(m_Player.GetDecodersCount() == 0)
            return;

        float tosecs = 0.f;
        GetParamValue(params, "tosecs", tosecs);
        float secs = 0.f;
        if(tosecs <= 0.f)
            GetParamValue(params, "secs", secs);

        int toindex = 0;
        GetParamValue(params, "toindex", toindex);
        int inindex = 0;
        if(toindex <= 0)
            GetParamValue(params, "inindex", inindex);

        dpp::voiceconn* v = this->get_shard(0)->get_voice(message.msg.guild_id);

        O_ASSERT(v && v->voiceclient && v->voiceclient->is_ready(), "Failed to skip.");

        //FUCK
        //if(toindex > 0 || inindex != 0)
        {

        }
        //else
        {
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

    void OrchestraDiscordBot::ConnectToMemberVoice(const dpp::message_create_t& message)
    {
        if(!dpp::find_guild(message.msg.guild_id)->connect_member_voice(message.msg.author.id))
            O_THROW("The user with id ", message.msg.author.id.str(), " is not in a voice channel.");
    }
    void OrchestraDiscordBot::WaitUntilJoined(const std::chrono::milliseconds& delay)
    {
        std::unique_lock joinLock{ m_JoinMutex };

        if(!m_JoinedCondition.wait_for(joinLock, delay, [this] { return m_IsJoined == true; }))
            O_THROW("The connection delay was bigger than ", delay.count(), "ms.");
    }
    dpp::voiceconn* OrchestraDiscordBot::IsVoiceConnectionReady(const dpp::snowflake& guildSnowflake)
    {
        dpp::voiceconn* voice = get_shard(0)->get_voice(guildSnowflake);

        if(!voice || !voice->voiceclient || !voice->voiceclient->is_ready())
            O_THROW("Failed to connect to a voice channel in a guild with id ", guildSnowflake);

        return voice;
    }

    void OrchestraDiscordBot::ReplyWithInfoAboutTrack(const dpp::message_create_t& message, const TrackInfo& trackInfo, const bool& outputURL)
    {
        std::wstring description = Logger::Format(L"**", trackInfo.title, L"** is going to be played for **", trackInfo.duration, L"** seconds.");

        if(outputURL)
            description += Logger::Format(L"\nURL: ", StringToWString(trackInfo.URL));

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