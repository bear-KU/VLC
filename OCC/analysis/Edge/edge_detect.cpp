#include <opencv2/opencv.hpp>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Usage: ./program image_file" << std::endl;
        return -1;
    }

    // 動画を開く
    cv::VideoCapture cap(argv[1]);
    if (!cap.isOpened()) {
        std::cout << "Could not open the video file" << std::endl;
        return -1;
    }

    // 最初のフレームを読み込む
    cv::Mat frame;
    for (int i = 0; i < 10; ++i) { // 最初の数フレームをスキップ
        cap >> frame;
    }
    if (frame.empty()) {
        std::cout << "Could not read the first frame" << std::endl;
        return -1;
    }


    // BGR -> RGB
    cv::Mat img_rgb;
    cv::cvtColor(frame, img_rgb, cv::COLOR_BGR2RGB);

    // RGB -> グレースケール
    cv::Mat img_gray;
    cv::cvtColor(img_rgb, img_gray, cv::COLOR_RGB2GRAY);

    // Gaussian Blur
    cv::Mat img_gray_blur;
    cv::GaussianBlur(img_gray, img_gray_blur, cv::Size(9, 9), 0);

    // モルフォロジー処理（クロージング）
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5,5));
    cv::morphologyEx(img_gray_blur, img_gray_blur, cv::MORPH_CLOSE, kernel);

    // Sobelフィルタ
    cv::Mat sobel_x, sobel_y;
    cv::Sobel(img_gray_blur, sobel_x, CV_32F, 1, 0, -1); // ksize=-1はScharr
    cv::Sobel(img_gray_blur, sobel_y, CV_32F, 0, 1, -1);

    // 絶対値化 & 8bit変換
    cv::Mat sobel_x_abs, sobel_y_abs;
    cv::convertScaleAbs(sobel_x, sobel_x_abs);
    cv::convertScaleAbs(sobel_y, sobel_y_abs);

    // XとYを合成
    cv::Mat sobel_combined;
    cv::addWeighted(sobel_x_abs, 0.5, sobel_y_abs, 0.5, 0, sobel_combined);

    // 表示
    cv::imshow("Original RGB", img_rgb);
    cv::imshow("Sobel Combined", sobel_combined);
    cv::waitKey(0);

    return 0;
}
