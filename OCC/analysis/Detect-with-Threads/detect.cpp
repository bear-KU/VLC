#include "tracker_thread.hpp"
#include <list> // std::listを使用
#include <atomic>

// --- メインスレッド側で使われる定数と変数 ---
const double DETECTION_THRESHOLD = 230.0;
const double MIN_CONTOUR_AREA = 10.0;
const double MAX_CONTOUR_AREA = 1000.0;
const int MISS_COUNT_FOR_DELETION = 30;
int tracker_id_counter = 0;

std::mutex boxes_mutex;

// 前方宣言
void mergeOverlappingBoxes(std::vector<cv::Rect>& boxes);

// スレッドプール用のクラス
class DetectorThreadPool
{
private:
    struct Task
    {
        cv::Rect roi;
        const cv::Mat* gray_ptr;
        std::vector<cv::Rect>* results_ptr;
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
            std::vector<cv::Rect> local_boxes;
            cv::Mat sub_gray = (*task.gray_ptr)(task.roi);

            cv::Mat thresh;
            cv::threshold(sub_gray, thresh, DETECTION_THRESHOLD, 255, cv::THRESH_BINARY);
            cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
            cv::morphologyEx(thresh, thresh, cv::MORPH_OPEN, kernel);
            cv::morphologyEx(thresh, thresh, cv::MORPH_CLOSE, kernel);

            std::vector<std::vector<cv::Point>> contours;
            cv::findContours(thresh, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
            
            for (const auto& contour : contours)
            {
                double area = cv::contourArea(contour);
                if (area >= MIN_CONTOUR_AREA && area <= MAX_CONTOUR_AREA)
                {
                    cv::Rect box = cv::boundingRect(contour);
                    box.x += task.roi.x;
                    box.y += task.roi.y;
                    local_boxes.push_back(box);
                }
            }

            // 結果をマージ
            if (!local_boxes.empty())
            {
                std::lock_guard<std::mutex> lock(boxes_mutex);
                task.results_ptr->insert(task.results_ptr->end(), local_boxes.begin(), local_boxes.end());
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
        worker_versions_.resize(num_threads_, 0);  // 全て0で初期化（task_version_も0から始まる）
        
        const int slice_height = image_rows / num_threads_;
        const int overlap = 20;

        // 各スレッドの担当範囲を計算
        for (int i = 0; i < num_threads_; ++i)
        {
            int y_start = i * slice_height;
            int y_end = (i == num_threads_ - 1) ? image_rows : y_start + slice_height;
            int roi_y_start = std::max(0, y_start - overlap);
            int roi_y_end = std::min(image_rows, y_end + overlap);
            
            tasks_[i].roi = cv::Rect(0, roi_y_start, image_cols, roi_y_end - roi_y_start);
        }

        // ワーカースレッドを起動
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

    std::vector<cv::Rect> detect(const cv::Mat& gray)
    {
        std::vector<cv::Rect> results;
        
        // タスクを設定
        {
            std::lock_guard<std::mutex> lock(mtx_);
            for (int i = 0; i < num_threads_; ++i)
            {
                tasks_[i].gray_ptr = &gray;
                tasks_[i].results_ptr = &results;
            }
            completed_count_ = 0;
            task_version_++;  // バージョンをインクリメント
        }

        // ワーカーを起動
        cv_start_.notify_all();

        // 完了を待つ
        {
            std::unique_lock<std::mutex> lock(mtx_);
            cv_done_.wait(lock, [this] { 
                return completed_count_.load() == num_threads_; 
            });
        }

        // 重複をマージ
        if (!results.empty())
        {
            mergeOverlappingBoxes(results);
        }

        return results;
    }
};

// 重なった領域をマージする関数
void mergeOverlappingBoxes(std::vector<cv::Rect>& boxes)
{
    if (boxes.empty())
    {
        return;
    }

    bool merged;
    do 
    {
        merged = false;
        for (size_t i = 0; i < boxes.size(); ++i)
        {
            for (size_t j = i + 1; j < boxes.size(); ++j)
            {
                // 2つの矩形の共通領域を計算
                cv::Rect intersection = boxes[i] & boxes[j];

                // 重なっている場合
                if (intersection.area() > 0)
                {
                    boxes[i] |= boxes[j];                    
                    boxes.erase(boxes.begin() + j);
                    --j;
                    
                    merged = true;
                }
            }
        }
    } while (merged);
}

// // フレームから明るい光源（LED）を検出する関数
// std::vector<cv::Rect> detectLightSource(const cv::Mat& gray)
// {
//     cv::Mat thresh;
//     cv::threshold(gray, thresh, DETECTION_THRESHOLD, 255, cv::THRESH_BINARY);
//     cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
//     cv::morphologyEx(thresh, thresh, cv::MORPH_OPEN, kernel);
//     cv::morphologyEx(thresh, thresh, cv::MORPH_CLOSE, kernel);

//     // cv::namedWindow("Debug - Threshold View", cv::WINDOW_NORMAL);
//     // cv::resizeWindow("Debug - Threshold View", 900, 1200);
//     // cv::imshow("Debug - Threshold View", thresh);

//     std::vector<std::vector<cv::Point>> contours;
//     cv::findContours(thresh, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    
//     std::vector<cv::Rect> boxes;
//     for (const auto& contour : contours)
//     {
//         double area = cv::contourArea(contour);
//         if (area >= MIN_CONTOUR_AREA && area <= MAX_CONTOUR_AREA)
//         {
//             boxes.push_back(cv::boundingRect(contour));
//         }
//     }
//     return boxes;
// }

int main(int argc, char *argv[])
{
    if (argc < 2) {
        std::cerr << "エラー: 動画ファイルのパスを指定してください．" << std::endl;
        return -1;
    }
    std::string video_path = argv[1];
    cv::VideoCapture cap(video_path);
    if (!cap.isOpened()) {
        std::cerr << "エラー: 動画ファイル '" << video_path << "' を開けませんでした．" << std::endl;
        return -1;
    }

    // cv::namedWindow("Detection View", cv::WINDOW_NORMAL);
    // cv::resizeWindow("Detection View", 900, 1200);
    
    // 最初のフレームを読んで画像サイズを取得
    cv::Mat first_frame;
    cap >> first_frame;
    if (first_frame.empty())
    {
        std::cerr << "エラー: 動画ファイルから最初のフレームを取得できませんでした．" << std::endl;
        return -1;
    }
    cv::flip(first_frame, first_frame, 0);
    
    // スレッドプールを初期化（画像サイズを基に担当範囲を決定）
    DetectorThreadPool detector_pool(first_frame.rows, first_frame.cols);
    
    // 最初のフレームに戻す
    cap.set(cv::CAP_PROP_POS_FRAMES, 0);
    
    auto start_time = std::chrono::high_resolution_clock::now();
    std::list<Tracker> activeTrackers;
    cv::Mat frame;

    while (true)
    {
        cap >> frame;
        cv::flip(frame, frame, 0); // 垂直反転
        if (frame.empty()) break;
        
        cv::Mat gray;
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
        cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0);

        std::vector<cv::Rect> detections = detector_pool.detect(gray);
        std::vector<bool> matched(detections.size(), false);

        for (auto& tracker : activeTrackers)
        {
            int best_index = -1;
            double best_dist = 15.0;
            for (size_t i = 0; i < detections.size(); ++i)
            {
                if (matched[i]) continue;
                cv::Point2f center(detections[i].x + detections[i].width/2.0, detections[i].y + detections[i].height/2.0);
                double dist = cv::norm(tracker.pos - center);
                if (dist < best_dist)
                {
                    best_dist = dist;
                    best_index = i;
                }
            }
           
            if (best_index != -1) 
            {
                tracker.pos = cv::Point2f(detections[best_index].x + detections[best_index].width/2.0, detections[best_index].y + detections[best_index].height/2.0);
                tracker.miss_count = 0;
                matched[best_index] = true;
                int x = std::clamp((int)tracker.pos.x, 0, gray.cols - 1);
                int y = std::clamp((int)tracker.pos.y, 0, gray.rows - 1);
                tracker.frame_queue.push({false, true, gray.at<uchar>(y, x)});
            }
            else
            {
                tracker.miss_count++;
                tracker.frame_queue.push({false, false, 0});
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
                new_tracker_ref.id = tracker_id_counter++;
                new_tracker_ref.pos = cv::Point2f(detections[i].x + detections[i].width/2.0, detections[i].y + detections[i].height/2.0);
                new_tracker_ref.start_time = std::chrono::high_resolution_clock::now(); // 開始時刻を記録
                new_tracker_ref.worker = std::thread(trackerThreadFunction, new_tracker_ref.id, &new_tracker_ref.frame_queue, &new_tracker_ref);
            }
        }
        
        for (const auto &tracker : activeTrackers)
        {
            cv::rectangle(frame, cv::Point(tracker.pos.x-15, tracker.pos.y-15), cv::Point(tracker.pos.x+15, tracker.pos.y+15), cv::Scalar(0, 255, 0), 2);
            cv::putText(frame, "Tracker " + std::to_string(tracker.id), cv::Point(tracker.pos.x-10, tracker.pos.y-20), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 0), 2);
        }

        // cv::imshow("Detection View", frame);
        if (cv::waitKey(1) == 27) break;
    }

    std::cout << "\n--- 動画ファイルの再生が終了しました。残りのトラッカーを終了します ---" << std::endl;
    activeTrackers.clear(); // listがclearされる際に各Trackerのデストラクタが呼ばれる

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;
    std::cout << "\n総解析時間: " << elapsed.count() << " 秒" << std::endl;

    cap.release();
    cv::destroyAllWindows();
    return 0;
}
