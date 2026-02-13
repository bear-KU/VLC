#include "tracker_thread.hpp"
#include <limits> // [ADD] std::numeric_limitsを使うために追加

// グローバルなMutexの実体を定義
std::mutex cout_mutex;

// Tracker オブジェクトが破棄されるときに実行される(デストラクタ)
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
      worker(std::move(other.worker)),
      start_time(other.start_time),
      end_time(other.end_time) {}

// Trackerのムーブ代入演算子
Tracker& Tracker::operator=(Tracker&& other) noexcept
{
    if (this != &other)
    {
        if(worker.joinable())
        {
            worker.join();
        }

        id = other.id;
        pos = other.pos;
        miss_count = other.miss_count;
        worker = std::move(other.worker);
        start_time = other.start_time;
        end_time = other.end_time;
    }
    return *this;
}

// 各トラッカーが実行するスレッド関数
void trackerThreadFunction(int id, ThreadSafeQueue<FrameUpdate>* queue, Tracker* tracker_ptr)
{
    std::vector<std::pair<bool, int>> states;
    bool last_state_is_on = false;
    int state_counter = 0;
    double T_frames = 0.0;

    // 平均面積計算用の変数
    double sum_on_area = 0.0;
    long count_on_frames = 0;

    // [ADD] 最小面積計算用の変数 (初期値はdoubleの最大値にしておく)
    double min_on_area = std::numeric_limits<double>::max();
    
    std::vector<double> sample_areas;

    while (true)
    {
        auto update_opt = queue->pop();
        if (!update_opt.has_value()) continue;
        
        FrameUpdate update = update_opt.value();

        if (update.terminate) break;

        // 先に点灯判定を行う
        bool current_state_is_on = update.found && (update.intensity > STATE_CHANGE_THRESHOLD);

        // 点灯している時だけ面積を積算・比較する
        if (current_state_is_on)
        {
            sum_on_area += update.area;
            count_on_frames++;

            // [ADD] 最小値の更新
            if (update.area < min_on_area) {
                min_on_area = update.area;
            }

            if (sample_areas.size() < 10) {
                sample_areas.push_back(update.area);
            }
        }

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

    // 終了時刻を記録
    tracker_ptr->end_time = std::chrono::high_resolution_clock::now();
    
    DecodeResult result = decodeFromStates(id, T_frames, states);
    
    // 処理時間を計算
    std::chrono::duration<double> elapsed = tracker_ptr->end_time - tracker_ptr->start_time;
    
    // 平均面積の計算
    double avg_area = (count_on_frames > 0) ? (sum_on_area / count_on_frames) : 0.0;
    
    // [ADD] 一度もONにならなかった場合は0.0にする
    if (count_on_frames == 0) {
        min_on_area = 0.0;
    }

    {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "\n--- [Tracker " << id << "] 処理終了 ---" << std::endl;
        std::cout << "処理時間: " << elapsed.count() << " 秒" << std::endl;
        std::cout << "点滅パターン: [ ";
        for (const auto& s : states) {
            std::cout << (s.first ? "1" : "0") << ":" << s.second << " ";
        }
        std::cout << "]" << std::endl;
        std::cout << "基準時間 T: " << T_frames << " フレーム" << std::endl;
        std::cout << "デコード結果 -> bits:  [" << result.bits << "]" << std::endl;
        std::cout << "デコード結果 -> ascii: [" << result.ascii << "]" << std::endl;

        std::cout << "検出面積サンプル(先頭10件): [ ";
        for (double a : sample_areas) {
            std::cout << a << " ";
        }
        std::cout << "... ]" << std::endl;
        
        // [MOD] 平均と最小値を表示
        std::cout << "平均検出面積(ON時のみ): " << avg_area << std::endl;
        std::cout << "最小検出面積(ON時のみ): " << min_on_area << " (サンプル数: " << count_on_frames << ")" << std::endl;
    }
}

// バイナリ文字列をASCIIに変換する関数
std::string binaryToAscii(const std::string& binary)
{
    std::string result;
    if (binary.empty()) return result;
    size_t parsable_length = binary.size() - (binary.size() % 8);
    for (size_t i = 0; i < parsable_length; i += 8)
    {
        std::bitset<8> bits(binary.substr(i, 8));
        result += static_cast<char>(bits.to_ulong());
    }
    return result;
}

// 記録された状態履歴から信号をデコードする関数
DecodeResult decodeFromStates(int id, double& T_frames, const std::vector<std::pair<bool, int>>& states)
{
    DecodeResult result;
    if (states.size() < 2) return result; 

    int first_on_index = -1;
    for (size_t i = 0; i < states.size(); ++i)
    {
        const auto& state = states[i];
        // [Note] ユーザー側のコードに合わせて閾値を8に設定しています
        if (state.first && state.second >= 8)
        {
            T_frames = static_cast<double>(state.second) / 8.0;
            first_on_index = static_cast<int>(i);
            break;
        }
    }

    if (first_on_index == -1) return result;

    for (size_t i = first_on_index + 1; i < states.size(); ++i)
    {
        const auto& state = states[i];
        if (!state.first) continue;
        double ratio = state.second / T_frames;
        if (ratio < 1.8) result.bits += "0";
        else if (ratio >= 1.8 && ratio < 5.0) result.bits += "1";
        else if (ratio >= 5.0) break;
    }
    result.ascii = binaryToAscii(result.bits);
    return result;
}
