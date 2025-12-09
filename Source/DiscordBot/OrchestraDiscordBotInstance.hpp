#pragma once

#include <mutex>
#include <atomic>

#include <semaphore>

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

    O_DEFINE_STRUCT_GUARD_BINARY_SEMAPHORE(OrchestraDiscordBotPlayer);
    O_DEFINE_STRUCT_GUARD_BINARY_SEMAPHORE(OrchestraDiscordBotInstanceProperties);
    O_DEFINE_STRUCT_GUARD_BINARY_SEMAPHORE(TracksQueue);

    class OrchestraDiscordBotPlayer
    {
    public:
        O_DEFINE_STRUCT_GUARD_BINARY_SEMAPHORE_GETTER(TracksQueue, &m_TracksQueue, &m_TracksQueueBinarySemaphore)
    public:
        OrchestraDiscordBotPlayer(uint32_t sentPacketsSize = 0, bool enableLogSentPackets = false);

        OrchestraDiscordBotPlayer(const OrchestraDiscordBotPlayer& other);
        OrchestraDiscordBotPlayer(OrchestraDiscordBotPlayer&& other) noexcept;
        OrchestraDiscordBotPlayer& operator=(const OrchestraDiscordBotPlayer& other);
        OrchestraDiscordBotPlayer& operator=(OrchestraDiscordBotPlayer&& other) noexcept;

        Player player;
        std::atomic_bool hasRawURLRetrievingCompleted;

        std::atomic_bool isStopped;
        std::mutex gettingRawURLMutex;
        //so this one should be notified only when a. track raw url has been retrieved; b. when we must skip current track. e.g if we are waiting for a raw url to arrive and we skip, delete current track so it is not needed currently
        std::condition_variable gettingRawURLCondition;

        std::atomic_uint32_t currentTrackIndex;
        std::atomic_uint32_t currentPlaylistIndex;
    private:
        TracksQueue m_TracksQueue;
        std::binary_semaphore m_TracksQueueBinarySemaphore{1};

    private:
        void CopyFrom(const OrchestraDiscordBotPlayer& other);
        void MoveFrom(OrchestraDiscordBotPlayer&& other) noexcept;
    };
    class OrchestraDiscordBotInstance
    {
    public:
        O_DEFINE_STRUCT_GUARD_BINARY_SEMAPHORE_GETTER(OrchestraDiscordBotInstanceProperties, &m_Properties, &m_BinarySemaphore)
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
        std::binary_semaphore m_BinarySemaphore{1};

        OrchestraDiscordBotInstanceProperties m_Properties;

    private:
        void CopyFrom(const OrchestraDiscordBotInstance& other);
        void MoveFrom(OrchestraDiscordBotInstance&& other) noexcept;
    };
}