#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <string_view>

#include <rapidjson/document.h>
#include <rapidjson/encodings.h>

#include "../Utils.hpp"

namespace Orchestra
{
    constexpr std::array<std::string_view, 2> g_SupportedYt_DlpSearchingEngines = { "yt", "sc" };

    using JSON = rapidjson::Document;
    using JSONValue = JSON::ValueType;

    template<typename T>
    T GetFromJSON(const rapidjson::Document::ValueType& JSON, const std::string_view& item)
    {
        const auto itFound = JSON.FindMember(item.data());

        O_ASSERT(itFound != JSON.MemberEnd(), "Failed to find member ", item);
        O_ASSERT(itFound->value.template Is<T>(), "The type of ", item, " is not ", typeid(T).name());

        return itFound->value.template Get<T>();
    }

    struct TrackInfo
    {
        std::string URL;
        std::string rawURL;
        std::string title;
        //seconds
        float duration;
        size_t uniqueIndex;
        size_t repeat;
        float speed;

        bool HasURL() const;
    };

    enum class SearchEngine : uint8_t
    {
        YouTube,
        SoundCloud
    };

    std::string_view SearchEngineToString(const SearchEngine& searchEngine);
    SearchEngine StringToSearchEngine(const std::string_view& str);

    class Yt_DlpManager
    {
    public:
        Yt_DlpManager() = default;
        Yt_DlpManager(std::filesystem::path yt_dlpExecutablePath);
        ~Yt_DlpManager() = default;

        Yt_DlpManager(const Yt_DlpManager& other);
        Yt_DlpManager(Yt_DlpManager&& other) noexcept = default;
        Yt_DlpManager& operator=(const Yt_DlpManager& other);
        Yt_DlpManager& operator=(Yt_DlpManager&& other) noexcept = default;

        //calls yt-dlp
        void FetchSearch(const std::filesystem::path& yt_dlpExecutablePath, const std::string_view& input,SearchEngine searchEngine = SearchEngine::YouTube);
        void FetchSearch(const std::string_view& input, SearchEngine searchEngine = SearchEngine::YouTube);
        //calls yt-dlp
        void FetchURL(const std::filesystem::path& yt_dlpExecutablePath, const std::string_view& url);
        void FetchURL(const std::string_view& url);

        //may call yt-dlp
        TrackInfo GetTrackInfo(const std::filesystem::path& yt_dlpExecutablePath, size_t index = 0, bool lookForRawURL = true) const;
        TrackInfo GetTrackInfo(size_t index = 0, bool lookForRawURL = true) const;

        std::string GetPlaylistName() const;
        //retrieve basic info for all tracks

        //doesn't reset yt_dlpExecutablePath
        void Reset();

        bool IsReady() const;
        bool IsPlaylist() const noexcept;
        //bool IsRaw() const noexcept;
        size_t GetPlaylistSize() const noexcept;

        const std::filesystem::path& GetYt_dlpExecutablePath() const;
        const JSON& GetJSON() const;
        //TODO: move to .cpp
        const rapidjson::GenericArray<false, rapidjson::GenericValue<rapidjson::UTF8<>>>& GetPlaylist() const;

    public:
        static std::string GetRawURLFromURL(const std::filesystem::path& yt_dlpExecutablePath, const std::string_view& url);
        std::string GetRawURLFromURL(const std::string_view& url) const;
        static std::string GetRawURLFromSearch(const std::filesystem::path& yt_dlpExecutablePath, const std::string_view& input, SearchEngine searchEngine);
        std::string GetRawURLFromSearch(const std::string_view& input, SearchEngine searchEngine) const;

    private:
        //gets just URL to youtube, title, duration. Relatively fast
        static TrackInfo RetrieveBasicTrackInfo(const JSONValue& JSON, bool isPlaylist = false);
        //this one gets also a raw URL to audio. Slow
        static TrackInfo RetrieveFullTrackInfo(const JSONValue& rawJSON, bool isPlaylist = false);
        //calls yt-dlp
        static JSON RetrieveJSONFromYt_dlp(const std::filesystem::path& yt_dlpExecutablePath, const std::string_view& input, bool useSearch = false, SearchEngine searchEngine = SearchEngine::YouTube);

    private:
        std::filesystem::path m_Yt_dlpExecutablePath;

        JSON m_JSON;
        //this exists, because I think that it is very expensive to make a copy of m_JSON to get the exact value
        JSON::ValueType* m_JSONValue;

        bool m_IsPlaylist;
        //bool m_IsRaw;
        size_t m_PlaylistSize;

        static constexpr std::array<std::string_view, 2> s_SupportedYt_DlpSearchingEngines = { "yt", "sc" };
        static constexpr std::string_view s_Yt_dlpParameters = "--dump-single-json --flat-playlist";

    private:
        void CopyFrom(const Yt_DlpManager& other);
    };
}