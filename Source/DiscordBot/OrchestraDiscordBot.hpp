#pragma once

#include <mutex>
#include <future>
#include <chrono>
#include <string_view>

#include <dpp/dpp.h>

#include "DiscordBot.hpp"
#include "OrchestraDiscordBotInstance.hpp"
#include "Yt_DlpManager.hpp"
#include "TracksQueue.hpp"

namespace Orchestra
{
    class OrchestraDiscordBot final : public DiscordBot
    {
    public:
        using BotInstanceProperties = OrchestraDiscordBotInstanceProperties;
        using FullBotInstanceProperties = FullOrchestraDiscordBotInstanceProperties;
        using BotPlayer = OrchestraDiscordBotPlayer;
        using BotInstance = OrchestraDiscordBotInstance;
    public:
        struct Paths
        {
            std::filesystem::path executablePath;
            std::filesystem::path commandsNamesConfigPath;
            std::filesystem::path commandsDescriptionsConfigPath;
            std::filesystem::path historyLogPath;
            std::filesystem::path guildsConfigPath;
            std::filesystem::path yt_dlpExecutablePath;
        };

    public:
        OrchestraDiscordBot(const std::string& token, Paths paths, FullBotInstanceProperties defaultGuildsValues = {}, dpp::snowflake bossSnowflake = 0, uint32_t intents = dpp::i_all_intents);

        //setters, getters
    public:
        void SetBossSnowflake(const dpp::snowflake& bossSnowflake);
        const dpp::snowflake& GetBossSnowflake() const;

        void RegisterCommands() override;

    private:
        void CommandHelp(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandCurrent(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandQueue(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandPlay(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandPlaylist(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandSpeed(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandBass(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandEqualizer(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandRepeat(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandInsert(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandTransfer(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandReverse(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandShuffle(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandDelete(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandStop(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandPause(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandSkip(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandLeave(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandTerminate(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);

    private:
        static void ConnectToMemberVoice(const dpp::message_create_t& message);

        void WaitUntilJoined(const dpp::snowflake& guildID, const std::chrono::milliseconds& delay);
        dpp::voiceconn* IsVoiceConnectionReady(const dpp::snowflake& guildID);

        void AddToQueue(const std::string_view& commandName, const dpp::message_create_t& message, const std::vector<Param>& params, std::string value, size_t insertIndex = std::numeric_limits<size_t>::max());

        template<GuelderConsoleLog::Concepts::STDOut... Args>
        static void Reply(const dpp::message_create_t& message, Args&&... args)
        {
            using namespace GuelderConsoleLog;

            const std::string reply = Logger::Format(std::forward<Args>(args)...);

            if(reply.size() > DPP_MAX_MESSAGE_LENGTH)
            {
                Reply(message, Logger::Format("Sorry no reply, cuz the length is bigger than ", DPP_MAX_MESSAGE_LENGTH));
                GE_LOG(Orchestra, Warning, "The message exceeds ", DPP_MAX_MESSAGE_LENGTH, " lenght.");
            }
            else
                ReplyWithMessage(message, { reply });
        }
        static void ReplyWithMessage(const dpp::message_create_t& message, dpp::message reply);

        void SendEmbedsSequentially(const dpp::message_create_t& event, const std::vector<dpp::embed>& embeds, size_t index = 0);

        uint32_t GetCurrentPlaylistIndex(const dpp::snowflake& guildID);
        void ReplyWithInfoAboutTrack(const dpp::snowflake& guildID, const dpp::message_create_t& message, const TrackInfo& trackInfo, bool outputURL = true, bool printCurrentTimestamp = false);

        BotInstance& GetBotInstance(const dpp::snowflake& guildID);
        BotPlayer& GetBotPlayer(const dpp::snowflake& guildID);

        static std::string GetRawVariableValue(const GuelderResourcesManager::ConfigFile& configFile, const std::string_view& path);
        std::string GetParamName(const std::string_view& commandName, const std::string_view& paramName) const;

        bool CommandChecker(const dpp::message_create_t& message, ParsedCommand& parsedCommand);

        void LogMessage(const std::filesystem::path& historyLogPath, const dpp::message& message);
        std::string LogMessage(std::string scope, const dpp::message& message, std::string messageNamespacePath = "", uint32_t maxDownloadFileSize = 0, std::filesystem::path fileSavePath = "");

    private:
        //first - guild id, second is BotInstance
        std::unordered_map<dpp::snowflake, BotInstance> m_GuildsBotInstances;
        std::mutex m_GuildCreateMutex;

        dpp::snowflake m_BossSnowflake;

        //std::vector<std::function<void()>> m_OnReadyCallbacks;
        bool m_IsReady;

        std::mt19937 m_RandomEngine;

        Paths m_Paths;

        GuelderResourcesManager::ConfigFile m_CommandsNamesConfig;

        std::mutex m_HistoryLogMutex;
    };
}