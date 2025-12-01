#include <iostream>
#include "read_frame.hpp"

// 映像ファイルから映像部分用のデコーダを準備
// cv::VideoCapture cap("video.mp4") に相当
bool open_video(const char* filename, AVFormatContext** fmt_ctx, int* video_stream_idx)
{
    *fmt_ctx = avformat_alloc_context();
    if (!*fmt_ctx) {
        std::cerr << "Failed to allocate AVFormatContext\n";
        return false;
    }

    if (avformat_open_input(fmt_ctx, filename, nullptr, nullptr) < 0) {
        std::cerr << "Could not open input file\n";
        return false;
    }

    if (avformat_find_stream_info(*fmt_ctx, nullptr) < 0) {
        std::cerr << "Failed to find stream info\n";
        return false;
    }

    *video_stream_idx = av_find_best_stream(*fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (*video_stream_idx < 0) {
        std::cerr << "Failed to find video stream\n";
        return false;
    }

    return true;
}

// 映像ストリーム用のデコーダを準備
bool open_codec(AVFormatContext* fmt_ctx, int video_stream_idx, AVCodecContext** dec_ctx)
{
    AVStream* stream = fmt_ctx->streams[video_stream_idx];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);

    if (!codec) {
        std::cerr << "Failed to find codec\n";
        return false;
    }

    *dec_ctx = avcodec_alloc_context3(codec);
    if (!*dec_ctx) {
        std::cerr << "Failed to allocate AVCodecContext\n";
        return false;
    }

    if (avcodec_parameters_to_context(*dec_ctx, stream->codecpar) < 0) {
        std::cerr << "Failed to copy codec parameters\n";
        return false;
    }

            
    // コーデックの能力に応じてスレッドタイプを設定
    (*dec_ctx)->thread_count = 0; // 0 = CPUコア数に応じて自動設定

    if (codec->capabilities & AV_CODEC_CAP_FRAME_THREADS) {
        std::cout << "Using frame threading." << std::endl;
        (*dec_ctx)->thread_type = FF_THREAD_FRAME; // フレーム並列（最も効果が高い）
    } else if (codec->capabilities & AV_CODEC_CAP_SLICE_THREADS) {
        std::cout << "Using slice threading." << std::endl;
        (*dec_ctx)->thread_type = FF_THREAD_SLICE; // スライス並列
    }

    if (avcodec_open2(*dec_ctx, codec, nullptr) < 0) {
        std::cerr << "Failed to open codec\n";
        return false;
    }

    return true;
}

SwsContext* create_sws_context(AVCodecContext* dec_ctx) {
    return sws_getContext(
        dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
        dec_ctx->width, dec_ctx->height, AV_PIX_FMT_BGR24,
        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr
    );
}

void prepare_buffer(AVCodecContext* dec_ctx, cv::Mat& dest_mat) {
    dest_mat.create(dec_ctx->height, dec_ctx->width, CV_8UC3);
}

static void convert_frame_to_mat(SwsContext* sws_ctx, AVCodecContext* dec_ctx, AVFrame* frame, cv::Mat& dest_mat) {
    uint8_t* dest_data[4] = { dest_mat.data, nullptr, nullptr, nullptr };
    int dest_linesize[4] = { (int)dest_mat.step[0], 0, 0, 0 };
    
    sws_scale(
        sws_ctx,
        frame->data, frame->linesize, 0, dec_ctx->height,
        dest_data, dest_linesize
    );
}

bool read_frame(AVFormatContext* fmt_ctx, AVCodecContext* dec_ctx, int stream_idx,
                SwsContext* sws_ctx, AVPacket* packet, AVFrame* frame, cv::Mat& dest_mat)
{
    int ret;

    // 1. デコーダ内部に既にバッファされているフレームがあるか確認
    ret = avcodec_receive_frame(dec_ctx, frame);
    if (ret == 0) {
        convert_frame_to_mat(sws_ctx, dec_ctx, frame, dest_mat);
        av_frame_unref(frame);
        return true; // フレーム取得成功
    }

    // 2. 新しいパケットを読み込んでフレームを取得するループ
    while (av_read_frame(fmt_ctx, packet) >= 0) {
        if (packet->stream_index == stream_idx) {
            
            // パケットをデコーダへ送信
            ret = avcodec_send_packet(dec_ctx, packet);
            av_packet_unref(packet); // 送信後はパケットデータを解放

            if (ret < 0) {
                continue; // エラー時は次のパケットへ
            }

            // フレームの受け取りを試行
            ret = avcodec_receive_frame(dec_ctx, frame);
            if (ret == 0) {
                convert_frame_to_mat(sws_ctx, dec_ctx, frame, dest_mat);
                av_frame_unref(frame);
                return true; // フレーム取得成功
            }
            // EAGAINの場合は「もっとパケットが必要」なのでループ継続
        } else {
            av_packet_unref(packet); // 対象外ストリームも解放が必要
        }
    }

    // 3. ファイル終端(EOF)後のフラッシュ処理
    //    デコーダに残っている最後のフレームを取り出す
    avcodec_send_packet(dec_ctx, nullptr); // EOF通知
    ret = avcodec_receive_frame(dec_ctx, frame);
    if (ret == 0) {
        convert_frame_to_mat(sws_ctx, dec_ctx, frame, dest_mat);
        av_frame_unref(frame);
        return true; // 残存フレーム取得成功
    }

    return false; // 完全に終了
}
