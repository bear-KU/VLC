#ifndef READ_FRAME_HPP
#define READ_FRAME_HPP

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

// OpenCV
#include <opencv2/opencv.hpp>

bool open_video(const char* filename, AVFormatContext** fmt_ctx, int* video_stream_idx);


bool open_codec(AVFormatContext* fmt_ctx, int video_stream_idx,
                AVCodecContext** dec_ctx);

SwsContext* create_sws_context(AVCodecContext* dec_ctx);

void prepare_buffer(AVCodecContext* dec_ctx, cv::Mat& dest_mat);

static void convert_frame_to_mat(SwsContext* sws_ctx, AVCodecContext* dec_ctx, AVFrame* frame, cv::Mat& dest_mat);

bool read_frame(AVFormatContext* fmt_ctx, AVCodecContext* dec_ctx, int stream_idx,
                SwsContext* sws_ctx,  AVPacket* packet, AVFrame* frame, cv::Mat& dest_mat);

#endif
