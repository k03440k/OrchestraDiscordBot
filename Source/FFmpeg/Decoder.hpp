#pragma once

#include <string_view>
#include <vector>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include "FFmpegUniquePtrManager.hpp"

namespace Orchestra
{
    class Decoder
    {
    public:
        static constexpr uint32_t DEFAULT_SAMPLE_RATE = 48000;
    public:
        Decoder(const std::string_view& url, const AVSampleFormat& outSampleFormat, const uint32_t& outSampleRate = DEFAULT_SAMPLE_RATE);
        ~Decoder() = default;

        Decoder(const Decoder& other);
        Decoder& operator=(const Decoder& other);
        Decoder(Decoder&& other) noexcept = default;
        Decoder& operator=(Decoder&& other) noexcept = default;

        std::vector<uint8_t> DecodeAudioFrame() const;
        uint32_t FindStreamIndex(const AVMediaType& mediaType) const;

        void SetSampleFormat(const AVSampleFormat& sampleFormat);
        void SetSampleRate(const uint32_t& sampleRate);

        bool AreThereFramesToProcess() const;
        //in seconds
        int64_t GetDuration() const;
        int GetMaxBufferSize() const;

    private:
        static void CopySwrParams(SwrContext* from, SwrContext* to);
        static SwrContext* DuplicateSwrContext(SwrContext* from);

    private:
        FFmpegUniquePtrManager::UniquePtrAVFormatContext m_FormatContext;
        FFmpegUniquePtrManager::UniquePtrAVCodecContext m_CodecContext;
        FFmpegUniquePtrManager::UniquePtrSwrContext m_SwrContext;
        FFmpegUniquePtrManager::UniquePtrAVPacket m_Packet;
        FFmpegUniquePtrManager::UniquePtrAVFrame m_Frame;

        uint32_t m_AudioStreamIndex;
        AVSampleFormat m_SampleFormat;
        uint32_t m_SampleRate;
    };
}