#pragma once

#include <atomic>
#include <mutex>
#include <future>
#include <chrono>
#include <string_view>

#include <dpp/dpp.h>

#include <rapidjson/document.h>
#include <rapidjson/encodings.h>

#include "DiscordBot.hpp"
#include "Player.hpp"

namespace Orchestra
{
    class OrchestraDiscordBot : public DiscordBot
    {
    public:
        OrchestraDiscordBot(const std::string_view& token, const std::string_view& yt_dlpPath, const std::string_view& prefix = "!", const char& paramPrefix = '-', uint32_t intents = dpp::i_all_intents);

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
        void CommandPlay(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandStop(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandPause(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandSkip(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandLeave(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);
        void CommandTerminate(const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value);

    private:
        using WJSON = rapidjson::GenericDocument<rapidjson::UTF16<>>;
    private:
        static std::string GetRawAudioUrlFromJSON(const WJSON& jsonRawAudio);
        static std::wstring GetRawAudioJsonWStringFromPlaylistJson(const rapidjson::GenericValue<rapidjson::UTF16<>>::Array& playlistArray, const std::string& yt_dlpPath, const size_t& index = 0);
        static void ReplyWithInfoAboutTrack(const dpp::message_create_t& message, const WJSON& jsonRawAudio, const bool& outputURL = true);
    private:
        Player m_Player;

        dpp::snowflake m_AdminSnowflake;

        const std::string m_yt_dlpPath;

        //the fist element is considered to be a default search engine
        static constexpr std::array<std::string_view, 2> s_SupportedYt_DlpSearchingEngines = {"yt", "sc"};
        static constexpr std::string_view s_Yt_dlpParameters = "--dump-single-json --flat-playlist";

        std::atomic_bool m_IsStopped;
        std::mutex m_PlayMutex;

        std::atomic_bool m_IsJoined;
        std::condition_variable m_JoinedCondition;
        std::mutex m_JoinMutex;

        std::atomic<std::shared_ptr<std::wstring>> m_CurrentPlayingURL;
        std::atomic<std::shared_ptr<std::wstring>> m_CurrentPlayingTrackTitle;
        std::atomic_int m_CurrentPlaylistTrackIndex;
        std::atomic_int m_CurrentPLaylistSize;
    };
}