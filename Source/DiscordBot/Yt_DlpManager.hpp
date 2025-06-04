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
    constexpr std::array<std::wstring_view, 2> g_WSupportedYt_DlpSearchingEngines = { L"yt", L"sc" };

    using WJSON = rapidjson::GenericDocument<rapidjson::UTF16<>>;
    using WGenericValue = rapidjson::GenericValue<rapidjson::UTF16<>>;

    template<typename T, typename Encoding = rapidjson::UTF16<>>
        requires requires { typename Encoding::Ch; }
    T GetFromJSON(const rapidjson::GenericValue<Encoding>& JSON, const std::basic_string_view<typename Encoding::Ch>& item)
    {
        const auto itFound = JSON.FindMember(item.data());
        if constexpr(std::is_same_v<typename Encoding::Ch, char>)
        {
            O_ASSERT(itFound != JSON.MemberEnd(), "Failed to find member ", item);
            O_ASSERT(itFound->value.template Is<T>(), "The type of ", item, " is not ", typeid(T).name());
        }
        else
        {
            O_ASSERT(itFound != JSON.MemberEnd(), "Failed to find member ", GuelderConsoleLog::WStringToString(item));
            O_ASSERT(itFound->value.template Is<T>(), "The type of ", GuelderConsoleLog::WStringToString(item), " is not ", typeid(T).name());
        }

        return itFound->value.template Get<T>();
    }

    struct TrackInfo
    {
        std::wstring URL;
        std::string rawURL;
        std::wstring title;
        //seconds
        float duration;
        size_t playlistIndex;
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
    std::wstring_view SearchEngineToWString(const SearchEngine& searchEngine);
    SearchEngine StringToSearchEngine(const std::string_view& str);

    class Yt_DlpManager
    {
    public:
        Yt_DlpManager() = default;
        Yt_DlpManager(const std::wstring& yt_dlpPath);
        ~Yt_DlpManager() = default;

        //calls yt-dlp
        void FetchSearch(const std::wstring_view& input, const SearchEngine& searchEngine = SearchEngine::YouTube);
        //calls yt-dlp
        void FetchURL(const std::wstring_view& url);

        //may call yt-dlp
        TrackInfo GetTrackInfo(const size_t& index = 0, const bool& lookForRawURL = true) const;
        //retrieve basic info for all tracks

        void Reset();

        bool IsReady() const;
        bool IsPlaylist() const noexcept;
        //bool IsRaw() const noexcept;
        size_t GetPlaylistSize() const noexcept;

        const std::wstring_view& GetYt_dlpPath() const;
        const WJSON& GetJSON() const;
        const WJSON::ConstArray& GetPlaylist() const;
    public:
        std::string GetRawURLFromURL(const std::wstring& url) const;
        std::string GetRawURLFromSearch(const std::wstring& input, const SearchEngine& searchEngine) const;
    private:
        //gets just URL to youtube, title, duration. Relatively fast
        static TrackInfo RetrieveBasicTrackInfo(const WGenericValue& JSON, const bool& isPlaylist = false);
        //this one gets also a raw URL to audio. Slow
        static TrackInfo RetrieveFullTrackInfo(const WGenericValue& rawJSON, const bool& isPlaylist = false);
        //calls yt-dlp
        static WJSON RetrieveJSONFromYt_dlp(const std::wstring_view& yt_dlpPath, const std::wstring_view& input, const bool& useSearch = false, const SearchEngine& searchEngine = SearchEngine::YouTube);
    private:
        std::wstring m_Yt_dlpPath;

        WJSON m_JSON;
        WJSON::ValueType* m_JSONValue;

        bool m_IsPlaylist;
        //bool m_IsRaw;
        size_t m_PlaylistSize;

        static constexpr std::array<std::string_view, 2> s_SupportedYt_DlpSearchingEngines = { "yt", "sc" };
        static constexpr std::string_view s_Yt_dlpParameters = "--dump-single-json --flat-playlist";
        static constexpr std::wstring_view s_Yt_dlpWParameters = L"--dump-single-json --flat-playlist";
    };
}
