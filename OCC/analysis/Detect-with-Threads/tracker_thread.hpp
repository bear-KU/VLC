#ifndef TRACKER_THREAD_HPP
#define TRACKER_THREAD_HPP

#include <iostream>
#include <string>
#include <bitset>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <queue>
#include <optional>
#include <chrono>
#include <opencv2/opencv.hpp>

// スレッドセーフなキューの実装
template <typename T>
class ThreadSafeQueue
{
public:
    void push(T value)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        queue_.push(std::move(value));
        cond_var_.notify_one();
    }

    std::optional<T> pop()
    {
        std::unique_lock<std::mutex> lock(mtx_);
        cond_var_.wait(lock, [this] { return !queue_.empty(); });
        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

private:
    std::queue<T> queue_;
    std::mutex mtx_;
    std::condition_variable cond_var_;
};

// フレーム更新情報を保持する構造体
struct FrameUpdate
{
    bool terminate = false;
    bool found = false;
    int intensity = 0;
    cv::Point2f pos;
};

// デコード結果を保持する構造体
struct DecodeResult
{
    std::string bits;
    std::string ascii;
};

// トラッカーの状態を保持する構造体
struct Tracker
{
    int id;
    cv::Point2f pos;
    int miss_count = 0;

    std::thread worker;
    ThreadSafeQueue<FrameUpdate> frame_queue;
    
    // 時間計測用
    std::chrono::high_resolution_clock::time_point start_time;
    std::chrono::high_resolution_clock::time_point end_time;

    ~Tracker();
    Tracker(Tracker&& other) noexcept;
    Tracker& operator=(Tracker&& other) noexcept;
    Tracker(const Tracker&) = delete;
    Tracker& operator=(const Tracker&) = delete;
    Tracker() = default;
};

// LEDを「見つける」ための輝度閾値 (明るい点として分離できる高めの値)
const double STATE_CHANGE_THRESHOLD = 150.0;

// 標準出力用のMutex(tracker_thread.cppで定義)
extern std::mutex cout_mutex;

// スレッドが実行する関数
void trackerThreadFunction(int id, ThreadSafeQueue<FrameUpdate>* queue, Tracker* tracker_ptr);

// デコード関連の関数
DecodeResult decodeFromStates(int id, double& T_frames, const std::vector<std::pair<bool, int>>& states);
std::string binaryToAscii(const std::string& binary);

#endif // TRACKER_THREAD_HPP
