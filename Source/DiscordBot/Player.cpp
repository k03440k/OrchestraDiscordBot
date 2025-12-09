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

//main stuff
namespace Orchestra
{
    Player::Player(uint32_t sentPacketsSize, bool enableLogSentPackets)
        : m_SentPacketSize(sentPacketsSize), m_EnableLogSentPackets(enableLogSentPackets), m_BassBoostSettings(0.f, 0.f, 0.f) {
    }
    Player::Player(const Player& other)
    {
        CopyFrom(other);
    }
    Player& Player::operator=(const Player& other)
    {
        CopyFrom(other);

        return *this;
    }
    Player::Player(Player&& other) noexcept
    {
        MoveFrom(std::move(other));
    }
    Player& Player::operator=(Player&& other) noexcept
    {
        MoveFrom(std::move(other));

        return *this;
    }

    void Player::CopyFrom(const Player& other)
    {
        m_Decoder = other.m_Decoder;
        m_SentPacketSize = other.m_SentPacketSize;
        m_EnableLogSentPackets = other.m_EnableLogSentPackets;
        m_IsDecoding = other.m_IsDecoding.load();
        m_IsSkippingFrames = other.m_IsSkippingFrames.load();
        m_ShouldReturnToCurrentTimestamp = other.m_ShouldReturnToCurrentTimestamp.load();
        m_PreviousSampleRate = other.m_PreviousSampleRate.load();
        m_IsPaused = other.m_IsPaused.load();
        m_CurrentDecodingTimestamp = other.m_CurrentDecodingTimestamp.load();
        m_BassBoostSettings = other.m_BassBoostSettings;
        m_EqualizerFrequencies = other.m_EqualizerFrequencies;
    }
    void Player::MoveFrom(Player&& other) noexcept
    {
        m_Decoder = std::move(other.m_Decoder);
        m_SentPacketSize = other.m_SentPacketSize;
        m_EnableLogSentPackets = other.m_EnableLogSentPackets;
        m_IsDecoding = other.m_IsDecoding.load();
        m_IsSkippingFrames = other.m_IsSkippingFrames.load();
        m_ShouldReturnToCurrentTimestamp = other.m_ShouldReturnToCurrentTimestamp.load();
        m_PreviousSampleRate = other.m_PreviousSampleRate.load();
        m_IsPaused = other.m_IsPaused.load();
        m_CurrentDecodingTimestamp = other.m_CurrentDecodingTimestamp.load();
        m_BassBoostSettings = other.m_BassBoostSettings;
        m_EqualizerFrequencies = std::move(other.m_EqualizerFrequencies);
    }
}
namespace Orchestra
{
    //TODO: maybe remake it somehow with WaitUntil
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
        //std::unique_lock decodingLock{ m_DecodingMutex };

        O_ASSERT(m_Decoder.IsReady(), "m_Decoder is not ready.");

        //if(!m_BassBoostSettings.IsEmpty() && !m_Decoder.IsBassBoostActive())
        m_Decoder.SetBassBoost(m_BassBoostSettings.decibelsBoost, m_BassBoostSettings.frequency, m_BassBoostSettings.bandwidth);
        m_Decoder.SetEqualizer(m_EqualizerFrequencies);

        //TODO: put this into config.txt
        constexpr float waitFactor = .9f;
        float currentWaitFactor = 1.f;

        GE_LOG(Orchestra, Info, "Total duration of audio: ", m_Decoder.GetTotalDurationSeconds(), "s.");

        std::vector<uint8_t> buffer;
        buffer.reserve(m_SentPacketSize);

        uint64_t totalReads = 0;
        uint64_t totalSentSize = 0;
        //float totalDuration = 0;

        GE_LOG(Orchestra, Info, "PlayAudio is executing on thread with index ", std::this_thread::get_id(), '.');

        m_IsDecoding = true;

        float currentSentDuration = 0.f;
        float totalSentDuration = 0.f;

        const int channelsCountTimesBytesPerSample = m_Decoder.GetChannelsCount() * m_Decoder.GetBytesPerSample();

        //bool skipping = false;

        constexpr int initialSampleRate = Decoder::DEFAULT_SAMPLE_RATE;
        m_PreviousSampleRate = initialSampleRate;

        bool areThereFramesToProcess = m_Decoder.AreThereFramesToProcess();
        bool waitingAfterLastBytes = false;

        while(areThereFramesToProcess || waitingAfterLastBytes)
        {
            std::unique_lock pauseLock{ m_PauseMutex };
            m_PauseCondition.wait(pauseLock, [this] { return m_IsPaused == false; });

            if(!m_IsDecoding)
                break;

            std::unique_lock decodingLock{ m_DecodingMutex };
            //if(!decodingLock.owns_lock())
                //decodingLock.lock();
            auto out = m_Decoder.DecodeAudioFrame();
            decodingLock.unlock();

            buffer.insert(buffer.end(), out.begin(), out.end());

            out.clear();
            out.shrink_to_fit();

            areThereFramesToProcess = m_Decoder.AreThereFramesToProcess();

            if(buffer.size() >= m_SentPacketSize || !areThereFramesToProcess)
            {
                decodingLock.lock();

                //TODO: make so that when I call !skip -secs to skip this loop and start decoding
                //I need to call from discord stop and proceed to decoding
                if(!m_IsSkippingFrames && !m_ShouldReturnToCurrentTimestamp)
                {
                    decodingLock.unlock();
                    LazyDecodingCheck(std::chrono::milliseconds{ static_cast<int>(voice->voiceclient->get_secs_remaining() * currentWaitFactor) * 1000 }, pauseLock);
                    decodingLock.lock();

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
                    const float remainingSeconds = voice->voiceclient->get_secs_remaining();
                    currentSentDuration = remainingSeconds - (currentSentDuration * (1.f - currentWaitFactor));

                    m_CurrentDecodingTimestamp += static_cast<float>(buffer.size()) / static_cast<float>(channelsCountTimesBytesPerSample) / static_cast<float>(m_Decoder.GetOutSampleRate())/* * sampleRateRatio*/;

                    totalSentDuration += currentSentDuration * sampleRateRatio;
                    
                    if(m_EnableLogSentPackets)
                        GE_LOG(Orchestra, Info, "m_CurrentDecodingTimestamp = ", m_CurrentDecodingTimestamp, "s",
                            "; totalSentDuration = ", totalSentDuration, "s",
                            "; voice->voiceclient->get_secs_remaining() = ", remainingSeconds, "s",
                            "; currentSentDuration = ", currentSentDuration, "s",
                            "; buffer.size() = ", buffer.size(),
                            "; totalReads = ", totalReads,
                            "; currentWaitFactor = ", currentWaitFactor);

                    if(currentWaitFactor == waitFactor)
                        currentWaitFactor = 1.f;
                    else
                        currentWaitFactor = waitFactor;
                }
                else if(m_ShouldReturnToCurrentTimestamp)
                {
                    const float remainingSeconds = voice->voiceclient->get_secs_remaining();
                    m_CurrentDecodingTimestamp -= remainingSeconds * prevSampleRateRatio;
                    GE_LOG(Orchestra, Warning, "There is a need to skip to currently playing frame!\nprevSampleRateRatio = ", prevSampleRateRatio, "\nm_CurrentDecodingTimestamp = ", m_CurrentDecodingTimestamp, "\nremainingSeconds = ", remainingSeconds);
                    m_Decoder.SkipToSeconds(m_CurrentDecodingTimestamp);
                }

                buffer.clear();

                decodingLock.unlock();
            }

            if(!areThereFramesToProcess)
            {
                LazyDecodingCheck(std::chrono::milliseconds{ static_cast<int>(voice->voiceclient->get_secs_remaining() * waitFactor) * 1000 }, pauseLock);

                areThereFramesToProcess = m_Decoder.AreThereFramesToProcess();
            }

            totalReads++;
        }

        if(m_EnableLogSentPackets)
            GE_LOG(Orchestra, Info, "Playback finished. Total number of reads: ", totalReads, " reads. Total size of sent data: ", totalSentSize, ". m_CurrentDecodingTimestamp: ", m_CurrentDecodingTimestamp, '.');

        m_IsDecoding = false;
        m_PreviousSampleRate = 0;
        m_CurrentDecodingTimestamp = 0.f;
    }

    void Player::Stop()
    {
        std::unique_lock decodingLock{ m_DecodingMutex };
        Pause(true);

        m_IsDecoding = false;
        m_IsPaused = false;
        m_PauseCondition.notify_all();

        Pause(false);
    }
    void Player::Pause(bool pause)
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

    void Player::SkipToSeconds(float seconds)
    {
        std::unique_lock decodingLock{ m_DecodingMutex };

        m_Decoder.SkipToSeconds(seconds);
        m_CurrentDecodingTimestamp = seconds;
        m_IsSkippingFrames = true;
    }
    void Player::SkipSeconds(float seconds)
    {
        std::unique_lock decodingLock{ m_DecodingMutex };

        m_CurrentDecodingTimestamp += seconds;
        m_Decoder.SkipToSeconds(m_CurrentDecodingTimestamp);
        m_IsSkippingFrames = true;
    }

    void Player::SetDecoder(const std::string_view& url, int sampleRate)
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

    void Player::SetBassBoost(BassBoostSettings bassBoostSettings)
    {
        std::lock_guard bassBoostSettingsLock{ m_DecodingMutex };

        m_BassBoostSettings = std::move(bassBoostSettings);

        if(m_Decoder.IsReady())
        {
            const bool wasPaused = m_IsPaused;

            if(!wasPaused)
                Pause(true);

            m_ShouldReturnToCurrentTimestamp = true;
            m_IsSkippingFrames = true;

            m_Decoder.SetBassBoost(m_BassBoostSettings.decibelsBoost, m_BassBoostSettings.frequency, m_BassBoostSettings.bandwidth);

            if(!wasPaused)
                Pause(false);
        }
    }
    void Player::SetBassBoost(float decibelsBoost, float frequencyToAdjust, float bandwidth)
    {
        SetBassBoost(BassBoostSettings{ decibelsBoost, frequencyToAdjust, bandwidth });
    }

    void Player::InsertOrAssignEqualizerFrequency(float frequency, float decibelsBoost)
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
    void Player::EraseEqualizerFrequency(float frequency)
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
}
//getters, setters
namespace Orchestra
{
    void Player::SetAudioSampleRate(int sampleRate)
    {
        std::unique_lock decodingLock{ m_DecodingMutex };

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

    int Player::GetAudioSampleRate() const
    {
        return m_Decoder.GetOutSampleRate();
    }

    void Player::SetEnableLogSentPackets(bool enable)
    {
        m_EnableLogSentPackets = enable;
    }
    void Player::SetSentPacketSize(uint32_t size)
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

    float Player::GetCurrentTimestamp() const
    {
        return m_CurrentDecodingTimestamp;
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