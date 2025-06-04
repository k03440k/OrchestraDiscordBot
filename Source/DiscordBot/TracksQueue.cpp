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

    void TracksQueue::FetchURL(const std::wstring_view& url, const bool& doShuffle, const float& speed, const size_t& repeat)
    {
        m_Yt_DlpManager.FetchURL(url);

        if(m_Yt_DlpManager.IsPlaylist())
        {
            const size_t playlistSize = m_Yt_DlpManager.GetPlaylistSize();

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

            for(size_t i = 0; i < indices.size(); i++)
                try
            {
                TrackInfo trackInfo = m_Yt_DlpManager.GetTrackInfo(indices[i], false);
                trackInfo.speed = speed;
                trackInfo.repeat = 1;
                trackInfo.playlistIndex = s_CurrentUniqueTrackIndex++;
                //trackInfo.playlistIndex += prevLastIndex;
                m_Tracks.push_back(std::move(trackInfo));
            }
            catch(const OrchestraException& exception)
            {
                GE_LOG(Orchestra, Warning, "Exception occured during getting track infos from a playlist, skipping the track. Exception: ", exception.GetFullMessage());
            }
            catch(const std::exception& exception)
            {
                GE_LOG(Orchestra, Warning, "Exception occured during getting track infos from a playlist, skipping the track. Exception: ", exception.what());
            }

            //PlaylistInfo tmp = { prevLastIndex, GetLastIndex(), repeat };

            //m_PlaylistInfos.push_back(std::move(tmp));
            m_PlaylistInfos.emplace_back(prevLastIndex, GetLastIndex(), repeat, s_CurrentUniquePlaylistIndex++);
        }
        else
        {
            TrackInfo trackInfo = m_Yt_DlpManager.GetTrackInfo(0, true);
            trackInfo.speed = speed;
            trackInfo.repeat = repeat;
            trackInfo.playlistIndex = s_CurrentUniqueTrackIndex++;
            //trackInfo.playlistIndex = m_Tracks.size();
            m_Tracks.push_back(std::move(trackInfo));
        }
    }
    void TracksQueue::FetchSearch(const std::wstring_view& input, const SearchEngine& searchEngine, const float& speed, const size_t& repeat)
    {
        m_Yt_DlpManager.FetchSearch(input, searchEngine);

        TrackInfo trackInfo = m_Yt_DlpManager.GetTrackInfo(0, true);
        trackInfo.speed = speed;
        trackInfo.repeat = repeat;
        trackInfo.playlistIndex = s_CurrentUniqueTrackIndex++;
        //trackInfo.playlistIndex = m_Tracks.size();
        m_Tracks.push_back(std::move(trackInfo));
    }
    //fills rawURL, NOT URL
    void TracksQueue::FetchRaw(const std::string_view& url, const float& speed, const size_t& repeat)
    {
        TrackInfo trackInfo = { .rawURL = url.data() };
        trackInfo.speed = speed;
        trackInfo.repeat = repeat;
        trackInfo.playlistIndex = s_CurrentUniqueTrackIndex++;
        m_Tracks.push_back(std::move(trackInfo));
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

    void TracksQueue::Clear()
    {
        m_Tracks.clear();
        m_PlaylistInfos.clear();
        m_Yt_DlpManager.Reset();
    }

    void TracksQueue::Shuffle()
    {
        m_PlaylistInfos.clear();
        std::ranges::shuffle(m_Tracks, m_RandomEngine);
    }
    void TracksQueue::Shuffle(const size_t& indexToSetFirst)
    {
        m_PlaylistInfos.clear();

        std::rotate(m_Tracks.begin(), m_Tracks.begin() + indexToSetFirst, (m_Tracks.begin() + indexToSetFirst + 1) - m_Tracks.begin() > m_Tracks.size() ? m_Tracks.end() : m_Tracks.begin() + indexToSetFirst + 1);
        std::shuffle(m_Tracks.begin() + 1, m_Tracks.end(), m_RandomEngine);
    }

    const std::vector<TrackInfo>& TracksQueue::GetTrackInfos() const { return m_Tracks; }
    const TrackInfo& TracksQueue::GetTrackInfo(const size_t& index) const { return m_Tracks[index]; }
    const std::vector<PlaylistInfo>& TracksQueue::GetPlaylistInfos() const { return m_PlaylistInfos; }
    size_t TracksQueue::GetSize() const { return m_Tracks.size(); }

    void TracksQueue::SetTrackTitle(const size_t& index, const std::wstring& title)
    {
        m_Tracks[index].title = title;
    }
    void TracksQueue::SetTrackDuration(const size_t& index, const float& duration)
    {
        m_Tracks[index].duration = duration;
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
}