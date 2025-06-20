#pragma once

#define NOMINMAX

#include <vector>
#include <string>
#include <string_view>
#include <random>

#include "Yt_DlpManager.hpp"

namespace Orchestra
{
    struct PlaylistInfo
    {
        std::wstring title;
        size_t beginIndex;
        size_t endIndex;
        size_t repeat;
        size_t uniqueIndex;
    };
    class TracksQueue
    {
    public:
        TracksQueue(const std::wstring_view& yt_dlpPath);

        //all these methods get only URL, NOT rawURL
        void FetchURL(const std::wstring_view& url, const bool& doShuffle = false, const float& speed = 1.f, const size_t& repeat = 1, size_t insertIndex = std::numeric_limits<size_t>::max());
        void FetchSearch(const std::wstring_view& input, const SearchEngine& searchEngine, const float& speed = 1.f, const size_t& repeat = 1, size_t insertIndex = std::numeric_limits<size_t>::max());
        //fills rawURL, NOT URL
        void FetchRaw(const std::string_view& url, const float& speed = 1.f, const size_t& repeat = 1, size_t insertIndex = std::numeric_limits<size_t>::max());

        //but this indeed gets a raw url
        //WARNING: Use this if only you are sure that your track info doesn't have a raw url, as for playlists, their tracks do not have those
        const TrackInfo& GetRawTrackURL(const size_t& index = 0);

        void DeleteTrack(const size_t& index);
        void DeleteTracks(const size_t& from, const size_t& to);

        void TransferTrack(const size_t& from, const size_t& to);

        void Clear();

        void AddPlaylist(const size_t& start, const size_t& end, const float& speed = 1.f, const size_t& repeatCount = 1, const std::wstring_view& name = L"");
        void DeletePlaylist(const size_t& index);
        void ClearPlaylists();

        void Shuffle(const size_t& indexToSetFirst = std::numeric_limits<size_t>::max());
        void Shuffle(const size_t& from, const size_t& to, const size_t& indexToSetFirst = std::numeric_limits<size_t>::max());

    public:
        size_t GetTracksSize() const;
        size_t GetPlaylistsSize() const;

        const std::vector<TrackInfo>& GetTrackInfos() const;
        const TrackInfo& GetTrackInfo(const size_t& index) const;

        const std::vector<PlaylistInfo>& GetPlaylistInfos() const;
        const PlaylistInfo& GetPlaylistInfo(const size_t& index) const;

        void SetTrackTitle(const size_t& index, const std::wstring& title);
        void SetTrackDuration(const size_t& index, const float& duration);
        void SetTrackSpeed(const size_t& index, const float& speed);
        void SetTrackRepeatCount(const size_t& index, const size_t& repeatCount);

        void SetPlaylistTitle(const size_t& index, const std::wstring& title);
        void SetPlaylistRepeatCount(const size_t& index, const size_t& repeatCount);
    private:
        size_t GetLastIndexWithCheck() const;
        size_t GetLastIndex() const;

        void AdjustInsertIndex(size_t& insertIndex) const;
        void AdjustPlaylistInfosIndicesAfterInsertion(const size_t& insertIndex, const size_t& addingTracksSize);

        void InsertTrackInfo(const size_t& insertIndex, TrackInfo&& trackInfo, const float& speed = 1.f, const size_t& repeat = 1);
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