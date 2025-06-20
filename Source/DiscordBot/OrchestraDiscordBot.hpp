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
        OrchestraDiscordBot(const std::string_view& token, const std::wstring_view& yt_dlpPath, const std::string_view& prefix = "!", const char& paramPrefix = '-', uint32_t intents = dpp::i_all_intents);

        //setters, getters
    public:
        void SetEnableLogSentPackets(const bool& enable);
        void SetSentPacketSize(const uint32_t& size);
        void SetAdminSnowflake(const dpp::snowflake& id);

        bool GetEnableLogSentPackets() const noexcept;
        uint32_t GetSentPacketSize() const noexcept;
        dpp::snowflake GetAdminSnowflake() const noexcept;

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
        void CommandShuffle(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandDelete(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandStop(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandPause(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandSkip(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandLeave(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandTerminate(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);

    private:
        static void ConnectToMemberVoice(const dpp::message_create_t& message);

        void WaitUntilJoined(const std::chrono::milliseconds& delay);
        dpp::voiceconn* IsVoiceConnectionReady(const dpp::snowflake& guildSnowflake);

        void AddToQueue(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value, size_t insertIndex = std::numeric_limits<size_t>::max());

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
        static void ReplyWithMessage(const dpp::message_create_t& message, const dpp::message& reply);

        void SendEmbedsSequentially(const dpp::message_create_t& event, const std::vector<dpp::embed>& embeds, size_t index = 0);

        uint32_t GetCurrentPlaylistIndex();

    private:
        void ReplyWithInfoAboutTrack(const dpp::message_create_t& message, const TrackInfo& trackInfo, const bool& outputURL = true, const bool& printCurrentTimestamp = false);

    private:
        Player m_Player;

        dpp::snowflake m_AdminSnowflake;

        std::atomic_bool m_IsStopped;
        std::mutex m_PlayMutex;

        std::atomic_bool m_IsJoined;
        std::condition_variable m_JoinedCondition;
        std::mutex m_JoinMutex;

        TracksQueue m_TracksQueue;
        std::atomic_uint32_t m_CurrentTrackIndex;
        std::atomic_uint32_t m_CurrentPlaylistIndex;
        mutable std::mutex m_TracksQueueMutex;
    };
}