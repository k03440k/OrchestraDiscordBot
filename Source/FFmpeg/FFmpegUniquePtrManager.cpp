#include "FFmpegUniquePtrManager.hpp"

namespace FSDB
{
	void FFmpegUniquePtrManager::FreeFormatContext(AVFormatContext* formatContext)
    {
        if(formatContext)
        {
            avformat_close_input(&formatContext);
            avformat_free_context(formatContext);
        }
    }
    void FFmpegUniquePtrManager::FreeAVCodecContext(AVCodecContext* codecContext)
    {
        if(codecContext)
            avcodec_free_context(&codecContext);
    }
    void FFmpegUniquePtrManager::FreeSwrContext(SwrContext* swrContext)
    {
        if(swrContext)
            swr_free(&swrContext);
    }
    void FFmpegUniquePtrManager::FreeAVPacket(AVPacket* packet)
    {
        if(packet)
            av_packet_free(&packet);
    }
    void FFmpegUniquePtrManager::FreeAVFrame(AVFrame* frame)
    {
        if(frame)
            av_frame_free(&frame);
    }
}