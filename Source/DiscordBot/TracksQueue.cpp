#include "TracksQueue.hpp"

#include <vector>
#include <string>
#include <string_view>
#include <random>
#include <algorithm>

#include "Yt_DlpManager.hpp"

namespace Orchestra
{
    size_t TracksQueue::s_CurrentUniqueTrackIndex = 0;
    size_t TracksQueue::s_CurrentUniquePlaylistIndex = 0;

    TracksQueue::TracksQueue(const std::wstring_view& yt_dlpPath)
        : m_Yt_DlpManager(yt_dlpPath.data()), m_RandomEngine(m_RandomDevice()) {}

    void TracksQueue::FetchURL(const std::wstring_view& url, const bool& doShuffle, const float& speed, const size_t& repeat, size_t insertIndex)
    {
        m_Yt_DlpManager.FetchURL(url);

        AdjustInsertIndex(insertIndex);

        if(m_Yt_DlpManager.IsPlaylist())
        {
            size_t playlistSize = m_Yt_DlpManager.GetPlaylistSize();

            m_Tracks.reserve(m_Tracks.size() + playlistSize);

            size_t prevLastIndex = GetLastIndexWithCheck();
            if(!m_Tracks.empty())
                prevLastIndex++;

            std::vector<int> indices;
            indices.resize(playlistSize);

            for(size_t i = 0; i < indices.size(); i++)
                indices[i] = i;

            if(doShuffle)
                std::ranges::shuffle(indices, m_RandomEngine);

            //const size_t sizeBefore = m_Tracks.size();

            size_t exceptionCounter = 0;
            for(size_t i = 0; i < indices.size(); i++)
            {
                try
                {
                    //TrackInfo trackInfo = m_Yt_DlpManager.GetTrackInfo(indices[i], false);
                    //trackInfo.speed = speed;
                    //trackInfo.repeat = 1;
                    //trackInfo.uniqueIndex = s_CurrentUniqueTrackIndex++;
                    ////m_Tracks.push_back(std::move(trackInfo));

                    //m_Tracks.insert(m_Tracks.begin() + insertIndex + /*(m_Tracks.size() - sizeBefore)*/(i - exceptionCounter), std::move(trackInfo));
                    //InsertTrackInfo(insertIndex + (i - exceptionCounter), m_Yt_DlpManager.GetTrackInfo(indices[i], false), speed, repeat);
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
                std::wstring playlistTitle;

                try
                {
                    playlistTitle = m_Yt_DlpManager.GetPlaylistName();
                }
                catch(...) {}

                //there is an issue
                //m_PlaylistInfos.emplace_back(prevLastIndex, GetLastIndex(), repeat, s_CurrentUniquePlaylistIndex++, std::move(playlistTitle));
                m_PlaylistInfos.emplace_back(std::move(playlistTitle), insertIndex, playlistSize - 1 + insertIndex, repeat, s_CurrentUniquePlaylistIndex++);
            }
        }
        else
        {
            /*TrackInfo trackInfo = m_Yt_DlpManager.GetTrackInfo(0, true);
            trackInfo.speed = speed;
            trackInfo.repeat = repeat;
            trackInfo.uniqueIndex = s_CurrentUniqueTrackIndex++;
            //m_Tracks.push_back(std::move(trackInfo));

            m_Tracks.insert(m_Tracks.begin() + insertIndex, std::move(trackInfo));*/

            InsertTrackInfo(insertIndex, m_Yt_DlpManager.GetTrackInfo(0, true), speed, repeat);

            AdjustPlaylistInfosIndicesAfterInsertion(insertIndex, 1);
        }
    }
    void TracksQueue::FetchSearch(const std::wstring_view& input, const SearchEngine& searchEngine, const float& speed, const size_t& repeat, size_t insertIndex)
    {
        AdjustInsertIndex(insertIndex);

        m_Yt_DlpManager.FetchSearch(input, searchEngine);

        InsertTrackInfo(insertIndex, m_Yt_DlpManager.GetTrackInfo(0, true), speed, repeat);
        /*TrackInfo trackInfo = m_Yt_DlpManager.GetTrackInfo(0, true);
        trackInfo.speed = speed;
        trackInfo.repeat = repeat;
        trackInfo.uniqueIndex = s_CurrentUniqueTrackIndex++;
        //trackInfo.uniqueIndex = m_Tracks.size();
        //m_Tracks.push_back(std::move(trackInfo));
        m_Tracks.insert(m_Tracks.begin() + insertIndex, std::move(trackInfo));*/

        AdjustPlaylistInfosIndicesAfterInsertion(insertIndex, 1);
    }
    //fills rawURL, NOT URL
    void TracksQueue::FetchRaw(const std::string_view& url, const float& speed, const size_t& repeat, size_t insertIndex)
    {
        using namespace GuelderConsoleLog;

        AdjustInsertIndex(insertIndex);

        InsertTrackInfo(insertIndex, { .rawURL = url.data(), .title = StringToWString(url.data()) }, speed, repeat);
        /*TrackInfo trackInfo = { .title = StringToWString(url.data()), .rawURL = url.data() };
        trackInfo.speed = speed;
        trackInfo.repeat = repeat;
        trackInfo.uniqueIndex = s_CurrentUniqueTrackIndex++;
        //m_Tracks.push_back(std::move(trackInfo));
        m_Tracks.insert(m_Tracks.begin() + insertIndex, std::move(trackInfo));*/

        AdjustPlaylistInfosIndicesAfterInsertion(insertIndex, 1);
    }

    //but this indeed gets a raw url
    //WARNING: Use this if only you are sure that your track info doesn't have a raw url, as for playlists, their tracks do not have those
    const TrackInfo& TracksQueue::GetRawTrackURL(const size_t& index)
    {
        TrackInfo& trackInfo = m_Tracks[index];

        trackInfo.rawURL = m_Yt_DlpManager.GetRawURLFromURL(trackInfo.URL);

        return trackInfo;
    }

    void TracksQueue::DeleteTrack(const size_t& index)
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
    void TracksQueue::DeleteTracks(const size_t& from, const size_t& to)
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
                if(from <= playlistInfo.beginIndex)
                {
                    playlistInfo.beginIndex = (sizeDeleted <= playlistInfo.beginIndex ? playlistInfo.beginIndex - sizeDeleted : 0);
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

    void TracksQueue::TransferTrack(const size_t& from, const size_t& to)
    {
        //probably it is better to add that tracks that are not present in some playlist do not enter that playlist, but I'm too lazy for this shit

        //probably it is better to add that tracks that are not present in some playlist do not enter that playlist, but I'm too lazy for this shit
        /*if(from < to)
            std::rotate(m_Tracks.begin() + from, m_Tracks.begin() + from + 1, m_Tracks.begin() + to + 1);
        else
            std::rotate(m_Tracks.begin() + to, m_Tracks.begin() + from, m_Tracks.begin() + from + 1);*/
        Transfer(m_Tracks, from, to);
    }

    void TracksQueue::Clear()
    {
        m_Tracks.clear();
        ClearPlaylists();
        m_Yt_DlpManager.Reset();
    }

    void TracksQueue::AddPlaylist(const size_t& start, const size_t& end, const float& speed, const size_t& repeatCount, const std::wstring_view& name)
    {
        //check if intersects
        //bool intersects = false;
        bool add = true;
        for(size_t i = 0; i < m_PlaylistInfos.size(); i += add)
        {
            add = true;
            PlaylistInfo& playlist = m_PlaylistInfos[i];

            if(start <= playlist.beginIndex && end >= playlist.beginIndex || start >= playlist.beginIndex && start < playlist.endIndex)
            {
                //intersects = true;
                m_PlaylistInfos.erase(m_PlaylistInfos.begin() + i);

                add = false;
            }
        }

        m_PlaylistInfos.emplace_back(name.data(), start, end, repeatCount);
    }

    void TracksQueue::DeletePlaylist(const size_t& index)
    {
        m_PlaylistInfos.erase(m_PlaylistInfos.begin() + index);
    }

    void TracksQueue::ClearPlaylists()
    {
        m_PlaylistInfos.clear();
    }


    void TracksQueue::Shuffle(const size_t& indexToSetFirst)
    {
        ClearPlaylists();

        //if(indexToSetFirst != std::numeric_limits<size_t>::max())
            //std::rotate(m_Tracks.begin(), m_Tracks.begin() + indexToSetFirst, (m_Tracks.begin() + indexToSetFirst + 1) - m_Tracks.begin() > m_Tracks.size() ? m_Tracks.end() : m_Tracks.begin() + indexToSetFirst + 1);
        //std::shuffle(m_Tracks.begin() + 1, m_Tracks.end(), m_RandomEngine);
        Shuffle(0, m_Tracks.size() - 1, indexToSetFirst);
    }

    void TracksQueue::Shuffle(const size_t& from, const size_t& to, const size_t& indexToSetFirst)
    {
        //ClearPlaylists();

        if(indexToSetFirst == std::numeric_limits<size_t>::max())
            std::shuffle(m_Tracks.begin() + from, m_Tracks.begin() + to, m_RandomEngine);
        else
        {
            TransferTrack(indexToSetFirst, from);
            std::shuffle(m_Tracks.begin() + from + 1, m_Tracks.begin() + to, m_RandomEngine);
            //std::rotate(m_Tracks.begin(), m_Tracks.begin() + indexToSetFirst, (m_Tracks.begin() + indexToSetFirst + 1) - m_Tracks.begin() > m_Tracks.size() ? m_Tracks.end() : m_Tracks.begin() + indexToSetFirst + 1);
        }
    }

    const std::vector<TrackInfo>& TracksQueue::GetTrackInfos() const { return m_Tracks; }
    const TrackInfo& TracksQueue::GetTrackInfo(const size_t& index) const { return m_Tracks[index]; }
    const std::vector<PlaylistInfo>& TracksQueue::GetPlaylistInfos() const { return m_PlaylistInfos; }
    const PlaylistInfo& TracksQueue::GetPlaylistInfo(const size_t& index) const
    {
        return m_PlaylistInfos[index];
    }
    size_t TracksQueue::GetTracksSize() const { return m_Tracks.size(); }

    size_t TracksQueue::GetPlaylistsSize() const
    {
        return m_PlaylistInfos.size();
    }

    void TracksQueue::SetTrackTitle(const size_t& index, const std::wstring& title)
    {
        m_Tracks[index].title = title;
    }
    void TracksQueue::SetTrackDuration(const size_t& index, const float& duration)
    {
        m_Tracks[index].duration = duration;
    }
    void TracksQueue::SetTrackSpeed(const size_t& index, const float& speed)
    {
        m_Tracks[index].speed = speed;
    }
    void TracksQueue::SetTrackRepeatCount(const size_t& index, const size_t& repeatCount)
    {
        m_Tracks[index].repeat = repeatCount;
    }

    void TracksQueue::SetPlaylistTitle(const size_t& index, const std::wstring& title)
    {
        m_PlaylistInfos[index].title = title;
    }
    void TracksQueue::SetPlaylistRepeatCount(const size_t& index, const size_t& repeatCount)
    {
        m_PlaylistInfos[index].repeat = repeatCount;
    }

    size_t TracksQueue::GetLastIndexWithCheck() const
    {
        if(m_Tracks.empty())
            return 0;
        else
            return GetLastIndex();
    }
    size_t TracksQueue::GetLastIndex() const
    {
        return m_Tracks.size() - 1;
    }

    void TracksQueue::AdjustInsertIndex(size_t& insertIndex) const
    {
        if(!m_Tracks.empty())
        {
            if(insertIndex == std::numeric_limits<size_t>::max())
                insertIndex = m_Tracks.size()/* - 1*/;

            O_ASSERT(insertIndex <= m_Tracks.size(), "Cannot insert tracks with index that is bigger than the last index of m_Tracks.");
        }
        else
            insertIndex = 0;
    }
    void TracksQueue::AdjustPlaylistInfosIndicesAfterInsertion(const size_t& insertIndex, const size_t& addingTracksSize)
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

    void TracksQueue::InsertTrackInfo(const size_t& insertIndex, TrackInfo&& trackInfo, const float& speed, const size_t& repeat)
    {
        trackInfo.speed = speed;
        trackInfo.repeat = repeat;
        trackInfo.uniqueIndex = s_CurrentUniqueTrackIndex++;
        //m_Tracks.push_back(std::move(trackInfo));
        m_Tracks.insert(m_Tracks.begin() + insertIndex, std::move(trackInfo));
    }
}