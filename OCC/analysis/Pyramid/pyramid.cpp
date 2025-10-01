#include <iostream>
#include <vector>
#include <string>
#include <bitset>

#include <opencv2/opencv.hpp>
#include <opencv2/tracking.hpp>
#include <opencv2/tracking/tracking_legacy.hpp>

int main(int argc, char** argv)
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

    cv::Mat frame;
    cap >> frame;
    if (frame.empty())
    {
        std::cerr << "エラー: 動画の最初のフレームを読み込めませんでした．" << std::endl;
        return -1;
    }

    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0);

    std::vector<cv::Mat> pyramid;
    cv::buildPyramid(frame, pyramid, 4);

    std::vector<cv::Point> candidates;
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(7, 7));
    
    for (size_t i = 0; i < pyramid.size(); ++i)
    {
        cv::Mat layer32 = pyramid[i];

        cv::Mat localMax32;
        cv::dilate(layer32, localMax32, kernel); // layer: ピラミッド層

        layer32.convertTo(layer32, CV_32F);
        localMax32.convertTo(localMax32, CV_32F);

        cv::Mat diff = layer32 - localMax32;

        cv::Mat mask;
        cv::threshold(diff, mask, 10, 255, cv::THRESH_BINARY); // 100は調整可能

        // マッピング
        for (int y = 0; y < mask.rows; y++) {
            for (int x = 0; x < mask.cols; x++) {
                if (mask.at<uchar>(y, x) > 0) {
                    int origX = x * (1 << i);
                    int origY = y * (1 << i);
                    candidates.push_back(cv::Point(origX, origY));
                }
            }
        }

        cv::imshow("Mask " + std::to_string(i), mask);
    }

    // 候補を描画
    for (auto& pt : candidates) {
        cv::circle(frame, pt, 3, cv::Scalar(0, 0, 255), -1);
    }

    cv::imshow("Candidates", frame);
    cv::waitKey(0);

    return 0;
}
