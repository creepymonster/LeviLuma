#pragma once
// Begrenzte, threadsichere Warteschlange (Ersatz fuer FreeRTOS-Queues im Simulator).
// push() verwirft bei voller Schlange wie xQueueSend(..., 0); pop() wartet hoechstens timeoutMs.
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>

template <typename T> class SimQueue {
public:
  explicit SimQueue(size_t cap) : cap_(cap) {}

  bool push(const T &v) {
    {
      std::lock_guard<std::mutex> g(m_);
      if (q_.size() >= cap_) {
        ++dropped_;
        return false;
      }
      q_.push_back(v);
    }
    cv_.notify_one();
    return true;
  }

  bool pop(T &out, uint32_t timeoutMs) {
    std::unique_lock<std::mutex> g(m_);
    if (!cv_.wait_for(g, std::chrono::milliseconds(timeoutMs), [&] { return !q_.empty(); })) {
      return false;
    }
    out = q_.front();
    q_.pop_front();
    return true;
  }

  // Weckt einen wartenden pop() (beim Beenden)
  void wake() {
    cv_.notify_all();
  }

  size_t dropped() {
    std::lock_guard<std::mutex> g(m_);
    return dropped_;
  }

private:
  std::mutex m_;
  std::condition_variable cv_;
  std::deque<T> q_;
  size_t cap_;
  size_t dropped_ = 0;
};
