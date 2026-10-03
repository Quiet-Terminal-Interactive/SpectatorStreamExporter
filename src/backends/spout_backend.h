#pragma once

#include "i_stream_backend.h"

#include <atomic>
#include <cstdint>
#include <vector>

class spoutDX;

namespace spectator {

class SpoutBackend : public IStreamBackend {
public:
    SpoutBackend();
    ~SpoutBackend() override;

    bool start(const StreamConfig &config, const BackendCallbacks &callbacks) override;
    void stop() override;
    bool push_frame(const ExportedFrame &frame) override;
    const char *name() const override { return "spout"; }

private:
    void report_error(bool fatal, const std::string &msg);

    StreamConfig m_config;
    BackendCallbacks m_callbacks;
    spoutDX *m_sender = nullptr;
    std::atomic<bool> m_started{false};
    std::vector<uint8_t> m_scratch;
};

} // namespace spectator
