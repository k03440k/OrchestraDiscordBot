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

namespace Orchestra
{
    Player::Player(const uint32_t& sentPacketsSize, const bool& enableLogSentPackets)
        : m_SentPacketSize(sentPacketsSize), m_EnableLogSentPackets(enableLogSentPackets), m_BassBoostSettings(0.f, 0.f, 0.f) {}

    void Player::LazyDecodingCheck(const std::chrono::milliseconds& toWait, std::unique_lock<std::mutex>& pauseLock, const std::chrono::milliseconds& sleepFor)
    {
        auto startedTime = std::chrono::steady_clock::now();

        while(m_IsDecoding)
        {
            const auto waited = std::chrono::steady_clock::now() - startedTime;

            m_PauseCondition.wait(pauseLock, [this, &startedTime, &waited] { startedTime = std::chrono::steady_clock::now() - waited; return m_IsPaused == false; });

            if(m_IsSkippingFrames || std::chrono::steady_clock::now() - startedTime >= toWait)
                break;

            std::this_thread::sleep_for(sleepFor);
        }
    }
    void Player::DecodeAndSendAudio(const dpp::voiceconn* voice)
    {
        O_ASSERT(m_Decoder.IsReady(), "m_Decoder is not ready.");

        //if(!m_BassBoostSettings.IsEmpty() && !m_Decoder.IsBassBoostActive())
        m_Decoder.SetBassBoost(m_BassBoostSettings.decibelsBoost, m_BassBoostSettings.frequency, m_BassBoostSettings.bandwidth);
        m_Decoder.SetEqualizer(m_EqualizerFrequencies);

        //TODO: put this into config.txt
        constexpr float waitFactor = .9f;

        GE_LOG(Orchestra, Info, "Total duration of audio: ", m_Decoder.GetTotalDurationSeconds(), "s.");

        std::vector<uint8_t> buffer;
        buffer.reserve(m_SentPacketSize);

        uint64_t totalReads = 0;
        uint64_t totalSentSize = 0;
        //float totalDuration = 0;

        GE_LOG(Orchestra, Info, "PlayAudio is executing on thread with index ", std::this_thread::get_id(), '.');

        m_IsDecoding = true;

        float currentSentDuration = 0.f;

        bool skipping = false;

        constexpr int initialSampleRate = Decoder::DEFAULT_SAMPLE_RATE;
        m_PreviousSampleRate = initialSampleRate;

        while(m_Decoder.AreThereFramesToProcess())
        {
            std::unique_lock pauseLock{ m_PauseMutex };
            m_PauseCondition.wait(pauseLock, [this] { return m_IsPaused == false; });

            if(!m_IsDecoding)
                break;

            std::unique_lock decodingLock{ m_DecodingMutex };
            auto out = m_Decoder.DecodeAudioFrame();
            decodingLock.unlock();

            buffer.insert(buffer.end(), out.begin(), out.end());

            out.clear();
            out.shrink_to_fit();

            if(buffer.size() >= m_SentPacketSize)
            {
                //TODO: make so that when I call !skip -secs to skip this loop and start decoding
                //I need to call from discord stop and proceed to decoding
                if(!m_IsSkippingFrames && !m_ShouldReturnToCurrentTimestamp)
                {
                    LazyDecodingCheck(std::chrono::milliseconds{ static_cast<int>(voice->voiceclient->get_secs_remaining() * waitFactor) * 1000 }, pauseLock);

                    if(!m_IsDecoding)
                        break;
                }
                else
                {
                    //if(!m_ShouldReturnToCurrentTimestamp)
                        //voice->voiceclient->stop_audio();
                    m_IsSkippingFrames = false;
                }

                const float sampleRateRatio = static_cast<float>(initialSampleRate) / m_Decoder.GetOutSampleRate();
                const float prevSampleRateRatio = static_cast<float>(initialSampleRate) / m_PreviousSampleRate;

                if(!m_IsSkippingFrames)
                {
                    if(m_ShouldReturnToCurrentTimestamp)
                    {
                        voice->voiceclient->stop_audio();
                        m_ShouldReturnToCurrentTimestamp = false;
                    }

                    voice->voiceclient->send_audio_raw(reinterpret_cast<uint16_t*>(buffer.data()), buffer.size());

                    totalSentSize += buffer.size();
                    currentSentDuration = voice->voiceclient->get_secs_remaining();
                    m_CurrentTimestamp += currentSentDuration * sampleRateRatio;

                    //GE_LOG(Orchestra, Warning, "sampleRateRatio = ", sampleRateRatio, "; totalDuration = ", totalDuration, "; currentSentDuration = ", currentSentDuration);

                    if(m_EnableLogSentPackets)
                        GE_LOG(Orchestra, Info, "Sent ", buffer.size(), " bytes of data; totalNumberOfReadings: ", totalReads, " for; sent data lasts for ", currentSentDuration, "s.");
                }
                else if(m_ShouldReturnToCurrentTimestamp)
                {

                    const float remainingSeconds = voice->voiceclient->get_secs_remaining();
                    m_CurrentTimestamp -= remainingSeconds * prevSampleRateRatio;
                    GE_LOG(Orchestra, Warning, "prevSampleRateRatio = ", prevSampleRateRatio, "; totalDuration = ", m_CurrentTimestamp, "; remainingSeconds = ", remainingSeconds, "; currentTimestamp in secs", static_cast<float>(m_Decoder.GetCurrentTimestamp()) * m_Decoder.GetTimestampToSecondsRatio());
                    m_Decoder.SkipToSeconds(m_CurrentTimestamp);
                }

                buffer.clear();
            }

            totalReads++;
        }

        //leftovers
        if(m_IsDecoding && !buffer.empty())
        {
            std::unique_lock pauseLock{ m_PauseMutex };
            m_PauseCondition.wait(pauseLock, [this] { return m_IsPaused == false; });

            LazyDecodingCheck(std::chrono::milliseconds{ static_cast<int>(voice->voiceclient->get_secs_remaining() * waitFactor) * 1000 }, pauseLock);

            if(m_IsDecoding)
            {
                voice->voiceclient->send_audio_raw(reinterpret_cast<uint16_t*>(buffer.data()), buffer.size());

                const float sampleRateRatio = static_cast<float>(initialSampleRate) / m_Decoder.GetOutSampleRate();

                totalSentSize += buffer.size();
                m_CurrentTimestamp += voice->voiceclient->get_secs_remaining() * sampleRateRatio;

                if(m_EnableLogSentPackets)
                    GE_LOG(Orchestra, Info, "Sent ", buffer.size(), " last bytes of data that lasts for ", voice->voiceclient->get_secs_remaining(), "s.");
            }
        }

        m_IsDecoding = false;
        m_CurrentTimestamp = 0.f;
        m_PreviousSampleRate = 0;

        if(m_EnableLogSentPackets)
            GE_LOG(Orchestra, Info, "Playback finished. Total number of reads: ", totalReads, " reads. Total size of sent data: ", totalSentSize, ". Total sent duration: ", m_CurrentTimestamp, '.');
    }

    void Player::Stop()
    {
        Pause(true);

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

    void Player::SkipToSeconds(const float& seconds)
    {
        m_Decoder.SkipToSeconds(seconds);
        m_CurrentTimestamp = seconds;
        m_IsSkippingFrames = true;
    }
    void Player::SkipSeconds(const float& seconds)
    {
        m_CurrentTimestamp += seconds;
        m_Decoder.SkipToSeconds(m_CurrentTimestamp);
        m_IsSkippingFrames = true;
    }

    void Player::SetDecoder(const std::string_view& url, const uint32_t& sampleRate)
    {
        m_Decoder = Decoder{ url, sampleRate };
    }

    void Player::ResetDecoder()
    {
        m_Decoder.Reset();
    }
    bool Player::IsDecoderReady() const
    {
        return m_Decoder.IsReady();
    }

    void Player::SetBassBoost(const float& decibelsBoost, const float& frequencyToAdjust, const float& bandwidth)
    {
        std::lock_guard bassBoostSettingsLock{ m_DecodingMutex };

        m_BassBoostSettings.decibelsBoost = decibelsBoost;
        m_BassBoostSettings.frequency = frequencyToAdjust;
        m_BassBoostSettings.bandwidth = bandwidth;

        if(m_Decoder.IsReady())
        {
            const bool wasPaused = m_IsPaused;

            if(!wasPaused)
                Pause(true);

            m_ShouldReturnToCurrentTimestamp = true;
            m_IsSkippingFrames = true;

            m_Decoder.SetBassBoost(decibelsBoost, frequencyToAdjust, bandwidth);

            if(!wasPaused)
                Pause(false);
        }
    }

    void Player::InsertOrAssignEqualizerFrequency(const float& frequency, const float& decibelsBoost)
    {
        std::lock_guard equalizersLock{ m_DecodingMutex };

        m_EqualizerFrequencies.insert_or_assign(frequency, decibelsBoost);

        if(m_Decoder.IsReady())
        {
            const bool wasPaused = m_IsPaused;

            if(!wasPaused)
                Pause(true);

            m_ShouldReturnToCurrentTimestamp = true;
            m_IsSkippingFrames = true;

            m_Decoder.SetEqualizer(m_EqualizerFrequencies);

            if(!wasPaused)
                Pause(false);
        }
    }
    void Player::EraseEqualizerFrequency(const float& frequency)
    {
        std::lock_guard equalizersLock{ m_DecodingMutex };

        m_EqualizerFrequencies.erase(frequency);

        if(m_Decoder.IsReady())
        {
            const bool wasPaused = m_IsPaused;

            if(!wasPaused)
                Pause(true);

            m_ShouldReturnToCurrentTimestamp = true;
            m_IsSkippingFrames = true;

            m_Decoder.SetEqualizer(m_EqualizerFrequencies);

            if(!wasPaused)
                Pause(false);
        }
    }
    void Player::ClearEqualizer()
    {
        std::lock_guard equalizersLock{ m_DecodingMutex };

        m_EqualizerFrequencies.clear();

        if(m_Decoder.IsReady())
        {
            const bool wasPaused = m_IsPaused;

            if(!wasPaused)
                Pause(true);

            m_ShouldReturnToCurrentTimestamp = true;
            m_IsSkippingFrames = true;

            m_Decoder.SetEqualizer(m_EqualizerFrequencies);

            if(!wasPaused)
                Pause(false);
        }
    }

    void Player::SetAudioSampleRate(const uint32_t& sampleRate)
    {
        const bool wasPaused = m_IsPaused;

        if(!wasPaused)
            Pause(true);

        m_PreviousSampleRate = m_Decoder.GetOutSampleRate();
        m_Decoder.SetOutSampleRate(sampleRate);

        m_ShouldReturnToCurrentTimestamp = true;
        m_IsSkippingFrames = true;

        if(!wasPaused)
            Pause(false);
    }

    void Player::SetEnableLogSentPackets(const bool& enable)
    {
        m_EnableLogSentPackets = enable;
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
    uint32_t Player::GetSentPacketSize() const noexcept
    {
        return m_SentPacketSize;
    }

    float Player::GetCurrentTimestamp() const noexcept
    {
        return m_CurrentTimestamp;
    }
    float Player::GetTotalDuration() const
    {
        return m_Decoder.GetTotalDurationSeconds();
    }

    const Player::BassBoostSettings& Player::GetBassBoostSettings() const
    {
        return m_BassBoostSettings;
    }

    const std::map<float, float>& Player::GetEqualizerFrequencies() const
    {
        return m_EqualizerFrequencies;
    }

    std::string Player::GetTitle() const
    {
        return m_Decoder.GetTitle();
    }

    bool Player::HasDecoderFinished() const
    {
        return !m_Decoder.AreThereFramesToProcess();
    }
}
