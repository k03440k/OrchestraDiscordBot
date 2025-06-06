#pragma once

#include <atomic>
#include <mutex>
#include <future>
#include <vector>
#include <string_view>

#include <dpp/dpp.h>

#include "../FFmpeg/Decoder.hpp"

namespace Orchestra
{
    class Player
    {
    public:
        Player(const uint32_t& sentPacketsSize = 200000, const bool& enableLazyDecoding = true, const bool& enableLogSentPackets = false);

        //blocks current thread
        void DecodeAndSendAudio(const dpp::voiceconn* voice, const size_t& index = 0);

        void AddDecoder(const std::string_view& url, const uint32_t& sampleRate = Decoder::DEFAULT_SAMPLE_RATE, const size_t& pos = 0);
        void AddDecoderBack(const std::string_view& url, const uint32_t& sampleRate = Decoder::DEFAULT_SAMPLE_RATE);

        void DeleteAudio(const size_t& index = 0);
        void DeleteAllAudio();

        void Stop();
        void Pause(const bool& pause);
        void Skip();
        
        void SkipToSeconds(const float& seconds, const size_t& index = 0);
        void SkipSeconds(const float& seconds, const size_t& index = 0);

        void Reserve(const size_t& capacity);

    public:
        void SetAudioSampleRate(const uint32_t& sampleRate, const size_t& index = 0);

        void SetEnableLogSentPackets(const bool& enable);
        void SetSentPacketSize(const uint32_t& size);

        bool GetIsPaused() const noexcept;
        bool GetIsDecoding() const noexcept;

        bool GetEnableLogSentPackets() const noexcept;
        uint32_t GetSentPacketSize() const noexcept;

        size_t GetDecodersCount() const noexcept;

        float GetCurrentDecodingDurationSeconds() const noexcept;
        //if return is 0, then there are no decoders
        float GetTotalDurationSeconds(const size_t& index) const;

        std::string GetTitle(const size_t& index) const;

        bool HasDecoderFinished(const size_t& index = 0) const;

    private:
        void LazyDecodingCheck(const std::chrono::milliseconds& toWait, std::unique_lock<std::mutex>& pauseLock, const std::chrono::milliseconds& sleepFor = std::chrono::milliseconds(10));

    private:
        std::vector<Decoder> m_Decoders;

        uint32_t m_SentPacketSize;
        bool m_EnableLogSentPackets : 1;

        std::atomic_bool m_IsDecoding;
        std::atomic_bool m_IsSkippingFrames;
        std::atomic_bool m_IsChangingSampleRate;

        std::atomic_int m_PreviousSampleRate;

        std::atomic_bool m_IsPaused;
        std::condition_variable m_PauseCondition;
        std::mutex m_PauseMutex;

        std::atomic<float> m_CurrentDecodingDuration;
    };
}