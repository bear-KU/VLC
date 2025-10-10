#include "tracker_thread.hpp"
#include <list> // std::listを使用

// --- メインスレッド側で使われる定数と変数 ---
const double DETECTION_THRESHOLD = 230.0;
const double MIN_CONTOUR_AREA = 10.0;
const double MAX_CONTOUR_AREA = 1000.0;
const int MISS_COUNT_FOR_DELETION = 30;
int tracker_id_counter = 0;

// フレームから明るい光源（LED）を検出する関数
std::vector<cv::Rect> detectLightSource(const cv::Mat& gray)
{
    cv::Mat thresh;
    cv::threshold(gray, thresh, DETECTION_THRESHOLD, 255, cv::THRESH_BINARY);
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
    cv::morphologyEx(thresh, thresh, cv::MORPH_OPEN, kernel);
    cv::morphologyEx(thresh, thresh, cv::MORPH_CLOSE, kernel);

    // cv::namedWindow("Debug - Threshold View", cv::WINDOW_NORMAL);
    // cv::resizeWindow("Debug - Threshold View", 900, 1200);
    // cv::imshow("Debug - Threshold View", thresh);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(thresh, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    
    std::vector<cv::Rect> boxes;
    for (const auto& contour : contours)
    {
        double area = cv::contourArea(contour);
        if (area >= MIN_CONTOUR_AREA && area <= MAX_CONTOUR_AREA)
        {
            boxes.push_back(cv::boundingRect(contour));
        }
    }
    return boxes;
}

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

    cv::namedWindow("Detection View", cv::WINDOW_NORMAL);
    cv::resizeWindow("Detection View", 900, 1200);
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

        std::vector<cv::Rect> detections = detectLightSource(gray);
        std::vector<bool> matched(detections.size(), false);

        for (auto& tracker : activeTrackers) {
            int best_index = -1;
            double best_dist = 15.0;
            for (size_t i = 0; i < detections.size(); ++i) {
                if (matched[i]) continue;
                cv::Point2f center(detections[i].x + detections[i].width/2.0, detections[i].y + detections[i].height/2.0);
                double dist = cv::norm(tracker.pos - center);
                if (dist < best_dist) {
                    best_dist = dist;
                    best_index = i;
                }
            }
           
            if (best_index != -1) {
                tracker.pos = cv::Point2f(detections[best_index].x + detections[best_index].width/2.0, detections[best_index].y + detections[best_index].height/2.0);
                tracker.miss_count = 0;
                matched[best_index] = true;
                int x = std::clamp((int)tracker.pos.x, 0, gray.cols - 1);
                int y = std::clamp((int)tracker.pos.y, 0, gray.rows - 1);
                tracker.frame_queue.push({false, true, gray.at<uchar>(y, x)});
            } else {
                tracker.miss_count++;
                tracker.frame_queue.push({false, false, 0});
            }
        }

        activeTrackers.remove_if([](Tracker& t) {
            if (t.miss_count > MISS_COUNT_FOR_DELETION) {
                // デストラクタがjoinを処理するので、ここではシグナルを送るだけでよい
                t.frame_queue.push({true});
                return true;
            }
            return false;
        });

        for (size_t i = 0; i < detections.size(); ++i) {
            if (!matched[i]) {
                activeTrackers.emplace_back(); // デフォルトコンストラクタで追加
                Tracker& new_tracker_ref = activeTrackers.back();
                new_tracker_ref.id = tracker_id_counter++;
                new_tracker_ref.pos = cv::Point2f(detections[i].x + detections[i].width/2.0, detections[i].y + detections[i].height/2.0);
                new_tracker_ref.worker = std::thread(trackerThreadFunction, new_tracker_ref.id, &new_tracker_ref.frame_queue);
            }
        }
        
        for (const auto &tracker : activeTrackers) {
            cv::rectangle(frame, cv::Point(tracker.pos.x-15, tracker.pos.y-15), cv::Point(tracker.pos.x+15, tracker.pos.y+15), cv::Scalar(0, 255, 0), 2);
            cv::putText(frame, "Tracker " + std::to_string(tracker.id), cv::Point(tracker.pos.x-10, tracker.pos.y-20), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 0), 2);
        }

        cv::imshow("Detection View", frame);
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
