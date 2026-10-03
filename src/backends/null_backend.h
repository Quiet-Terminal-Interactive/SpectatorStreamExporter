#pragma once

#include "i_stream_backend.h"

namespace spectator {

class NullBackend : public IStreamBackend {
public:
    bool start(const StreamConfig &config, const BackendCallbacks &callbacks) override;
    void stop() override;
    bool push_frame(const ExportedFrame &frame) override;
    const char *name() const override { return "null"; }

private:
    BackendCallbacks m_callbacks;
    StreamConfig m_config;
    bool m_started = false;
};

} // namespace spectator
