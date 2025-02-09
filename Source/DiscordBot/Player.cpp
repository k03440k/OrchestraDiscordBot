#include "Player.hpp"

#include <thread>
#include <atomic>
#include <mutex>
#include <future>
#include <chrono>
#include <vector>
#include <chrono>

extern "C"
{
#include <libavutil/samplefmt.h>
}

#include <dpp/dpp.h>

#include "../Utils.hpp"
#include "../FFmpeg/Decoder.hpp"

namespace FSDB
{
    Player::Player(const uint32_t& sentPacketsSize, const bool& enableLazyDecoding, const bool& enableLogSentPackets)
        : m_SentPacketSize(sentPacketsSize), m_EnableLazyDecoding(enableLazyDecoding), m_EnableLogSentPackets(enableLogSentPackets) {}

    void Player::LazyDecodingCheck(const std::chrono::milliseconds& toWait, const std::chrono::milliseconds& sleepFor) const
    {
        const auto startedTime = std::chrono::steady_clock::now();

        while(m_IsDecoding)
        {
            if(std::chrono::steady_clock::now() - startedTime >= toWait)
                break;

            std::this_thread::sleep_for(sleepFor);
        }
    }
    void Player::PlayAudio(const dpp::voiceconn* voice, const size_t& index)
    {
        GE_ASSERT(!m_Decoders.empty(), "m_Decoders is empty.");

        const Decoder& decoder = m_Decoders[index];

        LogInfo("Total duration of audio: ", decoder.GetDuration(), "s.");

        std::vector<uint8_t> buffer;
        buffer.reserve(m_SentPacketSize);

        uint64_t totalReads = 0;
        uint64_t totalSentSize = 0;
        float totalDuration = 0;

        LogError(std::this_thread::get_id());

        m_IsDecoding = true;

        while(decoder.AreThereFramesToProcess())
        {
            std::unique_lock pauseLock{ m_PauseMutex };
            m_PauseCondition.wait(pauseLock, [this] { return m_IsPaused == false; });

            if(!m_IsDecoding)
                break;

            auto out = decoder.DecodeAudioFrame();

            buffer.insert(buffer.end(), out.begin(), out.end());

            if(buffer.size() >= m_SentPacketSize)
            {
                if(m_EnableLazyDecoding)
                {
                    LazyDecodingCheck(std::chrono::milliseconds{ static_cast<int>(voice->voiceclient->get_secs_remaining() * .95f) * 1000 });

                    if(!m_IsDecoding)
                        break;
                }

                voice->voiceclient->send_audio_raw(reinterpret_cast<uint16_t*>(buffer.data()), buffer.size());

                totalSentSize += buffer.size();
                totalDuration += voice->voiceclient->get_secs_remaining();

                if(m_EnableLogSentPackets)
                    LogInfo("Sent ", buffer.size(), " bytes of data; totalNumberOfReadings: ", totalReads, " for; sent data lasts for ", voice->voiceclient->get_secs_remaining(), "s.");

                buffer.clear();
            }

            totalReads++;
        }

        //leftovers
        if(m_IsDecoding && !buffer.empty())
        {
            if(m_EnableLazyDecoding)
                LazyDecodingCheck(std::chrono::milliseconds{ static_cast<int>(voice->voiceclient->get_secs_remaining() * .95f) * 1000 });

            if(m_IsDecoding)
            {
                voice->voiceclient->send_audio_raw(reinterpret_cast<uint16_t*>(buffer.data()), buffer.size());

                totalSentSize += buffer.size();
                totalDuration += voice->voiceclient->get_secs_remaining();

                if(m_EnableLogSentPackets)
                    LogInfo("Sent ", buffer.size(), " last bytes of data that lasts for ", voice->voiceclient->get_secs_remaining(), "s.");
            }
        }

        m_IsDecoding = false;

        if(m_EnableLogSentPackets)
            LogInfo("Playback finished. Total number of reads: ", totalReads, " reads. Total size of sent data: ", totalSentSize, ". Total sent duration: ", totalDuration, '.');
    }

    void Player::AddAudio(const std::string_view& url, const uint32_t& sampleRate, const size_t& pos)
    {
        m_Decoders.emplace(m_Decoders.begin() + pos, url, AV_SAMPLE_FMT_S16, sampleRate);
    }
    void Player::AddAudioBack(const std::string_view& url, const uint32_t& sampleRate)
    {
        m_Decoders.emplace_back(url, AV_SAMPLE_FMT_S16, sampleRate);
    }

    void Player::DeleteAudio(const size_t& index)
    {
        m_Decoders.erase(m_Decoders.begin() + index);
    }
    void Player::DeleteAllAudio()
    {
        m_Decoders.clear();
    }

    void Player::Stop()
    {
        Pause(true);

        DeleteAllAudio();

        m_IsDecoding = false;
        m_IsPaused = false;
        m_PauseCondition.notify_all();

        Pause(false);
    }
    void Player::Pause(const bool& pause)
    {
        m_IsPaused = pause;
        m_PauseCondition.notify_all();
    }

    void Player::Skip()
    {
        m_IsDecoding = false;
        m_IsPaused = false;
        m_PauseCondition.notify_all();
    }

    void Player::Reserve(const size_t& capacity)
    {
        m_Decoders.reserve(capacity);
    }

    void Player::SetAudioSampleRate(const uint32_t& sampleRate, const size_t& index)
    {
        m_Decoders[index].SetSampleRate(sampleRate);
    }

    void Player::SetEnableLogSentPackets(const bool& enable)
    {
        m_EnableLogSentPackets = enable;
    }
    void Player::SetEnableLazyDecoding(const bool& enable)
    {
        m_EnableLazyDecoding = enable;
    }
    void Player::SetSentPacketSize(const uint32_t& size)
    {
        m_SentPacketSize = size;
    }

    bool Player::GetIsPaused() const noexcept
    {
        return m_IsPaused;
    }
    bool Player::GetIsDecoding() const noexcept
    {
        return m_IsDecoding;
    }

    bool Player::GetEnableLogSentPackets() const noexcept
    {
        return m_EnableLogSentPackets;
    }
    bool Player::GetEnableLazyDecoding() const noexcept
    {
        return m_EnableLazyDecoding;
    }
    uint32_t Player::GetSentPacketSize() const noexcept
    {
        return m_SentPacketSize;
    }

    size_t Player::GetAudioCount() const noexcept
    {
        return m_Decoders.size();
    }
}