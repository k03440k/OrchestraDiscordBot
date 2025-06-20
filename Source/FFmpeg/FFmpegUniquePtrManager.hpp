#pragma once

#include <memory>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libswresample/swresample.h>
#include <libavfilter/avfilter.h>
}
namespace Orchestra
{
    class FFmpegUniquePtrManager
    {
    public:
        FFmpegUniquePtrManager() = delete;
        FFmpegUniquePtrManager(const FFmpegUniquePtrManager&) = delete;
        FFmpegUniquePtrManager(FFmpegUniquePtrManager&&) = delete;
        FFmpegUniquePtrManager& operator=(const FFmpegUniquePtrManager&) = delete;
        FFmpegUniquePtrManager& operator=(FFmpegUniquePtrManager&&) = delete;
        ~FFmpegUniquePtrManager() = delete;

    public:
        using UniquePtrAVFormatContext = std::unique_ptr<AVFormatContext, void(*)(AVFormatContext*)>;
        using UniquePtrAVCodecContext = std::unique_ptr<AVCodecContext, void(*)(AVCodecContext*)>;
        using UniquePtrSwrContext = std::unique_ptr<SwrContext, void(*)(SwrContext*)>;
        using UniquePtrAVPacket = std::unique_ptr<AVPacket, void(*)(AVPacket*)>;
        using UniquePtrAVFrame = std::unique_ptr<AVFrame, void(*)(AVFrame*)>;
        using UniquePtrAVFilter = std::unique_ptr<AVFilter, void(*)(AVFilter*)>;
        using UniquePtrAVFilterGraph = std::unique_ptr<AVFilterGraph, void(*)(AVFilterGraph*)>;

        static void FreeFormatContext(AVFormatContext* formatContext);
        static void FreeAVCodecContext(AVCodecContext* codecContext);
        static void FreeSwrContext(SwrContext* swrContext);
        static void FreeAVPacket(AVPacket* packet);
        static void FreeAVFrame(AVFrame* frame);
        static void FreeAVFilterGraph(AVFilterGraph* filterGraph);
    };
}