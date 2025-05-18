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
#include "TracksQueue.hpp"

namespace Orchestra
{
    using namespace GuelderConsoleLog;

    OrchestraDiscordBot::OrchestraDiscordBot(const std::string_view& token, const std::wstring_view& yt_dlpPath, const std::string_view& prefix, const char& paramPrefix, uint32_t intents)
        : DiscordBot(token, prefix, paramPrefix, intents), m_Player(200000, true, false), m_AdminSnowflake(0), m_TracksQueue(yt_dlpPath.data())
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
        //add queue command - DONE
        //Queue Roadmap:
        //I need some vector with music values in order not to change a lot code in CommandPlay - DONE

        //Playlists Roadmap:
        //1. Add shuffling - DONE
        //2. Add skipping multiple tracks - DONE
        //3. Add getting to a certain track(almost the same as the 2nd clause) - DONE

        //TODO: custom static func to print messages, as the message cannot exceed 2000 letters - PARTIALLY
        //TODO: use PlaylistInfo - DONE

        //TODO: BEAUTY!!!!
        //TODO: fix Current timestamp: 7.79999s. if it is possible

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
            "Joins your voice channel and plays audio from YouTube or SoundCloud, or from raw URL to audio, or admin's local files(note: a whole bunch of formats are not supported). Playlists are supported!"
            });
        //skip
        this->AddCommand({ "skip",
            std::bind(&OrchestraDiscordBot::CommandSkip, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{"secs", Type::Float, "Skips given amount of seconds."},
                ParamProperties{"tosecs", Type::Float, "If tosecs < 0: it will skip to the beginning of the audio. Skips up to the given time."},
                ParamProperties{"in", Type::Int, "Skips in the given index in the current playing playlist."},
                ParamProperties{"to", Type::Int, "Skips to the given index in the current playing playlist. If toindex is bigger than a playlist size, then it will skip entire playlist."},
                ParamProperties{"playlist", Type::Bool, "Skips entire playlist."},
                ParamProperties{"middle", Type::Bool, "Skips to the middle of the queue."},
                ParamProperties{"last", Type::Bool, "Skips to the last track."}
            },
            "Skips current track."
            });
        //shuffle
        this->AddCommand({ "shuffle",
            std::bind(&OrchestraDiscordBot::CommandShuffle, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {},
            "Shuffles all tracks that are in the queue. Forgets about playlists repeats, but not the speeds."
            });
        //delete
        this->AddCommand({ "delete",
            std::bind(&OrchestraDiscordBot::CommandDelete, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{"current", Type::Bool, "Deletes current track from the queue."},
                ParamProperties{"from", Type::Int, "Deletes track from given index to the end of the queue or if used with \"to\" param, it will delete the given range of tracks."},
                ParamProperties{"to", Type::Int, "Deletes track from the queue from the beginning to the given index or if used with \"from\" param, it will delete the given range of tracks."},
            },
            "Deletes tracks in the queue by index. It is not recommended to use this command to delete a range of tracks, but you can try."
            });
        //current
        this->AddCommand({ "current",
            std::bind(&OrchestraDiscordBot::CommandCurrentTrack, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{"url", Type::Bool, "Whether to show urls of tracks."}
            },
            "Prints info about current track, if it plays."
            });
        //queue
        this->AddCommand({ "queue",
            std::bind(&OrchestraDiscordBot::CommandQueue, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{"url", Type::Bool, "Whether to show urls of tracks."}
            },
            "Prints current queue of tracks."
            });
        //pause
        this->AddCommand({ "pause",
            std::bind(&OrchestraDiscordBot::CommandPause, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {},
            "Pauses the audio."
            });
        //stop
        this->AddCommand({ "stop",
            std::bind(&OrchestraDiscordBot::CommandStop, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {},
            "Stops all audio."
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

    void OrchestraDiscordBot::CommandHelp(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        std::string reply;

        //FUCK
        //reserving
        /*{
            const size_t additionalSizePerCommand = m_Prefix.size() + 2 + 2 + 3 + 1;
            constexpr size_t additionalSizePerParamProperty = 10 + 2 + 1 + 2 + 2 + 2 + 3 + 1 + 1;

            size_t totalSizeToReserve = 0;
            for(auto&& command : m_Commands)
            {
                totalSizeToReserve += command.description.size() + additionalSizePerCommand;

                if(printDescription && !command.paramsProperties.empty())
                    for(const auto& [name, type, description] : command.paramsProperties)
                        totalSizeToReserve += description.size() + additionalSizePerParamProperty;
            }

            reply.reserve(totalSizeToReserve);
        }*/

        std::vector<dpp::embed> embeds;

        for(size_t i = 0; i < m_Commands.size(); i += DPP_MAX_EMBED_SIZE)
        {
            dpp::embed embed;
            embed.set_title("List of commands");
            embed.set_color(0x5865F2); // Optional color

            for(size_t j = i; j < std::min(i + DPP_MAX_EMBED_SIZE, m_Commands.size()); ++j)
            {
                const auto& command = m_Commands[j];

                std::string fieldTitle = Logger::Format("`", m_Prefix, command.name, "`");
                if(!command.description.empty())
                    fieldTitle += Logger::Format(" : ", command.description);

                std::string fieldValue;

                if(!command.paramsProperties.empty())
                {
                    fieldValue += "**Params:**\n";
                    for(const auto& paramProperties : command.paramsProperties)
                    {
                        fieldValue += Logger::Format("    [`", TypeToString(paramProperties.type), "`] **", m_ParamNamePrefix, paramProperties.name, "**");

                        if(!paramProperties.description.empty())
                            fieldValue += Logger::Format(" : ", paramProperties.description);

                        fieldValue += '\n';
                    }
                }

                embed.add_field(fieldTitle, fieldValue, false);
            }

            embeds.emplace_back(std::move(embed));
        }

        SendEmbedsSequentially(message, embeds);
    }

    void OrchestraDiscordBot::CommandCurrentTrack(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value) const
    {
        {
            std::lock_guard lock{ m_TracksQueueMutex };
            if(!m_TracksQueue.GetSize())
            {
                Reply(message, "I'm not even playing anything!");
                return;
            }
        }

        bool showURLs = false;
        GetParamValue(params, "url", showURLs);

        std::lock_guard lock{ m_TracksQueueMutex };

        const auto& [URL, rawURL, title, duration, playlistIndex, repeat, speed] = m_TracksQueue.GetTrackInfo(m_CurrentTrackIndex);

        std::string reply = Logger::Format('[', m_CurrentTrackIndex, "] **", WStringToString(title), "**. Current timestamp: ", m_Player.GetCurrentDecodingDurationSeconds(), "s. Duration: ", duration, "s.");

        if(showURLs)
            reply += Logger::Format(" URL: ", WStringToString(URL));

        Reply(message, reply);
    }
    void OrchestraDiscordBot::CommandQueue(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        std::lock_guard lock{ m_TracksQueueMutex };

        if(!m_TracksQueue.GetSize())
            Reply(message, "The queue is empty!");
        else
        {
            bool showURLs = false;
            GetParamValue(params, "url", showURLs);

            /*std::string reply = Logger::Format("The Queue size: ", m_TracksQueue.GetSize(), ". The Queue content:\n");

            for(size_t i = 0; i < m_TracksQueue.GetTrackInfos().size(); i++)
            {
                const auto& [URL, rawURL, title, duration, playlistIndex, repeat, speed] = m_TracksQueue.GetTrackInfo(i);

                reply += Logger::Format('[', i, ']');

                if(i == m_CurrentTrackIndex)
                    reply += Logger::Format("**", WStringToString(title), "** Duration: ", duration, "s. ", "Current timestamp: ", m_Player.GetCurrentDecodingDurationSeconds(), "s.");
                else
                    reply += Logger::Format(WStringToString(title), ". Duration: ", duration, "s.");

                if(speed != 1.f)
                    reply += Logger::Format(" Duration with speed applied: ", static_cast<float>(duration) * speed, "s.");
                if(repeat > 1)
                    reply += Logger::Format(" Repeat count: ", repeat, ".");

                if(showURLs)
                    reply += Logger::Format(" URL: ", WStringToString(URL));

                reply += '\n';
            }

            Reply(message, reply);*/
            std::vector<dpp::embed> embeds;

            for(size_t i = 0; i < m_TracksQueue.GetTrackInfos().size(); i += DPP_MAX_EMBED_SIZE)
            {
                dpp::embed embed;
                embed.set_title("Track Queue");
                embed.set_color(0x5865F2); // Optional color

                for(size_t j = i; j < std::min(i + DPP_MAX_EMBED_SIZE, m_TracksQueue.GetTrackInfos().size()); ++j)
                {
                    const auto& [URL, rawURL, title, duration, playlistIndex, repeat, speed] = m_TracksQueue.GetTrackInfo(j);

                    std::string fieldTitle = Logger::Format("[", j, "] ");
                    if(j == m_CurrentTrackIndex)
                        fieldTitle += Logger::Format("**", WStringToString(title), "** Duration: ", duration, "s. Current timestamp: ", m_Player.GetCurrentDecodingDurationSeconds(), "s.");
                    else
                        fieldTitle += Logger::Format(WStringToString(title), ". Duration: ", duration, "s.");

                    std::string fieldValue;
                    if(speed != 1.f)
                        fieldValue += Logger::Format("Duration with speed applied: ", static_cast<float>(duration) * speed, "s.\n");
                    if(repeat > 1)
                        fieldValue += Logger::Format("Repeat count: ", repeat, ".\n");
                    if(showURLs)
                        fieldValue += Logger::Format("URL: ", WStringToString(URL), "\n");

                    embed.add_field(fieldTitle, fieldValue, false);
                }

                embeds.emplace_back(std::move(embed));
            }

            SendEmbedsSequentially(message, embeds);
        }
    }
    void OrchestraDiscordBot::CommandPlay(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        if(!value.empty())
        {
            //join
            if(!m_IsJoined)
                ConnectToMemberVoice(message);

            m_IsStopped = false;

            const size_t tracksNumberBefore = m_TracksQueue.GetSize();

            PlayParams playParams = AddTrack(message, params, value);

            if(tracksNumberBefore)
                return;

            //joining stuff
            //if(!m_IsJoined)
            {
                WaitUntilJoined(std::chrono::seconds(2));

                //waiting for voice connection to be established
                WaitUntil(
                    [&]
                    {
                        const dpp::voiceconn* v = this->get_shard(0)->get_voice(message.msg.guild_id);

                        return v && v->voiceclient && v->voiceclient->is_ready();
                    },
                    std::chrono::seconds(2));
            }

            if(!m_IsStopped)
            {
                //checking if connection was successful
                const dpp::voiceconn* voice = IsVoiceConnectionReady(message.msg.guild_id);

                std::lock_guard lock{ m_PlayMutex };

                GetParamValue(params, "noinfo", playParams.noInfo);

                for(size_t i = 0, playlistRepeated = 0; true; i++)
                {
                    m_IncrementCurrentTrackIndex = true;
                    try
                    {
                        bool isCurrentTrackInPlaylist = false;
                        const PlaylistInfo* currentPlaylistInfo = nullptr;

                        //const bool wasPlaylistInfosEmpty = m_TracksQueue.GetPlaylistInfos().empty();
                        //size_t prevCurrentTrackIndex = m_CurrentTrackIndex;
                        //size_t prevTracksSize;
                        size_t prevPlaylistInfosSize;

                        {
                            std::lock_guard queueLock{ m_TracksQueueMutex };

                            if(m_CurrentTrackIndex >= m_TracksQueue.GetSize())
                                break;

                            //prevTracksSize = m_TracksQueue.GetSize();
                            prevPlaylistInfosSize = m_TracksQueue.GetPlaylistInfos().size();

                            const TrackInfo* currentTrackInfo = &m_TracksQueue.GetTrackInfos()[m_CurrentTrackIndex];

                            if(currentTrackInfo->rawURL.empty())
                                currentTrackInfo = &m_TracksQueue.GetRawTrackURL(m_CurrentTrackIndex);

                            GE_LOG(Orchestra, Info, "Received raw URL to a track: ", currentTrackInfo->rawURL);

                            if(!playParams.noInfo)
                                ReplyWithInfoAboutTrack(message, *currentTrackInfo);

                            playParams.speed = currentTrackInfo->speed;

                            m_Player.AddDecoderBack(currentTrackInfo->rawURL, Decoder::DEFAULT_SAMPLE_RATE * playParams.speed);

                            for(size_t j = 0; j < m_TracksQueue.GetPlaylistInfos().size(); j++)
                            {
                                const PlaylistInfo& playlistInfo = m_TracksQueue.GetPlaylistInfos()[j];
                                const bool isInRange = m_CurrentTrackIndex >= playlistInfo.beginIndex && m_CurrentTrackIndex <= playlistInfo.endIndex;

                                if(isInRange)
                                {
                                    currentPlaylistInfo = &playlistInfo;
                                    isCurrentTrackInPlaylist = true;
                                    playParams.repeat = playlistInfo.repeat;
                                    break;
                                }
                            }

                            if(!isCurrentTrackInPlaylist)
                                playParams.repeat = currentTrackInfo->repeat;
                        }

                        m_Player.DecodeAndSendAudio(voice);

                        if(m_Player.GetDecodersCount())
                            m_Player.DeleteAudio(0);

                        bool isPlaylistInfosEmpty;
                        //size_t currentTracksSize;
                        {
                            std::lock_guard queueLock{ m_TracksQueueMutex };
                            isPlaylistInfosEmpty = m_TracksQueue.GetPlaylistInfos().empty();
                            //currentTracksSize = m_TracksQueue.GetSize();

                            //as PlaylistInfo may be deleted
                            if(prevPlaylistInfosSize != m_TracksQueue.GetPlaylistInfos().size())
                                for(size_t j = 0; j < m_TracksQueue.GetPlaylistInfos().size(); j++)
                                {
                                    const PlaylistInfo& playlistInfo = m_TracksQueue.GetPlaylistInfos()[j];
                                    const bool isInRange = m_CurrentTrackIndex >= playlistInfo.beginIndex && m_CurrentTrackIndex <= playlistInfo.endIndex;

                                    if(isInRange)
                                    {
                                        currentPlaylistInfo = &playlistInfo;
                                        isCurrentTrackInPlaylist = true;
                                        //playParams.repeat = playlistInfo.repeat;
                                        break;
                                    }

                                    isCurrentTrackInPlaylist = false;
                                }
                        }

                        if(!isPlaylistInfosEmpty && isCurrentTrackInPlaylist && playParams.repeat > 1 && m_CurrentTrackIndex == currentPlaylistInfo->endIndex)
                        {
                            playlistRepeated++;

                            if(playlistRepeated >= playParams.repeat)
                            {
                                //exiting the playlist
                                m_CurrentTrackIndex = currentPlaylistInfo->endIndex + 1;
                                playlistRepeated = 0;
                            }
                            else
                                m_CurrentTrackIndex = currentPlaylistInfo->beginIndex;
                        }
                        else if(m_IncrementCurrentTrackIndex && ((currentPlaylistInfo && m_CurrentTrackIndex != currentPlaylistInfo->endIndex + 1) || !currentPlaylistInfo))
                            ++m_CurrentTrackIndex;
                    }
                    catch(const OrchestraException& e)
                    {
                        GE_LOG(Orchestra, Warning, "Exception occured, skipping the track. Exception: ", e.GetFullMessage());
                        Reply(message, "Exception just occurred, skipping the track! Exception: ", e.GetUserMessage());
                    }
                    catch(const std::exception& e)
                    {
                        GE_LOG(Orchestra, Warning, "Exception occured, skipping the track. Exception: ", e.what());
                        Reply(message, "Unknown exception just occurred with the track, skipping!");
                    }
                }

                {
                    std::lock_guard queueLock{ m_TracksQueueMutex };
                    m_TracksQueue.Clear();
                }

                m_CurrentTrackIndex = 0;

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
                    m_Player.SetAudioSampleRate(Decoder::DEFAULT_SAMPLE_RATE * speed, 0);
            }
        }
    }
    void OrchestraDiscordBot::CommandShuffle(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        if(m_TracksQueue.GetSize())
        {
            //should be remade
            std::lock_guard queueLock{ m_TracksQueueMutex };
            m_TracksQueue.Shuffle(m_CurrentTrackIndex);

            m_CurrentTrackIndex = 0;
        }
        else
            Reply(message, "Nothing plays!");
    }
    void OrchestraDiscordBot::CommandDelete(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        if(m_TracksQueue.GetSize())
        {
            const size_t index = (value.empty() ? std::numeric_limits<size_t>::max() : GuelderResourcesManager::StringToNumber<size_t>(value));
            const dpp::voiceconn* voice = IsVoiceConnectionReady(message.msg.guild_id);

            std::lock_guard queueLock{ m_TracksQueueMutex };

            if(index != std::numeric_limits<size_t>::max())
            {
                O_ASSERT(index < m_TracksQueue.GetSize(), "The index is bigger than the last item of queue with index ", m_TracksQueue.GetSize() - 1);

                if(m_CurrentTrackIndex == index)
                {
                    voice->voiceclient->stop_audio();
                    m_Player.Skip();
                    m_IncrementCurrentTrackIndex = false;
                }

                if(m_CurrentTrackIndex > 0)
                    --m_CurrentTrackIndex;

                m_TracksQueue.DeleteTrack(index);
            }
            else
            {
                bool doDeleteCurrentTrack = false;
                GetParamValue(params, "current", doDeleteCurrentTrack);

                if(doDeleteCurrentTrack)
                {
                    voice->voiceclient->stop_audio();
                    m_Player.Skip();
                    m_TracksQueue.DeleteTrack(m_CurrentTrackIndex);
                    m_IncrementCurrentTrackIndex = false;
                }
                else
                {
                    const int fromParamIndex = GetParamIndex(params, "from");
                    int from = 0;
                    if(fromParamIndex != -1)
                        from = params[fromParamIndex].GetValue<int>();

                    const int toParamIndex = GetParamIndex(params, "to");
                    int to = m_TracksQueue.GetSize() - 1;
                    if(toParamIndex != -1)
                        to = params[toParamIndex].GetValue<int>();

                    //O_ASSERT(fromParamIndex != -1 || toParamIndex != -1, "The command is empty, nothing to execute.");
                    O_ASSERT(fromParamIndex >= 0 && fromParamIndex < m_TracksQueue.GetSize() - 1 && to > 0 && to <= m_TracksQueue.GetSize() - 1, "\"from or \"to\" is outside of the range.");

                    const bool isCurrentTrackInRange = m_CurrentTrackIndex >= from && m_CurrentTrackIndex <= to;

                    if(isCurrentTrackInRange)
                    {
                        voice->voiceclient->stop_audio();
                        m_Player.Skip();

                        m_CurrentTrackIndex = from;
                        m_IncrementCurrentTrackIndex = false;

                        //if(to < m_TracksQueue.GetSize() - 1)
                            //m_CurrentTrackIndex = to + 1;
                    }

                    m_TracksQueue.DeleteTracks(from, to);
                }
            }
        }
        else
            Reply(message, "Nothing plays!");
    }

    void OrchestraDiscordBot::CommandStop(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        const dpp::voiceconn* v = IsVoiceConnectionReady(message.msg.guild_id);

        m_Player.Stop();
        m_IsStopped = true;

        {
            std::lock_guard lock{ m_TracksQueueMutex };
            m_TracksQueue.Clear();
        }

        v->voiceclient->pause_audio(m_Player.GetIsPaused());
        v->voiceclient->stop_audio();
    }
    void OrchestraDiscordBot::CommandPause(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        if(m_IsJoined)
        {
            const dpp::voiceconn* v = IsVoiceConnectionReady(message.msg.guild_id);

            m_Player.Pause(!m_Player.GetIsPaused());
            v->voiceclient->pause_audio(m_Player.GetIsPaused());

            Reply(message, "Pause is ", (m_Player.GetIsPaused() ? "on" : "off"), '.');
        }
        else
            Reply(message, "Why should I pause?");
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

        bool skipPlaylist = false;
        GetParamValue(params, "playlist", skipPlaylist);

        int toindex = -1;
        if(!skipPlaylist)
            GetParamValue(params, "to", toindex);
        int inindex = 0;
        if(!skipPlaylist && toindex <= 0)
            GetParamValue(params, "in", inindex);

        bool skipToMiddle = false;
        GetParamValue(params, "middle", skipToMiddle);
        bool skipToLast = false;
        GetParamValue(params, "last", skipToLast);

        const dpp::voiceconn* v = IsVoiceConnectionReady(message.msg.guild_id);

        const bool isToIndexValid = toindex >= 0;
        bool isInIndexValid;
        {
            std::lock_guard lockCurrentTrack{ m_TracksQueueMutex };
            isInIndexValid = inindex != 0 && inindex + static_cast<int>(m_CurrentTrackIndex) >= 0 && inindex + m_CurrentTrackIndex < m_TracksQueue.GetSize();
        }

        if(skipToMiddle)
        {
            {
                std::lock_guard lockCurrentTrack{ m_TracksQueueMutex };
                m_CurrentTrackIndex = m_TracksQueue.GetSize() / 2;
            }

            m_IncrementCurrentTrackIndex = false;
            v->voiceclient->stop_audio();
            m_Player.Skip();
        }
        else if(skipToLast)
        {
            {
                std::lock_guard lockCurrentTrack{ m_TracksQueueMutex };
                m_CurrentTrackIndex = m_TracksQueue.GetSize() - 1;
            }

            m_IncrementCurrentTrackIndex = false;
            v->voiceclient->stop_audio();
            m_Player.Skip();
        }
        else if(isToIndexValid || isInIndexValid || skipPlaylist)
        {
            {
                std::lock_guard lockCurrentTrack{ m_TracksQueueMutex };

                if(!skipPlaylist)
                {
                    if(isToIndexValid)
                        m_CurrentTrackIndex = toindex;
                    else
                        m_CurrentTrackIndex += inindex;

                    m_IncrementCurrentTrackIndex = false;
                }
                else
                {
                    for(size_t j = 0; j < m_TracksQueue.GetPlaylistInfos().size(); j++)
                    {
                        const PlaylistInfo& playlistInfo = m_TracksQueue.GetPlaylistInfos()[j];
                        const bool isInRange = m_CurrentTrackIndex >= playlistInfo.beginIndex && m_CurrentTrackIndex <= playlistInfo.endIndex;

                        if(isInRange)
                        {
                            m_IncrementCurrentTrackIndex = false;
                            m_CurrentTrackIndex = playlistInfo.endIndex + 1;
                            break;
                        }
                    }
                    //m_CurrentTrackIndex = std::numeric_limits<uint32_t>::max();
                }
            }

            v->voiceclient->stop_audio();
            m_Player.Skip();
        }
        else
        {
            const bool doSkipOneTrack = tosecs == 0.f && secs == 0.f;

            if(doSkipOneTrack)
            {
                v->voiceclient->stop_audio();
                m_Player.Skip();

                //std::lock_guard lockCurrentTrack{ m_TracksQueueMutex };
                //++m_CurrentTrackIndex;
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
        dpp::voiceconn* voice = IsVoiceConnectionReady(message.msg.guild_id);

        CommandStop(message, params, value);

        this->get_shard(0)->disconnect_voice(message.msg.guild_id);

        Reply(message, "Leaving.");
    }
    void OrchestraDiscordBot::CommandTerminate(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        if(m_AdminSnowflake != 0 && message.msg.author.id == m_AdminSnowflake)
        {
            //disconnect
            dpp::voiceconn* voice = this->get_shard(0)->get_voice(message.msg.guild_id);

            if(voice && voice->voiceclient && voice->is_ready())
                this->get_shard(0)->disconnect_voice(message.msg.guild_id);

            Reply(message, "Goodbye!");

            std::unique_lock lock{ m_JoinMutex };
            m_JoinedCondition.wait_for(lock, std::chrono::milliseconds(500), [this] { return m_IsJoined == false; });

            exit(0);
        }
        else
            Reply(message, "You are not an admin!");
    }

    void OrchestraDiscordBot::ConnectToMemberVoice(const dpp::message_create_t& message)
    {
        if(!dpp::find_guild(message.msg.guild_id)->connect_member_voice(message.msg.author.id))
            O_THROW("The user with id ", message.msg.author.id.str(), " is not in a voice channel.");
    }

    void OrchestraDiscordBot::ReplyWithMessage(const dpp::message_create_t& message, const dpp::message& reply)
    {
        message.reply(reply);
    }
    
    void OrchestraDiscordBot::SendEmbedsSequentially(const dpp::message_create_t& event, const std::vector<dpp::embed>& embeds, size_t index)
    {
        if(index >= embeds.size()) return;

        dpp::message msg(event.msg.channel_id, "");
        msg.add_embed(embeds[index]);

        message_create(msg, [=, this](const dpp::confirmation_callback_t& cb) {
            if(!cb.is_error())
                SendEmbedsSequentially(event, embeds, index + 1);
            });
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
            O_THROW("Failed to establish connection to a voice channel in a guild with id ", guildSnowflake);

        return voice;
    }

    OrchestraDiscordBot::PlayParams OrchestraDiscordBot::AddTrack(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        std::lock_guard queueLock{ m_TracksQueueMutex };

        PlayParams playParams{};

        //this one will be removed
        //as urls may contain non English letters
        std::wstring wValue = StringToWString(value);

        GE_LOG(Orchestra, Info, L"Received music value: ", wValue);

        GetParamValue(params, "raw", playParams.isRaw);

        if(!playParams.isRaw)
        {
            GetParamValue(params, "searchengine", playParams.searchEngine);

            const bool foundSearchEngine = std::ranges::find(g_SupportedYt_DlpSearchingEngines, playParams.searchEngine) != g_SupportedYt_DlpSearchingEngines.end();
            bool isUsingSearch;

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

                m_TracksQueue.FetchSearch(wValue, searchEngine);
            }
            else
            {
                GetParamValue(params, "speed", playParams.speed);
                GetParamValue(params, "repeat", playParams.repeat);
                if(playParams.repeat < 0)
                    playParams.repeat = std::numeric_limits<int>::max();

                GetParamValue(params, "shuffle", playParams.doShuffle);
                try
                {
                    m_TracksQueue.FetchURL(wValue, playParams.doShuffle, playParams.speed, static_cast<size_t>(playParams.repeat));
                }
                catch(const OrchestraException& e)
                {
                    //probably raw
                    playParams.isRaw = true;
                    if(!IsValidURL(value.data()) && message.msg.author.id != m_AdminSnowflake)
                    {
                        //Reply(message, "You don't have permission to access admin's local files.");

                        O_THROW("The user tried to access admin's file, while not being the admin.");
                    }

                    m_TracksQueue.FetchRaw(value);
                }
            }
        }

        return playParams;
    }

    void OrchestraDiscordBot::ReplyWithInfoAboutTrack(const dpp::message_create_t& message, const TrackInfo& trackInfo, const bool& outputURL)
    {
        std::wstring description = Logger::Format(L"**", trackInfo.title, L"** is going to be played for **", trackInfo.duration, L"** seconds.");

        if(outputURL)
            description += Logger::Format(L"\nURL: ", trackInfo.URL);

        Reply(message, WStringToString(description));
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