#include <iostream>
#include <vector>
#include <string>
#include <bitset>

#include <opencv2/opencv.hpp>
#include <opencv2/tracking.hpp>
#include <opencv2/tracking/tracking_legacy.hpp>

struct LedState
{
    bool is_on; // true: ON, false: OFF
    int duration_frames; // 継続したフレーム数
};


// LEDを「見つける」ためのしきい値 (findLED関数内で使用)
// 画面から明るい点として分離できる、高めの値が望ましい
const double DETECTION_THRESHOLD = 250.0;

// LEDの「ON/OFF状態」を判断するためのしきい値 (main関数内で使用)
// LEDのOFF状態とON状態の、中間の明るさに設定するのが理想
const double STATE_CHANGE_THRESHOLD = 240.0;


// フレームからLEDのROIを自動検出する関数
cv::Rect findLED(const cv::Mat& frame)
{
    // グレースケールに変換
    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0);

    // cv::imshow("Debug - Grayscale View", gray);

    // 閾値処理(二値化)
    cv::Mat thresh;
    cv::threshold(gray, thresh, DETECTION_THRESHOLD, 255, cv::THRESH_BINARY);


    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));

    // Opening 処理でノイズ除去
    cv::erode(thresh, thresh, kernel, cv::Point(-1, -1), 2);
    cv::dilate(thresh, thresh, kernel, cv::Point(-1, -1), 2);

    // Closing 処理で穴埋め
    cv::dilate(thresh, thresh, kernel, cv::Point(-1, -1), 2);
    cv::erode(thresh, thresh, kernel, cv::Point(-1, -1), 2);

    cv::imshow("Debug - Threshold View", thresh);

    // 輪郭検出
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(thresh, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty())
    {
        return cv::Rect();
    }

    double max_valid_area = 0.0;
    std::vector<cv::Point> best_contour;

    const double MIN_CONTOUR_AREA = 100.0;   // 最小輪郭面積
    const double MAX_CONTOUR_AREA = 500.0; // 最大輪郭面積

    // 妥当な面積の輪郭の中から最大のものを探す
    for (const auto& contour : contours)
    {
        double area = cv::contourArea(contour);
        if (area >= MIN_CONTOUR_AREA && area <= MAX_CONTOUR_AREA)
        {
            if (area > max_valid_area)
            {
                max_valid_area = area;
                best_contour = contour;
            }
        }
    }

    // 最大の輪郭のバウンディングボックスを返す
    if (max_valid_area > 0)
    {
        cv::Rect2d roi = cv::boundingRect(best_contour);

        roi.x = std::max(0.0, roi.x);
        roi.y = std::max(0.0, roi.y);
        if (roi.x + roi.width > frame.cols) {
            roi.width = frame.cols - roi.x;
        }
        if (roi.y + roi.height > frame.rows) {
            roi.height = frame.rows - roi.y;
        }

        return roi;
    }
    return cv::Rect();
}


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

    cv::Rect2d roi;

    std::vector<LedState> states;
    bool last_state_is_on = false;
    int state_counter = 0;

    cv::Ptr<cv::legacy::TrackerMOSSE> tracker = cv::legacy::TrackerMOSSE::create();
    bool tracker_initialized = false;

    cap.set(cv::CAP_PROP_POS_FRAMES, 0);

    cv::Mat frame;
    while (true)
    {
        cap >> frame;
        if (frame.empty())
        {
            break;
        }

        if (!tracker_initialized)
        {
            roi = findLED(frame);
            if (roi.width > 0 && roi.height > 0)
            {
                tracker->init(frame, roi);
                tracker_initialized = true;
            }
        }
        else
        {
            bool ok = tracker->update(frame, roi);
            if (!ok)
            {
                cv::Rect new_roi = findLED(frame);
                if (new_roi.width > 0 && new_roi.height > 0)
                {
                    tracker = cv::legacy::TrackerMOSSE::create();
                    tracker->init(frame, new_roi);
                    roi = new_roi;
                }
            }

            cv::Mat gray_roi;
            cv::cvtColor(frame(roi), gray_roi, cv::COLOR_BGR2GRAY);
            bool current_state_is_on = (cv::mean(gray_roi)[0] > STATE_CHANGE_THRESHOLD);

            if (current_state_is_on != last_state_is_on)
            {
                states.push_back({last_state_is_on, state_counter});
                state_counter = 0;
                last_state_is_on = current_state_is_on;
            }
            state_counter++;

            cv::rectangle(frame, roi, cv::Scalar(0, 255, 0), 2);
            cv::imshow("Detecting LED... (Press ESC to stop)", frame);
            if (cv::waitKey(1) == 27) break; // ESCキー
        }
    }
    states.push_back({last_state_is_on, state_counter});

    // デコード処理
    std::cout << "\n--- 検出された点滅パターン ---" << std::endl;
    for(const auto& state : states)
    {
        std::cout << (state.is_on ? "ON " : "OFF") << " for " << state.duration_frames << " frames" << std::endl;
    }

    // リーダから基準時間 T を計算
    double T_frames = 0.0;
    int first_on_state_index = -1;

    for (int i = 0; i < states.size(); ++i) {
        if (states[i].is_on && states[i].duration_frames > 20) { // リーダーは長いはずなので短いONは無視
            T_frames = static_cast<double>(states[i].duration_frames) / 8.0;
            first_on_state_index = i;
            break;
        }
    }

    if (first_on_state_index == -1) {
        std::cerr << "エラー: リーダ信号（最初の点灯）を検出できませんでした．" << std::endl;
        return -1;
    }

    std::cout << "\n--- 解析結果 ---" << std::endl;
    std::cout << "リーダ: " << states[first_on_state_index].duration_frames << " frames" << std::endl;
    std::cout << "基準時間 T: " << T_frames << " フレーム" << std::endl;

    std::string decoded_bits = "";
    bool trailer_found = false;

    // リーダの次の状態から解析を開始
    for (int i = first_on_state_index + 1; i < states.size(); ++i) {
        const auto& state = states[i];

        // ON状態の時のみデータを判定
        if (state.is_on) {
            // duration / T_frames の比率を計算
            double ratio = state.duration_frames / T_frames;

             if (ratio < 1.5) { // T -> '0' (許容範囲を設ける)
                decoded_bits += "0";
            } else if (ratio >= 1.5 && ratio < 3.0) { // 2T -> '1'
                decoded_bits += "1";
            } else if (ratio >= 4.0) { // 5T -> トレイラ
                trailer_found = true;
                std::cout << "トレイラを検出" << std::endl;
                break;
            } else {
                std::cout << "警告: 不明な点灯時間を検出 (長さ比: " << ratio << ")．スキップします．" << std::endl;
            }
        }
    }

    std::cout << "\n--- デコード結果 ---" << std::endl;
    if (!trailer_found) {
        std::cout << "警告: トレイラが見つからないまま解析が終了しました．" << std::endl;
    }
    
    std::cout << "bits: " << decoded_bits << std::endl;
    try {
        std::cout << "data: " << binaryToAscii(decoded_bits) << std::endl;
    } catch (const std::invalid_argument& e) {
        std::cerr << "ASCIIへの変換エラー: " << e.what() << std::endl;
    }

    // 解析にかかった時間を計測
    auto start_time = std::chrono::high_resolution_clock::now();
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;
    std::cout << "\n解析にかかった時間: " << elapsed.count() << " 秒" << std::endl;

    cap.release();
    cv::destroyAllWindows();

    return 0;
}
