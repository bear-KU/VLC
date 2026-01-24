#include "tracker_thread.hpp"
#include "read_frame.hpp"
#include <list> // std::listを使用
#include <atomic>
#include <algorithm> // std::remove_ifのために追加

// --- メインスレッド側で使われる定数と変数 ---
const double DETECTION_THRESHOLD = 200.0;

// [MOD] 最終的に必要な面積（マージ後の判定用）
const double MIN_CONTOUR_AREA = 200.0;

// [ADD] スレッド内での仮フィルタ用（ノイズ除去用）
// 分割された「破片」も通過させるため、十分に小さく設定する
const double MIN_FRAGMENT_AREA = 50.0; 

const double MAX_CONTOUR_AREA = 100000.0;
const int MISS_COUNT_FOR_DELETION = 50; // トラッカー削除までの許容ミスフレーム数(調整必要)
int tracker_id_counter = 0;

std::mutex boxes_mutex;

// =======================
// [ADD] Detection 構造体
// =======================
struct Detection
{
    cv::Rect box;
    double area; // contour union area
    std::vector<std::vector<cv::Point>> contours;
};

// 前方宣言
void mergeOverlappingDetections(std::vector<Detection>& detections);

// =======================
// [ADD] contour 和集合面積
// =======================
double computeUnionContourArea(
    const std::vector<std::vector<cv::Point>>& contours)
{
    if (contours.empty()) return 0.0;

    // --- [FIX] 全 contour の点を 1 つにまとめる ---
    std::vector<cv::Point> all_points;
    for (const auto& c : contours)
    {
        all_points.insert(all_points.end(), c.begin(), c.end());
    }

    if (all_points.empty()) return 0.0;

    cv::Rect bbox = cv::boundingRect(all_points);
    cv::Mat mask = cv::Mat::zeros(bbox.height, bbox.width, CV_8UC1);

    for (const auto& c : contours)
    {
        std::vector<std::vector<cv::Point>> shifted{c};
        for (auto& p : shifted[0])
        {
            p -= bbox.tl();
        }
        cv::drawContours(mask, shifted, -1, cv::Scalar(255), cv::FILLED);
    }

    return static_cast<double>(cv::countNonZero(mask));
}


// スレッドプール用のクラス
class DetectorThreadPool
{
private:
    struct Task
    {
        cv::Rect roi;
        const cv::Mat* gray_ptr;
        std::vector<Detection>* results_ptr; // [MOD]
    };

    int num_threads_;
    std::vector<std::thread> workers_;
    std::vector<Task> tasks_;
    std::vector<int> worker_versions_;  // 各ワーカーが処理した最後のバージョン
    std::mutex mtx_;
    std::condition_variable cv_start_;
    std::condition_variable cv_done_;
    std::atomic<int> completed_count_{0};
    std::atomic<int> task_version_{0};  // タスクのバージョン番号
    bool terminate_{false};

    void worker_loop(int thread_id)
    {
        while (true)
        {
            Task task;
            int current_version;
            
            // 仕事を待つ
            {
                std::unique_lock<std::mutex> lock(mtx_);
                cv_start_.wait(lock, [this, thread_id] { 
                    return task_version_.load() > worker_versions_[thread_id] || terminate_; 
                });
                
                if (terminate_) return;
                
                current_version = task_version_.load();
                task = tasks_[thread_id];
            }

            // 検出処理（ロックなし）
            std::vector<Detection> local_detections;
            cv::Mat sub_gray = (*task.gray_ptr)(task.roi);

            cv::Mat thresh;
            cv::threshold(sub_gray, thresh, DETECTION_THRESHOLD, 255, cv::THRESH_BINARY);
            cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
            cv::morphologyEx(thresh, thresh, cv::MORPH_OPEN, kernel);
            cv::morphologyEx(thresh, thresh, cv::MORPH_CLOSE, kernel);

            std::vector<std::vector<cv::Point>> contours;
            cv::findContours(thresh, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
            
            // オフセット座標（ROIの左上座標）
            cv::Point offset_point(task.roi.x, task.roi.y);

            for (auto& contour : contours)
            {
                double area = cv::contourArea(contour);
                
                // [MOD] 分割された破片も拾うため、ここでは小さい閾値(MIN_FRAGMENT_AREA)を使用
                if (area >= MIN_FRAGMENT_AREA && area <= MAX_CONTOUR_AREA)
                {
                    Detection det;

                    // [FIX] 輪郭の座標を「グローバル座標」に変換して保存する
                    // これを行わないと、マージ後に形状が壊れ、面積が正しく計算されません
                    for (auto& p : contour)
                    {
                        p += offset_point;
                    }
                    det.contours.push_back(contour);

                    // バウンディングボックスも変換後のcontourから計算（グローバル座標になる）
                    det.box = cv::boundingRect(contour);
                    
                    det.area = area;
                    local_detections.push_back(det);
                }
            }

            // 結果をマージ
            if (!local_detections.empty())
            {
                std::lock_guard<std::mutex> lock(boxes_mutex);
                task.results_ptr->insert(
                    task.results_ptr->end(),
                    local_detections.begin(),
                    local_detections.end());
            }

            // 完了を記録
            {
                std::lock_guard<std::mutex> lock(mtx_);
                worker_versions_[thread_id] = current_version;
            }
            
            int count = completed_count_.fetch_add(1) + 1;
            if (count == num_threads_)
            {
                std::lock_guard<std::mutex> lock(mtx_);
                cv_done_.notify_one();
            }
        }
    }

public:
    DetectorThreadPool(int image_rows, int image_cols)
    {
        num_threads_ = std::thread::hardware_concurrency();

        tasks_.resize(num_threads_);
        worker_versions_.resize(num_threads_, 0);
        
        const int slice_height = image_rows / num_threads_;
        const int overlap = 20;

        for (int i = 0; i < num_threads_; ++i)
        {
            int y_start = i * slice_height;
            int y_end = (i == num_threads_ - 1) ? image_rows : y_start + slice_height;
            int roi_y_start = std::max(0, y_start - overlap);
            int roi_y_end = std::min(image_rows, y_end + overlap);
            
            tasks_[i].roi = cv::Rect(0, roi_y_start, image_cols, roi_y_end - roi_y_start);
        }

        for (int i = 0; i < num_threads_; ++i)
        {
            workers_.emplace_back(&DetectorThreadPool::worker_loop, this, i);
        }
    }

    ~DetectorThreadPool()
    {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            terminate_ = true;
        }
        cv_start_.notify_all();
        
        for (auto& worker : workers_)
        {
            if (worker.joinable()) worker.join();
        }
    }

    std::vector<Detection> detect(const cv::Mat& gray)
    {
        std::vector<Detection> results;
        
        {
            std::lock_guard<std::mutex> lock(mtx_);
            for (int i = 0; i < num_threads_; ++i)
            {
                tasks_[i].gray_ptr = &gray;
                tasks_[i].results_ptr = &results;
            }
            completed_count_ = 0;
            task_version_++;
        }

        cv_start_.notify_all();

        {
            std::unique_lock<std::mutex> lock(mtx_);
            cv_done_.wait(lock, [this] { 
                return completed_count_.load() == num_threads_; 
            });
        }

        if (!results.empty())
        {
            // 1. 重なっている部分を結合（ここで面積も再計算される）
            mergeOverlappingDetections(results);

            // 2. [ADD] 結合後の面積が MIN_CONTOUR_AREA 未満のものを削除
            // これにより、分割されていたが合体して十分な大きさになったものは残り、
            // 合体しても小さいゴミはここで削除される。
            auto it = std::remove_if(results.begin(), results.end(),
                [](const Detection& d) {
                    return d.area < MIN_CONTOUR_AREA;
                });
            results.erase(it, results.end());
        }

        return results;
    }
};

// =======================
// [MOD] Detection 結合
// =======================
void mergeOverlappingDetections(std::vector<Detection>& detections)
{
    bool merged;
    do
    {
        merged = false;
        for (size_t i = 0; i < detections.size(); ++i)
        {
            for (size_t j = i + 1; j < detections.size(); ++j)
            {
                if ((detections[i].box & detections[j].box).area() > 0)
                {
                    detections[i].box |= detections[j].box;

                    detections[i].contours.insert(
                        detections[i].contours.end(),
                        detections[j].contours.begin(),
                        detections[j].contours.end());

                    detections[i].area =
                        computeUnionContourArea(detections[i].contours);

                    detections.erase(detections.begin() + j);
                    merged = true;
                    break;
                }
            }
            if (merged) break;
        }
    } while (merged);
}


int main(int argc, char *argv[])
{
    if (argc < 2) {
        std::cerr << "エラー: 動画ファイルのパスを指定してください．" << std::endl;
        return -1;
    }

    const char* video_path = argv[1];

    AVFormatContext *fmt_ctx = nullptr;
    int video_stream_idx = -1;
    AVCodecContext *dec_ctx = nullptr;

    if (!open_video(video_path, &fmt_ctx, &video_stream_idx)) {
        return -1;
    }

    if (!open_codec(fmt_ctx, video_stream_idx, &dec_ctx)) {
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    if (!packet || !frame) {
        std::cerr << "Failed to allocate packet or frame\n";
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    // フレーム用のメモリの確保
    // 縦 * 横 * RGB だけ
    cv::Mat cv_frame;
    prepare_buffer(dec_ctx, cv_frame);
    
    // YUV -> BGR変換(OpenCV は BGR フォーマットを使用)の準備
    // 幅・高さは変更しないため元サイズで指定
    SwsContext *sws_ctx = create_sws_context(dec_ctx);
    if (!sws_ctx) {
        std::cerr << "Failed to create SwsContext\n";
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return -1;
    }

#ifdef ENABLE_GUI
    // cv::namedWindow("gray", cv::WINDOW_NORMAL);
    // cv::resizeWindow("gray", 900, 1200);
    cv::namedWindow("Detection View", cv::WINDOW_NORMAL);
    cv::resizeWindow("Detection View", 900, 1200);
#endif
        
    // スレッドプールを初期化（画像サイズを基に担当範囲を決定）
    DetectorThreadPool detector_pool(dec_ctx->height, dec_ctx->width);
        
    std::list<Tracker> activeTrackers;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    /***********************************************************/
    auto getting_frame_sum = 0.0;
    auto detection_sum = 0.0;
    auto tracking_sum = 0.0;
    auto drawing_sum = 0.0;
    auto wait_sum = 0.0;

    std::chrono::high_resolution_clock::time_point getting_frame_start;
    std::chrono::high_resolution_clock::time_point getting_frame_end;
    std::chrono::high_resolution_clock::time_point detection_start;
    std::chrono::high_resolution_clock::time_point detection_end;
    std::chrono::high_resolution_clock::time_point tracking_start;
    std::chrono::high_resolution_clock::time_point tracking_end;
    std::chrono::high_resolution_clock::time_point drawing_start;
    std::chrono::high_resolution_clock::time_point drawing_end;
    std::chrono::high_resolution_clock::time_point wait_start;
    std::chrono::high_resolution_clock::time_point wait_end;
    /***********************************************************/

    bool ret;
    int frame_count = 0;
    while (true)
    {
        /***********************************************************/
        getting_frame_start = std::chrono::high_resolution_clock::now();
        /***********************************************************/

        ret = read_frame(fmt_ctx, dec_ctx, video_stream_idx, sws_ctx, packet, frame, cv_frame);
        if (!ret) {
            break; // 映像終了またはエラー
        }
        
        /***********************************************************/
        getting_frame_end = std::chrono::high_resolution_clock::now();
        getting_frame_sum += std::chrono::duration_cast<std::chrono::duration<double>>(getting_frame_end - getting_frame_start).count();
        detection_start = std::chrono::high_resolution_clock::now();
        /***********************************************************/

        cv::Mat gray;
        cv::cvtColor(cv_frame, gray, cv::COLOR_BGR2GRAY);
        cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0);

        // // DEBUG: 二値画像描画
        // cv::Mat thresh;
        // cv::threshold(gray, thresh, DETECTION_THRESHOLD, 255, cv::THRESH_BINARY);
        // cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
        // cv::morphologyEx(thresh, thresh, cv::MORPH_OPEN, kernel);
        // cv::morphologyEx(thresh, thresh, cv::MORPH_CLOSE, kernel);
        // cv::imshow("gray", thresh);

        std::vector<Detection> detections = detector_pool.detect(gray);
        std::vector<bool> matched(detections.size(), false);

        /***********************************************************/
        detection_end = std::chrono::high_resolution_clock::now();
        detection_sum += std::chrono::duration_cast<std::chrono::duration<double>>(detection_end - detection_start).count();
        tracking_start = std::chrono::high_resolution_clock::now();
        /***********************************************************/

        for (auto& tracker : activeTrackers)
        {
            int best_index = -1;
            double best_dist = 15.0;
            for (size_t i = 0; i < detections.size(); ++i)
            {
                if (matched[i]) continue;
                cv::Point2f center(detections[i].box.x + detections[i].box.width/2.0, detections[i].box.y + detections[i].box.height/2.0);
                double dist = cv::norm(tracker.pos - center);
                if (dist < best_dist)
                {
                    best_dist = dist;
                    best_index = i;
                }
            }
           
            if (best_index != -1) 
            {
                tracker.pos = cv::Point2f(detections[best_index].box.x + detections[best_index].box.width/2.0, detections[best_index].box.y + detections[best_index].box.height/2.0);
                
                tracker.size = detections[best_index].box.size();

                tracker.miss_count = 0;
                matched[best_index] = true;
                int x = std::clamp((int)tracker.pos.x, 0, gray.cols - 1);
                int y = std::clamp((int)tracker.pos.y, 0, gray.rows - 1);

                double area = detections[best_index].area;

                tracker.frame_queue.push({false, true, gray.at<uchar>(y, x), tracker.pos, area});
            }
            else
            {
                tracker.miss_count++;
                tracker.frame_queue.push({false, false, 0, tracker.pos, 0.0});
            }
        }

        activeTrackers.remove_if([](Tracker& t)
        {
            if (t.miss_count > MISS_COUNT_FOR_DELETION)
            {
                // デストラクタがjoinを処理するので、ここではシグナルを送るだけでよい
                t.frame_queue.push({true});
                return true;
            }
            return false;
        });

        for (size_t i = 0; i < detections.size(); ++i)
        {
            if (!matched[i])
            {
                activeTrackers.emplace_back(); // デフォルトコンストラクタで追加
                Tracker& new_tracker_ref = activeTrackers.back();
                new_tracker_ref.start_time = std::chrono::high_resolution_clock::now(); // 開始時刻を記録
                new_tracker_ref.id = tracker_id_counter++;
                new_tracker_ref.pos = cv::Point2f(detections[i].box.x + detections[i].box.width/2.0, detections[i].box.y + detections[i].box.height/2.0);

                new_tracker_ref.size = detections[i].box.size();

                new_tracker_ref.worker = std::thread(trackerThreadFunction, new_tracker_ref.id, &new_tracker_ref.frame_queue, &new_tracker_ref);
            }
        }
        
        /***********************************************************/
        tracking_end = std::chrono::high_resolution_clock::now();
        tracking_sum += std::chrono::duration_cast<std::chrono::duration<double>>(tracking_end - tracking_start).count();
        drawing_start = std::chrono::high_resolution_clock::now();
        // /***********************************************************/

#ifdef ENABLE_GUI
        cv::Mat display_frame;
        cv_frame.copyTo(display_frame);

        // for (const auto &tracker : activeTrackers)
        // {
        //     // int cx = static_cast<int>(std::round(tracker.pos.x));
        //     // int cy = static_cast<int>(std::round(tracker.pos.y));
        //     // cv::rectangle(display_frame, cv::Point(tracker.pos.x-15, tracker.pos.y-15), cv::Point(tracker.pos.x+15, tracker.pos.y+15), cv::Scalar(0, 255, 0), 2);

        //     cv::Point2f top_left = tracker.pos - cv::Point2f(tracker.size.width/2.0f, tracker.size.height/2.0f);
        //     cv::Rect rect(static_cast<int>(top_left.x), static_cast<int>(top_left.y), tracker.size.width, tracker.size.height);
        //     cv::rectangle(display_frame, rect, cv::Scalar(0, 255, 0), 2);

        //     cv::putText(display_frame, "Tracker " + std::to_string(tracker.id), cv::Point(tracker.pos.x-10, tracker.pos.y-20), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 0), 2);
        // }

        for (const auto &tracker : activeTrackers)
        {
            cv::Point2f top_left = tracker.pos - cv::Point2f(tracker.size.width / 2.0f, tracker.size.height / 2.0f);
            
            cv::Rect rect(
                static_cast<int>(top_left.x), 
                static_cast<int>(top_left.y), 
                tracker.size.width, 
                tracker.size.height
            );

            cv::rectangle(display_frame, rect, cv::Scalar(0, 255, 0), 2);

            cv::putText(display_frame, "Tracker " + std::to_string(tracker.id), 
                        cv::Point(rect.x, rect.y - 5), 
                        cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 0), 2);
        }

#endif
        // /***********************************************************/
        drawing_end = std::chrono::high_resolution_clock::now();
        drawing_sum += std::chrono::duration_cast<std::chrono::duration<double>>(drawing_end - drawing_start).count();
        /***********************************************************/

        
        wait_start = std::chrono::high_resolution_clock::now();

#ifdef ENABLE_GUI
        // cv::rotate(display_frame, display_frame, cv::ROTATE_90_CLOCKWISE);

        cv::imshow("Detection View", display_frame);
        if (cv::waitKey(1) == 27) break;
#endif

        wait_end = std::chrono::high_resolution_clock::now();
        wait_sum += std::chrono::duration_cast<std::chrono::duration<double>>(wait_end - wait_start).count();
        
        frame_count++;
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;

    std::cout << "\n--- 動画ファイルの再生が終了しました。残りのトラッカーを終了します ---" << std::endl;
    activeTrackers.clear(); // listがclearされる際に各Trackerのデストラクタが呼ばれる
    std::cout << "\n総解析時間: " << elapsed.count() << " 秒" << std::endl;

    /***********************************************************/
    std::cout << "総フレーム数: " << frame_count << std::endl;
    std::cout << "フレーム取得時間合計: " << getting_frame_sum << " 秒" << std::endl;
    std::cout << "検出処理時間合計: " << detection_sum << " 秒" << std::endl;
    std::cout << "追跡処理時間合計: " << tracking_sum << " 秒" << std::endl;
    std::cout << "描画処理時間合計: " << drawing_sum << " 秒" << std::endl;
    std::cout << "待機時間合計: " << wait_sum << " 秒" << std::endl;
    std::cout << "合計時間: " << (getting_frame_sum + detection_sum + tracking_sum) << " 秒" << std::endl;
    /***********************************************************/

    cv::destroyAllWindows();
    return 0;
}
