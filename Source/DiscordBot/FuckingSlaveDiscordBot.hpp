#pragma once

#include <atomic>
#include <mutex>
#include <future>
#include <chrono>
#include <string_view>

#include <dpp/dpp.h>

#include "DiscordBot.hpp"
#include "Player.hpp"

namespace FSDB
{
    //Fucking Slave Discord Bot
    GE_DECLARE_LOG_CATEGORY_EXTERN(FSDB, All, true, false, true);

    class FuckingSlaveDiscordBot : public DiscordBot
    {
    public:
        FuckingSlaveDiscordBot(const std::string_view& token, const std::string_view& prefix, const std::string& yt_dlpPath, uint32_t intents = dpp::i_all_intents);

        //setters, getters
    public:
        void SetEnableLogSentPackets(const bool& enable);
        void SetEnableLazyDecoding(const bool& enable);
        void SetSentPacketSize(const uint32_t& size);

        bool GetEnableLogSentPackets() const noexcept;
        bool GetEnableLazyDecoding() const noexcept;
        uint32_t GetSentPacketSize() const noexcept;

    private:

    private:
        Player m_Player;

        std::atomic_bool m_IsStopped;
        std::mutex m_PlayMutex;

        std::atomic_bool m_IsJoined;
        std::condition_variable m_JoinedCondition;
        std::mutex m_JoinMutex;
    };
}