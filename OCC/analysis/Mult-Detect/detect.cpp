#include <iostream>
#include <vector>
#include <string>
#include <bitset>

#include <opencv2/opencv.hpp>
#include <opencv2/tracking.hpp>
#include <opencv2/tracking/tracking_legacy.hpp>


struct Tracker
{
    int id;          // トラッカーID
    cv::Point2f pos; // 現在の位置
    int miss_count = 0;  // 連続して検出されなかったフレーム数(消灯用)

    std::vector<std::pair<bool, int>> states;  // LEDのON/OFF状態の履歴
    bool last_state_is_on = false; // 最後の状態
    int state_counter = 0;         // 現在の状態が続いているフレーム数
    bool finished = false;         // デコードが完了したかどうか
};

struct DecodeResult
{
    std::string bits;  // デコードされたビット列
    std::string ascii; // ASCIIに変換された文字列
};


// バイナリ文字列をASCIIに変換する関数
std::string binaryToAscii(const std::string& binary)
{
    std::string result;
    if (binary.size() % 8 != 0 && !binary.empty()) { // 空の場合は無視
         // デバッグ用に不完全なビットも表示
        std::cerr << "警告: 8の倍数でないバイナリ文字列です: " << binary << std::endl;
        size_t bits_to_complete = 8 - (binary.size() % 8);
        std::string padded_binary = binary + std::string(bits_to_complete, '0');
        std::cerr << "0でパディングして処理を続行します: " << padded_binary << std::endl;
        
        for (size_t i = 0; i < padded_binary.size(); i += 8) {
            std::bitset<8> bits(padded_binary.substr(i, 8));
            result += static_cast<char>(bits.to_ulong());
        }
    } else {
        for (size_t i = 0; i < binary.size(); i += 8) {
            std::bitset<8> bits(binary.substr(i, 8));
            result += static_cast<char>(bits.to_ulong());
        }
    }
    return result;
}

DecodeResult decodeFromStates(const std::vector<std::pair<bool,int>>& states)
{
    DecodeResult result;
    result.bits = "";
    result.ascii = "";

    // 1. リーダー信号を探して基準時間 T を決定
    double T_frames = 0.0;
    int first_on_index = -1;
    for (size_t i = 0; i < states.size(); ++i)
    {
        const auto& state = states[i];
        if (state.first && state.second > 10) // 長いONをリーダーと判断
        {
            std::cout << "リーダ: " << state.second << std::endl;

            T_frames = static_cast<double>(state.second) / 8.0; // 1ビットあたりの基準フレーム数
            first_on_index = static_cast<int>(i);
            break;
        }
    }

    if (first_on_index == -1)
    {
        std::cerr << "リーダ信号が見つかりませんでした。" << std::endl;
        return result;
    }

    // 2. リーダの次の状態から解析
    bool trailer_found = false;
    for (size_t i = first_on_index + 1; i < states.size(); ++i)
    {
        const auto& state = states[i];
        if (!state.first) continue; // OFFは無視

        double ratio = state.second / T_frames;

        if (ratio < 1.5)          result.bits += "0";   // T -> '0'
        else if (ratio >= 1.5 && ratio < 4.0) result.bits += "1"; // 2T -> '1'
        else if (ratio >= 4.0)     // 5T以上でトレイラ
        {
            trailer_found = true;
            break;
        }
        else
        {
            std::cerr << "警告: 不明な点灯時間 (ratio=" << ratio << ")" << std::endl;
        }
    }

    // 3. ビット列をASCIIに変換（binaryToAsciiを使用）
    result.ascii = binaryToAscii(result.bits);

    if (!trailer_found)
    {
        std::cerr << "警告: トレイラ信号が見つかりませんでした。" << std::endl;
    }

    return result;
}


// LEDを「見つける」ためのしきい値 (detectLightSource関数内で使用)
// 画面から明るい点として分離できる、高めの値が望ましい
const double DETECTION_THRESHOLD = 230.0;

// LEDの「ON/OFF状態」を判断するためのしきい値 (main関数内で使用)
// LEDのOFF状態とON状態の、中間の明るさに設定するのが理想
const double STATE_CHANGE_THRESHOLD = 210.0;

// トラッカーIDのカウンタ
int tracker_id_counter = 0;

// フレームからLEDのROIを自動検出する関数
std::vector<cv::Rect> detectLightSource(const cv::Mat& gray)
{
    // 閾値処理(二値化)
    cv::Mat thresh;
    cv::threshold(gray, thresh, DETECTION_THRESHOLD, 255, cv::THRESH_BINARY);

    // モルフォロジー変換
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));

    // Opening 処理でノイズ除去
    cv::erode(thresh, thresh, kernel, cv::Point(-1, -1), 2);
    cv::dilate(thresh, thresh, kernel, cv::Point(-1, -1), 2);

    // Closing 処理で穴埋め
    cv::dilate(thresh, thresh, kernel, cv::Point(-1, -1), 2);
    cv::erode(thresh, thresh, kernel, cv::Point(-1, -1), 2);

    cv::namedWindow("Debug - Threshold View", cv::WINDOW_NORMAL); // リサイズ可能に設定
    cv::resizeWindow("Debug - Threshold View", 900, 1200);
    cv::imshow("Debug - Threshold View", thresh);

    // 輪郭検出
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(thresh, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty())
    {
        return std::vector<cv::Rect>();
    }

    const double MIN_CONTOUR_AREA = 10.0;   // 最小輪郭面積
    const double MAX_CONTOUR_AREA = 1000.0; // 最大輪郭面積

    // 妥当な面積の輪郭の中から最大のものを探す
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

// トラッキングを行う関数
void updateTrackers(std::vector<Tracker> &trackers, const std::vector<cv::Rect> &detections)
{
    std::vector<bool> matched(detections.size(), false);  // detectionsのサイズに修正

    for (auto &tracker : trackers)
    {
        int best_index = -1;
        double best_dist = 10.0; // 距離の閾値

        // 最も近い検出を探す
        for (size_t i = 0; i < detections.size(); i++)
        {
            cv::Point2f center(detections[i].x + detections[i].width / 2.0,
                               detections[i].y + detections[i].height / 2.0);
            double dist = cv::norm(tracker.pos - center);
            if (dist < best_dist)
            {
                best_dist = dist;
                best_index = i;
            }
        }
           
        if (best_index >= 0)
        {
            // トラッカーの更新
            tracker.pos = cv::Point2f(detections[best_index].x + detections[best_index].width / 2.0,
                                      detections[best_index].y + detections[best_index].height / 2.0);
            tracker.miss_count = 0;
            matched[best_index] = true;
        }
        else
        {
            // 検出されなかった場合
            tracker.miss_count++;
            // std::cout << "[id: " << tracker.id << "] miss_count: " << tracker.miss_count << std::endl;
        }
    }

    // 新規トラッカーが必要な場合
    for (size_t i = 0; i < detections.size(); i++)
    {
        if (!matched[i])
        {  
            Tracker new_tracker;
            new_tracker.pos = cv::Point2f(detections[i].x + detections[i].width / 2.0,
                                          detections[i].y + detections[i].height / 2.0);
            new_tracker.miss_count = 0;
            new_tracker.id = tracker_id_counter++;
            trackers.push_back(new_tracker);
        }
    }

    // 長期間検出されなかったトラッカーを削除
    trackers.erase(std::remove_if(trackers.begin(), trackers.end(),
                                    [](const Tracker &t) { return t.miss_count > 15; }),
                    trackers.end());
}

// 復号化処理
void decodeSignal(Tracker &tracker, const cv::Mat &gray)
{
    if (tracker.finished)
    {
        return; // デコード完了済みのものは無視
    }

    // 1. 輝度をサンプリング
    int x = std::clamp((int)tracker.pos.x, 0, gray.cols-1);
    int y = std::clamp((int)tracker.pos.y, 0, gray.rows-1);
    int intensity = gray.at<uchar>(y, x);

    bool current_state_is_on = (intensity > STATE_CHANGE_THRESHOLD);

    // 2. 状態変化を検出
    if (current_state_is_on != tracker.last_state_is_on)
    {
        tracker.states.push_back({tracker.last_state_is_on, tracker.state_counter});
        tracker.state_counter = 0;
        tracker.last_state_is_on = current_state_is_on;
    }
    tracker.state_counter++;

    // std::cout << "states: [ ";
    // for (const auto& s : tracker.states) {
    //     std::cout << (s.first ? "1" : "0") << ":" << s.second << " ";
    // }
    // std::cout << "]" << std::endl;


    // 3. 終了条件チェック
    if (tracker.miss_count >= 15)
    {
        std::cout << "states: [ ";
        for (const auto& s : tracker.states) {
            std::cout << (s.first ? "1" : "0") << ":" << s.second << " ";
        }
        std::cout << "]" << std::endl;


        tracker.states.push_back({tracker.last_state_is_on, tracker.state_counter});
        tracker.finished = true;

        DecodeResult result = decodeFromStates(tracker.states);

        std::cout << "[Tracker " << tracker.id << "]" << std::endl;
        std::cout << "bits: " << result.bits << std::endl;
        std::cout << "ascii: " << result.ascii << std::endl;
    }
}




int main(int argc, char *argv[])
{
    // 動画ファイルの読み込み
    if (argc < 2)
    {
        std::cerr << "エラー: 動画ファイルのパスを指定してください．" << std::endl;
        return -1;
    }

    std::string video_path = argv[1];
    cv::VideoCapture cap(video_path);
    if (!cap.isOpened())
    {
        std::cerr << "エラー: 動画ファイルを開けませんでした．" << std::endl;
        return -1;
    }

    // LEDの位置を検出
    std::cout << "LEDの位置を検出しています..." << std::endl;

    std::vector<Tracker> trackers;

    // cv::Ptr<cv::legacy::TrackerMOSSE> tracker = cv::legacy::TrackerMOSSE::create();
    // bool tracker_initialized = false;
    // cv::Rect2d roi;
    // cap.set(cv::CAP_PROP_POS_FRAMES, 0);

    cv::Mat frame;
    auto start_time = std::chrono::high_resolution_clock::now();

    cv::namedWindow("Detection View", cv::WINDOW_NORMAL); // リサイズ可能に設定
    cv::resizeWindow("Detection View", 900, 1200);
    while (true)
    {
        cap >> frame;
        if (frame.empty())
        {
            break;
        }
        
        // グレースケールに変換
        cv::Mat gray;
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
        cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0);

        // LEDを検出
        std::vector<cv::Rect> detections = detectLightSource(gray);
        // std::cout << "検出されたLED数: " << detections.size() << std::endl;

        // トラッカーを更新
        updateTrackers(trackers, detections);
        // std::cout << "現在のトラッカー数: " << trackers.size() << std::endl;

        // 各トラッカーに対してデコード処理を実行
        for (auto &tracker : trackers)
        {
            // std::cout << "トラッカーID: " << tracker.id  << std::endl;

            decodeSignal(tracker, gray);
            
            // トラッカーの位置をフレーム範囲内にクランプ
            int x = std::max(0, std::min((int)tracker.pos.x - 10, frame.cols - 20));
            int y = std::max(0, std::min((int)tracker.pos.y - 10, frame.rows - 20));
            int width = std::min(20, frame.cols - x);
            int height = std::min(20, frame.rows - y);
            
            if (width > 0 && height > 0) {
                cv::rectangle(frame, cv::Rect(x, y, width, height), cv::Scalar(0, 255, 0), 2);
            }
        }

        // OpenCVの表示を安全に行う
        try {
            cv::imshow("Detection View", frame);
            if (cv::waitKey(1) == 27)
            {
                break; // ESCキーで終了
            }
        } catch (const cv::Exception& e) {
            std::cerr << "OpenCV 表示エラー: " << e.what() << std::endl;
            // 表示エラーが発生した場合は続行（表示なし）
        }
    }

    // ループ後に、すべてのトラッカーの未完了状態を処理
    std::cout << "\n--- デコード結果 ---" << std::endl;

    for (auto &tracker : trackers)
    {
        // 最後の状態を保存していない場合は追加
        if (!tracker.finished && tracker.state_counter > 0)
        {
            tracker.states.push_back({tracker.last_state_is_on, tracker.state_counter});
            tracker.finished = true;
        }

        // デコード
        DecodeResult result = decodeFromStates(tracker.states);

        std::cout << "[Tracker " << tracker.id << "]" << std::endl;
        std::cout << "bits:  " << result.bits << std::endl;
        std::cout << "ascii: " << result.ascii << std::endl;
    }

    // 解析にかかった時間を計測（必要なら開始時に start_time を取得）
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;
    std::cout << "\n解析にかかった時間: " << elapsed.count() << " 秒" << std::endl;

    cap.release();
    cv::destroyAllWindows();
    return 0;

}
