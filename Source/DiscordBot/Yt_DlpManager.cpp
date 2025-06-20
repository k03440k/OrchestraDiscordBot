#define NOMINMAX

#include "Yt_DlpManager.hpp"

#include <array>
#include <string>
#include <string_view>

#include <rapidjson/document.h>

#include "GuelderResourcesManager.hpp"
#include "../Utils.hpp"

namespace Orchestra
{
    bool TrackInfo::HasURL() const
    {
        return !URL.empty();
    }

    std::string_view SearchEngineToString(const SearchEngine& searchEngine)
    {
        switch(searchEngine)
        {
        case SearchEngine::YouTube: return g_SupportedYt_DlpSearchingEngines[0];
        case SearchEngine::SoundCloud: return g_SupportedYt_DlpSearchingEngines[1];
        }

        O_THROW("There is no such search engine ", static_cast<uint8_t>(searchEngine));
    }
    std::wstring_view SearchEngineToWString(const SearchEngine& searchEngine)
    {
        switch(searchEngine)
        {
        case SearchEngine::YouTube: return g_WSupportedYt_DlpSearchingEngines[0];
        case SearchEngine::SoundCloud: return g_WSupportedYt_DlpSearchingEngines[1];
        }

        O_THROW("There is no such search engine ", static_cast<uint8_t>(searchEngine));
    }
    SearchEngine StringToSearchEngine(const std::string_view& str)
    {
        const auto itFound = std::ranges::find(g_SupportedYt_DlpSearchingEngines, str);
        O_ASSERT(itFound != g_SupportedYt_DlpSearchingEngines.end(), "Failed to find ", str, " search engine.");

        if(str == g_SupportedYt_DlpSearchingEngines[0])
            return SearchEngine::YouTube;
        else
            return SearchEngine::SoundCloud;
    }

    Yt_DlpManager::Yt_DlpManager(const std::wstring& yt_dlpPath)
        : m_Yt_dlpPath(yt_dlpPath), m_JSONValue(nullptr), m_IsPlaylist(false), m_PlaylistSize(0) {}

    void Yt_DlpManager::FetchSearch(const std::wstring_view& input, const SearchEngine& searchEngine)
    {
        using namespace GuelderConsoleLog;

        m_JSON = RetrieveJSONFromYt_dlp(m_Yt_dlpPath, input, true, searchEngine);

        O_ASSERT(m_JSON.IsObject(), "m_JSON is not an object.");

        auto itEntries = m_JSON.FindMember(L"entries");

        O_ASSERT(itEntries != m_JSON.MemberEnd(), "Failed to find entries.");
        O_ASSERT(itEntries->value.IsArray(), "itEntries is not an array.");
        O_ASSERT(itEntries->value.GetArray().Size() > 0, "The size of itEntries is 0.");
        O_ASSERT(itEntries->value.GetArray().Begin()->IsObject(), "The first element of itEntries is not an object.");

        m_JSONValue = &*itEntries->value.GetArray().Begin();

        if(!m_JSONValue)
        {
            Reset();
            O_THROW("Failed to retrieve info about track.");
        }

        m_IsPlaylist = false;
        m_PlaylistSize = 1;
    }
    void Yt_DlpManager::FetchURL(const std::wstring_view& url)
    {
        using namespace GuelderConsoleLog;

        m_JSON = RetrieveJSONFromYt_dlp(m_Yt_dlpPath, url, false);

        //playlist
        {
            O_ASSERT(m_JSON.IsObject(), "m_JSON is not an object.");

            const auto itEntries = m_JSON.FindMember(L"entries");
            m_IsPlaylist = itEntries != m_JSON.MemberEnd() && itEntries->value.IsArray();

            if(m_IsPlaylist)
                m_PlaylistSize = itEntries->value.GetArray().Size();
            else
            {
                m_PlaylistSize = 1;
                O_ASSERT(m_JSON.FindMember(L"formats") != m_JSON.MemberEnd() && m_JSON.FindMember(L"formats")->value.IsArray() && m_JSON.FindMember(L"formats")->value.GetArray().Size() != 1, "The url is raw.");
            }

            m_JSONValue = &m_JSON;
        }
    }
    TrackInfo Yt_DlpManager::GetTrackInfo(const size_t& index, const bool& lookForRawURL) const
    {
        O_ASSERT(IsReady(), "Yt-dlpManager does not have a proper JSON. IsReady() == false.");

        if(m_IsPlaylist)
        {
            using namespace GuelderConsoleLog;

            O_ASSERT(index < m_PlaylistSize, "The input index ", index, " is bigger than the last element of the playlist with index ", m_PlaylistSize - 1);

            const auto itEntries = m_JSONValue->FindMember(L"entries");
            auto playlist = itEntries->value.GetArray();

            auto itTrack = (playlist.Begin() + index);

            //O_ASSERT(itTrack != playlist.End(), "itTrack == playlist.End(). Something went compeletely wrong...");

            TrackInfo trackInfo = RetrieveBasicTrackInfo(*itTrack, m_IsPlaylist);

            if(lookForRawURL)
                trackInfo.rawURL = RetrieveFullTrackInfo(RetrieveJSONFromYt_dlp(m_Yt_dlpPath, trackInfo.URL, false), false).rawURL;

            return trackInfo;
        }
        else
        {
            if(lookForRawURL)
                return RetrieveFullTrackInfo(*m_JSONValue, false);
            else
                return RetrieveBasicTrackInfo(*m_JSONValue, false);
        }
    }

    std::wstring Yt_DlpManager::GetPlaylistName() const
    {
        O_ASSERT(m_JSONValue, "The m_JSONValue == nullptr.");
        O_ASSERT(m_IsPlaylist, "The m_JSONValue is not a playlist.");

        O_ASSERT(m_JSONValue->IsObject(), "m_JSONValue->IsObject() == false.");
        const auto itTitle = m_JSONValue->FindMember(L"title");

        O_ASSERT(itTitle != m_JSONValue->MemberEnd(), "Failed to find title in a playlist.");
        O_ASSERT(itTitle->value.IsString(), "The title of the playlist is not a string.");

        return itTitle->value.GetString();
    }

    void Yt_DlpManager::Reset()
    {
        m_JSONValue = nullptr;
        m_IsPlaylist = false;
        m_PlaylistSize = 0;
        //probably clears
        m_JSON.SetObject();
    }

    bool Yt_DlpManager::IsReady() const { return !m_JSON.ObjectEmpty() && m_PlaylistSize > 0; }
    bool Yt_DlpManager::IsPlaylist() const noexcept { return m_IsPlaylist; }
    //bool Yt_DlpManager::IsRaw() const noexcept { return m_IsRaw; }
    size_t Yt_DlpManager::GetPlaylistSize() const noexcept { return m_PlaylistSize; }

    const std::wstring_view& Yt_DlpManager::GetYt_dlpPath() const { return m_Yt_dlpPath; }
    const WJSON& Yt_DlpManager::GetJSON() const { return m_JSON; }
    const WJSON::ConstArray& Yt_DlpManager::GetPlaylist() const
    {
        O_ASSERT(IsReady(), "The ", typeid(Yt_DlpManager).name(), " is not ready.");
        O_ASSERT(m_IsPlaylist, "The ", typeid(Yt_DlpManager).name(), " is not a playlist.");

        const auto itEntries = m_JSON.FindMember(L"entries");

        return itEntries->value.GetArray();
    }

    std::string Yt_DlpManager::GetRawURLFromURL(const std::wstring& url) const
    {
        const std::wstring pipeCommand = GuelderConsoleLog::Logger::Format(m_Yt_dlpPath, L" -f bestaudio --get-url \"", url, L'\"');

        const std::vector<std::string> output = GuelderResourcesManager::ResourcesManager::ExecuteCommand<wchar_t, char>(pipeCommand, 1);

        O_ASSERT(!output.empty(), "Failed to retrieve raw audio URL from yt-dlp.");

        return output[0];
    }
    std::string Yt_DlpManager::GetRawURLFromSearch(const std::wstring& input, const SearchEngine& searchEngine) const
    {
        const std::wstring pipeCommand = GuelderConsoleLog::Logger::Format(m_Yt_dlpPath, L" -f bestaudio --get-url \"", SearchEngineToWString(searchEngine), L"search:", input, L"\"");

        const std::vector<std::string> output = GuelderResourcesManager::ResourcesManager::ExecuteCommand<wchar_t, char>(pipeCommand, 1);

        O_ASSERT(!output.empty(), "Failed to retrieve raw audio URL from yt-dlp.");

        return output[0];
    }

    TrackInfo Yt_DlpManager::RetrieveBasicTrackInfo(const WGenericValue& JSON, const bool& isPlaylist)
    {
        TrackInfo out;

        if(isPlaylist)
            out.URL = GetFromJSON<const wchar_t*>(JSON, L"url");
        else
            out.URL = GetFromJSON<const wchar_t*>(JSON, L"webpage_url");

        out.title = GetFromJSON<const wchar_t*>(JSON, L"title");

        try
        {
            out.duration = GetFromJSON<float>(JSON, L"duration");
        }
        catch(...)
        {
            out.duration = GetFromJSON<int>(JSON, L"duration");
        }

        out.uniqueIndex = 0;
        out.speed = 0;
        out.repeat = 1;

        return out;
    }
    //this one gets also a raw URL to audio. Slow
    TrackInfo Yt_DlpManager::RetrieveFullTrackInfo(const WGenericValue& rawJSON, const bool& isPlaylist)
    {
        //raw URL

        const auto formats = GetFromJSON<WJSON::ConstArray>(rawJSON, L"formats");

        for(const auto& format : formats)
            if(format.IsObject())
                if(std::wcscmp(GetFromJSON<const wchar_t*>(format, L"resolution"), L"audio only") == 0)
                {
                    TrackInfo out = RetrieveBasicTrackInfo(rawJSON, isPlaylist);
                    out.rawURL = GuelderConsoleLog::WStringToString(GetFromJSON<const wchar_t*>(format, L"url"));

                    return out;
                }

        O_THROW("Failed to find info about a track in the playlist.");
    }
    //calls yt-dlp
    WJSON Yt_DlpManager::RetrieveJSONFromYt_dlp(const std::wstring_view& yt_dlpPath, const std::wstring_view& input, const bool& useSearch, const SearchEngine& searchEngine)
    {
        using namespace GuelderConsoleLog;

        WJSON JSON;

        std::wstring pipeCommand;

        if(useSearch)//this should be changed somehow
            pipeCommand = Logger::Format(yt_dlpPath, L' ', L"--dump-single-json", L" \"", SearchEngineToWString(searchEngine), L"search:", input, L"\"");
        else
            pipeCommand = Logger::Format(yt_dlpPath, L' ', s_Yt_dlpWParameters, L" \"", input, L'\"');

        const std::vector<std::wstring> output = GuelderResourcesManager::ResourcesManager::ExecuteCommand<wchar_t>(pipeCommand, 1);

        O_ASSERT(!output.empty(), "Failed to retrieve raw audio URL from yt-dlp.");

        //GE_LOG(Orchestra, Warning, output[0]);
        JSON.Parse(output[0].c_str());

        O_ASSERT(!JSON.HasParseError(), "Failed to parse JSON at offset ", JSON.GetErrorOffset());

        return JSON;
    }
}