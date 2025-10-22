#pragma once

#include <queue>
#include <mutex>
#include <condition_variable> 

template <typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() = default;
    ~ThreadSafeQueue() = default;


    void push(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        data_queue_.push(std::move(value));
        cond_var_.notify_one(); // 通知一个正在等待的线程
    }

    int size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return data_queue_.size();
    }

 
    bool wait_and_pop(T& out_value) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // 等待，直到队列不为空 或 收到停止信号
        // (你需要一个方法来停止它，我们稍后在 TcpPeer 中处理)
        cond_var_.wait(lock, [this] { return !data_queue_.empty(); });

        if (data_queue_.empty()) {
            return false; // 可能在唤醒时队列又空了 (罕见)
        }
        
        out_value = std::move(data_queue_.front());
        data_queue_.pop();
        return true;
    }

    bool try_pop(T& out_value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (data_queue_.empty()) {
            return false;
        }
        
        out_value = std::move(data_queue_.front());
        data_queue_.pop();
        return true;
    }

    //唤醒所有等待的线程 
    void notify_all() {
        cond_var_.notify_all();
    }

private:
    mutable std::mutex mutex_;
    std::queue<T> data_queue_;
    std::condition_variable cond_var_; // 用于 wait_and_pop
};