#pragma once

#include <iostream>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <deque>
#include <memory>
#include <shared_mutex>
#include <mutex>
#include <queue> // [新增] 引入堆头文件
#include "cppjieba/Jieba.hpp"

// 返回值结构保持不变
struct WordFreq {
    std::string word;
    int count;
};

class HotWordSystem {
public:
    HotWordSystem(const std::string& dict_dir, int window_duration_sec, int bucket_step_sec = 1);
    
    // 禁止拷贝
    HotWordSystem(const HotWordSystem&) = delete;
    HotWordSystem& operator=(const HotWordSystem&) = delete;

    void add_message(const std::string& content, long long timestamp_ms);
    std::vector<WordFreq> query_top_k(int k);

private:
    // [更改] 内部存储不再存 string，而是存 uint64_t (Word ID)
    struct TimeBucket {
        long long align_timestamp;
        std::unordered_map<uint64_t, int> word_counts; 
    };

    // [新增] 内部辅助函数：获取单词对应的ID，如果不存在则创建
    uint64_t get_or_create_word_id(const std::string& word);
    // [新增] 内部辅助函数：通过ID找回单词
    std::string get_word_by_id(uint64_t id) const;

    void slide_window(long long current_align_time);

private:
    int window_duration_sec_;
    int bucket_step_sec_;
    
    std::deque<TimeBucket> window_buckets_;
    
    // [更改] 全局计数 Key 变为 uint64_t，大幅提升 Map 性能
    std::unordered_map<uint64_t, int> global_count_;
    
    // [新增] 字符串字典化 (Interning) 映射表
    // 用于将 string <-> uint64_t 双向转换
    std::unordered_map<std::string, uint64_t> word_to_id_;
    std::unordered_map<uint64_t, std::string> id_to_word_;
    uint64_t next_word_id_ = 1; // ID 计数器

    std::unique_ptr<cppjieba::Jieba> jieba_;
    mutable std::shared_mutex mutex_; 
};
