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
        std::string title;
        size_t beginIndex;
        size_t endIndex;
        size_t repeat;
        size_t uniqueIndex;
    };
    class TracksQueue
    {
    public:
        TracksQueue() = default;
        TracksQueue(std::string yt_dlpPath);

        TracksQueue& operator=(TracksQueue&& other) noexcept;

        //all these methods get only URL, NOT rawURL
        void FetchURL(const std::string_view& url, bool doShuffle = false, float speed = 1.f, size_t repeat = 1, size_t insertIndex = std::numeric_limits<size_t>::max());
        void FetchSearch(const std::string_view& input, SearchEngine searchEngine, float speed = 1.f, size_t repeat = 1, size_t insertIndex = std::numeric_limits<size_t>::max());
        //fills rawURL, NOT URL
        void FetchRaw(std::string url, float speed = 1.f, size_t repeat = 1, size_t insertIndex = std::numeric_limits<size_t>::max());

        //but this indeed gets a raw url
        //WARNING: Use this if only you are sure that your track info doesn't have a raw url, as for playlists, their tracks do not have those
        const TrackInfo& GetRawTrackURL(size_t index = 0);

        void DeleteTrack(size_t index);
        void DeleteTracks(size_t from, size_t to);

        void TransferTrack(size_t from, size_t to);
        void Reverse(size_t from, size_t to);

        void Clear();

        void AddPlaylist(size_t start, size_t end, float speed = 1.f, size_t repeatCount = 1, std::string name = "");
        void DeletePlaylist(size_t index);
        void ClearPlaylists();

        void Shuffle(size_t from, size_t to, size_t indexToSetFirst = std::numeric_limits<size_t>::max());

    public:
        size_t GetTracksSize() const;
        size_t GetPlaylistsSize() const;

        const std::vector<TrackInfo>& GetTrackInfos() const;
        const TrackInfo& GetTrackInfo(size_t index) const;

        const std::vector<PlaylistInfo>& GetPlaylistInfos() const;
        const PlaylistInfo& GetPlaylistInfo(size_t index) const;

        void SetTrackTitle(size_t index, std::string title);
        void SetTrackDuration(size_t index, float duration);
        void SetTrackSpeed(size_t index, float speed);
        void SetTrackRepeatCount(size_t index, size_t repeatCount);

        void SetPlaylistTitle(size_t index, std::string title);
        void SetPlaylistRepeatCount(size_t index, size_t repeatCount);

    private:
        size_t GetLastIndexWithCheck() const;
        size_t GetLastIndex() const;

        void AdjustInsertIndex(size_t& insertIndex) const;
        void AdjustPlaylistInfosIndicesAfterInsertion(size_t insertIndex, size_t addingTracksSize);

        void InsertTrackInfo(size_t insertIndex, TrackInfo trackInfo, float speed = 1.f, size_t repeat = 1);

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