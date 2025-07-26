#pragma once

#include <atomic>
#include <mutex>
#include <future>
#include <chrono>
#include <string_view>

#include <dpp/dpp.h>

#include "DiscordBot.hpp"
#include "Player.hpp"
#include "Yt_DlpManager.hpp"
#include "TracksQueue.hpp"

namespace Orchestra
{
    class OrchestraDiscordBot : public DiscordBot
    {
    public:
        OrchestraDiscordBot(const std::string& token, std::string yt_dlpPath, std::string commandPrefix = "!", char paramPrefix = '-', uint32_t intents = dpp::i_all_intents);

        //setters, getters
    public:
        void SetEnableLogSentPackets(bool enable);
        void SetSentPacketSize(uint32_t size);
        void SetAdminSnowflake(dpp::snowflake id);

        /*bool GetEnableLogSentPackets() const noexcept;
        uint32_t GetSentPacketSize() const noexcept;
        dpp::snowflake GetAdminSnowflake() const noexcept;*/

    private:
        void CommandHelp(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandCurrentTrack(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
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
        struct BotPlayer;

        static void ConnectToMemberVoice(const dpp::message_create_t& message);

        void WaitUntilJoined(const dpp::snowflake& guildID, const std::chrono::milliseconds& delay);
        dpp::voiceconn* IsVoiceConnectionReady(const dpp::snowflake& guildID);

        void AddToQueue(const dpp::message_create_t& message, const std::vector<Param>& params, std::string value, size_t insertIndex = std::numeric_limits<size_t>::max());

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

        BotPlayer& GetBotPlayer(const dpp::snowflake& guildID);

    private:

        struct BotPlayer
        {
            BotPlayer() = default;
            BotPlayer(const std::string_view& yt_dlpPath, const dpp::snowflake& adminSnowflake = 0, const std::string_view& prefix = "!", const char& paramPrefix = '-');

            BotPlayer& operator=(BotPlayer&& other) noexcept;

            Player player;

            dpp::snowflake adminSnowflake;

            std::atomic_bool isStopped;
            std::mutex playMutex;

            std::atomic_bool isJoined;
            std::condition_variable joinedCondition;
            std::mutex joinMutex;

            TracksQueue tracksQueue;
            std::atomic_uint32_t currentTrackIndex;
            std::atomic_uint32_t currentPlaylistIndex;
            mutable std::mutex tracksQueueMutex;
        };

        //first - guild id, second is BotPlayer instance
        std::unordered_map<dpp::snowflake, BotPlayer> m_GuildsBotPlayers;
        std::mutex m_GuildCreateMutex;

        std::vector<std::function<void()>> m_OnReadyCallbacks;
        bool m_IsReady;
    };
}