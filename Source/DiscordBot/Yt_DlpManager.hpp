#pragma once

#include <array>
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
        Yt_DlpManager(std::string yt_dlpPath);
        ~Yt_DlpManager() = default;

        Yt_DlpManager(Yt_DlpManager&& other) noexcept = default;
        Yt_DlpManager& operator=(Yt_DlpManager&& other) noexcept = default;

        //calls yt-dlp
        void FetchSearch(const std::string_view& input, SearchEngine searchEngine = SearchEngine::YouTube);
        //calls yt-dlp
        void FetchURL(const std::string_view& url);

        //may call yt-dlp
        TrackInfo GetTrackInfo(size_t index = 0, bool lookForRawURL = true) const;

        std::string GetPlaylistName() const;
        //retrieve basic info for all tracks

        void Reset();

        bool IsReady() const;
        bool IsPlaylist() const noexcept;
        //bool IsRaw() const noexcept;
        size_t GetPlaylistSize() const noexcept;

        const std::string& GetYt_dlpPath() const;
        const JSON& GetJSON() const;
        const JSON::ConstArray& GetPlaylist() const;
    public:
        std::string GetRawURLFromURL(const std::string_view& url) const;
        std::string GetRawURLFromSearch(const std::string_view& input, SearchEngine searchEngine) const;
    private:
        //gets just URL to youtube, title, duration. Relatively fast
        static TrackInfo RetrieveBasicTrackInfo(const JSONValue& JSON, bool isPlaylist = false);
        //this one gets also a raw URL to audio. Slow
        static TrackInfo RetrieveFullTrackInfo(const JSONValue& rawJSON, bool isPlaylist = false);
        //calls yt-dlp
        static JSON RetrieveJSONFromYt_dlp(const std::string_view& yt_dlpPath, const std::string_view& input, bool useSearch = false, SearchEngine searchEngine = SearchEngine::YouTube);
    private:
        std::string m_Yt_dlpPath;

        JSON m_JSON;
        JSON::ValueType* m_JSONValue;

        bool m_IsPlaylist;
        //bool m_IsRaw;
        size_t m_PlaylistSize;

        static constexpr std::array<std::string_view, 2> s_SupportedYt_DlpSearchingEngines = { "yt", "sc" };
        static constexpr std::string_view s_Yt_dlpParameters = "--dump-single-json --flat-playlist";
    };
}