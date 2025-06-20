#include "FFmpegUniquePtrManager.hpp"

namespace Orchestra
{
    void FFmpegUniquePtrManager::FreeFormatContext(AVFormatContext* formatContext)
    {
        avformat_close_input(&formatContext);
        avformat_free_context(formatContext);
    }
    void FFmpegUniquePtrManager::FreeAVCodecContext(AVCodecContext* codecContext)
    {
        avcodec_free_context(&codecContext);
    }
    void FFmpegUniquePtrManager::FreeSwrContext(SwrContext* swrContext)
    {
        swr_free(&swrContext);
    }
    void FFmpegUniquePtrManager::FreeAVPacket(AVPacket* packet)
    {
        av_packet_free(&packet);
    }
    void FFmpegUniquePtrManager::FreeAVFrame(AVFrame* frame)
    {
        av_frame_free(&frame);
    }
    void FFmpegUniquePtrManager::FreeAVFilterGraph(AVFilterGraph* filterGraph)
    {
        avfilter_graph_free(&filterGraph);
    }
}
