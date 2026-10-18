#pragma once

#include "i_stream_backend.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include <pipewire/stream.h>
#include <spa/pod/pod.h>
#include <spa/utils/hook.h>

struct pw_thread_loop;
struct pw_context;
struct pw_core;
struct pw_stream;

namespace spectator {

class PipeWireBackend : public IStreamBackend {
public:
    PipeWireBackend();
    ~PipeWireBackend() override;

    bool start(const StreamConfig &config, const BackendCallbacks &callbacks) override;
    void stop() override;
    bool push_frame(const ExportedFrame &frame) override;
    bool wants_frames() const override { return m_started.load() && m_have_consumer.load() && m_negotiated.load(); }
    const char *name() const override { return "pipewire"; }

    static void on_stream_state_changed_c(void *data, enum pw_stream_state old_state, enum pw_stream_state new_state, const char *error);
    static void on_stream_param_changed_c(void *data, uint32_t id, const struct spa_pod *param);
    static void on_stream_process_c(void *data);

private:

    void on_stream_state_changed(enum pw_stream_state old_state, enum pw_stream_state new_state, const char *error);
    void on_stream_param_changed(uint32_t id, const struct spa_pod *param);
    void on_stream_process();

    void report_error(bool fatal, const std::string &msg);

    StreamConfig m_config;
    BackendCallbacks m_callbacks;

    pw_thread_loop *m_loop = nullptr;
    pw_context *m_context = nullptr;
    pw_core *m_core = nullptr;
    pw_stream *m_stream = nullptr;
    struct spa_hook m_stream_listener{};
    std::atomic<bool> m_started{false};
    std::atomic<bool> m_have_consumer{false};

    struct PendingFrame {
        std::vector<uint8_t> pixels;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t stride = 0;
        uint64_t frame_index = 0;
    };
    static constexpr size_t kPendingCapacity = 3;

    std::mutex m_pending_mutex;
    std::deque<PendingFrame> m_pending_queue;
    uint32_t m_negotiated_format = 0;
    uint32_t m_negotiated_width = 0;
    uint32_t m_negotiated_height = 0;
    std::atomic<bool> m_negotiated{false};
    bool m_logged_small_buffer = false;
};

} // namespace spectator
