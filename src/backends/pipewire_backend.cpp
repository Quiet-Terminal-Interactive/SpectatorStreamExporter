#include "pipewire_backend.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include <pipewire/pipewire.h>
#include <pipewire/stream.h>
#include <spa/debug/pod.h>
#include <spa/param/buffers.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/video/raw.h>
#include <spa/pod/builder.h>
#include <spa/utils/dict.h>
#include <spa/utils/result.h>

#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace spectator {

namespace {

static const struct pw_stream_events s_stream_events = {
    PW_VERSION_STREAM_EVENTS,
    /* destroy */ nullptr,
    /* state_changed */ &PipeWireBackend::on_stream_state_changed_c,
    /* control_info */ nullptr,
    /* io_changed */ nullptr,
    /* param_changed */ &PipeWireBackend::on_stream_param_changed_c,
    /* add_buffer */ nullptr,
    /* remove_buffer */ nullptr,
    /* process */ &PipeWireBackend::on_stream_process_c,
    /* drained */ nullptr,
    /* command */ nullptr,
    /* trigger_done */ nullptr,
};

const char *video_format_name(uint32_t format) {
    switch (format) {
        case SPA_VIDEO_FORMAT_BGRA: return "BGRA";
        case SPA_VIDEO_FORMAT_BGRx: return "BGRx";
        case SPA_VIDEO_FORMAT_RGBA: return "RGBA";
        default: return "unknown";
    }
}

void convert_rgba(const uint8_t *src, uint32_t src_width, uint32_t src_height,
                  uint8_t *dst, uint32_t dst_width, uint32_t dst_height, uint32_t format) {
    const uint32_t copy_width = src_width < dst_width ? src_width : dst_width;
    const uint32_t copy_height = src_height < dst_height ? src_height : dst_height;
    const size_t src_stride = (size_t)src_width * 4;
    const size_t dst_stride = (size_t)dst_width * 4;
    const bool swap_rb = format == SPA_VIDEO_FORMAT_BGRA || format == SPA_VIDEO_FORMAT_BGRx;

    if (copy_width != dst_width || copy_height != dst_height) {
        std::memset(dst, 0, dst_stride * dst_height);
    }

    for (uint32_t y = 0; y < copy_height; y++) {
        const uint8_t *s = src + y * src_stride;
        uint8_t *d = dst + y * dst_stride;
        if (!swap_rb) {
            std::memcpy(d, s, (size_t)copy_width * 4);
            continue;
        }
        for (uint32_t x = 0; x < copy_width; x++) {
            d[0] = s[2];
            d[1] = s[1];
            d[2] = s[0];
            d[3] = s[3];
            s += 4;
            d += 4;
        }
    }
}

void log_pod_line(struct spa_debug_context *, const char *fmt, ...) {
    char line[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    UtilityFunctions::print(String("[PipeWireBackend]   ") + String(line));
}

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

void log_pod(const String &label, const struct spa_pod *pod) {
    if (!debug_log_enabled()) {
        return;
    }
    struct spa_debug_context ctx = { &log_pod_line };
    UtilityFunctions::print(String("[PipeWireBackend] ") + label + ":");
    spa_debugc_pod(&ctx, 0, nullptr, pod);
}

void log_advertised_pods(const struct spa_pod *const *params, uint32_t n_params) {
    for (uint32_t i = 0; i < n_params; i++) {
        log_pod(String("EnumFormat pod ") + String::num_int64(i + 1) + "/" + String::num_int64(n_params), params[i]);
    }
}

} // namespace

PipeWireBackend::PipeWireBackend() {
    static bool pw_inited = false;
    if (!pw_inited) {
        pw_init(nullptr, nullptr);
        pw_inited = true;
    }
}

PipeWireBackend::~PipeWireBackend() {
    stop();
}

bool PipeWireBackend::start(const StreamConfig &config, const BackendCallbacks &callbacks) {
    m_config = config;
    m_callbacks = callbacks;

    m_loop = pw_thread_loop_new("spectator-pw-loop", nullptr);
    if (!m_loop) {
        report_error(true, "pw_thread_loop_new failed");
        return false;
    }

    m_context = pw_context_new(pw_thread_loop_get_loop(m_loop), nullptr, 0);
    if (!m_context) {
        report_error(true, "pw_context_new failed");
        stop();
        return false;
    }

    if (pw_thread_loop_start(m_loop) < 0) {
        report_error(true, "pw_thread_loop_start failed");
        stop();
        return false;
    }

    pw_thread_loop_lock(m_loop);

    m_core = pw_context_connect(m_context, nullptr, 0);
    if (!m_core) {
        pw_thread_loop_unlock(m_loop);
        report_error(true, "pw_context_connect failed (is PipeWire running?)");
        stop();
        return false;
    }

    struct pw_properties *props = pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Video",
            PW_KEY_MEDIA_CATEGORY, "Playback",
            PW_KEY_MEDIA_ROLE, "Game",
            PW_KEY_MEDIA_CLASS, "Video/Source",
            PW_KEY_NODE_NAME, m_config.stream_name.c_str(),
            PW_KEY_NODE_DESCRIPTION, m_config.stream_name.c_str(),
            nullptr);

    m_stream = pw_stream_new(m_core, m_config.stream_name.c_str(), props);
    if (!m_stream) {
        pw_thread_loop_unlock(m_loop);
        report_error(true, "pw_stream_new failed");
        stop();
        return false;
    }

    uint8_t buffer[4096];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    struct spa_rectangle size = SPA_RECTANGLE(m_config.width, m_config.height);
    struct spa_fraction framerate = SPA_FRACTION(60, 1);
    const struct spa_pod *params[1];
    const uint32_t n_params = 1;
    params[0] = (const struct spa_pod *)spa_pod_builder_add_object(
            &b,
            SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
            SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
            SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
            SPA_FORMAT_VIDEO_format, SPA_POD_Id(SPA_VIDEO_FORMAT_BGRA),
            SPA_FORMAT_VIDEO_size, SPA_POD_Rectangle(&size),
            SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction(&framerate));
    log_advertised_pods(params, n_params);

    std::memset(&m_stream_listener, 0, sizeof(m_stream_listener));
    pw_stream_add_listener(m_stream, &m_stream_listener, &s_stream_events, this);

    int flags = PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_DRIVER;
    int res = pw_stream_connect(m_stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                                (pw_stream_flags)flags, params, n_params);
    if (res < 0) {
        pw_thread_loop_unlock(m_loop);
        report_error(true, std::string("pw_stream_connect failed: ") + spa_strerror(res));
        stop();
        return false;
    }

    pw_thread_loop_unlock(m_loop);

    m_started = true;
    if (debug_log_enabled()) {
        UtilityFunctions::print(String("[PipeWireBackend] started as '") +
                                String(m_config.stream_name.c_str()) + "' " +
                                String::num_int64(m_config.width) + "x" +
                                String::num_int64(m_config.height));
    }
    if (m_callbacks.on_started) {
        m_callbacks.on_started();
    }
    return true;
}

void PipeWireBackend::stop() {
    if (m_loop) {
        pw_thread_loop_lock(m_loop);
        if (m_stream) {
            pw_stream_disconnect(m_stream);
            pw_stream_destroy(m_stream);
            m_stream = nullptr;
        }
        if (m_core) {
            pw_core_disconnect(m_core);
            m_core = nullptr;
        }
        if (m_context) {
            pw_context_destroy(m_context);
            m_context = nullptr;
        }
        pw_thread_loop_unlock(m_loop);
        pw_thread_loop_stop(m_loop);
        pw_thread_loop_destroy(m_loop);
        m_loop = nullptr;
    }
    m_started = false;
    m_have_consumer = false;
    m_negotiated = false;

    std::lock_guard<std::mutex> lock(m_pending_mutex);
    m_pending_pixels.clear();
    m_pending_available = false;
}

bool PipeWireBackend::push_frame(const ExportedFrame &frame) {
    if (!m_started.load()) {
        return false;
    }
    if (!m_have_consumer.load() || !m_negotiated.load()) {
        return false;
    }

    const PackedByteArray &data = frame.pixels;
    if (data.is_empty()) {
        return false;
    }

    const size_t src_bytes = (size_t)frame.width * 4 * (size_t)frame.height;
    if ((size_t)data.size() < src_bytes) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_pending_mutex);
        const uint32_t width = m_negotiated_width;
        const uint32_t height = m_negotiated_height;
        const uint32_t stride = width * 4;
        m_pending_pixels.resize((size_t)stride * height);
        convert_rgba(data.ptr(), frame.width, frame.height,
                     m_pending_pixels.data(), width, height, m_negotiated_format);
        m_pending_width = width;
        m_pending_height = height;
        m_pending_stride = stride;
        m_pending_frame_index = frame.frame_index;
        m_pending_available = true;
    }

    if (m_loop) {
        pw_thread_loop_lock(m_loop);
        if (m_stream) {
            pw_stream_trigger_process(m_stream);
        }
        pw_thread_loop_unlock(m_loop);
    }

    return true;
}

void PipeWireBackend::on_stream_state_changed_c(void *data, enum pw_stream_state old_state, enum pw_stream_state new_state, const char *error) {
    static_cast<PipeWireBackend *>(data)->on_stream_state_changed(old_state, new_state, error ? error : "");
}
void PipeWireBackend::on_stream_param_changed_c(void *data, uint32_t id, const struct spa_pod *param) {
    static_cast<PipeWireBackend *>(data)->on_stream_param_changed(id, param);
}
void PipeWireBackend::on_stream_process_c(void *data) {
    static_cast<PipeWireBackend *>(data)->on_stream_process();
}

void PipeWireBackend::on_stream_state_changed(enum pw_stream_state old_state, enum pw_stream_state new_state, const char *error) {
    // States we care about: PAUSED (ready, no consumer), STREAMING (consumer attached), ERROR (fatal), UNCONNECTED (torn down)
    switch (new_state) {
        case PW_STREAM_STATE_STREAMING:
            m_have_consumer = true;
            UtilityFunctions::print("[PipeWireBackend] state → STREAMING (consumer attached)");
            break;
        case PW_STREAM_STATE_PAUSED:
            m_have_consumer = false;
            break;
        case PW_STREAM_STATE_ERROR:
            report_error(true, std::string("pw stream ERROR: ") + (error ? error : "unknown"));
            break;
        default:
            break;
    }
}

void PipeWireBackend::on_stream_param_changed(uint32_t id, const struct spa_pod *param) {
    if (id != SPA_PARAM_Format) {
        return;
    }
    if (!param) {
        m_negotiated = false;
        if (debug_log_enabled()) {
            UtilityFunctions::print("[PipeWireBackend] format cleared");
        }
        return;
    }
    log_pod("negotiated Format pod", param);

    struct spa_video_info_raw info{};
    if (spa_format_video_raw_parse(param, &info) < 0) {
        m_negotiated = false;
        report_error(false, "failed to parse negotiated video format");
        return;
    }

    const bool size_matches = info.size.width == m_config.width && info.size.height == m_config.height;
    if (debug_log_enabled()) {
        String msg = String("[PipeWireBackend] format negotiated: ") + video_format_name(info.format) +
                     " (" + String::num_int64(info.format) + ") " +
                     String::num_int64(info.size.width) + "x" + String::num_int64(info.size.height) +
                     " @ " + String::num_int64(info.framerate.num) + "/" + String::num_int64(info.framerate.denom) +
                     ", viewport is " + String::num_int64(m_config.width) + "x" + String::num_int64(m_config.height);
        if (!size_matches) {
            msg += " (size MISMATCH, frames will be cropped/padded)";
        }
        UtilityFunctions::print(msg);
    }

    if (info.format != SPA_VIDEO_FORMAT_BGRA) {
        m_negotiated = false;
        report_error(false, "consumer negotiated an unsupported video format");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_pending_mutex);
        m_negotiated_format = info.format;
        m_negotiated_width = info.size.width;
        m_negotiated_height = info.size.height;
        m_pending_available = false;
    }

    const int32_t stride = (int32_t)info.size.width * 4;
    const int32_t size = stride * (int32_t)info.size.height;
    if (debug_log_enabled()) {
        UtilityFunctions::print(String("[PipeWireBackend] Buffers: size=") + String::num_int64(size) +
                                " stride=" + String::num_int64(stride));
    }
    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod *params[1];
    params[0] = (const struct spa_pod *)spa_pod_builder_add_object(
            &b,
            SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
            SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int(4, 2, 8),
            SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(1),
            SPA_PARAM_BUFFERS_size, SPA_POD_Int(size),
            SPA_PARAM_BUFFERS_stride, SPA_POD_Int(stride),
            SPA_PARAM_BUFFERS_align, SPA_POD_Int(16),
            SPA_PARAM_BUFFERS_dataType,
                SPA_POD_CHOICE_FLAGS_Int((1 << SPA_DATA_MemPtr) | (1 << SPA_DATA_MemFd)));
    log_pod("submitted Buffers pod", params[0]);
    int res = pw_stream_update_params(m_stream, params, 1);
    if (res < 0) {
        report_error(false, std::string("pw_stream_update_params(Buffers) failed: ") + spa_strerror(res));
    }

    m_logged_small_buffer = false;
    m_negotiated = true;
}

void PipeWireBackend::on_stream_process() {
    const bool debug = debug_log_enabled();
    static uint64_t s_calls = 0;
    String tag;
    if (debug) {
        tag = String("[PipeWireBackend] process #") + String::num_uint64(++s_calls) + ": ";
    }

    if (!m_stream) {
        return;
    }
    struct pw_buffer *pb = pw_stream_dequeue_buffer(m_stream);
    if (!pb) {
        if (debug) {
            UtilityFunctions::print(tag + "dequeue=NO (no free buffer)");
        }
        return;
    }
    struct spa_buffer *buf = pb->buffer;
    if (!buf || buf->n_datas == 0) {
        pw_stream_queue_buffer(m_stream, pb);
        return;
    }
    struct spa_data &d = buf->datas[0];
    if (d.type != SPA_DATA_MemPtr && d.type != SPA_DATA_MemFd) {
        if (debug) {
            UtilityFunctions::print(tag + "unsupported data type " + String::num_int64(d.type));
        }
        pw_stream_queue_buffer(m_stream, pb);
        return;
    }
    void *dst = d.data;
    if (!dst) {
        pw_stream_queue_buffer(m_stream, pb);
        return;
    }

    bool had_frame = false;
    bool too_small = false;
    uint32_t frame_bytes = 0;
    {
        std::lock_guard<std::mutex> lock(m_pending_mutex);
        had_frame = m_pending_available;
        d.chunk->offset = 0;
        d.chunk->stride = 0;
        d.chunk->size = 0;
        if (m_pending_available) {
            const uint32_t stride = m_pending_width * 4;
            frame_bytes = stride * m_pending_height;
            if (d.maxsize < frame_bytes) {
                too_small = true;
            } else {
                std::memcpy(dst, m_pending_pixels.data(), frame_bytes);
                d.chunk->offset = 0;
                d.chunk->size = frame_bytes;
                d.chunk->stride = (int32_t)stride;
            }
        }
    }

    if (too_small && !m_logged_small_buffer) {
        m_logged_small_buffer = true;
        report_error(false, std::string("PipeWire buffer too small: maxsize=") + std::to_string(d.maxsize) +
                            " < frame_bytes=" + std::to_string(frame_bytes) + ", skipping frames");
    }

    if (debug) {
        String msg = tag + "type=" + String::num_int64(d.type) +
                     " maxsize=" + String::num_int64(d.maxsize) +
                     " chunk.size=" + String::num_int64(d.chunk->size) +
                     " chunk.stride=" + String::num_int64(d.chunk->stride) +
                     " frame_available=" + (had_frame ? "yes" : "NO");
        if (d.chunk->size >= 4) {
            const uint8_t *px = static_cast<const uint8_t *>(dst);
            msg += String(" first4=") + String::num_int64(px[0], 16).pad_zeros(2) + " " +
                   String::num_int64(px[1], 16).pad_zeros(2) + " " +
                   String::num_int64(px[2], 16).pad_zeros(2) + " " +
                   String::num_int64(px[3], 16).pad_zeros(2);
        }
        UtilityFunctions::print(msg);
    }

    pw_stream_queue_buffer(m_stream, pb);
}

void PipeWireBackend::report_error(bool fatal, const std::string &msg) {
    UtilityFunctions::push_error(String("[PipeWireBackend] ") + String(msg.c_str()));
    if (m_callbacks.on_error) {
        m_callbacks.on_error(fatal, msg);
    }
}

} // namespace spectator
