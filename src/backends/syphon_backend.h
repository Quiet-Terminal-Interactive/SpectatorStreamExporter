#pragma once

#include "i_stream_backend.h"

#include <atomic>
#include <vector>

namespace spectator {

class SyphonBackend : public IStreamBackend {
public:
    SyphonBackend();
    ~SyphonBackend() override;

    bool start(const StreamConfig &config, const BackendCallbacks &callbacks) override;
    void stop() override;
    bool push_frame(const ExportedFrame &frame) override;
    const char *name() const override { return "syphon"; }

private:
    void report_error(bool fatal, const std::string &msg);

    StreamConfig m_config;
    BackendCallbacks m_callbacks;
    std::atomic<bool> m_started{false};

    void *m_metal_device = nullptr;
    void *m_metal_queue = nullptr;
    void *m_metal_texture = nullptr;
    void *m_syphon_server = nullptr;
    std::vector<uint8_t> m_scratch;
};

} // namespace spectator
