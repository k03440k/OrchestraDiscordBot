#define NOMINMAX

#include "OrchestraDiscordBot.hpp"

#include <atomic>
#include <mutex>
#include <future>
#include <chrono>
#include <string>
#include <string_view>
#include <random>
#include <utility>

#include <dpp/dpp.h>

#include <GuelderResourcesManager.hpp>
#include <GuelderConsoleLog.hpp>
#include <GuelderConsoleLogMacroses.hpp>

#include "../Utils.hpp"
#include "DiscordBot.hpp"
#include "OrchestraDiscordBotInstance.hpp"
#include "Player.hpp"
#include "Yt_DlpManager.hpp"
#include "TracksQueue.hpp"

using namespace GuelderConsoleLog;

//helper
namespace Orchestra
{
    std::string FormatTime(int time)
    {
        if(time < 10)
            return Logger::Format('0', time);
        else
            return Logger::Format(time);
    }
    std::string FormatTimeForFile(const std::chrono::time_point<std::chrono::system_clock>& timePoint = std::chrono::system_clock::now())
    {
        std::time_t time = std::chrono::system_clock::to_time_t(timePoint);

        std::tm localTm{};
#ifdef _WIN32
        localtime_s(&localTm, &time);  // Windows
#else
        localtime_r(&time, &localTm);  // POSIX
#endif

        return Logger::Format(std::put_time(&localTm, "%d-%m-%Y %H-%M-%S"));
    }
}
namespace Orchestra
{
    OrchestraDiscordBot::OrchestraDiscordBot(const std::string& token, Paths paths, FullBotInstanceProperties defaultGuildsValues, dpp::snowflake bossSnowflake, uint32_t intents)
        : DiscordBot(token, intents), m_BossSnowflake(bossSnowflake), m_IsReady(false), m_RandomEngine(std::random_device{}()), m_Paths(std::move(paths)), m_CommandsNamesConfig(m_Paths.commandsNamesConfigPath, false)
    {
        on_guild_create(
            [this, _properties = std::move(defaultGuildsValues)](const dpp::guild_create_t& event)
            {
                std::lock_guard guildLock{ m_GuildCreateMutex };
                if(!m_GuildsBotInstances.contains(event.created->id))
                {
                    GE_LOG(Orchestra, Info, "Adding guild with ID: ", event.created->id);

                    using namespace GuelderResourcesManager;

                    ConfigFile guildsConfig{ m_Paths.guildsConfigPath, true };

                    std::string guildsConfigScope = guildsConfig.GetConfigFileSource();

                    //ConfigFile::Parser::FindNamespace(guildsConfigScope, Logger::Format(event.created->id));
                    std::string variablePath;

                    auto properties = _properties;

                    try
                    {
                        variablePath = Logger::Format(event.created->id, "/sentPacketsSize");
                        try
                        {
                            properties.sentPacketsSize = guildsConfig.GetVariable(variablePath).GetValue<uint32_t>();
                        }
                        catch(...)
                        {
                            guildsConfig.WriteVariable({ variablePath, Logger::Format(properties.sentPacketsSize), DataType::UInt, false });
                        }

                        variablePath = Logger::Format(event.created->id, "/enableLogSentPackets");
                        try
                        {
                            properties.enableLogSentPackets = guildsConfig.GetVariable(variablePath).GetValue<bool>();
                        }
                        catch(...)
                        {
                            guildsConfig.WriteVariable({ variablePath, Logger::Format(properties.enableLogSentPackets), DataType::Bool, false });
                        }

                        variablePath = Logger::Format(event.created->id, "/commandsPrefix");
                        try
                        {
                            properties.properties.commandsPrefix = guildsConfig.GetVariable(variablePath).GetValue<std::string>();
                        }
                        catch(...)
                        {
                            guildsConfig.WriteVariable({ variablePath, Logger::Format(properties.properties.commandsPrefix), DataType::String, false });
                        }

                        variablePath = Logger::Format(event.created->id, "/paramsPrefix");
                        try
                        {
                            properties.properties.paramsPrefix = guildsConfig.GetVariable(variablePath).GetRawValue()[0];
                        }
                        catch(...)
                        {
                            guildsConfig.WriteVariable({ variablePath, Logger::Format(properties.properties.paramsPrefix), DataType::Char, false });
                        }

                        variablePath = Logger::Format(event.created->id, "/maxDownloadFileSize");
                        try
                        {
                            properties.properties.maxDownloadFileSize = guildsConfig.GetVariable(variablePath).GetValue<uint32_t>();
                        }
                        catch(...)
                        {
                            guildsConfig.WriteVariable({ variablePath, Logger::Format(properties.properties.maxDownloadFileSize), DataType::UInt, false });
                        }

                        variablePath = Logger::Format(event.created->id, "/adminSnowflake");
                        try
                        {
                            properties.properties.adminSnowflake = guildsConfig.GetVariable(variablePath).GetValue<std::string>();
                        }
                        catch(...)
                        {
                            properties.properties.adminSnowflake = event.created->owner_id;

                            guildsConfig.WriteVariable({ variablePath, properties.properties.adminSnowflake.str(), DataType::String, false });
                        }

                        //FUCK I FUCKING HATE FUCKING PARSING
                        //Caught exception: Failed to find variable with path Commands/play/Params/speed

                        //FUCKING FORMAT
                        guildsConfig.Format();
                    }
                    catch(...)
                    {
                        GE_LOG(Orchestra, Error, "Guild creations went completely wrong.");
                    }

                    m_GuildsBotInstances[event.created->id] = BotInstance{ std::move(properties) };
                }
            }
        );

        on_voice_state_update(
            [this](const dpp::voice_state_update_t& voiceState)
            {
                BotInstance& botInstance = GetBotInstance(voiceState.state.guild_id);

                if(voiceState.state.user_id == this->me.id)
                {
                    if(!voiceState.state.channel_id.empty())
                    {
                        botInstance.isJoined = true;
                        GE_LOG(Orchestra, Info, "Has just joined to ", voiceState.state.channel_id, " channel in ", voiceState.state.guild_id, " guild.");
                    }
                    else
                    {
                        botInstance.isJoined = false;
                        botInstance.player.player.Stop();
                        GE_LOG(Orchestra, Info, "Has just disconnected from voice channel.");
                    }

                    botInstance.joinedCondition.notify_all();
                    botInstance.player.player.Pause(false);
                }
            }
        );

        ////TODO: is this necessary? - yes it is
        //on_ready(
        //    [this/*, commandPrefix, paramPrefix*/](const dpp::ready_t& readyEvent)
        //    {
        //        //for(auto&& guildID : readyEvent.guilds)
        //            //if(!m_GuildsBotInstances.contains(guildID))
        //                //m_GuildsBotInstances[guildID] = BotPlayer{ _yt_dlpPath, 0, commandPrefix, paramPrefix };

        //        for(auto&& func : m_OnReadyCallbacks)
        //            func();

        //        m_OnReadyCallbacks.clear();
        //        m_IsReady = true;
        //    }
        //);

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

        //TODO: make so GetParamValue(params, "playlist", playlistIndexToDelete); param name truly works - DONE
        //TODO: commandChecker

        //help
        AddCommand({ m_CommandsNamesConfig.GetVariable("help").GetRawValue(),
            std::bind(&OrchestraDiscordBot::CommandHelp, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {}
            });
        //play
        AddCommand({ m_CommandsNamesConfig.GetVariable("play").GetRawValue(),
            std::bind(&OrchestraDiscordBot::CommandPlay, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{Type::Float,  GetParamName("play", "speed")},
                ParamProperties{Type::Float,  GetParamName("play", "bass")},
                ParamProperties{Type::Float,  GetParamName("play", "basshz")},
                ParamProperties{Type::Float,  GetParamName("play", "bassband")},
                ParamProperties{Type::Int,    GetParamName("play", "repeat")},
                ParamProperties{Type::Bool,   GetParamName("play", "search")},
                ParamProperties{Type::String, GetParamName("play", "searchengine")},
                ParamProperties{Type::Bool,   GetParamName("play", "noinfo")},
                ParamProperties{Type::Bool,   GetParamName("play", "raw")},
                ParamProperties{Type::Int,    GetParamName("play", "index")},
                ParamProperties{Type::Bool,   GetParamName("play", "shuffle")}
            }
            });
        //playlist
        AddCommand({ m_CommandsNamesConfig.GetVariable("playlist").GetRawValue(),
            std::bind(&OrchestraDiscordBot::CommandPlaylist, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{Type::String, GetParamName("playlist", "name")},
                ParamProperties{Type::Int,    GetParamName("playlist", "from")},
                ParamProperties{Type::Int,    GetParamName("playlist", "to")},
                ParamProperties{Type::Float,  GetParamName("playlist", "speed")},
                ParamProperties{Type::Int,    GetParamName("playlist", "repeat")},
                ParamProperties{Type::Int,    GetParamName("playlist", "delete")}
            }
            });
        //speed
        AddCommand({ m_CommandsNamesConfig.GetVariable("speed").GetRawValue(),
            std::bind(&OrchestraDiscordBot::CommandSpeed, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{Type::Int,    GetParamName("speed", "index")},
                ParamProperties{Type::Bool,   GetParamName("speed", "middle")},
                ParamProperties{Type::Bool,   GetParamName("speed", "last")},
                ParamProperties{Type::Int,    GetParamName("speed", "from")},
                ParamProperties{Type::Int,    GetParamName("speed", "to")},
                ParamProperties{Type::Int,    GetParamName("speed", "playlist")}
            }
            });
        //bass
        AddCommand({ m_CommandsNamesConfig.GetVariable("bass").GetRawValue(),
            std::bind(&OrchestraDiscordBot::CommandBass, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{Type::Bool,    GetParamName("bass", "clear")},
                ParamProperties{Type::Float,   GetParamName("bass", "hz")},
                ParamProperties{Type::Float,   GetParamName("bass", "band")}
            }
            });
        //equalizer
        AddCommand({ m_CommandsNamesConfig.GetVariable("equalizer").GetRawValue(),
            std::bind(&OrchestraDiscordBot::CommandEqualizer, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{Type::Bool,    GetParamName("equalizer", "clear")},
                ParamProperties{Type::Float,   GetParamName("equalizer", "hz")},
                ParamProperties{Type::Float,   GetParamName("equalizer", "delete")}
            }
            });
        //repeat
        AddCommand({ m_CommandsNamesConfig.GetVariable("repeat").GetRawValue(),
            std::bind(&OrchestraDiscordBot::CommandRepeat, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{Type::Int,     GetParamName("repeat", "index")},
                ParamProperties{Type::Bool,    GetParamName("repeat", "middle")},
                ParamProperties{Type::Bool,    GetParamName("repeat", "last")},
                ParamProperties{Type::Int,     GetParamName("repeat", "from")},
                ParamProperties{Type::Int,     GetParamName("repeat", "to")},
                ParamProperties{Type::Int,     GetParamName("repeat", "playlist")}
            }
            });
        //insert
        AddCommand({ m_CommandsNamesConfig.GetVariable("insert").GetRawValue(),
            std::bind(&OrchestraDiscordBot::CommandInsert, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{Type::Int,     GetParamName("insert", "after")},
                ParamProperties{Type::Float,   GetParamName("insert", "speed")},
                ParamProperties{Type::Int,     GetParamName("insert", "repeat")},
                ParamProperties{Type::Bool,    GetParamName("insert", "search")},
                ParamProperties{Type::String,  GetParamName("insert", "searchengine")},
                ParamProperties{Type::Bool,    GetParamName("insert", "raw")},
                ParamProperties{Type::Bool,    GetParamName("insert", "shuffle")}
            }
            });
        //transfer
        AddCommand({ m_CommandsNamesConfig.GetVariable("transfer").GetRawValue(),
            std::bind(&OrchestraDiscordBot::CommandTransfer, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{Type::Int,    GetParamName("transfer", "to")},
                ParamProperties{Type::Bool,   GetParamName("transfer", "current")},
                ParamProperties{Type::Bool,   GetParamName("transfer", "middle")}
            }
            });
        //reverse
        AddCommand({ m_CommandsNamesConfig.GetVariable("reverse").GetRawValue(),
            std::bind(&OrchestraDiscordBot::CommandReverse, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{Type::Int,   GetParamName("reverse", "from")},
                ParamProperties{Type::Int,   GetParamName("reverse", "to")},
                ParamProperties{Type::Int,   GetParamName("reverse", "playlist")}
            }
            });
        //skip
        AddCommand({ m_CommandsNamesConfig.GetVariable("skip").GetRawValue(),
            std::bind(&OrchestraDiscordBot::CommandSkip, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{Type::Float,  GetParamName("skip", "secs")},
                ParamProperties{Type::Float,  GetParamName("skip", "tosecs")},
                ParamProperties{Type::Int,    GetParamName("skip", "in")},
                ParamProperties{Type::Int,    GetParamName("skip", "to")},
                ParamProperties{Type::Bool,   GetParamName("skip", "playlist")},
                ParamProperties{Type::Bool,   GetParamName("skip", "middle")},
                ParamProperties{Type::Bool,   GetParamName("skip", "last")}
            }
            });
        //shuffle
        AddCommand({ m_CommandsNamesConfig.GetVariable("shuffle").GetRawValue(),
            std::bind(&OrchestraDiscordBot::CommandShuffle, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{Type::Int,    GetParamName("shuffle", "from")},
                ParamProperties{Type::Int,    GetParamName("shuffle", "to")},
                ParamProperties{Type::Int,    GetParamName("shuffle", "playlist")}
            }
            });
        //delete
        AddCommand({ m_CommandsNamesConfig.GetVariable("delete").GetRawValue(),
            std::bind(&OrchestraDiscordBot::CommandDelete, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{Type::Bool,   GetParamName("delete", "current")},
                ParamProperties{Type::Bool,   GetParamName("delete", "middle")},
                ParamProperties{Type::Bool,   GetParamName("delete", "last")},
                ParamProperties{Type::Int,    GetParamName("delete", "from")},
                ParamProperties{Type::Int,    GetParamName("delete", "to")},
                ParamProperties{Type::Int,    GetParamName("delete", "playlist")}
            }
            });
        //current
        AddCommand({ m_CommandsNamesConfig.GetVariable("current").GetRawValue(),
            std::bind(&OrchestraDiscordBot::CommandCurrent, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{Type::Bool,   GetParamName("current", "url")}
            }
            });
        //queue
        AddCommand({ m_CommandsNamesConfig.GetVariable("queue").GetRawValue(),
            std::bind(&OrchestraDiscordBot::CommandQueue, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {
                ParamProperties{Type::Bool,    GetParamName("queue", "url")}
            }
            });
        //pause
        AddCommand({ m_CommandsNamesConfig.GetVariable("pause").GetRawValue(),
            std::bind(&OrchestraDiscordBot::CommandPause, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {}
            });
        //stop
        AddCommand({ m_CommandsNamesConfig.GetVariable("stop").GetRawValue(),
            std::bind(&OrchestraDiscordBot::CommandStop, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {}
            });
        //leave
        AddCommand({ m_CommandsNamesConfig.GetVariable("leave").GetRawValue(),
            std::bind(&OrchestraDiscordBot::CommandLeave, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {}
            });
        //terminate
        AddCommand({ m_CommandsNamesConfig.GetVariable("terminate").GetRawValue(),
            std::bind(&OrchestraDiscordBot::CommandTerminate, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            {}
            });
    }

    void OrchestraDiscordBot::RegisterCommands()
    {
        m_WorkersManger.Reserve(m_Commands.size());

        //basically copy-paste
        if(m_Paths.historyLogPath.empty())
            on_message_create(
                [this](const dpp::message_create_t& message)
                {
                    if(message.msg.attachments.empty() && message.msg.author.id != me.id)
                    {
                        const auto& content = message.msg.content;

                        BotInstance& botInstance = GetBotInstance(message.msg.guild_id);

                        auto properties = botInstance.AccessOrchestraDiscordBotInstanceProperties();

                        if(const auto foundCommandPrefix = std::ranges::search(content, properties->commandsPrefix); foundCommandPrefix.begin() == content.begin())
                        {
                            const size_t commandOffset = foundCommandPrefix.size();

                            ParsedCommandWithIndex parsedCommandWithIndex = ParseCommand(m_Commands, content, commandOffset, properties->paramsPrefix);

                            properties.Unlock();

                            O_ASSERT(CommandChecker(message, parsedCommandWithIndex.parsedCommand), "The command check failed with user with ID ", message.msg.author.id);

                            const auto command = m_Commands.begin() + parsedCommandWithIndex.index;

                            const size_t id = m_WorkersManger.AddWorker(
                                [this, message, command, _parsedCommand = std::move(parsedCommandWithIndex.parsedCommand)]
                                {
                                    const size_t index = m_WorkersManger.GetCurrentWorkerIndex() - 1;

                                    GE_LOG(Orchestra, Info, "User with ID: ", message.msg.author.id, " \"", _parsedCommand.name, "\" command. Work with index ", index, " is staring on ", std::this_thread::get_id(), " thread");

                                    (*command)(message, _parsedCommand.params, _parsedCommand.value);

                                    GE_LOG(Orchestra, Info, "User with ID: ", message.msg.author.id, " \"", _parsedCommand.name, "\" command. Work with index ", index, " has just ended on ", std::this_thread::get_id(), " thread");
                                },
                                [message](const OrchestraException& oe) { message.reply(Logger::Format("**Exception:** ", oe.GetUserMessage())); },
                                true);

                            m_WorkersManger.Work(id);
                        }
                    }
                }
            );
        else
        {
            std::string timePrefix = FormatTimeForFile();
            timePrefix.push_back(' ');

            std::filesystem::path historyLogPath = m_Paths.historyLogPath;

            historyLogPath.replace_filename(timePrefix + historyLogPath.filename().string());

            on_message_create(
                [this, _historyLogPath = std::move(historyLogPath)](const dpp::message_create_t& message)
                {
                    if(message.msg.author.id != me.id)
                    {
                        const size_t historyLogID = m_WorkersManger.AddWorker(
                            [this, __historyLogPath = std::move(_historyLogPath), _message = message]
                            {
                                LogMessage(__historyLogPath, _message.msg);
                            },
                            [](const OrchestraException& oe) { GE_LOG(Core, Error, "Failed to write to history log."); },
                            false
                        );
                        m_WorkersManger.Work(historyLogID);

                        const auto& content = message.msg.content;

                        BotInstance& botInstance = GetBotInstance(message.msg.guild_id);

                        try
                        {
                            auto properties = botInstance.AccessOrchestraDiscordBotInstanceProperties();

                            if(const auto foundCommandPrefix = std::ranges::search(content, properties->commandsPrefix); !message.msg.content.empty() && foundCommandPrefix.begin() == content.begin())
                            {
                                const size_t commandOffset = foundCommandPrefix.size();

                                ParsedCommandWithIndex parsedCommandWithIndex = ParseCommand(m_Commands, content, commandOffset, properties->paramsPrefix);

                                properties.Unlock();

                                O_ASSERT(CommandChecker(message, parsedCommandWithIndex.parsedCommand), "The command check failed with user with ID ", message.msg.author.id);

                                const auto command = m_Commands.begin() + parsedCommandWithIndex.index;

                                const size_t id = m_WorkersManger.AddWorker(
                                    [this, message, command, _parsedCommand = std::move(parsedCommandWithIndex.parsedCommand)]
                                    {
                                        const size_t index = m_WorkersManger.GetCurrentWorkerIndex() - 1;

                                        GE_LOG(Orchestra, Warning, "User with ID: ", message.msg.author.id, " \"", _parsedCommand.name, "\" command. Work with index ", index, " is staring on ", std::this_thread::get_id(), " thread");

                                        (*command)(message, _parsedCommand.params, _parsedCommand.value);

                                        GE_LOG(Orchestra, Warning, "User with ID: ", message.msg.author.id, " \"", _parsedCommand.name, "\" command. Work with index ", index, " has just ended on ", std::this_thread::get_id(), " thread");
                                    },
                                    [message](const OrchestraException& oe) { message.reply(Logger::Format("**Exception:** ", oe.GetUserMessage())); },
                                    true);

                                m_WorkersManger.Work(id);
                            }
                        }
                        catch(const OrchestraException& e)
                        {
                            GE_LOG(Orchestra, Error, e.GetFullMessage());
                        }
                        catch(const std::exception& e)
                        {
                            GE_LOG(Orchestra, Error, e.what());
                        }
                        catch(...)
                        {
                            GE_LOG(Orchestra, Error, "Unknown exception occurred, while trying to parse the message.");
                        }

                        m_WorkersManger.GetWorker(historyLogID).GetFuture().wait();
                        m_WorkersManger.RemoveWorker(historyLogID);
                    }
                }
            );
        }
    }
}
//idk
namespace Orchestra
{
    void OrchestraDiscordBot::ConnectToMemberVoice(const dpp::message_create_t& message)
    {
        dpp::guild* guild = find_guild(message.msg.guild_id);

        O_ASSERT(guild, "Failed to find guild to connect.");

        O_ASSERT(guild->connect_member_voice(message.msg.author.id), "The user with id ", message.msg.author.id.str(), " is not in a voice channel.");
    }

    void OrchestraDiscordBot::WaitUntilJoined(const dpp::snowflake& guildID, const std::chrono::milliseconds& delay)
    {
        BotInstance& botInstance = GetBotInstance(guildID);

        std::unique_lock joinLock{ botInstance.joinMutex };

        O_ASSERT(botInstance.joinedCondition.wait_for(joinLock, delay, [&botInstance] { return botInstance.isJoined == true; }), "The connection delay was bigger than ", delay.count(), "ms");
    }
    dpp::voiceconn* OrchestraDiscordBot::IsVoiceConnectionReady(const dpp::snowflake& guildID)
    {
        BotInstance& botInstance = GetBotInstance(guildID);

        dpp::voiceconn* voice = get_shard(0)->get_voice(guildID);

        O_ASSERT(botInstance.isJoined && (voice || voice->voiceclient || voice->voiceclient->is_ready()), "Failed to establish connection to a voice channel in a guild with id ", guildID);

        return voice;
    }

    void OrchestraDiscordBot::AddToQueue(const std::string_view& commandName, const dpp::message_create_t& message, const std::vector<Param>& params, std::string value, size_t insertIndex)
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

        BotInstance& botInstance = GetBotInstance(message.msg.guild_id);
        BotPlayer& botPlayer = botInstance.player;
        auto tracksQueue = botPlayer.AccessTracksQueue();

        const size_t queueTracksSizeBefore = tracksQueue->GetTracksSize();

        if(insertIndex > tracksQueue->GetTracksSize())
            insertIndex = std::numeric_limits<size_t>::max();

        PlayParams playParams{};

        //as urls may contain non-English letters

        GE_LOG(Orchestra, Info, "Received music value: ", value);

        GetParamValue(params, GetParamName(commandName, "raw"), playParams.isRaw);

        GetParamValue(params, GetParamName(commandName, "speed"), playParams.speed);
        GetParamValue(params, GetParamName(commandName, "repeat"), playParams.repeat);
        if(playParams.repeat < 0)
            playParams.repeat = std::numeric_limits<int>::max();

        if(!playParams.isRaw)
        {
            GetParamValue(params, GetParamName(commandName, "searchengine"), playParams.searchEngine);

            const bool foundSearchEngine = std::ranges::find(g_SupportedYt_DlpSearchingEngines, playParams.searchEngine) != g_SupportedYt_DlpSearchingEngines.end();
            bool isUsingSearch;

            //finding out whether search is being used
            if(!foundSearchEngine)
            {
                if(const int paramIndex = GetParamIndex(params, GetParamName(commandName, "search")); paramIndex != -1)
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

                tracksQueue->FetchSearch(m_Paths.yt_dlpExecutablePath, value, searchEngine, playParams.speed, playParams.repeat, insertIndex);
            }
            else
            {
                GetParamValue(params, GetParamName(commandName, "shuffle"), playParams.doShuffle);

                tracksQueue->FetchURL(m_Paths.yt_dlpExecutablePath, value, m_RandomEngine, playParams.doShuffle, playParams.speed, static_cast<size_t>(playParams.repeat), insertIndex);
            }
        }
        else
        {
            O_ASSERT(message.msg.author.id == botInstance.AccessOrchestraDiscordBotInstanceProperties()->adminSnowflake, "The user tried to access admin's file, while not being the admin.");

            tracksQueue->FetchRaw(std::move(value), playParams.speed, playParams.repeat, insertIndex);
        }

        if(insertIndex <= botPlayer.currentTrackIndex)
            botPlayer.currentTrackIndex += tracksQueue->GetTracksSize() - queueTracksSizeBefore;

        Reply(message, "Added to the queue!");
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
        auto tracksQueue = botPlayer.AccessTracksQueue();

        if(botPlayer.currentPlaylistIndex == std::numeric_limits<uint32_t>::max())
        {
            bool found = false;
            for(size_t j = 0; j < tracksQueue->GetPlaylistsSize(); j++)
            {
                const PlaylistInfo& playlistInfo = tracksQueue->GetPlaylistInfos()[j];

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

        Reply(message, description);
    }

    OrchestraDiscordBot::BotInstance& OrchestraDiscordBot::GetBotInstance(const dpp::snowflake& guildID)
    {
        const auto itBotPlayer = m_GuildsBotInstances.find(guildID);

        O_ASSERT(itBotPlayer != m_GuildsBotInstances.end(), "Failed to find guild with snowflake ", guildID);

        return itBotPlayer->second;
    }

    OrchestraDiscordBot::BotPlayer& OrchestraDiscordBot::GetBotPlayer(const dpp::snowflake& guildID)
    {
        return GetBotInstance(guildID).player;
    }

    std::string OrchestraDiscordBot::GetRawVariableValue(const GuelderResourcesManager::ConfigFile& configFile, const std::string_view& path)
    {
        return configFile.GetVariable(path).GetRawValue();
    }

    std::string OrchestraDiscordBot::GetParamName(const std::string_view& commandName, const std::string_view& paramName) const
    {
        return GetRawVariableValue(m_CommandsNamesConfig, Logger::Format(commandName, '/', paramName));
    }

    bool OrchestraDiscordBot::CommandChecker(const dpp::message_create_t& message, ParsedCommand& parsedCommand)
    {
        return true;
    }

    void OrchestraDiscordBot::LogMessage(const std::filesystem::path& historyLogPath, const dpp::message& message)
    {
        //TODO: add also saving attachments
        using namespace GuelderResourcesManager;

        BotInstance& botInstance = GetBotInstance(message.guild_id);
        auto properties = botInstance.AccessOrchestraDiscordBotInstanceProperties();

        std::lock_guard lock{ m_HistoryLogMutex };

        std::string scope;
        if(exists(historyLogPath))
            scope = ResourcesManager::ReceiveFileSource(historyLogPath);

        const auto parentPath = historyLogPath.parent_path();

        scope = LogMessage(std::move(scope), message, "", properties->maxDownloadFileSize, parentPath);

        if(!exists(parentPath))
            create_directories(parentPath);

        scope = ConfigFile::Parser::FormatScope(std::move(scope));

        ResourcesManager::WriteToFile(historyLogPath, scope);
        //ResourcesManager::AppendToFile(historyLogPath, toAppend);
    }

    std::string OrchestraDiscordBot::LogMessage(std::string scope, const dpp::message& message, std::string messageNamespacePath, uint32_t maxDownloadFileSize, std::filesystem::path fileSavePath)
    {
        using namespace GuelderResourcesManager;

        dpp::guild* guild = find_guild(message.guild_id);
        dpp::channel* channel = find_channel(message.channel_id);

        if(guild)
            messageNamespacePath += Logger::Format(guild->id, '/');

        if(channel)
            messageNamespacePath += Logger::Format(channel->id, '/');

        messageNamespacePath += Logger::Format(message.id, '/');

        std::tm localTm{};
#ifdef _WIN32
        localtime_s(&localTm, &message.sent);  // Windows
#else
        localtime_r(&message.sent, &localTm);  // POSIX
#endif

        scope = ConfigFile::Parser::WriteVariables(std::move(scope),
            {
                { Logger::Format(messageNamespacePath, "timeSentString"), Logger::Format(std::put_time(&localTm, "%d.%m.%Y %H:%M:%S")), DataType::String },
                { Logger::Format(messageNamespacePath, "timeSent"), Logger::Format(message.sent), DataType::LongLong }
            }
        );

        if(guild)
            scope = ConfigFile::Parser::WriteVariables(std::move(scope),
                {
                    { Logger::Format(messageNamespacePath, "guildID"), Logger::Format(guild->id), DataType::String },
                    { Logger::Format(messageNamespacePath, "guildName"), ConfigFile::Parser::AddSpecialChars(Logger::Format(guild->name)), DataType::String }
                }
            );
        if(channel)
            scope = ConfigFile::Parser::WriteVariables(std::move(scope),
                {
                    {Logger::Format(messageNamespacePath, "channelID"), Logger::Format(channel->id), DataType::String},
                    {Logger::Format(messageNamespacePath, "channelName"), ConfigFile::Parser::AddSpecialChars(Logger::Format(channel->name)), DataType::String}
                }
            );

        scope = ConfigFile::Parser::WriteVariables(std::move(scope),
            {
                {Logger::Format(messageNamespacePath, "authorID"), Logger::Format(message.author.id), DataType::String},
                {Logger::Format(messageNamespacePath, "authorName"), ConfigFile::Parser::AddSpecialChars(message.author.global_name), DataType::String},
                {Logger::Format(messageNamespacePath, "messageContent"), ConfigFile::Parser::AddSpecialChars(message.content), DataType::String}
            }
        );

        if(const auto& filesData = message.file_data; !filesData.empty())
        {
            const std::string filesDataPath = Logger::Format(messageNamespacePath, "FilesData/");

            std::vector<Variable> vars;
            vars.reserve(filesData.size());

            for(size_t i = 0; i < filesData.size(); i++)
            {
                const auto& fileData = filesData[i];

                vars.emplace_back(Logger::Format(filesDataPath, "fileData", i, "Name"), ConfigFile::Parser::AddSpecialChars(fileData.name), DataType::String);
                vars.emplace_back(Logger::Format(filesDataPath, "fileData", i, "MimeType"), ConfigFile::Parser::AddSpecialChars(fileData.mimetype), DataType::String);
                vars.emplace_back(Logger::Format(filesDataPath, "fileData", i, "Content"), ConfigFile::Parser::AddSpecialChars(fileData.content), DataType::String);
            }

            scope = ConfigFile::Parser::WriteVariables(std::move(scope), vars);
        }
        if(const auto& embeds = message.embeds; !embeds.empty())
        {
            const std::string embedsPath = Logger::Format(messageNamespacePath, "Embeds/");

            std::vector<Variable> vars;
            vars.reserve(embeds.size());

            for(size_t i = 0; i < embeds.size(); i++)
            {
                const auto& embed = embeds[i];

                vars.emplace_back(Logger::Format(embedsPath, "embed", i, "Title"), ConfigFile::Parser::AddSpecialChars(embed.title), DataType::String);
                vars.emplace_back(Logger::Format(embedsPath, "embed", i, "Description"), ConfigFile::Parser::AddSpecialChars(embed.description), DataType::String);
            }

            scope = ConfigFile::Parser::WriteVariables(std::move(scope), vars);
        }
        if(const auto& attachments = message.attachments; !attachments.empty())
        {
            const std::string attachmentsPath = Logger::Format(messageNamespacePath, "Attachments/");

            std::vector<Variable> vars;
            vars.reserve(attachments.size());

            for(size_t i = 0; i < attachments.size(); i++)
            {
                const auto& attachment = attachments[i];

                vars.emplace_back(Logger::Format(attachmentsPath, "attachment", i, "FileName"), ConfigFile::Parser::AddSpecialChars(attachment.filename), DataType::String);
                vars.emplace_back(Logger::Format(attachmentsPath, "attachment", i, "FileSize"), Logger::Format(attachment.size), DataType::UInt);
                vars.emplace_back(Logger::Format(attachmentsPath, "attachment", i, "Description"), ConfigFile::Parser::AddSpecialChars(attachment.description), DataType::String);
                vars.emplace_back(Logger::Format(attachmentsPath, "attachment", i, "ContentType"), ConfigFile::Parser::AddSpecialChars(attachment.content_type), DataType::String);
                vars.emplace_back(Logger::Format(attachmentsPath, "attachment", i, "URL"), ConfigFile::Parser::AddSpecialChars(attachment.url), DataType::String);

                if(maxDownloadFileSize > 0 && attachment.size <= maxDownloadFileSize)
                {
                    std::filesystem::path _fileSavePath = fileSavePath;

                    if(guild)
                        _fileSavePath /= Logger::Format(guild->id);
                    if(channel)
                        _fileSavePath /= Logger::Format(channel->id);

                    _fileSavePath /= std::filesystem::path{ Logger::Format(message.id) } / Logger::Format(i, ' ', attachment.filename);

                    vars.emplace_back(Logger::Format(attachmentsPath, "attachment", i, "Path"), ConfigFile::Parser::AddSpecialChars(_fileSavePath.generic_string()), DataType::String);

                    attachment.download(
                        [this, _fileSavePath]
                        (const dpp::http_request_completion_t& info)
                        {
                            constexpr int STATUS_OK = 200;

                            if(info.status == STATUS_OK)
                            {
                                auto dir = _fileSavePath.parent_path();

                                if(!exists(dir))
                                    create_directories(dir);

                                ResourcesManager::WriteToFile(_fileSavePath, info.body);
                            }
                        }
                    );
                }
            }

            scope = ConfigFile::Parser::WriteVariables(std::move(scope), vars);
        }

//probably I need a recursive mutex. after this being finished, I'm making a commit
#if 0
        if(message.message_reference.message_id)
        {
            bool isMessageReferenceDone = false;
            std::condition_variable conditionVariable;

            std::string messageReferencePath = Logger::Format(messageNamespacePath, "MessageReference/");

            auto msgRef = message.message_reference;

            message_get(message.message_reference.message_id, message.message_reference.channel_id,
                [&isMessageReferenceDone, &conditionVariable, this, &scope, maxDownloadFileSize, fileSavePath = std::move(fileSavePath), messageReferencePath = std::move(messageReferencePath), msgRef = std::move(msgRef)](const dpp::confirmation_callback_t& callback)
                {
                    try
                    {
                        bool success = true;

                        if(callback.is_error())
                            success = false;

                        if(success)
                        {
                            scope = ConfigFile::Parser::WriteVariable(std::move(scope), { Logger::Format(messageReferencePath, "Status"), "Success", DataType::Bool, false });

                            const dpp::message& msg = callback.get<dpp::message>();

                            scope = LogMessage(std::move(scope), msg, std::move(messageReferencePath), maxDownloadFileSize, std::move(fileSavePath));
                        }
                        else
                        {
                            scope = ConfigFile::Parser::WriteVariables(std::move(scope),
                                {
                                    { Logger::Format(messageReferencePath, "Status"), callback.get_error().human_readable, DataType::String },
                                    { Logger::Format(messageReferencePath, "GuildID"), Logger::Format(msgRef.guild_id), DataType::String },
                                    { Logger::Format(messageReferencePath, "ChannelID"), Logger::Format(msgRef.channel_id), DataType::String },
                                    { Logger::Format(messageReferencePath, "MessageID"), Logger::Format(msgRef.message_id), DataType::String }
                                }
                            );
                        }
                    }
                    catch(...) { GE_LOG(Orchestra, Error, "Failed to retrieve info for message reference."); }

                    isMessageReferenceDone = true;
                    conditionVariable.notify_all();
                });

            std::mutex mutex;

            std::unique_lock joinLock{ mutex };

            conditionVariable.wait(joinLock, [&isMessageReferenceDone] { return isMessageReferenceDone; });
        }
#endif

        return scope;
    }
}
//getters, setters
namespace Orchestra
{
    void OrchestraDiscordBot::SetBossSnowflake(const dpp::snowflake& bossSnowflake)
    {
        m_BossSnowflake = bossSnowflake;
    }
    const dpp::snowflake& OrchestraDiscordBot::GetBossSnowflake() const
    {
        return m_BossSnowflake;
    }
}