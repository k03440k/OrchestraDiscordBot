#pragma once

#include <map>
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
        static constexpr int DEFAULT_SAMPLE_RATE = 48000;
        static constexpr AVSampleFormat DEFAULT_OUT_SAMPLE_FORMAT = AV_SAMPLE_FMT_S16;
    public:
        Decoder();
        Decoder(const std::string_view& url, int outSampleRate = DEFAULT_SAMPLE_RATE, AVSampleFormat outSampleFormat = DEFAULT_OUT_SAMPLE_FORMAT);
        ~Decoder() = default;

        Decoder(const Decoder& other);
        Decoder& operator=(const Decoder& other);
        Decoder(Decoder&& other) noexcept = default;
        Decoder& operator=(Decoder&& other) noexcept = default;

        std::vector<uint8_t> DecodeAudioFrame() const;

        void SkipToTimestamp(int64_t timestamp) const;
        void SkipTimestamp(int64_t timestamp) const;
        void SkipToSeconds(float seconds) const;
        void SkipSeconds(float seconds) const;

        void ResetGraph();

        uint32_t FindStreamIndex(AVMediaType mediaType) const;

        bool AreThereFramesToProcess() const;

        void Reset();
        bool IsReady() const;

        //getters, setters
    public:
        void SetBassBoost( float decibelsBoost = 0.f,  float frequencyToAdjust = 0.f, float bandwidth = 0.f) const;

        void SetEqualizer(const std::string_view& args) const;
        void SetEqualizer(const std::map<float, float>& frequencies) const;

        void SetLimiter(float limit);

        int GetInitialSampleRate() const;
        AVSampleFormat GetInitialSampleFormat() const;

        //recreates m_SwrContext
        void SetOutSampleFormat(AVSampleFormat sampleFormat);
        //recreates m_SwrContext
        void SetOutSampleRate(int sampleRate);

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

        AVFilterContext* CreateFilterContext(const std::string_view& filterNameToFind, AVFilterContext* link = nullptr, const std::string_view& customName = "", const std::string_view& args = "") const;

    private:
        FFmpegUniquePtrManager::UniquePtrAVFormatContext m_FormatContext;
        FFmpegUniquePtrManager::UniquePtrAVCodecContext m_CodecContext;
        FFmpegUniquePtrManager::UniquePtrSwrContext m_SwrContext;
        FFmpegUniquePtrManager::UniquePtrAVPacket m_Packet;
        FFmpegUniquePtrManager::UniquePtrAVFrame m_Frame;

        FFmpegUniquePtrManager::UniquePtrAVFilterGraph m_FilterGraph;
        struct
        {
            AVFilterContext* bufferSource;
            AVFilterContext* bass;
            AVFilterContext* equalizer;
            AVFilterContext* limiter;
            AVFilterContext* bufferSink;
        } m_Filters { nullptr, nullptr, nullptr, nullptr, nullptr };

        int m_MaxBufferSize;

        uint32_t m_AudioStreamIndex;
        AVSampleFormat m_OutSampleFormat;
        int m_OutSampleRate;
    };
}