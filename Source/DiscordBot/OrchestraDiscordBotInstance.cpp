#include "OrchestraDiscordBotInstance.hpp"

#include <atomic>

#include "Player.hpp"
#include "TracksQueue.hpp"

//BotPlayer
namespace Orchestra
{
    OrchestraDiscordBotPlayer::OrchestraDiscordBotPlayer(uint32_t sentPacketsSize, bool enableLogSentPackets)
        : player(sentPacketsSize, enableLogSentPackets), currentPlaylistIndex(std::numeric_limits<uint32_t>::max()), m_TracksQueue() {}

    OrchestraDiscordBotPlayer::OrchestraDiscordBotPlayer(const OrchestraDiscordBotPlayer& other)
    {
        CopyFrom(other);
    }
    OrchestraDiscordBotPlayer::OrchestraDiscordBotPlayer(OrchestraDiscordBotPlayer&& other) noexcept
    {
        MoveFrom(std::move(other));
    }
    OrchestraDiscordBotPlayer& OrchestraDiscordBotPlayer::operator=(const OrchestraDiscordBotPlayer& other)
    {
        CopyFrom(other);

        return *this;
    }
    OrchestraDiscordBotPlayer& OrchestraDiscordBotPlayer::operator=(OrchestraDiscordBotPlayer&& other) noexcept
    {
        MoveFrom(std::move(other));

        return *this;
    }
    void OrchestraDiscordBotPlayer::CopyFrom(const OrchestraDiscordBotPlayer& other)
    {
        player = other.player;
        isStopped = other.isStopped.load();
        m_TracksQueue = other.m_TracksQueue;
        currentTrackIndex = other.currentTrackIndex.load();
        currentPlaylistIndex = other.currentPlaylistIndex.load();
    }
    void OrchestraDiscordBotPlayer::MoveFrom(OrchestraDiscordBotPlayer&& other) noexcept
    {
        player = std::move(other.player);
        isStopped = other.isStopped.load();
        m_TracksQueue = std::move(other.m_TracksQueue);
        currentTrackIndex = other.currentTrackIndex.load();
        currentPlaylistIndex = other.currentPlaylistIndex.load();
    }
}
//BotInstance
namespace Orchestra
{
    OrchestraDiscordBotInstance::OrchestraDiscordBotInstance(FullOrchestraDiscordBotInstanceProperties properties)
        : player(properties.sentPacketsSize, properties.enableLogSentPackets), m_Properties(std::move(properties.properties)) {
    }
    OrchestraDiscordBotInstance::OrchestraDiscordBotInstance(const OrchestraDiscordBotInstance& other)
    {
        CopyFrom(other);
    }
    OrchestraDiscordBotInstance::OrchestraDiscordBotInstance(OrchestraDiscordBotInstance&& other) noexcept
    {
        MoveFrom(std::move(other));
    }
    OrchestraDiscordBotInstance& OrchestraDiscordBotInstance::operator=(const OrchestraDiscordBotInstance& other)
    {
        CopyFrom(other);

        return *this;
    }
    OrchestraDiscordBotInstance& OrchestraDiscordBotInstance::operator=(OrchestraDiscordBotInstance&& other) noexcept
    {
        MoveFrom(std::move(other));

        return *this;
    }

    void OrchestraDiscordBotInstance::CopyFrom(const OrchestraDiscordBotInstance& other)
    {
        player = other.player;
        m_Properties = other.m_Properties;
        isJoined = other.isJoined.load();
    }
    void OrchestraDiscordBotInstance::MoveFrom(OrchestraDiscordBotInstance&& other) noexcept
    {
        player = std::move(other.player);
        m_Properties = std::move(other.m_Properties);
        isJoined = other.isJoined.load();
    }
}