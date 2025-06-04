#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <random>

#include "Yt_DlpManager.hpp"

namespace Orchestra
{
    struct PlaylistInfo
    {
        size_t beginIndex;
        size_t endIndex;
        size_t repeat;
        size_t index;
        //TODO: title use
        std::wstring title;
    };
    class TracksQueue
    {
    public:
        TracksQueue(const std::wstring_view& yt_dlpPath);

        //all these methods get only URL, NOT rawURL
        void FetchURL(const std::wstring_view& url, const bool& doShuffle = false, const float& speed = 1.f, const size_t& repeat = 1);
        void FetchSearch(const std::wstring_view& input, const SearchEngine& searchEngine, const float& speed = 1.f, const size_t& repeat = 1);
        //fills rawURL, NOT URL
        void FetchRaw(const std::string_view& url, const float& speed = 1.f, const size_t& repeat = 1);

        //but this indeed gets a raw url
        //WARNING: Use this if only you are sure that your track info doesn't have a raw url, as for playlists, their tracks do not have those
        const TrackInfo& GetRawTrackURL(const size_t& index = 0);

        void DeleteTrack(const size_t& index);
        void DeleteTracks(const size_t& from, const size_t& to);

        void Clear();

        void Shuffle();
        void Shuffle(const size_t& indexToSetFirst);

    public:
        const std::vector<TrackInfo>& GetTrackInfos() const;
        const TrackInfo& GetTrackInfo(const size_t& index = 0) const;
        const std::vector<PlaylistInfo>& GetPlaylistInfos() const;
        size_t GetSize() const;

        void SetTrackTitle(const size_t& index, const std::wstring& title);
        void SetTrackDuration(const size_t& index, const float& duration);
    private:
        size_t GetLastIndexWithCheck() const;
        size_t GetLastIndex() const;
    private:
        static size_t s_CurrentUniqueTrackIndex;
        static size_t s_CurrentUniquePlaylistIndex;

        Yt_DlpManager m_Yt_DlpManager;
        std::vector<TrackInfo> m_Tracks;
        std::vector<PlaylistInfo> m_PlaylistInfos;

        std::random_device m_RandomDevice;
        std::mt19937 m_RandomEngine;
    };
}