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

using namespace GuelderConsoleLog;

namespace Orchestra
{
    OrchestraDiscordBot::OrchestraDiscordBot(const std::string& token, std::string yt_dlpPath, std::string commandPrefix, char paramPrefix, uint32_t intents)
        : DiscordBot(token, std::move(commandPrefix), paramPrefix, intents), m_IsReady(false)
    {
        on_guild_create(
            [this, _yt_dlpPath = yt_dlpPath, commandPrefix, paramPrefix](const dpp::guild_create_t& event)
            {
                std::lock_guard guildLock{ m_GuildCreateMutex };
                if(!m_GuildsBotPlayers.contains(event.created->id))
                    m_GuildsBotPlayers[event.created->id] = BotPlayer{ _yt_dlpPath, 0, commandPrefix, paramPrefix };
            }
        );

        on_voice_state_update(
            [this](const dpp::voice_state_update_t& voiceState)
            {
                BotPlayer& botPlayer = GetBotPlayer(voiceState.state.guild_id);

                if(voiceState.state.user_id == this->me.id)
                {
                    if(!voiceState.state.channel_id.empty())
                    {
                        botPlayer.isJoined = true;
                        GE_LOG(Orchestra, Info, "Has just joined to ", voiceState.state.channel_id, " channel in ", voiceState.state.guild_id, " guild.");
                    }
                    else
                    {
                        botPlayer.isJoined = false;
                        botPlayer.player.Stop();
                        GE_LOG(Orchestra, Info, "Has just disconnected from voice channel.");
                    }

                    botPlayer.joinedCondition.notify_all();
                    botPlayer.player.Pause(false);
                }
            }
        );

        //TODO: is this necessary?
        /*on_ready(
            [this, _yt_dlpPath = std::move(yt_dlpPath), commandPrefix, paramPrefix](const dpp::ready_t& readyEvent)
            {
                for(auto&& guildID : readyEvent.guilds)
                    if(!m_GuildsBotPlayers.contains(guildID))
                        m_GuildsBotPlayers[guildID] = BotPlayer{ _yt_dlpPath, 0, commandPrefix, paramPrefix };

                for(auto&& func : m_OnReadyCallbacks)
                    func();

                m_OnReadyCallbacks.clear();
                m_IsReady = true;
            }
        );*/

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
        //add possibility of making bass boost? - DONE
        //add configurable frequencies - DONE
        //rename to Orchestra and find an avatar - DONE
        //add queue command - DONE
        //add disconnecting if nobody is in the channel or if count of track is zero for some time
        //add more params to CommandDelete, CommandSpeed, CommandRepeat from, to, index, middle, last, playlist (boring shit) - DONE
        //Queue Roadmap:
        //I need some vector with music values in order not to change a lot code in CommandPlay - DONE

        //Playlists Roadmap:
        //1. Add shuffling - DONE
        //2. Add skipping multiple tracks - DONE
        //3. Add getting to a certain track(almost the same as the 2nd clause) - DONE

        //TODO: custom static func to print messages, as the message cannot exceed 2000 letters - DONE
        //TODO: use PlaylistInfo - DONE

        //TODO: BEAUTY!!!!
        //TODO: fix Current timestamp: 7.79999s. if it is possible - PARTIALLY

        //holy crap! it appears that wchar_t is useless...

        //FIX THIS SHIT
        //[0] The Chauffeur (2005 Remaster) is going to be played.
        //The track's duration: 324 seconds.
        //Current timestamp: 331.92 seconds.

        //help
        AddCommand({ "help",
            std::bind(&OrchestraDiscordBot::CommandHelp, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {},
            "Prints out all available commands and also the command's description if it exists."
            });
        //play
        AddCommand({ "play",
            std::bind(&OrchestraDiscordBot::CommandPlay, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{Type::Float, "speed", "Changes speed of audio. If speed < 1: audio plays faster."},
                ParamProperties{Type::Float, "bass", "Sets bass-boost."},
                ParamProperties{Type::Float, "basshz", "Sets bass-boost to a specific frequency. By default it equals 110."},
                ParamProperties{Type::Float, "bassband", "Sets bandwidth to the bass-boost. By default it equals 0.3."},
                ParamProperties{Type::Int, "repeat", Logger::Format("Repeats audio for certain number. If repeat < 0: the audio will be playing for ", std::numeric_limits<int>::max(), " times.")},
                ParamProperties{Type::Bool,"search", "If search is explicitly set: it will search or not search via yt-dlp."},
                ParamProperties{Type::String, "searchengine", "A certain search engine that will be used to find URL. Supported: yt - Youtube(default), sc - SoundCloud."},
                ParamProperties{Type::Bool, "noinfo", "If noinfo is true: the info(name, URL(optional), duration) about track won't be sent."},
                ParamProperties{Type::Bool, "raw", "If raw is false: it won't use yt-dlp for finding a raw URL to audio."},
                ParamProperties{Type::Int, "index", "The index of a playlist item. Used only if input music value is a playlist."},
                ParamProperties{Type::Bool, "shuffle", "Whether to shuffle tracks of a playlist."}
            },
            "Joins your voice channel and plays audio from YouTube or SoundCloud, or from raw URL to audio, or admin's local files(note: a whole bunch of formats are not supported). Playlists are supported!"
            });
        //playlist
        AddCommand({ "playlist",
            std::bind(&OrchestraDiscordBot::CommandPlaylist, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{Type::String, "name", "The name of playlist to add/change."},
                ParamProperties{Type::Int, "from", "The begin index of the playlist."},
                ParamProperties{Type::Int, "to", "The end index of the playlist."},
                ParamProperties{Type::Float, "speed", "The speed to set to the tracks."},
                ParamProperties{Type::Int, "repeat", "The repeat count of the playlist."},
                ParamProperties{Type::Int, "delete", "Deletes playlist by index."}
            },
            "Adds, changes, deletes playlists. By default it makes a playlist. If input indicies intersect with other playlists, they get destroyed."
            });
        //speed
        AddCommand({ "speed",
            std::bind(&OrchestraDiscordBot::CommandSpeed, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{Type::Int, "index", "The index of the track to change speed."},
                ParamProperties{Type::Bool, "middle", "The middle track of the queue."},
                ParamProperties{Type::Bool, "last", "The last track of the queue."},
                ParamProperties{Type::Int, "from", "Sets the speed to the tracks from given index to the end of the queue or if used with \"to\" param, it will set the speed to the given range of tracks."},
                ParamProperties{Type::Int, "to", "Sets the speed to the tracks from the queue from the beginning to the given index or if used with \"from\" param, it will set the speed to the given range of tracks."},
                ParamProperties{Type::Int, "playlist", "Sets the speed to the given playlist."}
            },
            "Sets speed to the track or tracks that correspond to the given index or range of indices. By default it sets speed to the current track."
            });
        //bass
        AddCommand({ "bass",
            std::bind(&OrchestraDiscordBot::CommandBass, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{Type::Bool, "clear", "Clears bass-boost."},
                ParamProperties{Type::Float, "hz", "Sets bass-boost to a specific frequency. By default it equals 110."},
                ParamProperties{Type::Float, "band", "Sets bandwidth to the bass-boost. By default it equals 0.3."}
            },
            "Sets bass-boost decibels to the queue, that means that it is not specific to any tracks, like speed or repeat command are, but rather global. By default the value is zero."
            });
        //equalizer
        AddCommand({ "equalizer",
            std::bind(&OrchestraDiscordBot::CommandEqualizer, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{Type::Bool, "clear", "Clears equalizer."},
                ParamProperties{Type::Float, "hz", "A frequency to adjust."},
                ParamProperties{Type::Float, "delete", "Whether to delete frequency. Note that command value is ignored."}
            },
            "Sets equalizer frequencies(Hz) and decibels boosts to them to the queue, that means that it is not specific to any tracks, like speed or repeat command are, but rather global. By default the value is zero."
            });
        //repeat
        AddCommand({ "repeat",
            std::bind(&OrchestraDiscordBot::CommandRepeat, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{Type::Int, "index", "The index of the track to change repeat count."},
                ParamProperties{Type::Bool, "middle", "The middle track of the queue."},
                ParamProperties{ Type::Bool,"last", "The last track of the queue."},
                ParamProperties{Type::Int, "from", "Sets the repeat count to the tracks from given index to the end of the queue or if used with \"to\" param, it will set the repeat count to the given range of tracks."},
                ParamProperties{Type::Int, "to", "Sets the repeat count to the tracks from the queue from the beginning to the given index or if used with \"from\" param, it will set the repeat count to the given range of tracks."},
                ParamProperties{Type::Int, "playlist", "Sets the  repeat count to the given playlist."}
            },
            "Sets repeat count to the track or tracks that correspond to the given index or range of indices. By default it sets repeat count to the current track."
            });
        //insert
        AddCommand({ "insert",
            std::bind(&OrchestraDiscordBot::CommandInsert, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{Type::Int, "after", "An index after which track or tracks will be inserted."},
                ParamProperties{Type::Float, "speed", "Changes speed of audio. If speed < 1: audio plays faster. This param can be called while audio is playing."},
                ParamProperties{Type::Int, "repeat", Logger::Format("Repeats audio for certain number. If repeat < 0: the audio will be playing for ", std::numeric_limits<int>::max(), " times.")},
                ParamProperties{Type::Bool, "search", "If search is explicitly set: it will search or not search via yt-dlp."},
                ParamProperties{Type::String, "searchengine", "A certain search engine that will be used to find URL. Supported: yt - Youtube(default), sc - SoundCloud."},
                ParamProperties{Type::Bool, "raw", "If raw is false: it won't use yt-dlp for finding a raw URL to audio."},
                ParamProperties{Type::Bool, "shuffle", "Whether to shuffle tracks of a playlist."}
            },
            "Inserts tracks into specific position. By default it inserts tracks right after a current one."
            });
        //transfer
        AddCommand({ "transfer",
            std::bind(&OrchestraDiscordBot::CommandTransfer, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{Type::Int, "to", "An index after which track will be inserted."},
                ParamProperties{Type::Bool,"current",  "After current track."},
                ParamProperties{Type::Bool, "middle", "After the middle track."}
            },
            "Transfers track from index to index. You must provide int as a value to this command. By default it will transfer a track to the end."
            });
        //reverse
        AddCommand({ "reverse",
            std::bind(&OrchestraDiscordBot::CommandReverse, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{Type::Int, "from", "The index from which to the end queue will be reversed. If used with \"to\" param it will reverse the given range."},
                ParamProperties{Type::Int, "to", "The index to which from the beginning queue will be reversed. If used with \"from\" param it will reverse the given range."},
                ParamProperties{Type::Int, "playlist", "The index of playlist to reverse. If this param is equal to -1, it'll choose the current playlist."}
            },
            "Reverses the order of tracks."
            });
        //skip
        AddCommand({ "skip",
            std::bind(&OrchestraDiscordBot::CommandSkip, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{Type::Float, "secs", "Skips given amount of seconds."},
                ParamProperties{Type::Float, "tosecs", "If tosecs < 0: it will skip to the beginning of the audio. Skips up to the given time."},
                ParamProperties{Type::Int, "in", "Skips in the given index in the current playing playlist."},
                ParamProperties{Type::Int, "to", "Skips to the given index in the current playing playlist. If toindex is bigger than a playlist size, then it will skip entire playlist."},
                ParamProperties{Type::Bool, "playlist", "Skips entire playlist."},
                ParamProperties{Type::Bool, "middle", "Skips to the middle of the queue."},
                ParamProperties{Type::Bool, "last", "Skips to the last track."}
            },
            "Skips current track."
            });
        //shuffle
        AddCommand({ "shuffle",
            std::bind(&OrchestraDiscordBot::CommandShuffle, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{Type::Int, "from", "From which to shuffle"},
                ParamProperties{Type::Int, "to", "To which shuffle"},
                ParamProperties{Type::Int, "playlist", "Playlist index to shuffle."}
            },
            "Shuffles all tracks that are in the queue. Forgets about playlists repeats, but not the speeds."
            });
        //delete
        AddCommand({ "delete",
            std::bind(&OrchestraDiscordBot::CommandDelete, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{Type::Bool, "current", "Deletes current track from the queue."},
                ParamProperties{Type::Bool, "middle", "Deletes middle track from the queue."},
                ParamProperties{Type::Bool, "last", "Deletes last track from the queue."},
                ParamProperties{Type::Int, "from", "Deletes track from given index to the end of the queue or if used with \"to\" param, it will delete the given range of tracks."},
                ParamProperties{Type::Int, "to", "Deletes track from the queue from the beginning to the given index or if used with \"from\" param, it will delete the given range of tracks."},
                ParamProperties{Type::Int, "playlist", "Deletes playlist by index. If input \"playlist\" == -1, it will remove current playlist."}
            },
            "Deletes tracks in the queue by index. It is not recommended to use this command to delete a range of tracks, but you can try."
            });
        //current
        AddCommand({ "current",
            std::bind(&OrchestraDiscordBot::CommandCurrentTrack, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{Type::Bool, "url", "Whether to show url of track."}
            },
            "Prints info about current track, if it plays."
            });
        //queue
        AddCommand({ "queue",
            std::bind(&OrchestraDiscordBot::CommandQueue, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{Type::Bool, "url", "Whether to show urls of tracks."}
            },
            "Prints current queue of tracks."
            });
        //pause
        AddCommand({ "pause",
            std::bind(&OrchestraDiscordBot::CommandPause, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {},
            "Pauses the audio."
            });
        //stop
        AddCommand({ "stop",
            std::bind(&OrchestraDiscordBot::CommandStop, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {},
            "Stops all audio."
            });
        //leave
        AddCommand({ "leave",
            std::bind(&OrchestraDiscordBot::CommandLeave, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {},
            "Disconnects from a voice channel."
            });
        //terminate
        AddCommand({ "terminate",
            std::bind(&OrchestraDiscordBot::CommandTerminate, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {},
            "Terminates the bot. Only admin can use this command."
            });
    }
}
//commands
namespace Orchestra
{
    void OrchestraDiscordBot::CommandHelp(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        //idk. for some reason(probably because of long description) it doesn't send any embeds with DPP_MAX_EMBED_SIZE or 11
        constexpr size_t _DPP_MAX_EMBED_SIZE = 10;

        const size_t totalFieldsSize = m_Commands.size();

        const size_t totalEmbedsCount = std::ceil(static_cast<float>(totalFieldsSize) / _DPP_MAX_EMBED_SIZE);

        std::vector<dpp::embed> embeds;
        embeds.reserve(totalEmbedsCount);

        size_t commandIndex = 0;

        for(size_t embedsCount = 0; embedsCount < totalEmbedsCount; embedsCount++)
        {
            dpp::embed embed;

            //reserving
            {
                const size_t remaining = totalFieldsSize - embedsCount * _DPP_MAX_EMBED_SIZE;
                embed.fields.reserve(remaining > _DPP_MAX_EMBED_SIZE ? _DPP_MAX_EMBED_SIZE : remaining);
            }
            if(embedsCount == 0)
                embed.set_title(Logger::Format("Help. Total Commands size: ", totalFieldsSize, '.'));

            for(; commandIndex < totalFieldsSize; commandIndex++)
            {
                if(embed.fields.size() + 1 > _DPP_MAX_EMBED_SIZE)
                    break;

                const Command& command = m_Commands[commandIndex];

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

            embeds.push_back(std::move(embed));
        }

        SendEmbedsSequentially(message, embeds);
    }

    void OrchestraDiscordBot::CommandCurrentTrack(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);

        {
            std::lock_guard lock{ botPlayer.tracksQueueMutex };
            if(!botPlayer.tracksQueue.GetTracksSize())
            {
                Reply(message, "I'm not even playing anything!");
                return;
            }
        }

        bool showURL = false;
        GetParamValue(params, "url", showURL);

        std::lock_guard lock{ botPlayer.tracksQueueMutex };

        ReplyWithInfoAboutTrack(message.msg.guild_id, message, botPlayer.tracksQueue.GetTrackInfo(botPlayer.currentTrackIndex), showURL, true);
    }
    void OrchestraDiscordBot::CommandQueue(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);

        std::lock_guard lock{ botPlayer.tracksQueueMutex };

        dpp::voiceconn* voice = IsVoiceConnectionReady(message.msg.guild_id);

        O_ASSERT(botPlayer.tracksQueue.GetTracksSize() > 0, "The queue is empty!");

        bool showURLs = false;
        GetParamValue(params, "url", showURLs);

        const size_t queueSize = botPlayer.tracksQueue.GetTracksSize();
        const size_t playlistsOffset = botPlayer.tracksQueue.GetPlaylistsSize() * 2;//as playlist contains beginIndex and endIndex which should be displayed

        const size_t totalFieldsSize = queueSize + playlistsOffset;

        const size_t totalEmbedsCount = std::ceil(static_cast<float>(totalFieldsSize) / DPP_MAX_EMBED_SIZE);

        size_t trackIndex = 0;

        size_t playlistInfoIndex = botPlayer.tracksQueue.GetPlaylistInfos().empty() ? std::numeric_limits<size_t>::max() : 0;

        std::string playlistTitle;

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
            if(embedsCount == 0)
                embed.set_title(Logger::Format("Tracks queue. The size: ", queueSize));

            for(; trackIndex < queueSize; trackIndex++)
            {
                if(embed.fields.size() + 1 > DPP_MAX_EMBED_SIZE)
                    break;

                const bool isPlaylistInfoValid = playlistInfoIndex != std::numeric_limits<size_t>::max();
                const PlaylistInfo* currentPlaylistInfo = isPlaylistInfoValid ? &botPlayer.tracksQueue.GetPlaylistInfos()[playlistInfoIndex] : nullptr;

                if(isPlaylistInfoValid && trackIndex == currentPlaylistInfo->beginIndex)
                {
                    playlistTitle = currentPlaylistInfo->title;

                    std::string playlistBeginField = Logger::Format("++(", playlistInfoIndex, ") ");

                    if(!playlistTitle.empty())
                        playlistBeginField += Logger::Format("**", (playlistTitle), "**. ");

                    playlistBeginField += Logger::Format("Playlist begins. Size: ", currentPlaylistInfo->endIndex - currentPlaylistInfo->beginIndex + 1, '.');

                    if(currentPlaylistInfo->repeat > 1)
                        playlistBeginField += Logger::Format(" Repeat count: ", currentPlaylistInfo->repeat, '.');

                    embed.add_field(playlistBeginField, "");
                }

                const auto& [URL, rawURL, title, duration, playlistIndex, repeat, speed] = botPlayer.tracksQueue.GetTrackInfo(trackIndex);

                std::string fieldTitle;

                if(trackIndex == botPlayer.currentTrackIndex)
                    fieldTitle = Logger::Format("> [", trackIndex, "] **", (title), "**");
                else
                    fieldTitle = Logger::Format("[", trackIndex, "] **", (title), "**");

                std::string fieldValue;
                if(trackIndex == botPlayer.currentTrackIndex)
                    fieldValue += Logger::Format("Duration: ", duration, "s.\nCurrent timestamp: ", botPlayer.player.GetCurrentTimestamp() - voice->voiceclient->get_secs_remaining(), "s.\n");
                else
                    fieldValue += Logger::Format("Duration: ", duration, "s.\n");

                if(speed != 1.f)
                    fieldValue += Logger::Format("Speed: ", speed, ".\nDuration with speed applied: ", static_cast<float>(duration) * speed, "s.\n");
                if(repeat > 1)
                    fieldValue += Logger::Format("Repeat count: ", repeat, ".\n");
                if(showURLs)
                    fieldValue += Logger::Format("URL: ", (URL.empty() ? rawURL : (URL)), "\n");

                embed.add_field(fieldTitle, fieldValue, false);

                if(isPlaylistInfoValid && trackIndex == currentPlaylistInfo->endIndex)
                {
                    if(embed.fields.size() + 1 > DPP_MAX_EMBED_SIZE)
                        break;

                    std::string playlistEndField = Logger::Format("--(", playlistInfoIndex, ") ");

                    if(!playlistTitle.empty())
                        playlistEndField += Logger::Format("**", (playlistTitle), "**. ");

                    playlistEndField += "Playlist ends.";

                    embed.add_field(playlistEndField, "");

                    playlistTitle.clear();

                    playlistInfoIndex++;

                    if(playlistInfoIndex >= botPlayer.tracksQueue.GetPlaylistsSize())
                        playlistInfoIndex = std::numeric_limits<size_t>::max();
                }
            }

            embeds.push_back(std::move(embed));
        }

        SendEmbedsSequentially(message, embeds);
    }
    void OrchestraDiscordBot::CommandPlay(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);

        O_ASSERT(!value.empty(), "No music value provided.");

        //join
        if(!botPlayer.isJoined)
            ConnectToMemberVoice(message);

        botPlayer.isStopped = false;

        std::unique_lock _queueLock{ botPlayer.tracksQueueMutex };

        const size_t tracksNumberBefore = botPlayer.tracksQueue.GetTracksSize();

        AddToQueue(message, params, value.data());

        if(tracksNumberBefore)
            return;

        int beginIndex = 0;
        GetParamValue(params, "index", beginIndex);
        botPlayer.currentTrackIndex = beginIndex;

        //joining stuff
        {
            WaitUntilJoined(message.msg.guild_id, std::chrono::seconds(2));

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

        if(!botPlayer.isStopped)
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

            struct
            {
                float decibelsBoost = 0.f;
                float frequencyToAdjust = 110.f;
                float bandwidth = .3f;
            } bassBoostSettings;

            GetParamValue(params, "bass", bassBoostSettings.decibelsBoost);
            GetParamValue(params, "basshz", bassBoostSettings.frequencyToAdjust);
            GetParamValue(params, "bassband", bassBoostSettings.bandwidth);

            botPlayer.player.SetBassBoost(bassBoostSettings.decibelsBoost, bassBoostSettings.frequencyToAdjust, bassBoostSettings.bandwidth);

            for(size_t i = 0, playlistRepeated = 0, trackRepeated = 0; true; ++i)
            {
                const TrackInfo* currentTrackInfo = nullptr;

                bool caughtException = false;
                try
                {
                    std::lock_guard playLock{ botPlayer.playMutex };
                    {
                        std::lock_guard queueLock{ botPlayer.tracksQueueMutex };

                        if(botPlayer.currentTrackIndex >= botPlayer.tracksQueue.GetTracksSize())
                            break;

                        currentTrackInfo = &botPlayer.tracksQueue.GetTrackInfos()[botPlayer.currentTrackIndex];

                        prevTrackIndex = currentTrackInfo->uniqueIndex;

                        if(currentTrackInfo->rawURL.empty())
                            currentTrackInfo = &botPlayer.tracksQueue.GetRawTrackURL(botPlayer.currentTrackIndex);

                        GE_LOG(Orchestra, Info, "Received raw URL to a track: ", currentTrackInfo->rawURL);

                        botPlayer.player.SetDecoder(currentTrackInfo->rawURL, Decoder::DEFAULT_SAMPLE_RATE * currentTrackInfo->speed);

                        //printing info about the track
                        if(!noInfo)
                        {
                            if(currentTrackInfo->title.empty())
                            {
                                //it is raw, trying to get title with ffmpeg
                                std::string title;
                                std::string titleTmp = botPlayer.player.GetTitle();

                                if(!titleTmp.empty())
                                    title = (titleTmp);

                                if(!title.empty())
                                    botPlayer.tracksQueue.SetTrackTitle(botPlayer.currentTrackIndex, title);
                            }
                            if(currentTrackInfo->duration == 0.f)
                            {
                                //it is raw, trying to get duration with ffmpeg
                                float duration;
                                duration = botPlayer.player.GetTotalDuration();
                                if(duration != 0.f)
                                    botPlayer.tracksQueue.SetTrackDuration(0, duration);
                            }

                            TrackInfo tmp = *currentTrackInfo;
                            tmp.repeat -= trackRepeated;

                            ReplyWithInfoAboutTrack(message.msg.guild_id, message, tmp);
                        }
                    }

                    botPlayer.player.DecodeAndSendAudio(voice);
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

                //indices shit. I strongly hope that it is the last version of this shit, cuz I'm tired by this stuff
                std::lock_guard queueLock{ botPlayer.tracksQueueMutex };

                if(caughtException)
                {
                    //haven't tested this
                    botPlayer.tracksQueue.DeleteTrack(botPlayer.currentTrackIndex);
                    continue;
                }

                if(botPlayer.currentTrackIndex >= botPlayer.tracksQueue.GetTracksSize())
                    break;

                currentTrackInfo = &botPlayer.tracksQueue.GetTrackInfos()[botPlayer.currentTrackIndex];

                const bool wasTrackSkipped = prevTrackIndex != currentTrackInfo->uniqueIndex;
                bool incrementCurrentIndex = !wasTrackSkipped;

                if(!wasTrackSkipped)
                    trackRepeated++;

                bool found = false;
                for(size_t j = 0; j < botPlayer.tracksQueue.GetPlaylistsSize(); j++)
                {
                    const PlaylistInfo& playlistInfo = botPlayer.tracksQueue.GetPlaylistInfos()[j];

                    const bool isInRange = botPlayer.currentTrackIndex >= playlistInfo.beginIndex && botPlayer.currentTrackIndex <= playlistInfo.endIndex;

                    if(isInRange)
                    {
                        currentPlaylistInfo = &playlistInfo;
                        isInPlaylist = true;
                        playlistRepeat = playlistInfo.repeat;
                        found = true;

                        if(prevPlaylistIndex != currentPlaylistInfo->uniqueIndex)
                            playlistRepeated = 0;

                        botPlayer.currentPlaylistIndex = j;

                        break;
                    }
                }
                if(!found)
                {
                    isInPlaylist = false;
                    currentPlaylistInfo = nullptr;
                    prevPlaylistIndex = std::numeric_limits<size_t>::max();
                    playlistRepeated = 0;
                    botPlayer.currentPlaylistIndex = std::numeric_limits<uint32_t>::max();
                }

                if(!wasTrackSkipped)
                {
                    if(isInPlaylist && botPlayer.currentTrackIndex == currentPlaylistInfo->endIndex)
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
                            botPlayer.currentTrackIndex = currentPlaylistInfo->beginIndex;
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
                    prevPlaylistIndex = currentPlaylistInfo->uniqueIndex;

                if(incrementCurrentIndex)
                {
                    ++botPlayer.currentTrackIndex;
                    trackRepeated = 0;
                }
            }

            {
                std::lock_guard queueLock{ botPlayer.tracksQueueMutex };
                botPlayer.tracksQueue.Clear();
            }

            botPlayer.currentTrackIndex = 0;

            botPlayer.player.ResetDecoder();
        }
    }

    void OrchestraDiscordBot::CommandPlaylist(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);

        std::lock_guard queueLock{ botPlayer.tracksQueueMutex };

        int deleteIndex = -1;
        GetParamValue(params, "delete", deleteIndex);

        if(deleteIndex == -1)
        {
            int from = 0;
            int fromParamIndex = GetParamIndex(params, "from");
            if(fromParamIndex != -1)
                from = GetParamValue<int>(params, fromParamIndex);

            int to = botPlayer.tracksQueue.GetTracksSize() - 1;
            int toParamIndex = GetParamIndex(params, "to");
            if(toParamIndex != -1)
                to = GetParamValue<int>(params, toParamIndex);

            float speed = 1.f;
            GetParamValue(params, "speed", speed);

            std::string name;
            GetParamValue(params, "name", name);

            int repeat = 1;
            GetParamValue(params, "repeat", repeat);
            if(repeat < 0)
                repeat = std::numeric_limits<int>::max();

            O_ASSERT(from < to && to < botPlayer.tracksQueue.GetTracksSize(), "Invalid start or end indicies.");
            O_ASSERT(speed > 0.f, "Invalid speed value.");

            for(size_t i = from; i <= to; i++)
                botPlayer.tracksQueue.SetTrackSpeed(i, speed);

            if(botPlayer.currentTrackIndex >= from && botPlayer.currentTrackIndex <= to)
                botPlayer.player.SetAudioSampleRate(Decoder::DEFAULT_SAMPLE_RATE * speed);

            botPlayer.tracksQueue.AddPlaylist(from, to, speed, repeat, (name));
        }
        else
        {
            O_ASSERT(deleteIndex >= 0 && deleteIndex < botPlayer.tracksQueue.GetPlaylistsSize(), "Invalid delete index");

            botPlayer.tracksQueue.DeletePlaylist(deleteIndex);
        }
    }

    void OrchestraDiscordBot::CommandSpeed(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        using namespace GuelderResourcesManager;

        IsVoiceConnectionReady(message.msg.guild_id);

        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);

        std::lock_guard queueLock{ botPlayer.tracksQueueMutex };

        O_ASSERT(botPlayer.tracksQueue.GetTracksSize() > 0, "The tracks queue size is 0.");
        O_ASSERT(!value.empty(), "No speed value provided.");

        const float speed = StringToNumber<float>(value);

        O_ASSERT(speed > 0.f, "Invalid speed value");

        const int fromParamIndex = GetParamIndex(params, "from");
        int from = 0;
        if(fromParamIndex != -1)
            from = params[fromParamIndex].GetValue<int>();

        const int toParamIndex = GetParamIndex(params, "to");
        int to = botPlayer.tracksQueue.GetTracksSize() - 1;
        if(toParamIndex != -1)
            to = params[toParamIndex].GetValue<int>();

        int playlistParamIndex = -1;

        if(fromParamIndex == -1 && toParamIndex == -1)
        {
            playlistParamIndex = GetParamIndex(params, "playlist");

            if(playlistParamIndex != -1)
            {
                int playlistIndex = GetParamValue<int>(params, playlistParamIndex);

                if(playlistIndex == -1)
                    playlistIndex = GetCurrentPlaylistIndex(message.msg.guild_id);

                O_ASSERT(playlistIndex >= 0 && playlistIndex < botPlayer.tracksQueue.GetPlaylistsSize(), "Playlist index is invalid");

                from = botPlayer.tracksQueue.GetPlaylistInfos()[playlistIndex].beginIndex;
                to = botPlayer.tracksQueue.GetPlaylistInfos()[playlistIndex].endIndex;
            }
        }

        if(fromParamIndex != -1 || toParamIndex != -1 || playlistParamIndex != -1)
        {
            O_ASSERT(from >= 0 && from < to && to < botPlayer.tracksQueue.GetTracksSize(), "\"from or \"to\" is outside of the range.");

            if(from <= botPlayer.currentTrackIndex && to >= botPlayer.currentTrackIndex)
            {
                O_ASSERT(botPlayer.player.IsDecoderReady(), "The Decoder is not ready.");
                botPlayer.player.SetAudioSampleRate(Decoder::DEFAULT_SAMPLE_RATE * speed);
            }

            for(int i = from; i <= to; i++)
                botPlayer.tracksQueue.SetTrackSpeed(i, speed);
        }
        else
        {
            size_t index = botPlayer.currentTrackIndex;
            GetParamValue(params, "index", index);

            {
                bool deleteMiddleTrack = false;
                GetParamValue(params, "middle", deleteMiddleTrack);

                if(deleteMiddleTrack)
                    index = (botPlayer.tracksQueue.GetTracksSize() - 1) / 2;
                else
                {
                    bool deleteLastTrack = false;
                    GetParamValue(params, "last", deleteLastTrack);

                    if(deleteLastTrack)
                        index = botPlayer.tracksQueue.GetTracksSize() - 1;
                }
            }

            O_ASSERT(index < botPlayer.tracksQueue.GetTracksSize(), "Invalid index.");

            if(index == botPlayer.currentTrackIndex)
            {
                O_ASSERT(botPlayer.player.IsDecoderReady(), "The Decoder is not ready.");
                botPlayer.player.SetAudioSampleRate(Decoder::DEFAULT_SAMPLE_RATE * speed);
            }

            botPlayer.tracksQueue.SetTrackSpeed(index, speed);
        }
    }

    void OrchestraDiscordBot::CommandBass(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);

        const Player::BassBoostSettings bassBoostSettings = botPlayer.player.GetBassBoostSettings();

        bool clear = false;
        GetParamValue(params, "clear", clear);

        if(clear)
        {
            botPlayer.player.SetBassBoost(0.f, 110.f, .3f);
            return;
        }

        float decibelsBoost;

        try
        {
            decibelsBoost = GuelderResourcesManager::StringToNumber<float>(value);
        }
        catch(...)
        {
            decibelsBoost = bassBoostSettings.decibelsBoost;
        }

        float frequency = bassBoostSettings.frequency;
        GetParamValue(params, "hz", frequency);

        float bandwidth = bassBoostSettings.bandwidth;
        GetParamValue(params, "band", bandwidth);

        if(decibelsBoost == bassBoostSettings.decibelsBoost && frequency == bassBoostSettings.frequency && bandwidth == bassBoostSettings.bandwidth)
        {
            std::string reply = Logger::Format("The Bass-Boost:\nBass-boost decibels: ", decibelsBoost, ".\nBass-boost frequency: ", frequency, "\nBass-boost bandwidth: ", bandwidth, "\n");

            Reply(message, reply);
        }
        else
            botPlayer.player.SetBassBoost(decibelsBoost, frequency, bandwidth);
    }

    void OrchestraDiscordBot::CommandEqualizer(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);

        bool clear = false;
        GetParamValue(params, "clear", clear);

        if(clear)
        {
            botPlayer.player.ClearEqualizer();
            return;
        }

        bool addFrequency = false;

        GetParamValue(params, "delete", addFrequency);

        addFrequency = !addFrequency;

        float frequency = 0.f;
        int frequencyParamIndex = GetParamIndex(params, "hz");
        if(frequencyParamIndex != -1)
            frequency = GetParamValue<float>(params, frequencyParamIndex);

        if(frequency != 0.f)
            if(addFrequency)
            {
                float decibelsBoost = 0.f;

                try
                {
                    decibelsBoost = GuelderResourcesManager::StringToNumber<float>(value);
                }
                catch(...) {}

                botPlayer.player.InsertOrAssignEqualizerFrequency(frequency, decibelsBoost);
            }
            else
            {
                botPlayer.player.EraseEqualizerFrequency(frequency);
            }
        else
        {
            std::string reply;

            if(botPlayer.player.GetEqualizerFrequencies().empty())
                reply = "The Equalizer is empty.";
            else
            {
                reply = Logger::Format("The Equalizer. The Size: ", botPlayer.player.GetEqualizerFrequencies().size(), ".\n");

                for(const auto& [_frequency, decibels] : botPlayer.player.GetEqualizerFrequencies())
                    reply += Logger::Format('\n', _frequency, "hz - ", decibels, "db;");
            }

            Reply(message, reply);
        }
    }

    //the code is almost the same as in CommandSpeed
    void OrchestraDiscordBot::CommandRepeat(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        using namespace GuelderResourcesManager;

        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);

        //IsVoiceConnectionReady(message.msg.guild_id);

        std::lock_guard queueLock{ botPlayer.tracksQueueMutex };

        O_ASSERT(botPlayer.tracksQueue.GetTracksSize() > 0, "The tracks queue size is 0.");
        O_ASSERT(!value.empty(), "No speed value provided.");

        int repeatCount = StringToNumber<int>(value);

        O_ASSERT(repeatCount != 0, "Invalid repetCount value, repeatCount is 0.");

        if(repeatCount < 0)
            repeatCount = std::numeric_limits<int>::max();

        const int fromParamIndex = GetParamIndex(params, "from");
        int from = 0;
        if(fromParamIndex != -1)
            from = params[fromParamIndex].GetValue<int>();

        const int toParamIndex = GetParamIndex(params, "to");
        int to = botPlayer.tracksQueue.GetTracksSize() - 1;
        if(toParamIndex != -1)
            to = params[toParamIndex].GetValue<int>();

        int playlistParamIndex = -1;
        int playlistIndex;

        if(fromParamIndex == -1 && toParamIndex == -1)
        {
            playlistParamIndex = GetParamIndex(params, "playlist");

            if(playlistParamIndex != -1)
            {
                playlistIndex = GetParamValue<int>(params, playlistParamIndex);

                if(playlistIndex == -1)
                    playlistIndex = GetCurrentPlaylistIndex(message.msg.guild_id);

                O_ASSERT(playlistIndex >= 0 && playlistIndex < botPlayer.tracksQueue.GetPlaylistsSize(), "Playlist index is invalid");
            }
        }

        if(fromParamIndex != -1 || toParamIndex != -1 || playlistParamIndex != -1)
        {
            O_ASSERT(from >= 0 && from < to && to < botPlayer.tracksQueue.GetTracksSize(), "\"from or \"to\" is outside of the range.");

            if(playlistParamIndex == -1)
                for(int i = from; i <= to; i++)
                    botPlayer.tracksQueue.SetTrackRepeatCount(i, repeatCount);
            else
                botPlayer.tracksQueue.SetPlaylistRepeatCount(playlistIndex, repeatCount);
        }
        else
        {
            size_t index = botPlayer.currentTrackIndex;
            GetParamValue(params, "index", index);

            {
                bool deleteMiddleTrack = false;
                GetParamValue(params, "middle", deleteMiddleTrack);

                if(deleteMiddleTrack)
                    index = (botPlayer.tracksQueue.GetTracksSize() - 1) / 2;
                else
                {
                    bool deleteLastTrack = false;
                    GetParamValue(params, "last", deleteLastTrack);

                    if(deleteLastTrack)
                        index = botPlayer.tracksQueue.GetTracksSize() - 1;
                }
            }

            O_ASSERT(index < botPlayer.tracksQueue.GetTracksSize(), "Invalid index.");

            botPlayer.tracksQueue.SetTrackRepeatCount(index, repeatCount);
        }
    }

    void OrchestraDiscordBot::CommandInsert(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        //IsVoiceConnectionReady(message.msg.guild_id);

        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);

        std::lock_guard lock{ botPlayer.tracksQueueMutex };

        O_ASSERT(botPlayer.tracksQueue.GetTracksSize() > 0, "The queue is empty!");

        int after = botPlayer.currentTrackIndex + 1;

        if(const int afterParamIndex = GetParamIndex(params, "after"); afterParamIndex != -1)
        {
            after = GetParamValue<int>(params, afterParamIndex);

            if(after < 0 || after > botPlayer.tracksQueue.GetTracksSize())
                after = botPlayer.currentTrackIndex;

            if(after != 0)
                after++;
        }

        AddToQueue(message, params, value.data(), after);
    }

    void OrchestraDiscordBot::CommandTransfer(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);

        std::lock_guard queueLock{ botPlayer.tracksQueueMutex };

        O_ASSERT(botPlayer.tracksQueue.GetTracksSize() > 0, "The tracks queue is empty.");

        const int targetIndex = GuelderResourcesManager::StringToNumber<int>(value);

        int to = -1;

        GetParamValue(params, "to", to);

        if(to == -1)
        {
            const int middleParamIndex = GetParamIndex(params, "middle");
            if(middleParamIndex >= 0)
                to = (botPlayer.tracksQueue.GetTracksSize() - 1) / 2;
            else
                to = botPlayer.tracksQueue.GetTracksSize() - 1;
        }
        else
            if(to >= botPlayer.tracksQueue.GetTracksSize())
                to = botPlayer.tracksQueue.GetTracksSize() - 1;

        O_ASSERT(to >= 0 && targetIndex != to, "Invalid after index.");

        if(targetIndex == botPlayer.currentTrackIndex)
            botPlayer.currentTrackIndex = to;
        else if(to == botPlayer.currentTrackIndex)
            ++botPlayer.currentTrackIndex;

        botPlayer.tracksQueue.TransferTrack(targetIndex, to);
    }

    void OrchestraDiscordBot::CommandReverse(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        using namespace GuelderResourcesManager;

        IsVoiceConnectionReady(message.msg.guild_id);

        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);

        std::lock_guard queueLock{ botPlayer.tracksQueueMutex };

        O_ASSERT(botPlayer.tracksQueue.GetTracksSize() > 0, "The tracks queue size is 0.");

        const int fromParamIndex = GetParamIndex(params, "from");
        int from = 0;
        if(fromParamIndex != -1)
            from = params[fromParamIndex].GetValue<int>();

        const int toParamIndex = GetParamIndex(params, "to");
        int to = botPlayer.tracksQueue.GetTracksSize() - 1;
        if(toParamIndex != -1)
            to = params[toParamIndex].GetValue<int>();

        int playlistParamIndex = -1;

        if(fromParamIndex == -1 && toParamIndex == -1)
        {
            playlistParamIndex = GetParamIndex(params, "playlist");

            if(playlistParamIndex != -1)
            {
                int playlistIndex = GetParamValue<int>(params, playlistParamIndex);

                if(playlistIndex == -1)
                    playlistIndex = GetCurrentPlaylistIndex(message.msg.guild_id);

                O_ASSERT(playlistIndex >= 0 && playlistIndex < botPlayer.tracksQueue.GetPlaylistsSize(), "Playlist index is invalid");

                from = botPlayer.tracksQueue.GetPlaylistInfos()[playlistIndex].beginIndex;
                to = botPlayer.tracksQueue.GetPlaylistInfos()[playlistIndex].endIndex;
            }
        }

        O_ASSERT(from >= 0 && from < to && to < botPlayer.tracksQueue.GetTracksSize(), "\"from or \"to\" is outside of the range.");

        //fix this
        if(from <= botPlayer.currentTrackIndex || to == botPlayer.currentTrackIndex)
            botPlayer.currentTrackIndex = to - (botPlayer.currentTrackIndex - from);

        botPlayer.tracksQueue.Reverse(from, to);
    }

    void OrchestraDiscordBot::CommandShuffle(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        //IsVoiceConnectionReady(message.msg.guild_id);

        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);

        std::lock_guard queueLock{ botPlayer.tracksQueueMutex };

        O_ASSERT(botPlayer.tracksQueue.GetTracksSize() > 0, "The queue is empty.");

        int playlistIndex = -1;
        GetParamValue(params, "playlist", playlistIndex);

        int from = 0;
        int to = botPlayer.tracksQueue.GetTracksSize() - 1;

        const int fromParamIndex = GetParamIndex(params, "from");
        const int toParamIndex = GetParamIndex(params, "to");

        if(playlistIndex >= 0)
        {
            O_ASSERT(playlistIndex < botPlayer.tracksQueue.GetPlaylistsSize(), "Invalid playlist index.");

            from = botPlayer.tracksQueue.GetPlaylistInfo(playlistIndex).beginIndex;
            to = botPlayer.tracksQueue.GetPlaylistInfo(playlistIndex).endIndex;
        }
        else
        {
            if(fromParamIndex != -1)
                from = GetParamValue<int>(params, fromParamIndex);
            if(toParamIndex != -1)
                to = GetParamValue<int>(params, toParamIndex);
        }

        const bool isCurrentTrackInScope = botPlayer.currentTrackIndex >= from && botPlayer.currentTrackIndex <= to;

        if(fromParamIndex == -1 && toParamIndex == -1)
            botPlayer.tracksQueue.ClearPlaylists();

        botPlayer.tracksQueue.Shuffle(from, to + 1, (isCurrentTrackInScope ? botPlayer.currentTrackIndex.load() : std::numeric_limits<size_t>::max()));

        if(isCurrentTrackInScope)
            botPlayer.currentTrackIndex = from;
    }
    void OrchestraDiscordBot::CommandDelete(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);

        O_ASSERT(botPlayer.tracksQueue.GetTracksSize() > 0, "The queue is empty.");

        size_t index = (value.empty() ? std::numeric_limits<size_t>::max() : GuelderResourcesManager::StringToNumber<size_t>(value));
        const dpp::voiceconn* voice = IsVoiceConnectionReady(message.msg.guild_id);

        std::lock_guard queueLock{ botPlayer.tracksQueueMutex };

        {
            bool deleteCurrentTrack = false;
            GetParamValue(params, "current", deleteCurrentTrack);

            if(deleteCurrentTrack)
                index = botPlayer.currentTrackIndex;
            else
            {
                bool deleteMiddleTrack = false;
                GetParamValue(params, "middle", deleteMiddleTrack);

                if(deleteMiddleTrack)
                    index = (botPlayer.tracksQueue.GetTracksSize() - 1) / 2;
                else
                {
                    bool deleteLastTrack = false;
                    GetParamValue(params, "last", deleteLastTrack);

                    if(deleteLastTrack)
                        index = botPlayer.tracksQueue.GetTracksSize() - 1;
                }
            }
        }

        if(index != std::numeric_limits<size_t>::max())
        {
            O_ASSERT(index < botPlayer.tracksQueue.GetTracksSize(), "The index is bigger than the last item of queue with index ", botPlayer.tracksQueue.GetTracksSize() - 1);

            if(botPlayer.currentTrackIndex == index)
            {
                voice->voiceclient->stop_audio();
                botPlayer.player.Skip();
            }

            //why it was here before?
            //if(m_CurrentTrackIndex > 0)
                //--m_CurrentTrackIndex;

            if(index < botPlayer.currentTrackIndex)
                --botPlayer.currentTrackIndex;

            botPlayer.tracksQueue.DeleteTrack(index);
        }
        else
        {
            const int fromParamIndex = GetParamIndex(params, "from");
            int from = 0;
            if(fromParamIndex != -1)
                from = params[fromParamIndex].GetValue<int>();

            const int toParamIndex = GetParamIndex(params, "to");
            int to = botPlayer.tracksQueue.GetTracksSize() - 1;
            if(toParamIndex != -1)
                to = params[toParamIndex].GetValue<int>();

            if(fromParamIndex == -1 && toParamIndex == -1)
            {
                int playlistIndexToDelete = std::numeric_limits<int>::max();
                try
                {
                    GetParamValue(params, "playlist", playlistIndexToDelete);
                }
                catch(...)
                {
                    playlistIndexToDelete = GetCurrentPlaylistIndex(message.msg.guild_id);
                }

                //if(playlistIndexToDelete == -1)
                    //playlistIndexToDelete = 0;

                O_ASSERT(playlistIndexToDelete < botPlayer.tracksQueue.GetPlaylistsSize() && playlistIndexToDelete >= 0, "The playlist index is invalid.");

                const PlaylistInfo& playlistInfo = botPlayer.tracksQueue.GetPlaylistInfos()[playlistIndexToDelete];

                from = playlistInfo.beginIndex;
                to = playlistInfo.endIndex;
            }

            //O_ASSERT(fromParamIndex != -1 || toParamIndex != -1, "The command is empty, nothing to execute.");
            O_ASSERT(from >= 0 && from < to && to < botPlayer.tracksQueue.GetTracksSize(), "\"from or \"to\" is outside of the range.");

            const bool isCurrentTrackInRange = botPlayer.currentTrackIndex >= from && botPlayer.currentTrackIndex <= to;

            if(isCurrentTrackInRange)
            {
                voice->voiceclient->stop_audio();
                botPlayer.player.Skip();

                botPlayer.currentTrackIndex = from;
            }
            else if(to < botPlayer.currentTrackIndex)
                botPlayer.currentTrackIndex -= to - from + 1;

            botPlayer.tracksQueue.DeleteTracks(from, to);
        }
    }

    void OrchestraDiscordBot::CommandStop(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);

        const dpp::voiceconn* v = IsVoiceConnectionReady(message.msg.guild_id);

        botPlayer.player.Stop();
        botPlayer.isStopped = true;

        botPlayer.currentTrackIndex = std::numeric_limits<uint32_t>::max();

        v->voiceclient->pause_audio(botPlayer.player.GetIsPaused());
        v->voiceclient->stop_audio();
    }
    void OrchestraDiscordBot::CommandPause(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);

        const dpp::voiceconn* v = IsVoiceConnectionReady(message.msg.guild_id);

        botPlayer.player.Pause(!botPlayer.player.GetIsPaused());
        v->voiceclient->pause_audio(botPlayer.player.GetIsPaused());

        Reply(message, "Pause is ", (botPlayer.player.GetIsPaused() ? "on" : "off"), '.');
    }
    void OrchestraDiscordBot::CommandSkip(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);

        const dpp::voiceconn* v = IsVoiceConnectionReady(message.msg.guild_id);

        std::lock_guard queueLock{ botPlayer.tracksQueueMutex };

        O_ASSERT(botPlayer.tracksQueue.GetTracksSize() != 0, "The tracks queue is empty.");

        //not sure about this one
        //O_ASSERT(m_Player.GetDecodersCount() != 0, "Decoders are empty.");

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

        bool skip = botPlayer.player.IsDecoderReady() > 0;
        int skipToIndex = botPlayer.currentTrackIndex;

        if(skipPlaylist)
        {
            bool found = false;
            for(const auto& playlistInfo : botPlayer.tracksQueue.GetPlaylistInfos())
            {
                const bool isInRange = botPlayer.currentTrackIndex >= playlistInfo.beginIndex && botPlayer.currentTrackIndex <= playlistInfo.endIndex;

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
            O_ASSERT(toindex >= 0 && toindex < botPlayer.tracksQueue.GetTracksSize(), "Parameter \"toindex\" is invalid.");

            skipToIndex = toindex;
        }
        else if(inindex != 0)
        {
            O_ASSERT(botPlayer.currentTrackIndex + inindex < botPlayer.tracksQueue.GetTracksSize(), "Parameter \"inindex\" is invalid.");

            skipToIndex = botPlayer.currentTrackIndex + inindex;
        }
        else if(skipToMiddle)
        {
            skipToIndex = (botPlayer.tracksQueue.GetTracksSize() - 1) / 2;
        }
        else if(skipToLast)
        {
            skipToIndex = botPlayer.tracksQueue.GetTracksSize() - 1;
        }
        else if(tosecsIndex != -1)
        {
            skip = false;
            botPlayer.player.SkipToSeconds(tosecs);
        }
        else if(secs != 0.f)
        {
            skip = false;
            botPlayer.player.SkipSeconds(secs - v->voiceclient->get_secs_remaining());
        }
        else
            skipToIndex++;

        botPlayer.currentTrackIndex = skipToIndex;

        if(skip)
            botPlayer.player.Skip();
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
        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);

        if(botPlayer.adminSnowflake != 0 && message.msg.author.id == botPlayer.adminSnowflake)
        {
            //disconnect
            dpp::voiceconn* voice = this->get_shard(0)->get_voice(message.msg.guild_id);

            if(voice && voice->voiceclient && voice->is_ready())
                this->get_shard(0)->disconnect_voice(message.msg.guild_id);

            Reply(message, "Goodbye!");

            std::unique_lock lock{ botPlayer.joinMutex };
            botPlayer.joinedCondition.wait_for(lock, std::chrono::milliseconds(500), [this, &botPlayer] { return botPlayer.isJoined == false; });

            exit(0);
        }
        else
            Reply(message, "You are not an admin!");
    }
}
//idk
namespace Orchestra
{
    void OrchestraDiscordBot::ConnectToMemberVoice(const dpp::message_create_t& message)
    {
        if(!dpp::find_guild(message.msg.guild_id)->connect_member_voice(message.msg.author.id))
            O_THROW("The user with id ", message.msg.author.id.str(), " is not in a voice channel.");
    }

    void OrchestraDiscordBot::WaitUntilJoined(const dpp::snowflake& guildID, const std::chrono::milliseconds& delay)
    {
        BotPlayer& botPlayer = GetBotPlayer(guildID);

        std::unique_lock joinLock{ botPlayer.joinMutex };

        O_ASSERT(botPlayer.joinedCondition.wait_for(joinLock, delay, [&botPlayer] { return botPlayer.isJoined == true; }), "The connection delay was bigger than ", delay.count(), "ms.");
    }
    dpp::voiceconn* OrchestraDiscordBot::IsVoiceConnectionReady(const dpp::snowflake& guildID)
    {
        BotPlayer& botPlayer = GetBotPlayer(guildID);

        dpp::voiceconn* voice = get_shard(0)->get_voice(guildID);

        O_ASSERT(botPlayer.isJoined && (voice || voice->voiceclient || voice->voiceclient->is_ready()), "Failed to establish connection to a voice channel in a guild with id ", guildID);

        return voice;
    }

    void OrchestraDiscordBot::AddToQueue(const dpp::message_create_t& message, const std::vector<Param>& params, std::string value, size_t insertIndex)
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

        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);

        const size_t queueTracksSizeBefore = botPlayer.tracksQueue.GetTracksSize();

        if(insertIndex > botPlayer.tracksQueue.GetTracksSize())
            insertIndex = std::numeric_limits<size_t>::max();

        PlayParams playParams{};

        //as urls may contain non-English letters

        GE_LOG(Orchestra, Info, "Received music value: ", value);

        GetParamValue(params, "raw", playParams.isRaw);

        GetParamValue(params, "speed", playParams.speed);
        GetParamValue(params, "repeat", playParams.repeat);
        if(playParams.repeat < 0)
            playParams.repeat = std::numeric_limits<int>::max();

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

                botPlayer.tracksQueue.FetchSearch(value, searchEngine, playParams.speed, playParams.repeat, insertIndex);
            }
            else
            {
                GetParamValue(params, "shuffle", playParams.doShuffle);

                botPlayer.tracksQueue.FetchURL(value, playParams.doShuffle, playParams.speed, static_cast<size_t>(playParams.repeat), insertIndex);
            }
        }
        else
        {
            O_ASSERT(/*IsValidURL(value.data()) && */message.msg.author.id == botPlayer.adminSnowflake, "The user tried to access admin's file, while not being the admin.");

            botPlayer.tracksQueue.FetchRaw(std::move(value), playParams.speed, playParams.repeat, insertIndex);
        }

        if(insertIndex <= botPlayer.currentTrackIndex)
            botPlayer.currentTrackIndex += botPlayer.tracksQueue.GetTracksSize() - queueTracksSizeBefore;
    }

    void OrchestraDiscordBot::ReplyWithMessage(const dpp::message_create_t& message, dpp::message reply)
    {
        message.reply(std::move(reply));
    }
    //TODO: make some sort of move optimization
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

    uint32_t OrchestraDiscordBot::GetCurrentPlaylistIndex(const dpp::snowflake& guildID)
    {
        BotPlayer& botPlayer = GetBotPlayer(guildID);

        if(botPlayer.currentPlaylistIndex == std::numeric_limits<uint32_t>::max())
        {
            bool found = false;
            for(size_t j = 0; j < botPlayer.tracksQueue.GetPlaylistsSize(); j++)
            {
                const PlaylistInfo& playlistInfo = botPlayer.tracksQueue.GetPlaylistInfos()[j];

                const bool isInRange = botPlayer.currentTrackIndex >= playlistInfo.beginIndex && botPlayer.currentTrackIndex <= playlistInfo.endIndex;

                if(isInRange)
                {
                    found = true;
                    botPlayer.currentPlaylistIndex = j;

                    break;
                }
            }
            if(!found)
                botPlayer.currentPlaylistIndex = std::numeric_limits<uint32_t>::max();
        }

        return botPlayer.currentPlaylistIndex;
    }

    void OrchestraDiscordBot::ReplyWithInfoAboutTrack(const dpp::snowflake& guildID, const dpp::message_create_t& message, const TrackInfo& trackInfo, bool outputURL, bool printCurrentTimestamp)
    {
        BotPlayer& botPlayer = GetBotPlayer(guildID);

        //std::string description = Logger::Format(L"**[", botPlayer.m_CurrentTrackIndex, "]** ");

        //if(trackInfo.title.empty())
        //    description += Logger::Format(L"The name of the track is unknown.\n");
        //else
        //    description += Logger::Format(L"**", trackInfo.title, L"** is going to be played.\n");

        //if(trackInfo.duration > 0.f)
        //    description += Logger::Format(L"The track's duration: **", trackInfo.duration, L"** seconds.\n");
        //if(printCurrentTimestamp)
        //{
        //    const dpp::voiceconn* voice = IsVoiceConnectionReady(message.msg.guild_id);
        //    description += Logger::Format(L"Current timestamp: ", botPlayer.m_Player.GetCurrentTimestamp() - voice->voiceclient->get_secs_remaining(), " seconds.\n");
        //}
        ///*if(m_Player.GetBassBoostSettings().decibelsBoost)
        //{
        //    auto [boost, freq, band] = m_Player.GetBassBoostSettings();
        //    description += Logger::Format(L"Bass-boost decibels: ", boost, L".\nBass-boost frequency: ", freq, L"\nBass-boost bandwidth: ", band, "\n");
        //}*/
        //if(trackInfo.speed != 1.f)
        //    description += Logger::Format(L"The speed: ", trackInfo.speed, ".\nThe duration with speed applied: ", trackInfo.duration * trackInfo.speed, L" seconds.\n");
        //if(trackInfo.repeat > 1)
        //    description += Logger::Format(L"Repeat count: ", trackInfo.repeat, L".\n");

        //if(outputURL)
        //    description += Logger::Format(L"URL: ", (trackInfo.URL.empty() ? StringToWString(trackInfo.rawURL) : trackInfo.URL));
        std::string description = Logger::Format("**[", botPlayer.currentTrackIndex, "]** ");

        if(trackInfo.title.empty())
            description += Logger::Format("The name of the track is unknown.\n");
        else
            description += Logger::Format("**", (trackInfo.title), "** is going to be played.\n");

        if(trackInfo.duration > 0.f)
            description += Logger::Format("The track's duration: **", trackInfo.duration, "** seconds.\n");

        if(printCurrentTimestamp)
        {
            const dpp::voiceconn* voice = IsVoiceConnectionReady(message.msg.guild_id);
            description += Logger::Format("Current timestamp: ",
                botPlayer.player.GetCurrentTimestamp() - voice->voiceclient->get_secs_remaining(),
                " seconds.\n");
        }

        /*
        if (m_Player.GetBassBoostSettings().decibelsBoost)
        {
            auto [boost, freq, band] = m_Player.GetBassBoostSettings();
            description += Logger::Format("Bass-boost decibels: ", boost, ".\nBass-boost frequency: ",
                                          freq, "\nBass-boost bandwidth: ", band, "\n");
        }
        */

        if(trackInfo.speed != 1.f)
            description += Logger::Format("The speed: ", trackInfo.speed,
                ".\nThe duration with speed applied: ", trackInfo.duration * trackInfo.speed, " seconds.\n");

        if(trackInfo.repeat > 1)
            description += Logger::Format("Repeat count: ", trackInfo.repeat, ".\n");

        if(outputURL)
        {
            const std::string& url = trackInfo.URL.empty() ? trackInfo.rawURL : (trackInfo.URL);
            description += Logger::Format("URL: ", url);
        }

        //Reply(message, WStringToString(description));
        Reply(message, description);
    }

    OrchestraDiscordBot::BotPlayer& OrchestraDiscordBot::GetBotPlayer(const dpp::snowflake& guildID)
    {
        const auto itBotPlayer = m_GuildsBotPlayers.find(guildID);

        O_ASSERT(itBotPlayer != m_GuildsBotPlayers.end(), "Failed to find guild with snowflake ", guildID);

        return itBotPlayer->second;
    }

    OrchestraDiscordBot::BotPlayer::BotPlayer(const std::string_view& yt_dlpPath, const dpp::snowflake& adminSnowflake, const std::string_view& prefix, const char& paramPrefix)
        : player(200000, false), adminSnowflake(adminSnowflake), tracksQueue(yt_dlpPath.data()), currentPlaylistIndex(std::numeric_limits<uint32_t>::max()) {
    }

    OrchestraDiscordBot::BotPlayer& OrchestraDiscordBot::BotPlayer::operator=(BotPlayer&& other) noexcept
    {
        player = std::move(other.player);
        adminSnowflake = std::move(other.adminSnowflake);
        isStopped = other.isStopped.load();
        isJoined = other.isJoined.load();
        tracksQueue = std::move(other.tracksQueue);
        currentTrackIndex = other.currentTrackIndex.load();
        currentPlaylistIndex = other.currentPlaylistIndex.load();

        return *this;
    }
}
//getters, setters
namespace Orchestra
{
    void OrchestraDiscordBot::SetEnableLogSentPackets(bool enable)
    {
        /*if(!m_IsReady)
            m_OnReadyCallbacks.emplace_back(
                [this, enable]
                {
                    for(auto& botPlayer : m_GuildsBotPlayers | std::views::values)
                        botPlayer.player.SetEnableLogSentPackets(enable);
                }
            );
        else*/
            for(auto& botPlayer : m_GuildsBotPlayers | std::views::values)
                botPlayer.player.SetEnableLogSentPackets(enable);
    }
    void OrchestraDiscordBot::SetSentPacketSize(uint32_t size)
    {
        /*if(!m_IsReady)
            m_OnReadyCallbacks.emplace_back(
                [this, size]
                {
                    for(auto& botPlayer : m_GuildsBotPlayers | std::views::values)
                        botPlayer.player.SetSentPacketSize(size);
                }
            );
        else*/
            for(auto& botPlayer : m_GuildsBotPlayers | std::views::values)
                botPlayer.player.SetSentPacketSize(size);
    }
    void OrchestraDiscordBot::SetAdminSnowflake(dpp::snowflake id)
    {
        /*if(!m_IsReady)
            m_OnReadyCallbacks.emplace_back(
                [this, id]
                {
                    for(auto& botPlayer : m_GuildsBotPlayers | std::views::values)
                        botPlayer.adminSnowflake = std::move(id);
                }
            );
        else*/
            for(auto& botPlayer : m_GuildsBotPlayers | std::views::values)
                botPlayer.adminSnowflake = std::move(id);
    }

    /*bool OrchestraDiscordBot::GetEnableLogSentPackets() const noexcept
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
    }*/
}