#include "TracksQueue.hpp"

#include <vector>
#include <string>
#include <string_view>
#include <random>
#include <algorithm>

#include "Yt_DlpManager.hpp"

//main stuff
namespace Orchestra
{
    size_t TracksQueue::s_CurrentUniqueTrackIndex = 0;
    size_t TracksQueue::s_CurrentUniquePlaylistIndex = 0;

    TracksQueue::TracksQueue(std::string yt_dlpPath)
        : m_Yt_DlpManager(std::move(yt_dlpPath)), m_RandomEngine(m_RandomDevice()) {}

    TracksQueue& TracksQueue::operator=(TracksQueue&& other) noexcept
    {
        m_Yt_DlpManager = std::move(other.m_Yt_DlpManager);
        m_Tracks = std::move(other.m_Tracks);
        m_PlaylistInfos = std::move(other.m_PlaylistInfos);
        m_RandomEngine = std::mt19937{m_RandomDevice()};

        return *this;
    }

    void TracksQueue::FetchURL(const std::string_view& url, bool doShuffle, float speed, size_t repeat, size_t insertIndex)
    {
        m_Yt_DlpManager.FetchURL(url);

        AdjustInsertIndex(insertIndex);

        if(m_Yt_DlpManager.IsPlaylist())
        {
            size_t playlistSize = m_Yt_DlpManager.GetPlaylistSize();

            m_Tracks.reserve(m_Tracks.size() + playlistSize);

            std::vector<int> indices;
            indices.resize(playlistSize);

            for(size_t i = 0; i < indices.size(); i++)
                indices[i] = i;

            if(doShuffle)
                std::ranges::shuffle(indices, m_RandomEngine);

            size_t exceptionCounter = 0;
            for(size_t i = 0; i < indices.size(); i++)
            {
                try
                {
                    InsertTrackInfo(insertIndex + (i - exceptionCounter), m_Yt_DlpManager.GetTrackInfo(indices[i], false), speed, 1);
                }
                catch(const std::exception& exception)
                {
                    exceptionCounter += 2;
                    GE_LOG(Orchestra, Warning, "Exception occured during getting track infos from a playlist, skipping the track. Exception: ", exception.what());
                }
            }

            playlistSize -= exceptionCounter;

            bool isThisPlaylistInnerPlaylist = false;

            //it is almost the same as AdjustPlaylistInfosIndicesAfterInsertion, but isThisPlaylistInnerPlaylist = true;
            for(auto&& playlistInfo : m_PlaylistInfos)
            {
                if(insertIndex < playlistInfo.beginIndex)
                {
                    playlistInfo.beginIndex += playlistSize;
                    playlistInfo.endIndex += playlistSize;
                }
                else if(insertIndex >= playlistInfo.beginIndex && insertIndex <= playlistInfo.endIndex)
                {
                    isThisPlaylistInnerPlaylist = true;

                    if(insertIndex == playlistInfo.beginIndex)
                        playlistInfo.beginIndex += playlistSize;

                    playlistInfo.endIndex += playlistSize;
                }
            }

            if(!isThisPlaylistInnerPlaylist)
            {
                std::string playlistTitle;

                try
                {
                    playlistTitle = m_Yt_DlpManager.GetPlaylistName();
                }
                catch(...) {}

                m_PlaylistInfos.emplace_back(std::move(playlistTitle), insertIndex, playlistSize - 1 + insertIndex, repeat, s_CurrentUniquePlaylistIndex++);
            }
        }
        else
        {
            InsertTrackInfo(insertIndex, m_Yt_DlpManager.GetTrackInfo(0, true), speed, repeat);

            AdjustPlaylistInfosIndicesAfterInsertion(insertIndex, 1);
        }
    }
    void TracksQueue::FetchSearch(const std::string_view& input, SearchEngine searchEngine, float speed, size_t repeat, size_t insertIndex)
    {
        AdjustInsertIndex(insertIndex);

        m_Yt_DlpManager.FetchSearch(input, searchEngine);

        InsertTrackInfo(insertIndex, m_Yt_DlpManager.GetTrackInfo(0, true), speed, repeat);

        AdjustPlaylistInfosIndicesAfterInsertion(insertIndex, 1);
    }
    //fills rawURL, NOT URL
    void TracksQueue::FetchRaw(std::string url, float speed, size_t repeat, size_t insertIndex)
    {
        using namespace GuelderConsoleLog;

        AdjustInsertIndex(insertIndex);

        InsertTrackInfo(insertIndex, { .rawURL = std::move(url), .title = std::move(url) }, speed, repeat);

        AdjustPlaylistInfosIndicesAfterInsertion(insertIndex, 1);
    }

    //but this indeed gets a raw url
    //WARNING: Use this if only you are sure that your track info doesn't have a raw url, as for playlists, their tracks do not have those
    const TrackInfo& TracksQueue::GetRawTrackURL(size_t index)
    {
        TrackInfo& trackInfo = m_Tracks[index];

        trackInfo.rawURL = m_Yt_DlpManager.GetRawURLFromURL(trackInfo.URL);

        return trackInfo;
    }

    void TracksQueue::DeleteTrack(size_t index)
    {
        m_Tracks.erase(m_Tracks.begin() + index);

        for(size_t i = 0; i < m_PlaylistInfos.size();)
        {
            PlaylistInfo& playlistInfo = m_PlaylistInfos[i];

            if(index < playlistInfo.beginIndex)
            {
                playlistInfo.beginIndex--;
                playlistInfo.endIndex--;
            }
            else if(index <= playlistInfo.endIndex)
            {
                playlistInfo.endIndex--;
            }

            if(playlistInfo.beginIndex == playlistInfo.endIndex)
            {
                m_PlaylistInfos.erase(m_PlaylistInfos.begin() + i);
                break;
            }

            i++;
        }
    }
    void TracksQueue::DeleteTracks(size_t from, size_t to)
    {
        m_Tracks.erase(m_Tracks.begin() + from, m_Tracks.begin() + to + 1);

        const size_t sizeDeleted = to - from + 1;

        for(size_t i = 0; i < m_PlaylistInfos.size();)
        {
            PlaylistInfo& playlistInfo = m_PlaylistInfos[i];

            const bool capturesFullRangeOfPlaylist = from <= playlistInfo.beginIndex && to >= playlistInfo.endIndex;
            bool eraseCurrentPlaylist = false;

            if(capturesFullRangeOfPlaylist)
                eraseCurrentPlaylist = true;
            else
            {
                if(from < playlistInfo.beginIndex)
                {
                    playlistInfo.beginIndex = (sizeDeleted <= playlistInfo.beginIndex ? playlistInfo.beginIndex - sizeDeleted : 0);
                    playlistInfo.endIndex -= sizeDeleted;
                }
                else if(from == playlistInfo.beginIndex)
                {
                    playlistInfo.beginIndex = from;
                    playlistInfo.endIndex -= sizeDeleted;
                }
                else if(from > playlistInfo.beginIndex)
                {
                    if(from < playlistInfo.endIndex)
                        playlistInfo.endIndex = from - 1;//as we have deleted this element, so 1 should be here
                    else if(from == playlistInfo.endIndex)
                        playlistInfo.endIndex--;
                }

                eraseCurrentPlaylist = playlistInfo.beginIndex == playlistInfo.endIndex;
            }

            if(eraseCurrentPlaylist)
            {
                m_PlaylistInfos.erase(m_PlaylistInfos.begin() + i);
                continue;
            }

            //do not touch this, look at "continue" above
            i++;
        }
    }

    void TracksQueue::TransferTrack(size_t from, size_t to)
    {
        //probably it is better to add that tracks that are not present in some playlist do not enter that playlist, but I'm too lazy for this shit
        Transfer(m_Tracks, from, to);
    }

    void TracksQueue::Reverse(size_t from, size_t to)
    {
        std::reverse(m_Tracks.begin() + from, m_Tracks.begin() + to + 1);
    }

    void TracksQueue::Clear()
    {
        m_Tracks.clear();
        ClearPlaylists();
        m_Yt_DlpManager.Reset();
    }

    //TODO: use speed
    void TracksQueue::AddPlaylist(size_t start, size_t end, float speed, size_t repeatCount, std::string name)
    {
        //check if intersects
        bool add = true;
        for(size_t i = 0; i < m_PlaylistInfos.size(); i += add)
        {
            add = true;
            PlaylistInfo& playlist = m_PlaylistInfos[i];

            if(start <= playlist.beginIndex && end >= playlist.beginIndex || start >= playlist.beginIndex && start < playlist.endIndex)
            {
                m_PlaylistInfos.erase(m_PlaylistInfos.begin() + i);

                add = false;
            }
        }

        m_PlaylistInfos.emplace_back(std::move(name), start, end, repeatCount);
    }

    void TracksQueue::DeletePlaylist(size_t index)
    {
        m_PlaylistInfos.erase(m_PlaylistInfos.begin() + index);
    }

    void TracksQueue::ClearPlaylists()
    {
        m_PlaylistInfos.clear();
    }

    void TracksQueue::Shuffle(size_t from, size_t to, size_t indexToSetFirst)
    {
        if(indexToSetFirst == std::numeric_limits<size_t>::max())
            std::shuffle(m_Tracks.begin() + from, m_Tracks.begin() + to, m_RandomEngine);
        else
        {
            TransferTrack(indexToSetFirst, from);
            std::shuffle(m_Tracks.begin() + from + 1, m_Tracks.begin() + to, m_RandomEngine);
        }
    }

}
//getters, setters
namespace Orchestra
{
    const std::vector<TrackInfo>& TracksQueue::GetTrackInfos() const { return m_Tracks; }
    const TrackInfo& TracksQueue::GetTrackInfo(size_t index) const { return m_Tracks[index]; }
    const std::vector<PlaylistInfo>& TracksQueue::GetPlaylistInfos() const { return m_PlaylistInfos; }
    const PlaylistInfo& TracksQueue::GetPlaylistInfo(size_t index) const { return m_PlaylistInfos[index]; }
    size_t TracksQueue::GetTracksSize() const { return m_Tracks.size(); }

    size_t TracksQueue::GetPlaylistsSize() const { return m_PlaylistInfos.size(); }

    void TracksQueue::SetTrackTitle(size_t index, std::string title) { m_Tracks[index].title = std::move(title); }
    void TracksQueue::SetTrackDuration(size_t index, float duration) { m_Tracks[index].duration = duration; }
    void TracksQueue::SetTrackSpeed(size_t index, float speed) { m_Tracks[index].speed = speed; }
    void TracksQueue::SetTrackRepeatCount(size_t index, size_t repeatCount) { m_Tracks[index].repeat = repeatCount; }

    void TracksQueue::SetPlaylistTitle(size_t index, std::string title) { m_PlaylistInfos[index].title = std::move(title); }
    void TracksQueue::SetPlaylistRepeatCount(size_t index, size_t repeatCount) { m_PlaylistInfos[index].repeat = repeatCount; }

    size_t TracksQueue::GetLastIndexWithCheck() const
    {
        if(m_Tracks.empty())
            return 0;
        else
            return GetLastIndex();
    }
    size_t TracksQueue::GetLastIndex() const { return m_Tracks.size() - 1; }
}
//private stuff
namespace Orchestra
{
    void TracksQueue::AdjustInsertIndex(size_t& insertIndex) const
    {
        if(!m_Tracks.empty())
        {
            if(insertIndex == std::numeric_limits<size_t>::max())
                insertIndex = m_Tracks.size();

            O_ASSERT(insertIndex <= m_Tracks.size(), "Cannot insert tracks with index that is bigger than the last index of m_Tracks.");
        }
        else
            insertIndex = 0;
    }
    void TracksQueue::AdjustPlaylistInfosIndicesAfterInsertion(size_t insertIndex, size_t addingTracksSize)
    {
        for(auto&& playlistInfo : m_PlaylistInfos)
            if(insertIndex < playlistInfo.beginIndex)
            {
                playlistInfo.beginIndex += addingTracksSize;
                playlistInfo.endIndex += addingTracksSize;
            }
            else if(insertIndex >= playlistInfo.beginIndex && insertIndex <= playlistInfo.endIndex)
            {
                if(insertIndex == playlistInfo.beginIndex)
                    playlistInfo.beginIndex += addingTracksSize;

                playlistInfo.endIndex += addingTracksSize;
            }
    }

    void TracksQueue::InsertTrackInfo(size_t insertIndex, TrackInfo trackInfo, float speed, size_t repeat)
    {
        trackInfo.speed = speed;
        trackInfo.repeat = repeat;
        trackInfo.uniqueIndex = s_CurrentUniqueTrackIndex++;

        m_Tracks.insert(m_Tracks.begin() + insertIndex, std::move(trackInfo));
    }
}