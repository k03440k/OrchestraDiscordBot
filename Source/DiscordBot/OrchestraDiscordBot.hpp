#pragma once

#include <atomic>
#include <mutex>
#include <future>
#include <chrono>
#include <string_view>

#include <dpp/dpp.h>

#include "DiscordBot.hpp"
#include "Player.hpp"

namespace Orchestra
{
    class OrchestraDiscordBot : public DiscordBot
    {
    public:
        OrchestraDiscordBot(const std::string_view& token, const std::string& yt_dlpPath, const std::string_view& prefix = "!", const char& paramPrefix = '-', uint32_t intents = dpp::i_all_intents);

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
        void CommandPlay(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandStop(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandPause(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandSkip(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandLeave(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandTerminate(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);

    private:
        Player m_Player;

        dpp::snowflake m_AdminSnowflake;

        const std::string m_yt_dlpPath;

        std::atomic_bool m_IsStopped;
        std::mutex m_PlayMutex;

        std::atomic_bool m_IsJoined;
        std::condition_variable m_JoinedCondition;
        std::mutex m_JoinMutex;
    };
}