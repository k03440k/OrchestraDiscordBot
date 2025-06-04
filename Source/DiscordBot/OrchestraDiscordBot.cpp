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
        //make and update documentation and changelog
        //support of commands like: "!play -speed .4" so to finish the Param system - DONE
        //add support of playlists - DONE
        //add support of streaming audio from raw URL, like from google drive - PROBABLY DONE
        //add support of skipping a certain time - DONE
        //add gui?
        //add support of looking for URL depending on the name so that message contain only the name, for instance, "!play "Linkpark: Numb"" - DONE
        //make first decoding faster then next so to make play almost instantly
        //make beauty and try optimize all possible things - PARTIALLY
        //add possibility of making bass boost?
        //add configurable frequencies
        //rename to Orchestra and find an avatar - DONE
        //add queue command - DONE
        //add disconnecting if nobody is in the channel or if count of track is zero for some time
        //fix bugs with Current timestamp
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
                ParamProperties{"middle", Type::Bool, "Deletes middle track from the queue."},
                ParamProperties{"last", Type::Bool, "Deletes last track from the queue."},
                ParamProperties{"from", Type::Int, "Deletes track from given index to the end of the queue or if used with \"to\" param, it will delete the given range of tracks."},
                ParamProperties{"to", Type::Int, "Deletes track from the queue from the beginning to the given index or if used with \"from\" param, it will delete the given range of tracks."},
                ParamProperties{"playlist", Type::Int, "Deletes playlist by index. If input \"playlist\" == -1, it will remove current playlist."}
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

        std::vector<dpp::embed> embeds;

        for(size_t i = 0; i < m_Commands.size(); i += DPP_MAX_EMBED_SIZE)
        {
            dpp::embed embed;
            embed.set_title("List of commands");

            //TODO: make the same as in CommandQueue

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
                        fieldValue += Logger::Format("> [`", TypeToString(paramProperties.type), "`] **", m_ParamNamePrefix, paramProperties.name, "**");

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

    void OrchestraDiscordBot::CommandCurrentTrack(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        {
            std::lock_guard lock{ m_TracksQueueMutex };
            if(!m_TracksQueue.GetSize())
            {
                Reply(message, "I'm not even playing anything!");
                return;
            }
        }

        bool showURL = false;
        GetParamValue(params, "url", showURL);

        //dpp::voiceconn* voice = IsVoiceConnectionReady(message.msg.guild_id);

        std::lock_guard lock{ m_TracksQueueMutex };

        ReplyWithInfoAboutTrack(message, m_TracksQueue.GetTrackInfo(m_CurrentTrackIndex), showURL, true);

        /*const auto& [URL, rawURL, title, duration, playlistIndex, repeat, speed] = m_TracksQueue.GetTrackInfo(m_CurrentTrackIndex);

        std::string reply = Logger::Format('[', m_CurrentTrackIndex, "] **", WStringToString(title), "**.\nCurrent timestamp: ", m_Player.GetCurrentDecodingDurationSeconds() - voice->voiceclient->get_secs_remaining(), "s.\nDuration: ", duration, "s.\n");

        if(showURL)
            reply += Logger::Format("URL: ", WStringToString(URL));

        Reply(message, reply);*/
    }
    void OrchestraDiscordBot::CommandQueue(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        std::lock_guard lock{ m_TracksQueueMutex };

        dpp::voiceconn* voice = IsVoiceConnectionReady(message.msg.guild_id);

        O_ASSERT(m_TracksQueue.GetSize() > 0, "The queue is empty!");

        bool showURLs = false;
        GetParamValue(params, "url", showURLs);

        const size_t queueSize = m_TracksQueue.GetSize();
        const size_t playlistsOffset = m_TracksQueue.GetPlaylistInfos().size() * 2;//as playlist contains beginIndex and endIndex which should be displayed

        const size_t totalFieldsSize = queueSize + playlistsOffset;

        const size_t totalEmbedsCount = std::ceil(static_cast<float>(totalFieldsSize) / DPP_MAX_EMBED_SIZE);

        size_t trackIndex = 0;

        size_t playlistInfoIndex = m_TracksQueue.GetPlaylistInfos().empty() ? std::numeric_limits<size_t>::max() : 0;

        std::vector<dpp::embed> embeds;
        embeds.reserve(totalEmbedsCount);

        for(size_t embedsCount = 0; embedsCount < totalEmbedsCount; embedsCount++)
        {
            dpp::embed embed;

            //reserving
            {
                const size_t remaining = totalFieldsSize - embedsCount * DPP_MAX_EMBED_SIZE;
                embed.fields.reserve(remaining > DPP_MAX_EMBED_SIZE ? DPP_MAX_EMBED_SIZE : remaining);
            }
            embed.set_title(Logger::Format("Tracks queue. The size: ", queueSize));

            //auto tmp = ((int)trackIndex - int(embedsCount * DPP_MAX_EMBED_SIZE));
            for(; trackIndex < queueSize/* && tmp < DPP_MAX_EMBED_SIZE*/; trackIndex++)
            {
                if(embed.fields.size() + 1 > DPP_MAX_EMBED_SIZE)
                    break;

                const bool isPlaylistInfoValid = playlistInfoIndex != std::numeric_limits<size_t>::max();
                const PlaylistInfo* currentPlaylistInfo = isPlaylistInfoValid ? &m_TracksQueue.GetPlaylistInfos()[playlistInfoIndex] : nullptr;

                if(isPlaylistInfoValid && trackIndex == currentPlaylistInfo->beginIndex)
                {

                    std::string fieldTitle = Logger::Format("++(", playlistInfoIndex, ") Playlist begins. Size: ", currentPlaylistInfo->endIndex - currentPlaylistInfo->beginIndex + 1, '.');

                    if(currentPlaylistInfo->repeat > 1)
                        fieldTitle += Logger::Format(" Repeat count: ", currentPlaylistInfo->repeat, '.');

                    embed.add_field(fieldTitle, "");
                }

                const auto& [URL, rawURL, title, duration, playlistIndex, repeat, speed] = m_TracksQueue.GetTrackInfo(trackIndex);

                std::string fieldTitle;

                if(trackIndex == m_CurrentTrackIndex)
                    fieldTitle = Logger::Format("> [", trackIndex, "] **", WStringToString(title), "**");
                else
                    fieldTitle = Logger::Format("[", trackIndex, "] **", WStringToString(title), "**");

                std::string fieldValue;
                if(trackIndex == m_CurrentTrackIndex)
                    fieldValue += Logger::Format("Duration: ", duration, "s.\nCurrent timestamp: ", m_Player.GetCurrentDecodingDurationSeconds() - voice->voiceclient->get_secs_remaining(), "s.\n");
                else
                    fieldValue += Logger::Format("Duration: ", duration, "s.\n");

                if(speed != 1.f)
                    fieldValue += Logger::Format("Duration with speed applied: ", static_cast<float>(duration) * speed, "s.\n");
                if(repeat > 1)
                    fieldValue += Logger::Format("Repeat count: ", repeat, ".\n");
                if(showURLs)
                    fieldValue += Logger::Format("URL: ", (URL.empty() ? rawURL : WStringToString(URL)), "\n");

                embed.add_field(fieldTitle, fieldValue, false);

                if(isPlaylistInfoValid && trackIndex == currentPlaylistInfo->endIndex)
                {
                    if(embed.fields.size() + 1 > DPP_MAX_EMBED_SIZE)
                        break;

                    embed.add_field(Logger::Format("--(", playlistInfoIndex, ") Playlist ends."), "");

                    playlistInfoIndex++;

                    if(playlistInfoIndex >= m_TracksQueue.GetPlaylistInfos().size())
                        playlistInfoIndex = std::numeric_limits<size_t>::max();
                }
            }

            embeds.push_back(std::move(embed));

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

            std::unique_lock _queueLock{ m_TracksQueueMutex };

            const size_t tracksNumberBefore = m_TracksQueue.GetSize();

            /*PlayParams playParams = */AddTrack(message, params, value);

            if(tracksNumberBefore)
                return;

            int beginIndex = 0;
            GetParamValue(params, "index", beginIndex);
            m_CurrentTrackIndex = beginIndex;

            //joining stuff
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

            _queueLock.unlock();

            if(!m_IsStopped)
            {
                //checking if connection was successful
                const dpp::voiceconn* voice = IsVoiceConnectionReady(message.msg.guild_id);

                bool noInfo = false;

                GetParamValue(params, "noinfo", noInfo);

                bool isInPlaylist = false;
                const PlaylistInfo* currentPlaylistInfo = nullptr;
                size_t prevPlaylistIndex = std::numeric_limits<size_t>::max();
                size_t prevTrackIndex;
                size_t playlistRepeat = 0;

                for(size_t i = 0, playlistRepeated = 0, trackRepeated = 0; true; ++i)
                {
                    //std::unique_lock queueLock{ m_TracksQueueMutex };
                    //const TrackInfo* currentTrackInfo = &m_TracksQueue.GetTrackInfos()[m_CurrentTrackIndex];
                    const TrackInfo* currentTrackInfo = nullptr;

                    //prevTrackIndex = currentTrackInfo->playlistIndex;

                    //I do it here, cuz I'm too lazy to make it inside try-catch block
                    //queueLock.unlock();

                    bool caughtException = false;
                    try
                    {
                        std::lock_guard playLock{ m_PlayMutex };
                        {
                            std::lock_guard queueLock{ m_TracksQueueMutex };

                            if(m_CurrentTrackIndex >= m_TracksQueue.GetSize())
                                break;

                            currentTrackInfo = &m_TracksQueue.GetTrackInfos()[m_CurrentTrackIndex];

                            prevTrackIndex = currentTrackInfo->playlistIndex;

                            if(currentTrackInfo->rawURL.empty())
                                currentTrackInfo = &m_TracksQueue.GetRawTrackURL(m_CurrentTrackIndex);

                            GE_LOG(Orchestra, Info, "Received raw URL to a track: ", currentTrackInfo->rawURL);

                            //playParams.speed = currentTrackInfo->speed;

                            m_Player.AddDecoderBack(currentTrackInfo->rawURL, Decoder::DEFAULT_SAMPLE_RATE * currentTrackInfo->speed);

                            //printing info about the track
                            if(!noInfo)
                            {
                                if(currentTrackInfo->title.empty())
                                {
                                    //it is raw, trying to get title with ffmpeg
                                    std::wstring title;
                                    std::string titleTmp = m_Player.GetTitle(0);

                                    if(!titleTmp.empty())
                                        title = StringToWString(titleTmp);

                                    if(!title.empty())
                                        m_TracksQueue.SetTrackTitle(m_CurrentTrackIndex, title);
                                }
                                if(currentTrackInfo->duration == 0.f)
                                {
                                    //it is raw, trying to get duration with ffmpeg
                                    float duration;
                                    duration = m_Player.GetTotalDurationSeconds(0);
                                    if(duration != 0.f)
                                        m_TracksQueue.SetTrackDuration(0, duration);
                                }

                                TrackInfo tmp = *currentTrackInfo;
                                tmp.repeat -= trackRepeated;

                                ReplyWithInfoAboutTrack(message, tmp);
                            }
                        }

                        m_Player.DecodeAndSendAudio(voice);

                        if(!m_IsStopped)
                            m_Player.DeleteAudio(0);
                    }
                    catch(const OrchestraException& e)
                    {
                        caughtException = true;
                        GE_LOG(Orchestra, Warning, "Caught OrchestraException during playling loop. Exception: ", e.GetFullMessage());
                        Reply(message, "Failed to play track, due to ", e.GetUserMessage(), ". Skipping.");
                    }
                    catch(const std::exception& e)
                    {
                        caughtException = true;
                        GE_LOG(Orchestra, Warning, "Caught std::exception during playling loop. Exception: ", e.what());
                        Reply(message, "Failed to play track, skipping.");
                    }

                    if(caughtException)
                    {
                        std::lock_guard queueLock{ m_TracksQueueMutex };
                        //haven't tested this
                        m_TracksQueue.DeleteTrack(m_CurrentTrackIndex);
                        continue;
                    }

                    /*bool isPlaylistInfosEmpty;
                    {
                        std::lock_guard queueLock{ m_TracksQueueMutex };

                        if(m_IsStopped || m_TracksQueue.GetSize() == 0)
                            break;

                        isPlaylistInfosEmpty = m_TracksQueue.GetPlaylistInfos().empty();
                    }*/

                    //indices shit. I strongly hope that it is the last version of this shit, cuz I'm tired by this stuff
                    std::lock_guard queueLock{ m_TracksQueueMutex };
                    //queueLock.lock();

                    if(m_CurrentTrackIndex >= m_TracksQueue.GetSize())
                        break;

                    currentTrackInfo = &m_TracksQueue.GetTrackInfos()[m_CurrentTrackIndex];

                    const bool wasTrackSkipped = prevTrackIndex != currentTrackInfo->playlistIndex;
                    bool incrementCurrentIndex = !wasTrackSkipped;

                    if(!wasTrackSkipped)
                        trackRepeated++;

                    bool found = false;
                    for(const auto& playlistInfo : m_TracksQueue.GetPlaylistInfos())
                    {
                        const bool isInRange = m_CurrentTrackIndex >= playlistInfo.beginIndex && m_CurrentTrackIndex <= playlistInfo.endIndex;

                        if(isInRange)
                        {
                            currentPlaylistInfo = &playlistInfo;
                            isInPlaylist = true;
                            playlistRepeat = playlistInfo.repeat;
                            found = true;

                            if(prevPlaylistIndex != currentPlaylistInfo->index)
                                playlistRepeated = 0;

                            break;
                        }
                    }
                    if(!found)
                    {
                        isInPlaylist = false;
                        currentPlaylistInfo = nullptr;
                        prevPlaylistIndex = std::numeric_limits<size_t>::max();
                        playlistRepeated = 0;
                    }

                    if(!wasTrackSkipped)
                    {
                        if(isInPlaylist && m_CurrentTrackIndex == currentPlaylistInfo->endIndex)
                        {
                            playlistRepeated++;

                            if(playlistRepeated >= playlistRepeat)
                            {
                                //exiting the playlist
                                incrementCurrentIndex = true;
                                playlistRepeated = 0;
                            }
                            else
                            {
                                incrementCurrentIndex = false;
                                m_CurrentTrackIndex = currentPlaylistInfo->beginIndex;
                                trackRepeated = 0;
                            }
                        }
                        else
                        {
                            if(trackRepeated >= currentTrackInfo->repeat)
                                incrementCurrentIndex = true;
                            else
                                incrementCurrentIndex = false;
                        }
                    }

                    if(isInPlaylist)
                        prevPlaylistIndex = currentPlaylistInfo->index;

                    if(incrementCurrentIndex)
                    {
                        ++m_CurrentTrackIndex;
                        trackRepeated = 0;
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
            IsVoiceConnectionReady(message.msg.guild_id);

            O_ASSERT(m_Player.GetDecodersCount() > 0, "Decoders are empty");

            float speed = 0.f;
            GetParamValue(params, "speed", speed);

            O_ASSERT(speed > 0.f, "Speed cannot be lesser or equal to 0.");

            m_Player.SetAudioSampleRate(Decoder::DEFAULT_SAMPLE_RATE * speed, 0);
        }
    }
    void OrchestraDiscordBot::CommandShuffle(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        IsVoiceConnectionReady(message.msg.guild_id);

        std::lock_guard queueLock{ m_TracksQueueMutex };

        O_ASSERT(m_TracksQueue.GetSize() > 0, "The queue is empty.");

        m_TracksQueue.Shuffle(m_CurrentTrackIndex);

        m_CurrentTrackIndex = 0;
    }
    void OrchestraDiscordBot::CommandDelete(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        O_ASSERT(m_TracksQueue.GetSize() > 0, "The queue is empty.");

        size_t index = (value.empty() ? std::numeric_limits<size_t>::max() : GuelderResourcesManager::StringToNumber<size_t>(value));
        const dpp::voiceconn* voice = IsVoiceConnectionReady(message.msg.guild_id);

        std::lock_guard queueLock{ m_TracksQueueMutex };

        {
            bool deleteCurrentTrack = false;
            GetParamValue(params, "current", deleteCurrentTrack);

            if(deleteCurrentTrack)
                index = m_CurrentTrackIndex;
            else
            {
                bool deleteMiddleTrack = false;
                GetParamValue(params, "middle", deleteMiddleTrack);

                if(deleteMiddleTrack)
                    index = m_TracksQueue.GetSize() / 2;
                else
                {
                    bool deleteLastTrack = false;
                    GetParamValue(params, "last", deleteLastTrack);

                    if(deleteLastTrack)
                        index = m_TracksQueue.GetSize() - 1;
                }
            }
        }

        if(index != std::numeric_limits<size_t>::max())
        {
            O_ASSERT(index < m_TracksQueue.GetSize(), "The index is bigger than the last item of queue with index ", m_TracksQueue.GetSize() - 1);

            if(m_CurrentTrackIndex == index)
            {
                voice->voiceclient->stop_audio();
                m_Player.Skip();
            }

            //why it was here before?
            //if(m_CurrentTrackIndex > 0)
                //--m_CurrentTrackIndex;

            m_TracksQueue.DeleteTrack(index);
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

            if(fromParamIndex == -1 && toParamIndex == -1)
            {
                int playlistIndexToDelete = std::numeric_limits<int>::max();
                GetParamValue(params, "playlist", playlistIndexToDelete);

                if(playlistIndexToDelete == -1)
                    playlistIndexToDelete = 0;

                O_ASSERT(playlistIndexToDelete < m_TracksQueue.GetPlaylistInfos().size() && playlistIndexToDelete >= 0, "The playlist index is invalid.");

                const PlaylistInfo& playlistInfo = m_TracksQueue.GetPlaylistInfos()[playlistIndexToDelete];

                from = playlistInfo.beginIndex;
                to = playlistInfo.endIndex;
            }

            //O_ASSERT(fromParamIndex != -1 || toParamIndex != -1, "The command is empty, nothing to execute.");
            O_ASSERT(from >= 0 && from < m_TracksQueue.GetSize() - 1 && to > 0 && to <= m_TracksQueue.GetSize() - 1, "\"from or \"to\" is outside of the range.");

            const bool isCurrentTrackInRange = m_CurrentTrackIndex >= from && m_CurrentTrackIndex <= to;

            if(isCurrentTrackInRange)
            {
                voice->voiceclient->stop_audio();
                m_Player.Skip();

                m_CurrentTrackIndex = from;
            }

            m_TracksQueue.DeleteTracks(from, to);
        }
    }

    void OrchestraDiscordBot::CommandStop(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        const dpp::voiceconn* v = IsVoiceConnectionReady(message.msg.guild_id);

        m_Player.Stop();
        m_IsStopped = true;

        m_CurrentTrackIndex = std::numeric_limits<uint32_t>::max();
        /*{
            std::lock_guard lock{ m_TracksQueueMutex };
            m_TracksQueue.Clear();
        }*/

        v->voiceclient->pause_audio(m_Player.GetIsPaused());
        v->voiceclient->stop_audio();
    }
    void OrchestraDiscordBot::CommandPause(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        const dpp::voiceconn* v = IsVoiceConnectionReady(message.msg.guild_id);

        m_Player.Pause(!m_Player.GetIsPaused());
        v->voiceclient->pause_audio(m_Player.GetIsPaused());

        Reply(message, "Pause is ", (m_Player.GetIsPaused() ? "on" : "off"), '.');
    }
    void OrchestraDiscordBot::CommandSkip(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        const dpp::voiceconn* v = IsVoiceConnectionReady(message.msg.guild_id);
        {
            std::lock_guard queueLock{ m_TracksQueueMutex };
            O_ASSERT(m_TracksQueue.GetSize() != 0, "The tracks queue is empty.");
        }
        //not sure about this one
        O_ASSERT(m_Player.GetDecodersCount() != 0, "Decoders are empty.");

        //ugly shit
        bool skipPlaylist = false;
        GetParamValue(params, "playlist", skipPlaylist);

        int toindex = -1;
        if(!skipPlaylist)
            GetParamValue(params, "to", toindex);

        int inindex = 0;
        if(!skipPlaylist && toindex < 0)
            GetParamValue(params, "in", inindex);

        bool skipToMiddle = false;
        if(!skipPlaylist && toindex < 0 && inindex == 0)
            GetParamValue(params, "middle", skipToMiddle);

        bool skipToLast = false;
        if(!skipPlaylist && toindex < 0 && inindex == 0 && !skipToMiddle)
            GetParamValue(params, "last", skipToLast);

        float tosecs = 0.f;

        const int tosecsIndex = GetParamIndex(params, "tosecs");

        if(!skipPlaylist && toindex < 0 && inindex == 0 && !skipToMiddle && !skipToLast && tosecsIndex != -1)
            tosecs = GetParamValue<int>(params, tosecsIndex);

        float secs = 0.f;
        if(!skipPlaylist && toindex < 0 && inindex == 0 && !skipToMiddle && !skipToLast && tosecsIndex == -1)
            GetParamValue(params, "secs", secs);

        bool skip = true;
        int skipToIndex = m_CurrentTrackIndex;

        if(skipPlaylist)
        {
            std::lock_guard queueLock{ m_TracksQueueMutex };
            bool found = false;
            for(const auto& playlistInfo : m_TracksQueue.GetPlaylistInfos())
            {
                const bool isInRange = m_CurrentTrackIndex >= playlistInfo.beginIndex && m_CurrentTrackIndex <= playlistInfo.endIndex;

                if(isInRange)
                {
                    skipToIndex = playlistInfo.endIndex + 1;
                    found = true;
                    break;
                }
            }

            O_ASSERT(found, "Current track is not in a playlist.");
        }
        else if(toindex != -1)
        {
            std::lock_guard queueLock{ m_TracksQueueMutex };
            O_ASSERT(toindex >= 0 && toindex < m_TracksQueue.GetSize(), "Parameter \"toindex\" is invalid.");

            skipToIndex = toindex;
        }
        else if(inindex != 0)
        {
            std::lock_guard queueLock{ m_TracksQueueMutex };
            O_ASSERT(m_CurrentTrackIndex + inindex < m_TracksQueue.GetSize(), "Parameter \"inindex\" is invalid.");

            skipToIndex = m_CurrentTrackIndex + inindex;
        }
        else if(skipToMiddle)
        {
            std::lock_guard lockCurrentTrack{ m_TracksQueueMutex };
            skipToIndex = m_TracksQueue.GetSize() / 2;
        }
        else if(skipToLast)
        {
            std::lock_guard lockCurrentTrack{ m_TracksQueueMutex };
            skipToIndex = m_TracksQueue.GetSize() - 1;
        }
        else if(tosecsIndex != -1)
        {
            skip = false;
            m_Player.SkipToSeconds(tosecs, 0);
        }
        else if(secs != 0.f)
        {
            skip = false;
            m_Player.SkipSeconds(secs - v->voiceclient->get_secs_remaining(), 0);
        }
        else
            skipToIndex++;

        m_CurrentTrackIndex = skipToIndex;

        if(skip)
            m_Player.Skip();
        v->voiceclient->stop_audio();
    }
    void OrchestraDiscordBot::CommandLeave(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        //just to assert
        IsVoiceConnectionReady(message.msg.guild_id);

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

        message_create(msg, [event, embeds, index, this](const dpp::confirmation_callback_t& cb) {
            if(!cb.is_error())
                SendEmbedsSequentially(event, embeds, index + 1);
            });
    }

    void OrchestraDiscordBot::WaitUntilJoined(const std::chrono::milliseconds& delay)
    {
        std::unique_lock joinLock{ m_JoinMutex };

        O_ASSERT(m_JoinedCondition.wait_for(joinLock, delay, [this] { return m_IsJoined == true; }), "The connection delay was bigger than ", delay.count(), "ms.");
    }
    dpp::voiceconn* OrchestraDiscordBot::IsVoiceConnectionReady(const dpp::snowflake& guildSnowflake)
    {
        dpp::voiceconn* voice = get_shard(0)->get_voice(guildSnowflake);

        O_ASSERT(m_IsJoined && voice || voice->voiceclient || voice->voiceclient->is_ready(), "Failed to establish connection to a voice channel in a guild with id ", guildSnowflake);

        return voice;
    }

    void OrchestraDiscordBot::AddTrack(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
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

        PlayParams playParams{};

        //as urls may contain non-English letters
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

            GetParamValue(params, "speed", playParams.speed);
            GetParamValue(params, "repeat", playParams.repeat);
            if(playParams.repeat < 0)
                playParams.repeat = std::numeric_limits<int>::max();

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

                m_TracksQueue.FetchSearch(wValue, searchEngine, playParams.speed, playParams.repeat);
            }
            else
            {
                GetParamValue(params, "shuffle", playParams.doShuffle);
                //try
                //{
                m_TracksQueue.FetchURL(wValue, playParams.doShuffle, playParams.speed, static_cast<size_t>(playParams.repeat));
                //}
                //catch(const OrchestraException& e)
                //{
                //    //probably raw
                //    playParams.isRaw = true;
                //    if(!IsValidURL(value.data()) && message.msg.author.id != m_AdminSnowflake)
                //    {
                //        //Reply(message, "You don't have permission to access admin's local files.");

                //        O_THROW("The user tried to access admin's file, while not being the admin.");
                //    }

                //    m_TracksQueue.FetchRaw(value);
                //}
            }
        }
        else
        {
            if(!IsValidURL(value.data()) && message.msg.author.id != m_AdminSnowflake)
                //Reply(message, "You don't have permission to access admin's local files.");
                O_THROW("The user tried to access admin's file, while not being the admin.");

            m_TracksQueue.FetchRaw(value);
        }

        //return playParams;
    }

    void OrchestraDiscordBot::ReplyWithInfoAboutTrack(const dpp::message_create_t& message, const TrackInfo& trackInfo, const bool& outputURL, const bool& printCurrentTimestamp)
    {
        std::wstring description = Logger::Format(L"**[", m_CurrentTrackIndex, "]** ");

        if(trackInfo.title.empty())
            description += Logger::Format(L"The name of the track is unknown.\n");
        else
            description += Logger::Format(L"**", trackInfo.title, L"** is going to be played.\n");

        if(trackInfo.duration > 0.f)
            description += Logger::Format(L"The track's duration: **", trackInfo.duration, L"** seconds.\n");
        if(printCurrentTimestamp)
        {
            const dpp::voiceconn* voice = IsVoiceConnectionReady(message.msg.guild_id);
            description += Logger::Format(L"Current timestamp: ", m_Player.GetCurrentDecodingDurationSeconds() - voice->voiceclient->get_secs_remaining(), " seconds.\n");
        }
        if(trackInfo.speed != 1.f)
            description += Logger::Format(L"The speed: ", trackInfo.speed, ". The duration with speed applied: ", trackInfo.duration * trackInfo.speed, L" seconds.\n");
        if(trackInfo.repeat > 1)
            description += Logger::Format(L"Repeat count: ", trackInfo.repeat, L".\n");

        if(outputURL)
            description += Logger::Format(L"URL: ", (trackInfo.URL.empty() ? StringToWString(trackInfo.rawURL) : trackInfo.URL));

        Reply(message, WStringToString(description));
    }

    void OrchestraDiscordBot::SetEnableLogSentPackets(const bool& enable)
    {
        m_Player.SetEnableLogSentPackets(enable);
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
    uint32_t OrchestraDiscordBot::GetSentPacketSize() const noexcept
    {
        return m_Player.GetSentPacketSize();
    }
    dpp::snowflake OrchestraDiscordBot::GetAdminSnowflake() const noexcept
    {
        return m_AdminSnowflake;
    }
}