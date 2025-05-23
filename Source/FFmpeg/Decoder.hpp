#pragma once

#include <string>
#include <string_view>
#include <vector>

extern "C"
{
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include "FFmpegUniquePtrManager.hpp"

namespace Orchestra
{
    class Decoder
    {
    public:
        static constexpr uint32_t DEFAULT_SAMPLE_RATE = 48000;
        static constexpr AVSampleFormat DEFAULT_OUT_SAMPLE_FORMAT = AV_SAMPLE_FMT_S16;
    public:
        Decoder(const std::string_view& url, const AVSampleFormat& outSampleFormat = DEFAULT_OUT_SAMPLE_FORMAT, const uint32_t& outSampleRate = DEFAULT_SAMPLE_RATE);
        ~Decoder() = default;

        Decoder(const Decoder& other);
        Decoder& operator=(const Decoder& other);
        Decoder(Decoder&& other) noexcept = default;
        Decoder& operator=(Decoder&& other) noexcept = default;

        std::vector<uint8_t> DecodeAudioFrame() const;

        void SkipToTimestamp(const int64_t& timestamp) const;
        void SkipTimestamp(const int64_t& timestamp) const;
        void SkipToSeconds(const float& seconds) const;
        void SkipSeconds(const float& seconds) const;

        uint32_t FindStreamIndex(const AVMediaType& mediaType) const;

        bool AreThereFramesToProcess() const;

        //getters, setters
    public:
        int GetInitialSampleRate() const;
        AVSampleFormat GetInitialSampleFormat() const;

        //recreates m_SwrContext
        void SetOutSampleFormat(const AVSampleFormat& sampleFormat);
        //recreates m_SwrContext
        void SetOutSampleRate(const uint32_t& sampleRate);

        AVSampleFormat GetOutSampleFormat() const;
        int GetOutSampleRate() const;
        
        int64_t GetTotalDuration() const;
        float GetTotalDurationSeconds() const;

        int GetMaxBufferSize() const;

        int64_t GetCurrentTimestamp() const;

        double GetTimestampToSecondsRatio() const;

        //metadata
        std::string GetTitle() const;
    private:
        static void CopySwrParams(SwrContext* from, SwrContext* to);
        static SwrContext* DuplicateSwrContext(SwrContext* from);

        AVStream* GetStream() const;
    private:
        FFmpegUniquePtrManager::UniquePtrAVFormatContext m_FormatContext;
        FFmpegUniquePtrManager::UniquePtrAVCodecContext m_CodecContext;
        FFmpegUniquePtrManager::UniquePtrSwrContext m_SwrContext;
        FFmpegUniquePtrManager::UniquePtrAVPacket m_Packet;
        FFmpegUniquePtrManager::UniquePtrAVFrame m_Frame;

        int m_MaxBufferSize;

        uint32_t m_AudioStreamIndex;
        AVSampleFormat m_OutSampleFormat;
        int m_OutSampleRate;
    };
}