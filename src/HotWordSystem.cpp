#include "HotWordSystem.h"

HotWordSystem::HotWordSystem(const std::string& dict_dir, int window_duration_sec, int bucket_step_sec)
    : window_duration_sec_(window_duration_sec), bucket_step_sec_(bucket_step_sec) {
    
    std::string dict = dict_dir + "/jieba.dict.utf8";
    std::string hmm = dict_dir + "/hmm_model.utf8";
    std::string user = dict_dir + "/user.dict.utf8";
    std::string idf = dict_dir + "/idf.utf8";
    std::string stop = dict_dir + "/stop_words.utf8";
    
    jieba_ = std::make_unique<cppjieba::Jieba>(dict, hmm, user, idf, stop);
}

// [新增] 核心逻辑：将字符串转为 ID
uint64_t HotWordSystem::get_or_create_word_id(const std::string& word) {
    // 注意：此函数在 add_message 内部调用，已经持有写锁，所以这里不需要额外加锁
    auto it = word_to_id_.find(word);
    if (it != word_to_id_.end()) {
        return it->second;
    }
    
    uint64_t new_id = next_word_id_++;
    word_to_id_[word] = new_id;
    id_to_word_[new_id] = word;
    return new_id;
}

// [新增] 核心逻辑：将 ID 转回字符串
std::string HotWordSystem::get_word_by_id(uint64_t id) const {
    // 注意：此函数在 query_top_k 内部调用，持有读锁
    auto it = id_to_word_.find(id);
    if (it != id_to_word_.end()) {
        return it->second;
    }
    return ""; // 理论上不应发生
}

void HotWordSystem::add_message(const std::string& content, long long timestamp_ms) {
    // 1. 分词 (锁外进行)
    std::vector<std::string> words;
    jieba_->Cut(content, words, true);
    if (words.empty()) return;

    long long timestamp_sec = timestamp_ms / 1000;
    long long align_time = timestamp_sec - (timestamp_sec % bucket_step_sec_);

    // 2. 加写锁
    std::unique_lock<std::shared_mutex> lock(mutex_);

    if (window_buckets_.empty() || window_buckets_.back().align_timestamp != align_time) {
        if (!window_buckets_.empty() && align_time < window_buckets_.back().align_timestamp) {
             // 乱序处理逻辑...
        } else {
            window_buckets_.push_back({align_time, {}});
        }
    }

    TimeBucket& current_bucket = window_buckets_.back();

    for (const auto& w : words) {
        if (w.size() < 3) continue;

        // [更改] 这里不再直接存 string，而是获取 ID
        uint64_t wid = get_or_create_word_id(w);

        // [更改] 所有的 Map 操作现在都是基于 uint64_t，速度极快
        current_bucket.word_counts[wid]++;
        global_count_[wid]++;
    }

    slide_window(align_time);
}

void HotWordSystem::slide_window(long long current_align_time) {
    long long expire_threshold = current_align_time - window_duration_sec_;

    while (!window_buckets_.empty()) {
        TimeBucket& front = window_buckets_.front();
        
        if (front.align_timestamp <= expire_threshold) {
            for (const auto& kv : front.word_counts) {
                // [更改] kv.first 是 uint64_t ID
                auto it = global_count_.find(kv.first);
                if (it != global_count_.end()) {
                    it->second -= kv.second;
                    if (it->second <= 0) {
                        global_count_.erase(it);
                        // 注意：这里我们只删除了计数，没有从 id_to_word_ 中删除 ID 映射。
                        // 这样做是为了性能（避免频繁操作字典表）。
                        // 长期运行如果单词量无限增长，这里需要额外的清理策略 (LRU等)。
                    }
                }
            }
            window_buckets_.pop_front();
        } else {
            break;
        }
    }
}

std::vector<WordFreq> HotWordSystem::query_top_k(int k) {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    if (global_count_.empty() || k <= 0) {
        return {};
    }

    // [新增] 定义小顶堆 (Min-Heap)
    // pair<count, word_id>，默认是按 pair.first 比较
    using Node = std::pair<int, uint64_t>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> min_heap;

    // [更改] 遍历 global_count_ (Key是ID)
    for (const auto& kv : global_count_) {
        int count = kv.second;
        uint64_t id = kv.first;

        if (count <= 0) continue;

        if (min_heap.size() < static_cast<size_t>(k)) {
            min_heap.push({count, id});
        } else {
            // 如果当前词频比堆顶（目前TopK里最小的）要大，则替换
            if (count > min_heap.top().first) {
                min_heap.pop();
                min_heap.push({count, id});
            }
        }
    }

    // [更改] 将堆中数据转为结果
    // 注意：堆弹出的顺序是 从小到大，我们需要反转
    std::vector<WordFreq> result;
    result.reserve(min_heap.size());

    while (!min_heap.empty()) {
        auto node = min_heap.top();
        min_heap.pop();
        // [关键] 只有在这里，才把 ID 转换回 String
        result.push_back({get_word_by_id(node.second), node.first});
    }

    // 因为是小顶堆，pop出来是升序，TopK通常需要降序
    std::reverse(result.begin(), result.end());

    return result;
}