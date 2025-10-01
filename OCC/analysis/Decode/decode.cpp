#include <iostream>
#include <vector>
#include <string>
#include <bitset>
#include <opencv2/opencv.hpp>

struct LedState {
    bool is_on; // true: ON, false: OFF
    int duration_frames; // 継続したフレーム数
};

// 閾値
const double BRIGHTNESS_THRESHOLD = 200.0;

// バイナリ文字列をASCIIに変換する関数
std::string binaryToAscii(const std::string& binary) {
    std::string result;
    if (binary.size() % 8 != 0) {
        throw std::invalid_argument("バイナリ文字列は8の倍数である必要があります");
    }

    for (size_t i = 0; i < binary.size(); i += 8) {
        std::bitset<8> bits(binary.substr(i, 8));
        char c = static_cast<char>(bits.to_ulong());
        result += c;
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


    // フレームキャプチャ
    cv::Mat frame;
    cap >> frame;
    if (frame.empty())
    {
        std::cerr << "エラー: 動画からフレームを読み込めませんでした．" << std::endl;
        return -1;
    }


    // ROI (興味領域) をマウスで選択
    // 現時点では，検出は手動で選択する方法を採っている
    std::cout << "解析したいLEDの領域をマウスでドラッグし，Enterキーを押してください．" << std::endl;
    cv::Rect roi = cv::selectROI("ROI Selector", frame);
    cv::destroyWindow("ROI Selector");
    if (roi.width == 0 || roi.height == 0)
    {
        std::cerr << "エラー: ROIが選択されませんでした．" << std::endl;
        return -1;
    }

    // 動画のフレーム数
    int total_frames = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
    std::cout << "動画の総フレーム数: " << total_frames << std::endl;

    // 解析までにかかった時間を計測
    auto start_time = std::chrono::high_resolution_clock::now();

    // 各フレームの解析
    std::vector<LedState> states;
    bool last_state_is_on = false;
    int state_counter = 0;
    
    // 初期状態の判定
    cv::Mat first_roi_gray;
    cv::cvtColor(frame(roi), first_roi_gray, cv::COLOR_BGR2GRAY);
    last_state_is_on = (cv::mean(first_roi_gray)[0] > BRIGHTNESS_THRESHOLD);
    std::cout << cv::mean(first_roi_gray)[0] << std::endl;

    cap.set(cv::CAP_PROP_POS_FRAMES, 0);
    while (true)
    {
        cap >> frame;
        if (frame.empty()) break;

        cv::Mat gray_roi;
        cv::cvtColor(frame(roi), gray_roi, cv::COLOR_BGR2GRAY);
        bool current_state_is_on = (cv::mean(gray_roi)[0] > BRIGHTNESS_THRESHOLD);
        std::cout << cv::mean(gray_roi)[0] << std::endl;

        if (current_state_is_on != last_state_is_on) 
        {
            states.push_back({last_state_is_on, state_counter});
            state_counter = 0;
            last_state_is_on = current_state_is_on;
        }
        state_counter++;
        
        // 解析中のプレビュー表示
        cv::rectangle(frame, roi, cv::Scalar(0, 255, 0), 2);
        cv::imshow("Analyzing...", frame);
        if (cv::waitKey(1) == 27) break; // ESCキーで中断
    }
    states.push_back({last_state_is_on, state_counter});
    std::cout << "動画の解析が完了しました．" << std::endl;

    if (states.empty())
    {
        std::cerr << "状態変化を検出できませんでした．" << std::endl;
        return -1;
    }
    
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
        if (states[i].is_on) {
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

            if (ratio < 2.0) { // T -> '0'
                decoded_bits += "0";
            } else if (ratio >= 2.0 && ratio < 4.0) { // 2T -> '1'
                decoded_bits += "1";
            } else if (ratio >= 4.0) { // 5T -> トレイラ
                trailer_found = true;
                std::cout << "トレイラを検出" << std::endl;
                break; // トレイラ
            } else {
                // T, 2T, 5T のいずれでもない予期せぬ長さの点灯
                std::cout << "警告: 不明な点灯時間を検出 (長さ比: " << ratio << ")．スキップします．" << std::endl;
            }
        }
        // OFF状態はデータ間の区切りなので，ここでは何もしない
    }

    std::cout << "\n--- デコード結果 ---" << std::endl;
    if (!trailer_found) {
        std::cout << "警告: トレイラが見つからないまま解析が終了しました．" << std::endl;
    }
    std::cout << "data: " << binaryToAscii(decoded_bits) << std::endl;

    // 解析にかかった時間を計測
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;
    std::cout << "\n解析にかかった時間: " << elapsed.count() << " 秒" << std::endl;

    cap.release();
    cv::destroyAllWindows();



    return 0;
}
