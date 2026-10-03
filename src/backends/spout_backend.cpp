#include "spout_backend.h"

#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#if __has_include(<SpoutDX.h>)
#include <SpoutDX.h>
#define SPECTATOR_SPOUT_AVAILABLE 1
#else
#define SPECTATOR_SPOUT_AVAILABLE 0
class spoutDX {
public:
    void ReleaseSender() {}
    bool SendImage(const unsigned char *, unsigned int, unsigned int, unsigned int = 0) { return false; }
};
#endif

using namespace godot;

namespace spectator {

namespace {

constexpr const char *k_spout_log_file = "SpectatorStreamSpout.log";

bool debug_log_enabled() {
    static const bool enabled = [] {
#ifdef SPECTATOR_STREAM_DEBUG_LOG
        return true;
#endif
        const char *v = std::getenv("SPECTATOR_STREAM_DEBUG_LOG");
        return v && std::strcmp(v, "1") == 0;
    }();
    return enabled;
}

void debug_print(const String &msg) {
    UtilityFunctions::print(String("[SpoutBackend] ") + msg);
}

void rgba_to_bgra(const uint8_t *src, uint8_t *dst, size_t pixel_count) {
    for (size_t i = 0; i < pixel_count; i++) {
        dst[0] = src[2];
        dst[1] = src[1];
        dst[2] = src[0];
        dst[3] = src[3];
        src += 4;
        dst += 4;
    }
}

#if SPECTATOR_SPOUT_AVAILABLE

constexpr DXGI_FORMAT k_sender_format = DXGI_FORMAT_B8G8R8A8_UNORM;

void log_create_sender_begin(const StreamConfig &config) {
    spoututils::SetSpoutLogLevel(spoututils::SPOUT_LOG_VERBOSE);
    spoututils::EnableSpoutLog();
    spoututils::EnableSpoutLogFile(k_spout_log_file);
    debug_print(String("Spout logging enabled (console + ") + String(spoututils::GetSpoutLogPath().c_str()) + ")");

    debug_print(String("creating spoutDX sender: name='") + String(config.stream_name.c_str()) + "', width=" +
                String::num_int64(config.width) + ", height=" + String::num_int64(config.height) +
                ", format=" + String::num_int64(k_sender_format) + " (DXGI_FORMAT_B8G8R8A8_UNORM)");
}

String failed_step(spoutDX &sender, const StreamConfig &config, const std::string &log) {
    struct LogMarker {
        const char *text;
        const char *step;
    };
    static const LogMarker markers[] = {
        { "device creation failed", "OpenDirectX11: D3D11 device creation failed" },
        { "could not create shared texture", "CheckSender -> CreateSharedDX11Texture: shared texture creation failed" },
        { "could not get create sender", "CheckSender -> sendernames.CreateSender: sender name registration in shared memory failed" },
    };
    for (const LogMarker &m : markers) {
        if (log.find(m.text) != std::string::npos) {
            return m.step;
        }
    }

    if (!sender.GetDX11Device()) {
        return "OpenDirectX11 (inferred: no D3D11 device)";
    }
    if (config.width == 0 || config.height == 0) {
        return "CheckSender: width or height is 0";
    }
    return "unknown (no matching Spout log line; see the log above)";
}

void log_create_sender_failure(spoutDX &sender, const StreamConfig &config) {
    const std::string log = spoututils::GetSpoutLog();

    debug_print("---- Spout log ----");
    if (log.empty()) {
        debug_print("  (empty or unreadable: " + String(spoututils::GetSpoutLogPath().c_str()) + ")");
    }
    std::istringstream lines(log);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        debug_print(String("  ") + String(line.c_str()));
    }
    debug_print("---- end Spout log ----");

    const int current = sender.GetAdapter();
    const int count = sender.GetNumAdapters();
    debug_print(String("GetAdapter() = ") + String::num_int64(current) + ", GetNumAdapters() = " + String::num_int64(count));
    for (int i = 0; i < count; i++) {
        char name[256] = {};
        const bool ok = sender.GetAdapterName(i, name, (int)sizeof(name));
        debug_print(String("  GetAdapterName(") + String::num_int64(i) + ") = " +
                    (ok ? String("'") + String(name) + "'" : String("failed")) +
                    (i == current ? " <- current" : ""));
    }
    debug_print(String("D3D11 device: ") + (sender.GetDX11Device() ? "created" : "none") +
                ", sender initialized: " + (sender.IsInitialized() ? "yes" : "no"));

    debug_print(String("failed step: ") + failed_step(sender, config, log));
}

#endif

} // namespace

SpoutBackend::SpoutBackend() = default;

SpoutBackend::~SpoutBackend() {
    stop();
}

bool SpoutBackend::start(const StreamConfig &config, const BackendCallbacks &callbacks) {
    m_config = config;
    m_callbacks = callbacks;

#if !SPECTATOR_SPOUT_AVAILABLE
    report_error(true, "Spout SDK not linked in this build (SPOUT_SDK env var was unset)");
    return false;
#else
    const bool debug = debug_log_enabled();
    if (debug) {
        log_create_sender_begin(m_config);
    }

    m_sender = new spoutDX();
    const char *failed = nullptr;

    m_sender->OpenDirectX11();
    if (!m_sender->GetDX11Device()) {
        failed = "spoutDX::OpenDirectX11 created no D3D11 device";
    } else {
        m_sender->SetSenderName(m_config.stream_name.c_str());
        m_sender->SetSenderFormat(k_sender_format);
        if (debug) {
            debug_print(String("sender name after SetSenderName: '") + String(m_sender->GetName()) + "'");
        }

        std::vector<uint8_t> black((size_t)m_config.width * (size_t)m_config.height * 4, 0);
        if (!m_sender->SendImage(black.data(), m_config.width, m_config.height) || !m_sender->IsInitialized()) {
            failed = "spoutDX::SendImage could not create the sender";
        }
    }

    if (failed) {
        if (debug) {
            log_create_sender_failure(*m_sender, m_config);
        }
        report_error(true, failed);
        delete m_sender;
        m_sender = nullptr;
        return false;
    }

    m_started = true;
    UtilityFunctions::print(String("[SpoutBackend] started as '") +
                            String(m_sender->GetName()) + "' " +
                            String::num_int64(m_config.width) + "x" +
                            String::num_int64(m_config.height));
    if (m_callbacks.on_started) {
        m_callbacks.on_started();
    }
    return true;
#endif
}

void SpoutBackend::stop() {
    if (m_sender) {
        m_sender->ReleaseSender();
        delete m_sender;
        m_sender = nullptr;
    }
    m_started = false;
    m_scratch.clear();
    m_scratch.shrink_to_fit();
}

bool SpoutBackend::push_frame(const ExportedFrame &frame) {
    if (!m_started.load() || !m_sender) {
        return false;
    }

    const PackedByteArray &data = frame.pixels;
    const size_t pixel_count = (size_t)frame.width * (size_t)frame.height;
    const size_t need = pixel_count * 4;
    if ((size_t)data.size() < need) {
        return false;
    }

    m_scratch.resize(need);
    rgba_to_bgra(data.ptr(), m_scratch.data(), pixel_count);

    m_sender->SendImage(m_scratch.data(), frame.width, frame.height);
    return true;
}

void SpoutBackend::report_error(bool fatal, const std::string &msg) {
    UtilityFunctions::push_error(String("[SpoutBackend] ") + String(msg.c_str()));
    if (m_callbacks.on_error) {
        m_callbacks.on_error(fatal, msg);
    }
}

} // namespace spectator
