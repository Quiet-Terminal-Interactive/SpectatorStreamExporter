#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

#include <godot_cpp/variant/packed_byte_array.hpp>

namespace spectator {

struct FrameToken {
    uint64_t frame_index = 0;
    godot::PackedByteArray pixels;
    uint64_t native_handle = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    int32_t godot_format = 0;
    bool valid = false;
};

class FrameQueue {
public:
    static constexpr size_t CAPACITY = 3;

    FrameQueue() = default;
    FrameQueue(const FrameQueue &) = delete;
    FrameQueue &operator=(const FrameQueue &) = delete;

    bool try_publish(const FrameToken &token) {
        bool dropped = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_size == CAPACITY) {
                m_head = (m_head + 1) % CAPACITY;
                m_size--;
                dropped = true;
            }
            m_buffer[(m_head + m_size) % CAPACITY] = token;
            m_size++;
        }
        m_cv.notify_one();
        return dropped;
    }

    bool wait_pop(FrameToken &out) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this]() { return m_size > 0 || m_stopped.load(); });
        if (m_size == 0) {
            return false;
        }
        out = m_buffer[m_head];
        m_head = (m_head + 1) % CAPACITY;
        m_size--;
        return true;
    }

    void stop() {
        m_stopped.store(true);
        m_cv.notify_all();
    }

    void reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_head = 0;
        m_size = 0;
        m_stopped.store(false);
    }

    bool is_stopped() const { return m_stopped.load(); }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    FrameToken m_buffer[CAPACITY]{};
    size_t m_head = 0;
    size_t m_size = 0;
    std::atomic<bool> m_stopped{false};
};

} // namespace spectator
