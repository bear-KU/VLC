#include "tracker_thread.hpp"

// グローバルなMutexの実体を定義
std::mutex cout_mutex;

// Tracker オブジェクトが破棄されるときに実行される(デストラクタ)
// スレッドを終了させる
Tracker::~Tracker()
{
    if (worker.joinable())
    {
        frame_queue.push({true});
        worker.join();
    }
}

// Trackerのムーブコンストラクタ
Tracker::Tracker(Tracker&& other) noexcept
    : id(other.id),
      pos(other.pos),
      miss_count(other.miss_count),
      worker(std::move(other.worker)) {}

// Trackerのムーブ代入演算子
Tracker& Tracker::operator=(Tracker&& other) noexcept {
    if (this != &other) {
        if(worker.joinable())
        {
            worker.join();
        }

        id = other.id;
        pos = other.pos;
        miss_count = other.miss_count;
        worker = std::move(other.worker);
    }
    return *this;
}

// 各トラッカーが実行するスレッド関数
void trackerThreadFunction(int id, ThreadSafeQueue<FrameUpdate>* queue)
{
    std::vector<std::pair<bool, int>> states;
    bool last_state_is_on = false;
    int state_counter = 0;
    double T_frames = 0.0;
    bool is_rejected = false;

    while (true)
    {
        auto update_opt = queue->pop();
        if (!update_opt.has_value()) continue;
        
        FrameUpdate update = update_opt.value();

        if (update.terminate) break;

        bool current_state_is_on = update.found && (update.intensity > STATE_CHANGE_THRESHOLD);

        if (current_state_is_on != last_state_is_on)
        {
            if (state_counter > 0)
            {
                states.push_back({last_state_is_on, state_counter});
            }
            state_counter = 0;
            last_state_is_on = current_state_is_on;
        }
        state_counter++;
    }

    if (state_counter > 0)
    {
        states.push_back({last_state_is_on, state_counter});
    }

    DecodeResult result = decodeFromStates(id, T_frames, states);
    
    {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "\n--- [Tracker " << id << "] 処理終了 ---" << std::endl;
        std::cout << "点滅パターン: [ ";
        for (const auto& s : states) {
            std::cout << (s.first ? "1" : "0") << ":" << s.second << " ";
        }
        std::cout << "]" << std::endl;
        std::cout << "基準時間 T: " << T_frames << " フレーム" << std::endl;
        std::cout << "デコード結果 -> bits:  [" << result.bits << "]" << std::endl;
        std::cout << "デコード結果 -> ascii: [" << result.ascii << "]" << std::endl;
    }
}

// バイナリ文字列をASCIIに変換する関数
std::string binaryToAscii(const std::string& binary)
{
    std::string result;
    if (binary.empty()) return result;
    size_t parsable_length = binary.size() - (binary.size() % 8);
    for (size_t i = 0; i < parsable_length; i += 8) {
        std::bitset<8> bits(binary.substr(i, 8));
        result += static_cast<char>(bits.to_ulong());
    }
    return result;
}

// 記録された状態履歴から信号をデコードする関数
DecodeResult decodeFromStates(int id, double& T_frames, const std::vector<std::pair<bool, int>>& states)
{
    DecodeResult result;
    if (states.size() < 2) return result; // 状態が少なすぎる場合は終了

    int first_on_index = -1;
    for (size_t i = 0; i < states.size(); ++i) {
        const auto& state = states[i];
        if (state.first && state.second > 10) {
            T_frames = static_cast<double>(state.second) / 8.0;
            first_on_index = static_cast<int>(i);
            break;
        }
    }

    if (first_on_index == -1) return result;

    for (size_t i = first_on_index + 1; i < states.size(); ++i) {
        const auto& state = states[i];
        if (!state.first) continue;
        double ratio = state.second / T_frames;
        if (ratio < 1.5) result.bits += "0";
        else if (ratio >= 1.5 && ratio < 4.0) result.bits += "1";
        else if (ratio >= 4.0) break;
    }
    result.ascii = binaryToAscii(result.bits);
    return result;
}
