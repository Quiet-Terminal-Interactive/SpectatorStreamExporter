#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <godot_cpp/variant/packed_byte_array.hpp>

namespace spectator {

struct StreamConfig {
    std::string stream_name;
    uint32_t width = 0;
    uint32_t height = 0;
    int32_t godot_format = 0;
};

struct ExportedFrame {
    uint64_t frame_index = 0;
    godot::PackedByteArray pixels;
    uint64_t native_handle = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    int32_t godot_format = 0;
};

struct BackendCallbacks {
    std::function<void()> on_started;
    std::function<void(bool fatal, const std::string &message)> on_error;
};

class IStreamBackend {
public:
    virtual ~IStreamBackend() = default;

    virtual bool start(const StreamConfig &config, const BackendCallbacks &callbacks) = 0;

    virtual void stop() = 0;

    virtual bool push_frame(const ExportedFrame &frame) = 0;

    virtual bool wants_frames() const { return true; }

    virtual const char *name() const = 0;
};

} // namespace spectator
