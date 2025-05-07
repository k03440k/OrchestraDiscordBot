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

namespace Orchestra
{
    class OrchestraDiscordBot : public DiscordBot
    {
    public:
        OrchestraDiscordBot(const std::string_view& token, const std::wstring_view& yt_dlpPath, const std::string_view& prefix = "!", const char& paramPrefix = '-', uint32_t intents = dpp::i_all_intents);

        //setters, getters
    public:
        void SetEnableLogSentPackets(const bool& enable);
        void SetEnableLazyDecoding(const bool& enable);
        void SetSentPacketSize(const uint32_t& size);
        void SetAdminSnowflake(const dpp::snowflake& id);

        bool GetEnableLogSentPackets() const noexcept;
        bool GetEnableLazyDecoding() const noexcept;
        uint32_t GetSentPacketSize() const noexcept;
        dpp::snowflake GetAdminSnowflake() const noexcept;

    private:
        void CommandHelp(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value) const;
        void CommandCurrentTrack(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value) const;
        void CommandQueue(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value) const;
        void CommandPlay(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandStop(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandPause(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandSkip(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandLeave(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandTerminate(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
    private:
        static void ConnectToMemberVoice(const dpp::message_create_t& message);
        void WaitUntilJoined(const std::chrono::milliseconds& delay);
        dpp::voiceconn* IsVoiceConnectionReady(const dpp::snowflake& guildSnowflake);
    private:
        static void ReplyWithInfoAboutTrack(const dpp::message_create_t& message, const TrackInfo& trackInfo, const bool& outputURL = true);
    private:
        Player m_Player;

        dpp::snowflake m_AdminSnowflake;

        std::atomic_bool m_IsStopped;
        std::mutex m_PlayMutex;

        std::atomic_bool m_IsJoined;
        std::condition_variable m_JoinedCondition;
        std::mutex m_JoinMutex;

        Yt_DlpManager m_Yt_DlpManager;
        TrackInfo m_CurrentTrack;
        mutable std::mutex m_CurrentTrackMutex;

        //std::vector<std::wstring> m_MusicValues;
        //mutable std::mutex m_MusicValuesMutex;
    };
}