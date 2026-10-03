#include "null_backend.h"

#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/string.hpp>

using namespace godot;

namespace spectator {

bool NullBackend::start(const StreamConfig &config, const BackendCallbacks &callbacks) {
    m_config = config;
    m_callbacks = callbacks;
    m_started = true;
    UtilityFunctions::print(String("[NullBackend] start name=") + String(config.stream_name.c_str()) +
                            " size=" + String::num_int64(config.width) + "x" + String::num_int64(config.height));
    if (m_callbacks.on_started) {
        m_callbacks.on_started();
    }
    return true;
}

void NullBackend::stop() {
    if (!m_started) {
        return;
    }
    UtilityFunctions::print("[NullBackend] stop");
    m_started = false;
}

bool NullBackend::push_frame(const ExportedFrame &frame) {
    if ((frame.frame_index % 60) == 0) {
        UtilityFunctions::print(String("[NullBackend] would push frame ") +
                                String::num_int64(frame.frame_index) +
                                " handle=0x" + String::num_int64(frame.native_handle, 16) +
                                " " + String::num_int64(frame.width) + "x" + String::num_int64(frame.height));
    }
    return true;
}

} // namespace spectator
