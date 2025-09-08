#pragma once

#include <mutex>
#include <atomic>

#include <dpp/dpp.h>

#include "../Utils.hpp"
#include "Player.hpp"
#include "TracksQueue.hpp"

namespace Orchestra
{
    struct OrchestraDiscordBotInstanceProperties
    {
        std::string commandsPrefix = "!";
        char paramsPrefix = '-';
        uint32_t maxDownloadFileSize = 0;
        dpp::snowflake adminSnowflake = 0;
    };
    struct FullOrchestraDiscordBotInstanceProperties
    {
        uint32_t sentPacketsSize = 200000;
        bool enableLogSentPackets = false;

        OrchestraDiscordBotInstanceProperties properties = {};
    };
}
namespace Orchestra
{
    class OrchestraDiscordBotPlayer;
    
    O_DEFINE_STRUCT_GUARD_STD_MUTEX(OrchestraDiscordBotPlayer);
    O_DEFINE_STRUCT_GUARD_STD_MUTEX(TracksQueue);
    O_DEFINE_STRUCT_GUARD_STD_MUTEX(OrchestraDiscordBotInstanceProperties);

    class OrchestraDiscordBotPlayer
    {
    public:
        O_DEFINE_STRUCT_GUARD_GETTER(TracksQueue, &m_TracksQueue, m_TracksQueueMutex);
    public:
        OrchestraDiscordBotPlayer(uint32_t sentPacketsSize = 0, bool enableLogSentPackets = false);

        OrchestraDiscordBotPlayer(const OrchestraDiscordBotPlayer& other);
        OrchestraDiscordBotPlayer(OrchestraDiscordBotPlayer&& other) noexcept;
        OrchestraDiscordBotPlayer& operator=(const OrchestraDiscordBotPlayer& other);
        OrchestraDiscordBotPlayer& operator=(OrchestraDiscordBotPlayer&& other) noexcept;

        Player player;

        std::atomic_bool isStopped;
        std::mutex playMutex;

        std::atomic_uint32_t currentTrackIndex;
        std::atomic_uint32_t currentPlaylistIndex;
    private:
        TracksQueue m_TracksQueue;
        std::mutex m_TracksQueueMutex;

    private:
        void CopyFrom(const OrchestraDiscordBotPlayer& other);
        void MoveFrom(OrchestraDiscordBotPlayer&& other) noexcept;
    };
    class OrchestraDiscordBotInstance
    {
    public:
        O_DEFINE_STRUCT_GUARD_GETTER(OrchestraDiscordBotInstanceProperties, &m_Properties, m_Mutex)
    public:
        OrchestraDiscordBotInstance(FullOrchestraDiscordBotInstanceProperties properties = {});

        OrchestraDiscordBotInstance(const OrchestraDiscordBotInstance& other);
        OrchestraDiscordBotInstance(OrchestraDiscordBotInstance&& other) noexcept;
        OrchestraDiscordBotInstance& operator=(const OrchestraDiscordBotInstance& other);
        OrchestraDiscordBotInstance& operator=(OrchestraDiscordBotInstance&& other) noexcept;

        std::atomic_bool isJoined;
        std::condition_variable joinedCondition;
        std::mutex joinMutex;
        
        OrchestraDiscordBotPlayer player;

    private:
        std::mutex m_Mutex;

        OrchestraDiscordBotInstanceProperties m_Properties;

    private:
        void CopyFrom(const OrchestraDiscordBotInstance& other);
        void MoveFrom(OrchestraDiscordBotInstance&& other) noexcept;
    };
}