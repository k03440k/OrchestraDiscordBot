#pragma once

#include <atomic>
#include <mutex>
#include <future>
#include <vector>
#include <string_view>
#include <map>

#include <dpp/dpp.h>

#include "../FFmpeg/Decoder.hpp"

namespace Orchestra
{
    class Player
    {
    public:
        struct BassBoostSettings
        {
            float decibelsBoost;
            float frequency;
            float bandwidth;

            bool IsEmpty() const { return !decibelsBoost && !frequency && !bandwidth; }
        };
    public:
        Player(uint32_t sentPacketsSize = 0, bool enableLogSentPackets = false);

        Player(const Player& other);
        Player& operator=(const Player& other);
        Player(Player&& other) noexcept;
        Player& operator=(Player&& other) noexcept;

        //blocks current thread
        void DecodeAndSendAudio(const dpp::voiceconn* voice);

        void Stop();
        void Pause(bool pause);
        void Skip();
        
        void SkipToSeconds(float seconds);
        void SkipSeconds(float seconds);

        void SetDecoder(const std::string_view& url, int sampleRate = Decoder::DEFAULT_SAMPLE_RATE);

        void ResetDecoder();
        bool IsDecoderReady() const;

        void SetBassBoost(float decibelsBoost = 0.f, float frequencyToAdjust = 0.f, float bandwidth = 0.f);

        void InsertOrAssignEqualizerFrequency(float frequency, float decibelsBoost);
        void EraseEqualizerFrequency(float frequency);
        void ClearEqualizer();

    public:
        void SetAudioSampleRate(uint32_t sampleRate);

        void SetEnableLogSentPackets(bool enable);
        void SetSentPacketSize(uint32_t size);

        bool GetIsPaused() const noexcept;
        bool GetIsDecoding() const noexcept;

        bool GetEnableLogSentPackets() const noexcept;
        uint32_t GetSentPacketSize() const noexcept;

        float GetCurrentTimestamp() const noexcept;
        //if return is 0, then there are no decoders
        float GetTotalDuration() const;

        const BassBoostSettings& GetBassBoostSettings() const;

        const std::map<float, float>& GetEqualizerFrequencies() const;

        std::string GetTitle() const;

        bool HasDecoderFinished() const;

    private:
        void LazyDecodingCheck(const std::chrono::milliseconds& toWait, std::unique_lock<std::mutex>& pauseLock, const std::chrono::milliseconds& sleepFor = std::chrono::milliseconds(10));

    private:
        void CopyFrom(const Player& other);
        void MoveFrom(Player&& other) noexcept;
    private:
        Decoder m_Decoder;

        uint32_t m_SentPacketSize;
        bool m_EnableLogSentPackets : 1;

        std::mutex m_DecodingMutex;

        std::atomic_bool m_IsDecoding;
        std::atomic_bool m_IsSkippingFrames;
        std::atomic_bool m_ShouldReturnToCurrentTimestamp;

        std::atomic_int m_PreviousSampleRate;

        std::atomic_bool m_IsPaused;
        std::condition_variable m_PauseCondition;
        std::mutex m_PauseMutex;

        std::atomic<float> m_CurrentTimestamp;

        BassBoostSettings m_BassBoostSettings;
        //first - frequency, second - decibels boost
        std::map<float, float> m_EqualizerFrequencies;
    };
}