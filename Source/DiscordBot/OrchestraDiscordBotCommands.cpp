#include "OrchestraDiscordBot.hpp"

#include <string_view>
#include <string>

#include <dpp/dpp.h>

#include <GuelderResourcesManager.hpp>
#include <GuelderConsoleLog.hpp>
#include <GuelderConsoleLogMacroses.hpp>

#include "OrchestraDiscordBotInstance.hpp"

//commands
namespace Orchestra
{
    using namespace GuelderConsoleLog;
    using namespace GuelderResourcesManager;

    void OrchestraDiscordBot::CommandHelp(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        constexpr std::string_view commandName = "help";

        BotInstance& botInstance = GetBotInstance(message.msg.guild_id);

        std::string commandsPrefix;
        char paramsPrefix;

        {
            auto properties = botInstance.AccessBinarySemaphoreOrchestraDiscordBotInstanceProperties();

            commandsPrefix = properties->commandsPrefix;
            paramsPrefix = properties->paramsPrefix;
        }

        //idk. for some reason(probably because of long description) it doesn't send any embeds with DPP_MAX_EMBED_SIZE or 11
        constexpr size_t _DPP_MAX_EMBED_SIZE = 10;

        const size_t totalFieldsSize = m_Commands.size();

        const size_t totalEmbedsCount = std::ceil(static_cast<float>(totalFieldsSize) / _DPP_MAX_EMBED_SIZE);

        std::vector<dpp::embed> embeds{ totalEmbedsCount };
        //embeds.reserve(totalEmbedsCount);

        GuelderResourcesManager::ConfigFile commandsDescriptionsConfig{ m_Paths.commandsDescriptionsConfigPath, false };

        dpp::embed embed;

        embed.set_title(Logger::Format("Help. Total Commands size: ", totalFieldsSize, '.'));

        size_t commandVariableIndex = 0;
        for(size_t embedsCount = 0; embedsCount < totalEmbedsCount; embedsCount++)
        {
            //reserving
            {
                const size_t remaining = totalFieldsSize - embedsCount * _DPP_MAX_EMBED_SIZE;
                embed.fields.reserve(remaining > _DPP_MAX_EMBED_SIZE ? _DPP_MAX_EMBED_SIZE : remaining);
            }

            //commands
            for(; commandVariableIndex < m_CommandsNamesConfig.GetVariables().size(); commandVariableIndex++)
            {
                if(embed.fields.size() + 1 > _DPP_MAX_EMBED_SIZE)
                    break;

                auto& potentialCommandVariable = m_CommandsNamesConfig.GetVariables()[commandVariableIndex];

                std::string_view potentialCommandVariableName = potentialCommandVariable.GetName();

                //const bool isCommandName = potentialCommandVariableName == std::string_view{potentialCommandVariable.GetPath().begin(), potentialCommandVariable.GetPath().begin() + potentialCommandVariableName.size()};
                const bool isCommandName = std::ranges::find(potentialCommandVariable.GetPath(), '/') == potentialCommandVariable.GetPath().end();

                if(isCommandName)
                {
                    std::string fieldTitle = Logger::Format("`", commandsPrefix, potentialCommandVariable.GetRawValue(), "`");

                    std::string_view commandDescription = commandsDescriptionsConfig.GetVariable(potentialCommandVariable.GetPath()).GetRawValue();
                    if(!commandDescription.empty())
                        fieldTitle += Logger::Format(" : ", commandDescription);

                    std::string fieldValue;

                    auto FindParam = [this, &potentialCommandVariableName, &commandVariableIndex](size_t offset)
                        {
                            int counter;

                            return std::find_if(m_CommandsNamesConfig.GetVariables().begin() + offset, m_CommandsNamesConfig.GetVariables().end(),
                                [&potentialCommandVariableName, &counter, &commandVariableIndex](const GuelderResourcesManager::Variable& variable)
                                {
                                    counter++;

                                    if(counter == commandVariableIndex)
                                        return false;

                                    return variable.GetPath().size() > potentialCommandVariableName.size() + 1 && GuelderResourcesManager::ConfigFile::Parser::IsFullSubstringSame(variable.GetPath(), 0, potentialCommandVariableName) && variable.GetPath()[potentialCommandVariableName.size()] == '/';
                                });
                        };

                    //fuck me
                    //params
                    auto paramIt = FindParam(commandVariableIndex + 1);

                    if(paramIt != m_CommandsNamesConfig.GetVariables().end())
                    {
                        fieldValue += "**Params:**\n";

                        for(; paramIt != m_CommandsNamesConfig.GetVariables().end(); paramIt = FindParam(paramIt - m_CommandsNamesConfig.GetVariables().begin() + 1))
                        {
                            std::string_view paramName = paramIt->GetRawValue();

                            auto foundCommand = std::ranges::find_if(m_Commands, [&potentialCommandVariable](const Command& command) { return command.name == potentialCommandVariable.GetRawValue(); });

                            O_ASSERT(foundCommand != m_Commands.end(), "Failed to find command in m_Commands with name ", potentialCommandVariable.GetRawValue());

                            auto foundParam = std::ranges::find_if(foundCommand->paramsProperties, [&paramName](const ParamProperties& paramProperties) { return paramProperties.name == paramName; });

                            O_ASSERT(foundParam != foundCommand->paramsProperties.end(), "Failed to find param with name ", paramName, " in command with name ", potentialCommandVariable.GetRawValue());

                            fieldValue += Logger::Format("> [`", TypeToString(foundParam->type), "`] **", paramsPrefix, paramName, "**");

                            std::string_view paramDescription = commandsDescriptionsConfig.GetVariable(paramIt->GetPath()).GetRawValue();

                            if(!paramDescription.empty())
                                fieldValue += Logger::Format(" : ", paramDescription);

                            fieldValue += '\n';
                        }
                    }

                    embed.add_field(fieldTitle, fieldValue, false);
                }
            }

            //wft?
            //embeds.push_back(std::move(embed));
            embeds[embedsCount].title = std::move(embed.title);
            embeds[embedsCount].fields = std::move(embed.fields);
        }

        SendEmbedsSequentially(message, embeds);
    }

    void OrchestraDiscordBot::CommandCurrent(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        constexpr std::string_view commandName = "current";

        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);
        auto tracksQueue = botPlayer.AccessBinarySemaphoreTracksQueue();

        if(!tracksQueue->GetTracksSize())
        {
            Reply(message, "I'm not even playing anything!");
            return;
        }

        bool showURL = false;
        GetParamValue(params, GetParamName(commandName, "url"), showURL);

        ReplyWithInfoAboutTrack(message.msg.guild_id, message, tracksQueue->GetTrackInfo(botPlayer.currentTrackIndex), showURL, true);
    }
    void OrchestraDiscordBot::CommandQueue(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        constexpr std::string_view commandName = "queue";

        dpp::voiceconn* voice = IsVoiceConnectionReady(message.msg.guild_id);

        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);
        auto tracksQueue = botPlayer.AccessBinarySemaphoreTracksQueue();

        O_ASSERT(tracksQueue->GetTracksSize() > 0, "The queue is empty!");

        bool showURLs = false;
        GetParamValue(params, GetParamName(commandName, "url"), showURLs);

        const size_t queueSize = tracksQueue->GetTracksSize();
        const size_t playlistsOffset = tracksQueue->GetPlaylistsSize() * 2;//as playlist contains beginIndex and endIndex which should be displayed

        const size_t totalFieldsSize = queueSize + playlistsOffset;

        const size_t totalEmbedsCount = std::ceil(static_cast<float>(totalFieldsSize) / DPP_MAX_EMBED_SIZE);

        size_t trackIndex = 0;

        size_t playlistInfoIndex = tracksQueue->GetPlaylistInfos().empty() ? std::numeric_limits<size_t>::max() : 0;

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
                const PlaylistInfo* currentPlaylistInfo = isPlaylistInfoValid ? &tracksQueue->GetPlaylistInfos()[playlistInfoIndex] : nullptr;

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

                const auto& [URL, rawURL, title, duration, playlistIndex, repeat, speed] = tracksQueue->GetTrackInfo(trackIndex);

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
                    fieldValue += Logger::Format("Speed: ", speed, ".\nDuration with speed applied: ", static_cast<float>(duration) / speed, "s.\n");
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

                    if(playlistInfoIndex >= tracksQueue->GetPlaylistsSize())
                        playlistInfoIndex = std::numeric_limits<size_t>::max();
                }
            }

            embeds.push_back(std::move(embed));
        }

        SendEmbedsSequentially(message, embeds);
    }

    void OrchestraDiscordBot::CommandPlay(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        constexpr std::string_view commandName = "play";

        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);

        O_ASSERT(!value.empty(), "No music value provided.");

        //join
        //TODO:
        //if(!botPlayer.isJoined)
        ConnectToMemberVoice(message);

        botPlayer.isStopped = false;

        auto tracksQueue = botPlayer.AccessBinarySemaphoreTracksQueue();

        const size_t tracksNumberBefore = tracksQueue->GetTracksSize();

        AddToQueue(*tracksQueue, commandName, message, params, value.data());

        //tracksQueue.Unlock();

        if(tracksNumberBefore)
            return;

        int beginIndex = 0;
        GetParamValue(params, GetParamName(commandName, "index"), beginIndex);
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

        if(!botPlayer.isStopped)
        {
            //checking if connection was successful
            const dpp::voiceconn* voice = IsVoiceConnectionReady(message.msg.guild_id);

            bool noInfo = false;

            GetParamValue(params, GetParamName(commandName, "noinfo"), noInfo);

            bool isInPlaylist = false;
            const PlaylistInfo* currentPlaylistInfo = nullptr;
            size_t prevPlaylistIndex = std::numeric_limits<size_t>::max();
            size_t prevUniqueTrackIndex;
            size_t playlistRepeat = 0;

            struct
            {
                float decibelsBoost = 0.f;
                float frequencyToAdjust = 110.f;
                float bandwidth = .3f;
            } bassBoostSettings;

            GetParamValue(params, GetParamName(commandName, "bass"), bassBoostSettings.decibelsBoost);
            GetParamValue(params, GetParamName(commandName, "basshz"), bassBoostSettings.frequencyToAdjust);
            GetParamValue(params, GetParamName(commandName, "bassband"), bassBoostSettings.bandwidth);

            botPlayer.player.SetBassBoost(bassBoostSettings.decibelsBoost, bassBoostSettings.frequencyToAdjust, bassBoostSettings.bandwidth);

            //decltype(botPlayer.AccessBinarySemaphoreTracksQueue()) tracksQueue1{};

            //tracksQueue = botPlayer.AccessBinarySemaphoreTracksQueue();

            botPlayer.hasRawURLRetrievingCompleted = false;

            for(size_t i = 0, playlistRepeated = 0, trackRepeated = 0; true; ++i)
            {
                const TrackInfo* currentTrackInfo = nullptr;

                bool caughtException = false;
                try
                {
                    bool decodeCurrentTrack = true;

                    if(botPlayer.currentTrackIndex >= tracksQueue->GetTracksSize())
                        break;

                    size_t indexToSetRawURL = botPlayer.currentTrackIndex;
                    currentTrackInfo = &tracksQueue->GetTrackInfos()[botPlayer.currentTrackIndex];

                    prevUniqueTrackIndex = currentTrackInfo->uniqueIndex;

                    //this if is the shittiest in the entire solution
                    if(currentTrackInfo->rawURL.empty())
                    {
                        bool receivedRawURL = false;

                        auto processReadInfo = Yt_DlpManager::StartGetRawURLFromURL(m_Paths.yt_dlpExecutablePath, currentTrackInfo->URL);

                        bool rethrow = false;

                        std::jthread gettingRawURLThread{ [&]
                        {
                            //a long one
                            auto result = Yt_DlpManager::FinishGetRawURLFromURL(processReadInfo);

                            if(result.has_value())
                            {
                                tracksQueue = botPlayer.AccessBinarySemaphoreTracksQueue();

                                auto found = std::ranges::find_if(tracksQueue->GetTrackInfos(), [&prevUniqueTrackIndex](const TrackInfo& info) { return info.uniqueIndex == prevUniqueTrackIndex; });
                                O_ASSERT(found != tracksQueue->GetTrackInfos().end(), "Failed to find a track with unique index ", prevUniqueTrackIndex, ", which would have received a raw url.");

                                tracksQueue->SetTrackRawURL(found - tracksQueue->GetTrackInfos().begin(),std::move(result.value()[0]));

                                GE_LOG(Orchestra, Warning, tracksQueue->GetTrackInfo(found - tracksQueue->GetTrackInfos().begin()).rawURL);

                                receivedRawURL = true;

                                //tracksQueue.Unlock();

                                botPlayer.gettingRawURLCondition.notify_all();
                            }
                        } };

                        tracksQueue.Unlock();
                        std::unique_lock lock{ botPlayer.gettingRawURLMutex };
                        botPlayer.gettingRawURLCondition.wait(lock);

                        //if(!*tracksQueue)
                            tracksQueue = botPlayer.AccessBinarySemaphoreTracksQueue();

                        if(rethrow)
                            O_THROW("Failed to get raw track URL.");

                        if(!receivedRawURL)
                        {
                            GE_LOG(Orchestra, Warning, "TERMINATING");
                            processReadInfo.processInfo.TerminateProcess();
                            decodeCurrentTrack = false;
                        }
                    }

                    if(decodeCurrentTrack)
                    {
                        botPlayer.player.SetDecoder(currentTrackInfo->rawURL, Decoder::DEFAULT_SAMPLE_RATE / currentTrackInfo->speed);

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
                                    tracksQueue->SetTrackTitle(indexToSetRawURL, title);
                            }
                            if(currentTrackInfo->duration == 0.f)
                            {
                                //it is raw, trying to get duration with ffmpeg
                                float duration;
                                duration = botPlayer.player.GetTotalDuration();
                                if(duration != 0.f)
                                    tracksQueue->SetTrackDuration(0, duration);
                            }

                            TrackInfo tmp = *currentTrackInfo;
                            tmp.repeat -= trackRepeated;

                            ReplyWithInfoAboutTrack(message.msg.guild_id, message, tmp);
                        }

                        tracksQueue.Unlock();

                        //GE_LOG(Orchestra, Error, "\tPLAY DECODING", indexToSetRawURL);

                        botPlayer.player.DecodeAndSendAudio(voice);
                    }
                    //else
                        //tracksQueue.Unlock();
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
                //auto tracksQueue2 = botPlayer.AccessBinarySemaphoreTracksQueue();

                if(caughtException)
                {
                    //haven't tested this
                    tracksQueue->DeleteTrack(botPlayer.currentTrackIndex);
                    continue;
                }

                if(!*tracksQueue)
                    tracksQueue = botPlayer.AccessBinarySemaphoreTracksQueue();

                if(botPlayer.currentTrackIndex >= tracksQueue->GetTracksSize())
                    break;

                currentTrackInfo = &tracksQueue->GetTrackInfos()[botPlayer.currentTrackIndex];

                const bool wasTrackSkipped = prevUniqueTrackIndex != currentTrackInfo->uniqueIndex;
                bool incrementCurrentIndex = !wasTrackSkipped;

                if(!wasTrackSkipped)
                    trackRepeated++;

                bool found = false;
                for(size_t j = 0; j < tracksQueue->GetPlaylistsSize(); j++)
                {
                    const PlaylistInfo& playlistInfo = tracksQueue->GetPlaylistInfos()[j];

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

            tracksQueue->Clear();

            botPlayer.currentTrackIndex = 0;

            tracksQueue.Unlock();

            auto prevBass = botPlayer.player.GetBassBoostSettings();

            botPlayer.player.ResetDecoder();

            //I know that std::move has no effect here, I just do it
            botPlayer.player.SetBassBoost(std::move(prevBass));
        }

        botPlayer.hasRawURLRetrievingCompleted = true;
    }

    void OrchestraDiscordBot::CommandPlaylist(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        constexpr std::string_view commandName = "playlist";

        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);
        auto tracksQueue = botPlayer.AccessBinarySemaphoreTracksQueue();

        int deleteIndex = -1;
        GetParamValue(params, GetParamName(commandName, "delete"), deleteIndex);

        if(deleteIndex == -1)
        {
            int from = 0;
            int fromParamIndex = GetParamIndex(params, GetParamName(commandName, "from"));
            if(fromParamIndex != -1)
                from = GetParamValue<int>(params, fromParamIndex);

            int to = tracksQueue->GetTracksSize() - 1;
            int toParamIndex = GetParamIndex(params, GetParamName(commandName, "to"));
            if(toParamIndex != -1)
                to = GetParamValue<int>(params, toParamIndex);

            float speed = 1.f;
            GetParamValue(params, GetParamName(commandName, "speed"), speed);

            std::string name;
            GetParamValue(params, GetParamName(commandName, "name"), name);

            int repeat = 1;
            GetParamValue(params, GetParamName(commandName, "repeat"), repeat);
            if(repeat < 0)
                repeat = std::numeric_limits<int>::max();

            O_ASSERT(from < to && to < tracksQueue->GetTracksSize(), "Invalid start or end indicies.");
            O_ASSERT(speed > 0.f, "Invalid speed value.");

            for(size_t i = from; i <= to; i++)
                tracksQueue->SetTrackSpeed(i, speed);

            if(botPlayer.currentTrackIndex >= from && botPlayer.currentTrackIndex <= to)
                botPlayer.player.SetAudioSampleRate(Decoder::DEFAULT_SAMPLE_RATE * speed);

            tracksQueue->AddPlaylist(from, to, speed, repeat, (name));
        }
        else
        {
            O_ASSERT(deleteIndex >= 0 && deleteIndex < tracksQueue->GetPlaylistsSize(), "Invalid delete index");

            tracksQueue->DeletePlaylist(deleteIndex);
        }
    }

    void OrchestraDiscordBot::CommandSpeed(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        constexpr std::string_view commandName = "speed";

        using namespace GuelderResourcesManager;

        IsVoiceConnectionReady(message.msg.guild_id);

        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);
        auto tracksQueue = botPlayer.AccessBinarySemaphoreTracksQueue();

        O_ASSERT(tracksQueue->GetTracksSize() > 0, "The tracks queue size is 0.");
        O_ASSERT(!value.empty(), "No speed value provided.");

        const float speed = StringToNumber<float>(value);

        O_ASSERT(speed > 0.f, "Invalid speed value");

        const int fromParamIndex = GetParamIndex(params, GetParamName(commandName, "from"));
        int from = 0;
        if(fromParamIndex != -1)
            from = params[fromParamIndex].GetValue<int>();

        const int toParamIndex = GetParamIndex(params, GetParamName(commandName, "to"));
        int to = tracksQueue->GetTracksSize() - 1;
        if(toParamIndex != -1)
            to = params[toParamIndex].GetValue<int>();

        int playlistParamIndex = -1;

        if(fromParamIndex == -1 && toParamIndex == -1)
        {
            playlistParamIndex = GetParamIndex(params, GetParamName(commandName, "playlist"));

            if(playlistParamIndex != -1)
            {
                int playlistIndex = GetParamValue<int>(params, playlistParamIndex);

                if(playlistIndex == -1)
                    playlistIndex = GetCurrentPlaylistIndex(message.msg.guild_id, *tracksQueue);

                O_ASSERT(playlistIndex >= 0 && playlistIndex < tracksQueue->GetPlaylistsSize(), "Playlist index is invalid");

                from = tracksQueue->GetPlaylistInfos()[playlistIndex].beginIndex;
                to = tracksQueue->GetPlaylistInfos()[playlistIndex].endIndex;
            }
        }

        if(fromParamIndex != -1 || toParamIndex != -1 || playlistParamIndex != -1)
        {
            O_ASSERT(from >= 0 && from < to && to < tracksQueue->GetTracksSize(), "\"from or \"to\" is outside of the range.");

            if(from <= botPlayer.currentTrackIndex && to >= botPlayer.currentTrackIndex)
            {
                O_ASSERT(botPlayer.player.IsDecoderReady(), "The Decoder is not ready.");

                if(tracksQueue->GetTrackInfo(botPlayer.currentTrackIndex).speed != speed)
                    botPlayer.player.SetAudioSampleRate(Decoder::DEFAULT_SAMPLE_RATE / speed);
            }

            for(int i = from; i <= to; i++)
                tracksQueue->SetTrackSpeed(i, speed);
        }
        else
        {
            size_t index = botPlayer.currentTrackIndex;
            GetParamValue(params, GetParamName(commandName, "index"), index);

            {
                bool deleteMiddleTrack = false;
                GetParamValue(params, GetParamName(commandName, "middle"), deleteMiddleTrack);

                if(deleteMiddleTrack)
                    index = (tracksQueue->GetTracksSize() - 1) / 2;
                else
                {
                    bool deleteLastTrack = false;
                    GetParamValue(params, GetParamName(commandName, "last"), deleteLastTrack);

                    if(deleteLastTrack)
                        index = tracksQueue->GetTracksSize() - 1;
                }
            }

            O_ASSERT(index < tracksQueue->GetTracksSize(), "Invalid index.");

            if(index == botPlayer.currentTrackIndex)
            {
                O_ASSERT(botPlayer.player.IsDecoderReady(), "The Decoder is not ready.");

                if(tracksQueue->GetTrackInfo(botPlayer.currentTrackIndex).speed != speed)
                    botPlayer.player.SetAudioSampleRate(Decoder::DEFAULT_SAMPLE_RATE / speed);
            }

            tracksQueue->SetTrackSpeed(index, speed);
        }
    }

    void OrchestraDiscordBot::CommandBass(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        constexpr std::string_view commandName = "bass";

        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);

        const Player::BassBoostSettings bassBoostSettings = botPlayer.player.GetBassBoostSettings();

        bool clear = false;
        GetParamValue(params, GetParamName(commandName, "clear"), clear);

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
        GetParamValue(params, GetParamName(commandName, "hz"), frequency);

        float bandwidth = bassBoostSettings.bandwidth;
        GetParamValue(params, GetParamName(commandName, "band"), bandwidth);

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
        constexpr std::string_view commandName = "equalizer";

        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);

        bool clear = false;
        GetParamValue(params, GetParamName(commandName, "clear"), clear);

        if(clear)
        {
            botPlayer.player.ClearEqualizer();
            return;
        }

        bool addFrequency = false;

        GetParamValue(params, GetParamName(commandName, "delete"), addFrequency);

        addFrequency = !addFrequency;

        float frequency = 0.f;
        int frequencyParamIndex = GetParamIndex(params, GetParamName(commandName, "hz"));
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
        constexpr std::string_view commandName = "repeat";

        using namespace GuelderResourcesManager;

        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);
        auto tracksQueue = botPlayer.AccessBinarySemaphoreTracksQueue();

        //IsVoiceConnectionReady(message.msg.guild_id);

        O_ASSERT(tracksQueue->GetTracksSize() > 0, "The tracks queue size is 0.");
        O_ASSERT(!value.empty(), "No speed value provided.");

        int repeatCount = StringToNumber<int>(value);

        O_ASSERT(repeatCount != 0, "Invalid repetCount value, repeatCount is 0.");

        if(repeatCount < 0)
            repeatCount = std::numeric_limits<int>::max();

        const int fromParamIndex = GetParamIndex(params, GetParamName(commandName, "from"));
        int from = 0;
        if(fromParamIndex != -1)
            from = params[fromParamIndex].GetValue<int>();

        const int toParamIndex = GetParamIndex(params, GetParamName(commandName, "to"));
        int to = tracksQueue->GetTracksSize() - 1;
        if(toParamIndex != -1)
            to = params[toParamIndex].GetValue<int>();

        int playlistParamIndex = -1;
        int playlistIndex;

        if(fromParamIndex == -1 && toParamIndex == -1)
        {
            playlistParamIndex = GetParamIndex(params, GetParamName(commandName, "playlist"));

            if(playlistParamIndex != -1)
            {
                playlistIndex = GetParamValue<int>(params, playlistParamIndex);

                if(playlistIndex == -1)
                    playlistIndex = GetCurrentPlaylistIndex(message.msg.guild_id, *tracksQueue);

                O_ASSERT(playlistIndex >= 0 && playlistIndex < tracksQueue->GetPlaylistsSize(), "Playlist index is invalid");
            }
        }

        if(fromParamIndex != -1 || toParamIndex != -1 || playlistParamIndex != -1)
        {
            O_ASSERT(from >= 0 && from < to && to < tracksQueue->GetTracksSize(), "\"from or \"to\" is outside of the range.");

            if(playlistParamIndex == -1)
                for(int i = from; i <= to; i++)
                    tracksQueue->SetTrackRepeatCount(i, repeatCount);
            else
                tracksQueue->SetPlaylistRepeatCount(playlistIndex, repeatCount);
        }
        else
        {
            size_t index = botPlayer.currentTrackIndex;
            GetParamValue(params, GetParamName(commandName, "index"), index);

            {
                bool deleteMiddleTrack = false;
                GetParamValue(params, GetParamName(commandName, "middle"), deleteMiddleTrack);

                if(deleteMiddleTrack)
                    index = (tracksQueue->GetTracksSize() - 1) / 2;
                else
                {
                    bool deleteLastTrack = false;
                    GetParamValue(params, GetParamName(commandName, "last"), deleteLastTrack);

                    if(deleteLastTrack)
                        index = tracksQueue->GetTracksSize() - 1;
                }
            }

            O_ASSERT(index < tracksQueue->GetTracksSize(), "Invalid index.");

            tracksQueue->SetTrackRepeatCount(index, repeatCount);
        }
    }

    void OrchestraDiscordBot::CommandInsert(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        constexpr std::string_view commandName = "insert";

        //IsVoiceConnectionReady(message.msg.guild_id);

        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);
        auto tracksQueue = botPlayer.AccessBinarySemaphoreTracksQueue();

        O_ASSERT(tracksQueue->GetTracksSize() > 0, "The queue is empty!");

        int after = botPlayer.currentTrackIndex + 1;

        if(const int afterParamIndex = GetParamIndex(params, GetParamName(commandName, "after")); afterParamIndex != -1)
        {
            after = GetParamValue<int>(params, afterParamIndex);

            if(after < 0 || after > tracksQueue->GetTracksSize())
                after = botPlayer.currentTrackIndex;

            if(after != 0)
                after++;
        }

        AddToQueue(*tracksQueue, commandName, message, params, value.data(), after);
    }

    void OrchestraDiscordBot::CommandTransfer(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        constexpr std::string_view commandName = "transfer";

        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);
        auto tracksQueue = botPlayer.AccessBinarySemaphoreTracksQueue();

        O_ASSERT(tracksQueue->GetTracksSize() > 0, "The tracks queue is empty.");

        const int targetIndex = GuelderResourcesManager::StringToNumber<int>(value);

        int to = -1;

        GetParamValue(params, GetParamName(commandName, "to"), to);

        if(to == -1)
        {
            const int middleParamIndex = GetParamIndex(params, GetParamName(commandName, "middle"));
            if(middleParamIndex >= 0)
                to = (tracksQueue->GetTracksSize() - 1) / 2;
            else
                to = tracksQueue->GetTracksSize() - 1;
        }
        else
            if(to >= tracksQueue->GetTracksSize())
                to = tracksQueue->GetTracksSize() - 1;

        O_ASSERT(to >= 0 && targetIndex != to, "Invalid after index.");

        if(targetIndex == botPlayer.currentTrackIndex)
            botPlayer.currentTrackIndex = to;
        else if(to == botPlayer.currentTrackIndex)
            ++botPlayer.currentTrackIndex;

        tracksQueue->TransferTrack(targetIndex, to);
    }

    void OrchestraDiscordBot::CommandReverse(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        constexpr std::string_view commandName = "reverse";

        using namespace GuelderResourcesManager;

        IsVoiceConnectionReady(message.msg.guild_id);

        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);
        auto tracksQueue = botPlayer.AccessBinarySemaphoreTracksQueue();

        O_ASSERT(tracksQueue->GetTracksSize() > 0, "The tracks queue size is 0.");

        const int fromParamIndex = GetParamIndex(params, GetParamName(commandName, "from"));
        int from = 0;
        if(fromParamIndex != -1)
            from = params[fromParamIndex].GetValue<int>();

        const int toParamIndex = GetParamIndex(params, GetParamName(commandName, "to"));
        int to = tracksQueue->GetTracksSize() - 1;
        if(toParamIndex != -1)
            to = params[toParamIndex].GetValue<int>();

        int playlistParamIndex = -1;

        if(fromParamIndex == -1 && toParamIndex == -1)
        {
            playlistParamIndex = GetParamIndex(params, GetParamName(commandName, "playlist"));

            if(playlistParamIndex != -1)
            {
                int playlistIndex = GetParamValue<int>(params, playlistParamIndex);

                if(playlistIndex == -1)
                    playlistIndex = GetCurrentPlaylistIndex(message.msg.guild_id, *tracksQueue);

                O_ASSERT(playlistIndex >= 0 && playlistIndex < tracksQueue->GetPlaylistsSize(), "Playlist index is invalid");

                from = tracksQueue->GetPlaylistInfos()[playlistIndex].beginIndex;
                to = tracksQueue->GetPlaylistInfos()[playlistIndex].endIndex;
            }
        }

        O_ASSERT(from >= 0 && from < to && to < tracksQueue->GetTracksSize(), "\"from or \"to\" is outside of the range.");

        //fix this
        if(from <= botPlayer.currentTrackIndex || to == botPlayer.currentTrackIndex)
        {
            botPlayer.currentTrackIndex = to - (botPlayer.currentTrackIndex - from);
            //botPlayer.gettingRawURLCondition.notify_all();
        }

        tracksQueue->Reverse(from, to);
    }

    void OrchestraDiscordBot::CommandShuffle(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        constexpr std::string_view commandName = "shuffle";

        //IsVoiceConnectionReady(message.msg.guild_id);

        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);
        auto tracksQueue = botPlayer.AccessBinarySemaphoreTracksQueue();

        O_ASSERT(tracksQueue->GetTracksSize() > 0, "The queue is empty.");

        int playlistIndex = -1;
        int playlistParamIndex = GetParamIndex(params, GetParamName(commandName, "playlist"));
        if(playlistParamIndex >= 0)
            playlistIndex = GetParamValue<int>(params, playlistParamIndex);

        int from = 0;
        int to = tracksQueue->GetTracksSize() - 1;

        const int fromParamIndex = GetParamIndex(params, GetParamName(commandName, "from"));
        const int toParamIndex = GetParamIndex(params, GetParamName(commandName, "to"));

        if(playlistParamIndex >= 0)
        {
            if(playlistIndex == -1)
                playlistIndex = GetCurrentPlaylistIndex(message.msg.guild_id, *tracksQueue);

            O_ASSERT(playlistIndex < tracksQueue->GetPlaylistsSize(), "Invalid playlist index.");

            from = tracksQueue->GetPlaylistInfo(playlistIndex).beginIndex;
            to = tracksQueue->GetPlaylistInfo(playlistIndex).endIndex;
        }
        else
        {
            if(fromParamIndex != -1)
                from = GetParamValue<int>(params, fromParamIndex);
            if(toParamIndex != -1)
                to = GetParamValue<int>(params, toParamIndex);
        }

        const bool isCurrentTrackInScope = botPlayer.currentTrackIndex >= from && botPlayer.currentTrackIndex <= to;

        if(fromParamIndex == -1 && toParamIndex == -1 && playlistParamIndex == -1)
            tracksQueue->ClearPlaylists();

        tracksQueue->Shuffle(m_RandomEngine, from, to + 1, (isCurrentTrackInScope ? botPlayer.currentTrackIndex.load() : std::numeric_limits<size_t>::max()));

        if(isCurrentTrackInScope)
        {
            botPlayer.currentTrackIndex = from;
            //there must not be such stuff as the track's index remains the same as we set it first
            //botPlayer.gettingRawURLCondition.notify_all();
        }

        GE_LOG(Orchestra, Error, "\tSHUFFLE ", botPlayer.currentTrackIndex);
    }
    void OrchestraDiscordBot::CommandDelete(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        constexpr std::string_view commandName = "delete";

        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);
        auto tracksQueue = botPlayer.AccessBinarySemaphoreTracksQueue();

        O_ASSERT(tracksQueue->GetTracksSize() > 0, "The queue is empty.");

        size_t index = (value.empty() ? std::numeric_limits<size_t>::max() : GuelderResourcesManager::StringToNumber<size_t>(value));
        const dpp::voiceconn* voice = IsVoiceConnectionReady(message.msg.guild_id);

        {
            bool deleteCurrentTrack = false;
            GetParamValue(params, GetParamName(commandName, "current"), deleteCurrentTrack);

            if(deleteCurrentTrack)
                index = botPlayer.currentTrackIndex;
            else
            {
                bool deleteMiddleTrack = false;
                GetParamValue(params, GetParamName(commandName, "middle"), deleteMiddleTrack);

                if(deleteMiddleTrack)
                    index = (tracksQueue->GetTracksSize() - 1) / 2;
                else
                {
                    bool deleteLastTrack = false;
                    GetParamValue(params, GetParamName(commandName, "last"), deleteLastTrack);

                    if(deleteLastTrack)
                        index = tracksQueue->GetTracksSize() - 1;
                }
            }
        }

        if(index != std::numeric_limits<size_t>::max())
        {
            O_ASSERT(index < tracksQueue->GetTracksSize(), "The index is bigger than the last item of queue with index ", tracksQueue->GetTracksSize() - 1);

            if(index == botPlayer.currentTrackIndex)
            {
                voice->voiceclient->stop_audio();
                botPlayer.player.Skip();
                botPlayer.gettingRawURLCondition.notify_all();
            }

            //why it was here before?
            //if(m_CurrentTrackIndex > 0)
                //--m_CurrentTrackIndex;

            else if(index < botPlayer.currentTrackIndex)
            {
                --botPlayer.currentTrackIndex;
                //botPlayer.gettingRawURLCondition.notify_all();
            }

            tracksQueue->DeleteTrack(index);
        }
        else
        {
            const int fromParamIndex = GetParamIndex(params, GetParamName(commandName, "from"));
            int from = 0;
            if(fromParamIndex != -1)
                from = params[fromParamIndex].GetValue<int>();

            const int toParamIndex = GetParamIndex(params, GetParamName(commandName, "to"));
            int to = tracksQueue->GetTracksSize() - 1;
            if(toParamIndex != -1)
                to = params[toParamIndex].GetValue<int>();

            if(fromParamIndex == -1 && toParamIndex == -1)
            {
                int playlistIndexToDelete = std::numeric_limits<int>::max();
                try
                {
                    GetParamValue(params, GetParamName(commandName, "playlist"), playlistIndexToDelete);
                }
                catch(...)
                {
                    playlistIndexToDelete = GetCurrentPlaylistIndex(message.msg.guild_id, *tracksQueue);
                }

                //if(playlistIndexToDelete == -1)
                    //playlistIndexToDelete = 0;

                O_ASSERT(playlistIndexToDelete < tracksQueue->GetPlaylistsSize() && playlistIndexToDelete >= 0, "The playlist index is invalid.");

                const PlaylistInfo& playlistInfo = tracksQueue->GetPlaylistInfos()[playlistIndexToDelete];

                from = playlistInfo.beginIndex;
                to = playlistInfo.endIndex;
            }

            //O_ASSERT(fromParamIndex != -1 || toParamIndex != -1, "The command is empty, nothing to execute.");
            O_ASSERT(from >= 0 && from < to && to < tracksQueue->GetTracksSize(), "\"from or \"to\" is outside of the range.");

            const bool isCurrentTrackInRange = botPlayer.currentTrackIndex >= from && botPlayer.currentTrackIndex <= to;

            if(isCurrentTrackInRange)
            {
                voice->voiceclient->stop_audio();
                botPlayer.player.Skip();

                botPlayer.currentTrackIndex = from;
                botPlayer.gettingRawURLCondition.notify_all();
            }
            else if(to < botPlayer.currentTrackIndex)
            {
                botPlayer.currentTrackIndex -= to - from + 1;
                //botPlayer.gettingRawURLCondition.notify_all();
            }

            tracksQueue->DeleteTracks(from, to);
        }
    }

    void OrchestraDiscordBot::CommandStop(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        constexpr std::string_view commandName = "stop";

        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);
        //we need this one to block the mutex so that it will get a correct index
        auto tracksQueue = botPlayer.AccessBinarySemaphoreTracksQueue();

        const dpp::voiceconn* v = IsVoiceConnectionReady(message.msg.guild_id);

        botPlayer.player.Stop();
        botPlayer.isStopped = true;

        botPlayer.currentTrackIndex = std::numeric_limits<uint32_t>::max();
        botPlayer.gettingRawURLCondition.notify_all();

        v->voiceclient->pause_audio(botPlayer.player.GetIsPaused());
        v->voiceclient->stop_audio();
    }
    void OrchestraDiscordBot::CommandPause(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        constexpr std::string_view commandName = "pause";

        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);

        const dpp::voiceconn* v = IsVoiceConnectionReady(message.msg.guild_id);

        botPlayer.player.Pause(!botPlayer.player.GetIsPaused());
        v->voiceclient->pause_audio(botPlayer.player.GetIsPaused());

        Reply(message, "Pause is ", (botPlayer.player.GetIsPaused() ? "on" : "off"), '.');
    }
    void OrchestraDiscordBot::CommandSkip(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        constexpr std::string_view commandName = "skip";

        BotPlayer& botPlayer = GetBotPlayer(message.msg.guild_id);
        auto tracksQueue = botPlayer.AccessBinarySemaphoreTracksQueue();

        const dpp::voiceconn* v = IsVoiceConnectionReady(message.msg.guild_id);

        O_ASSERT(tracksQueue->GetTracksSize() != 0, "The tracks queue is empty.");

        //not sure about this one
        //O_ASSERT(m_Player.GetDecodersCount() != 0, "Decoders are empty.");

        //ugly shit
        bool skipPlaylist = false;
        GetParamValue(params, GetParamName(commandName, "playlist"), skipPlaylist);

        int toindex = -1;
        if(!skipPlaylist)
            GetParamValue(params, GetParamName(commandName, "to"), toindex);

        int inindex = 0;
        if(!skipPlaylist && toindex < 0)
            GetParamValue(params, GetParamName(commandName, "in"), inindex);

        bool skipToMiddle = false;
        if(!skipPlaylist && toindex < 0 && inindex == 0)
            GetParamValue(params, GetParamName(commandName, "middle"), skipToMiddle);

        bool skipToLast = false;
        if(!skipPlaylist && toindex < 0 && inindex == 0 && !skipToMiddle)
            GetParamValue(params, GetParamName(commandName, "last"), skipToLast);

        float tosecs = 0.f;

        const int tosecsIndex = GetParamIndex(params, GetParamName(commandName, "tosecs"));

        if(!skipPlaylist && toindex < 0 && inindex == 0 && !skipToMiddle && !skipToLast && tosecsIndex != -1)
            tosecs = GetParamValue<int>(params, tosecsIndex);

        float secs = 0.f;
        if(!skipPlaylist && toindex < 0 && inindex == 0 && !skipToMiddle && !skipToLast && tosecsIndex == -1)
            GetParamValue(params, GetParamName(commandName, "secs"), secs);

        bool skip = botPlayer.player.IsDecoderReady();
        int skipToIndex = botPlayer.currentTrackIndex;

        if(skipPlaylist)
        {
            bool found = false;
            for(const auto& playlistInfo : tracksQueue->GetPlaylistInfos())
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
            O_ASSERT(toindex >= 0 && toindex < tracksQueue->GetTracksSize(), "Parameter \"toindex\" is invalid.");

            skipToIndex = toindex;
        }
        else if(inindex != 0)
        {
            O_ASSERT(botPlayer.currentTrackIndex + inindex < tracksQueue->GetTracksSize(), "Parameter \"inindex\" is invalid.");

            skipToIndex = botPlayer.currentTrackIndex + inindex;
        }
        else if(skipToMiddle)
        {
            skipToIndex = (tracksQueue->GetTracksSize() - 1) / 2;
        }
        else if(skipToLast)
        {
            skipToIndex = tracksQueue->GetTracksSize() - 1;
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

        if(skipToIndex != botPlayer.currentTrackIndex)
        {
            botPlayer.currentTrackIndex = skipToIndex;
            botPlayer.gettingRawURLCondition.notify_all();
        }
        if(skip)
        {
            botPlayer.player.Skip();
            Reply(message, "Skipped!");
        }

        v->voiceclient->stop_audio();
    }
    void OrchestraDiscordBot::CommandLeave(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        constexpr std::string_view commandName = "leave";

        //just to assert
        IsVoiceConnectionReady(message.msg.guild_id);

        CommandStop(message, params, value);

        this->get_shard(0)->disconnect_voice(message.msg.guild_id);

        Reply(message, "Leaving.");
    }
    void OrchestraDiscordBot::CommandTerminate(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
    {
        constexpr std::string_view commandName = "terminate";

        BotInstance& botInstance = GetBotInstance(message.msg.guild_id);

        if(m_BossSnowflake != 0 && message.msg.author.id == m_BossSnowflake)
        {
            Reply(message, "Goodbye!");

            Shutdown();
        }
        else
            Reply(message, "You are not The Boss!");
    }
}