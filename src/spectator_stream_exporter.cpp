#include "spectator_stream_exporter.h"

#include <cstdlib>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "backends/null_backend.h"

#if defined(SPECTATOR_STREAM_LINUX)
#include "backends/pipewire_backend.h"
#elif defined(SPECTATOR_STREAM_WINDOWS)
#include "backends/spout_backend.h"
#elif defined(SPECTATOR_STREAM_MACOS)
#include "backends/syphon_backend.h"
#endif

using namespace godot;

namespace {

#ifdef SPECTATOR_STREAM_DEBUG_LOG
constexpr bool k_debug_log = true;
#else
constexpr bool k_debug_log = false;
#endif

inline void debug_log(const char *msg) {
    if (k_debug_log) {
        UtilityFunctions::print(String("[SpectatorStreamExporter] ") + msg);
    }
}

} // namespace

SpectatorStreamExporter::SpectatorStreamExporter() {}

SpectatorStreamExporter::~SpectatorStreamExporter() {
    if (m_engaged) {
        disengage();
    }
}

void SpectatorStreamExporter::_notification(int p_what) {
    switch (p_what) {
        case NOTIFICATION_EXIT_TREE:
            if (m_engaged) {
                disengage();
            }
            break;
        default:
            break;
    }
}

void SpectatorStreamExporter::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_target_viewport", "path"), &SpectatorStreamExporter::set_target_viewport);
    ClassDB::bind_method(D_METHOD("get_target_viewport"), &SpectatorStreamExporter::get_target_viewport);
    ClassDB::bind_method(D_METHOD("set_stream_name", "name"), &SpectatorStreamExporter::set_stream_name);
    ClassDB::bind_method(D_METHOD("get_stream_name"), &SpectatorStreamExporter::get_stream_name);
    ClassDB::bind_method(D_METHOD("set_active", "active"), &SpectatorStreamExporter::set_active);
    ClassDB::bind_method(D_METHOD("is_active"), &SpectatorStreamExporter::is_active);
    ClassDB::bind_method(D_METHOD("set_target_fps", "fps"), &SpectatorStreamExporter::set_target_fps);
    ClassDB::bind_method(D_METHOD("get_target_fps"), &SpectatorStreamExporter::get_target_fps);
    ClassDB::bind_method(D_METHOD("restart"), &SpectatorStreamExporter::restart);

    ClassDB::bind_method(D_METHOD("_on_backend_started"), &SpectatorStreamExporter::_on_backend_started);
    ClassDB::bind_method(D_METHOD("_on_backend_error", "fatal", "message"), &SpectatorStreamExporter::_on_backend_error);

    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "target_viewport"), "set_target_viewport", "get_target_viewport");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "stream_name"), "set_stream_name", "get_stream_name");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "active"), "set_active", "is_active");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "target_fps"), "set_target_fps", "get_target_fps");

    ADD_SIGNAL(MethodInfo("stream_started"));
    ADD_SIGNAL(MethodInfo("stream_stopped"));
    ADD_SIGNAL(MethodInfo("stream_error", PropertyInfo(Variant::STRING, "message")));
}

void SpectatorStreamExporter::set_target_viewport(const NodePath &p_path) {
    m_target_viewport_path = p_path;
}
NodePath SpectatorStreamExporter::get_target_viewport() const {
    return m_target_viewport_path;
}

void SpectatorStreamExporter::set_stream_name(const String &p_name) {
    m_stream_name = p_name;
}
String SpectatorStreamExporter::get_stream_name() const {
    return m_stream_name;
}

void SpectatorStreamExporter::set_target_fps(int p_fps) {
    m_target_fps = p_fps < 0 ? 0 : p_fps;
}
int SpectatorStreamExporter::get_target_fps() const {
    return m_target_fps;
}

bool SpectatorStreamExporter::is_active() const {
    return m_active;
}

void SpectatorStreamExporter::set_active(bool p_active) {
    if (p_active == m_active) {
        return;
    }
    if (p_active) {
        if (engage()) {
            m_active = true;
        } else {
            // engage() already emitted stream_error
            m_active = false;
        }
    } else {
        disengage();
        m_active = false;
        emit_signal("stream_stopped");
    }
}

void SpectatorStreamExporter::restart() {
    if (!m_active) {
        return;
    }
    disengage();
    if (!engage()) {
        m_active = false;
        emit_signal("stream_stopped");
    }
}

bool SpectatorStreamExporter::engage() {
    if (m_engaged) {
        return true;
    }

    Node *node = get_node_or_null(m_target_viewport_path);
    SubViewport *sub = Object::cast_to<SubViewport>(node);
    if (!sub) {
        String msg = "target_viewport does not resolve to a SubViewport: " + m_target_viewport_path;
        UtilityFunctions::push_error(msg);
        emit_signal("stream_error", msg);
        return false;
    }

    Vector2i size = sub->get_size();
    if (size.x <= 0 || size.y <= 0) {
        String msg = "SubViewport has zero-sized rendering area";
        UtilityFunctions::push_error(msg);
        emit_signal("stream_error", msg);
        return false;
    }
    m_last_width = (uint32_t)size.x;
    m_last_height = (uint32_t)size.y;
    m_target_viewport_id = sub->get_instance_id();

    m_backend = create_backend();
    if (!m_backend) {
        String msg = "no stream backend available for this platform";
        UtilityFunctions::push_error(msg);
        emit_signal("stream_error", msg);
        return false;
    }

    spectator::StreamConfig cfg;
    cfg.stream_name = m_stream_name.utf8().get_data();
    cfg.width = m_last_width;
    cfg.height = m_last_height;
    cfg.godot_format = 0;

    spectator::BackendCallbacks cbs;
    uint64_t id = get_instance_id();
    cbs.on_started = [id]() {
        Object *obj = ObjectDB::get_instance(id);
        if (obj) {
            obj->call_deferred("_on_backend_started");
        }
    };
    cbs.on_error = [id](bool fatal, const std::string &message) {
        Object *obj = ObjectDB::get_instance(id);
        if (obj) {
            obj->call_deferred("_on_backend_error", fatal, String(message.c_str()));
        }
    };

    if (!m_backend->start(cfg, cbs)) {
        String msg = String("backend '") + m_backend->name() + "' failed to start";
        UtilityFunctions::push_error(msg);
        emit_signal("stream_error", msg);
        m_backend.reset();
        return false;
    }

    m_queue.reset();
    m_frame_index = 0;
    m_last_publish_usec = 0;

    m_worker = std::thread([this]() { worker_main(); });

    RenderingServer *rs = RenderingServer::get_singleton();
    if (rs) {
        Callable cb = callable_mp(this, &SpectatorStreamExporter::on_frame_post_draw);
        if (!rs->is_connected("frame_post_draw", cb)) {
            rs->connect("frame_post_draw", cb);
        }
    }

    m_engaged = true;
    debug_log("engaged");
    return true;
}

void SpectatorStreamExporter::disengage() {
    if (!m_engaged) {
        return;
    }

    RenderingServer *rs = RenderingServer::get_singleton();
    if (rs) {
        Callable cb = callable_mp(this, &SpectatorStreamExporter::on_frame_post_draw);
        if (rs->is_connected("frame_post_draw", cb)) {
            rs->disconnect("frame_post_draw", cb);
        }
    }

    m_queue.stop();
    if (m_worker.joinable()) {
        m_worker.join();
    }

    if (m_backend) {
        m_backend->stop();
        m_backend.reset();
    }

    m_target_viewport_id = 0;
    m_engaged = false;
    debug_log("disengaged");
}

void SpectatorStreamExporter::on_frame_post_draw() {
    if (m_target_viewport_id == 0) {
        return;
    }
    Object *obj = ObjectDB::get_instance(m_target_viewport_id);
    SubViewport *sub = Object::cast_to<SubViewport>(obj);
    if (!sub) {
        return; // Viewport was freed, skip silently
    }

    if (m_target_fps > 0) {
        uint64_t now_usec = Time::get_singleton()->get_ticks_usec();
        uint64_t min_interval = 1'000'000ULL / (uint64_t)m_target_fps;
        if (m_last_publish_usec != 0 && (now_usec - m_last_publish_usec) < min_interval) {
            return;
        }
        m_last_publish_usec = now_usec;
    }

    Ref<ViewportTexture> vp_tex = sub->get_texture();
    if (vp_tex.is_null()) {
        return;
    }
    RID tex_rid = vp_tex->get_rid();
    if (!tex_rid.is_valid()) {
        return;
    }

    if (!m_backend || !m_backend->wants_frames()) {
        return;
    }

    RenderingServer *rs = RenderingServer::get_singleton();
    if (!rs) {
        return;
    }

    RID rd_tex = rs->texture_get_rd_texture(tex_rid);
    if (!rd_tex.is_valid()) {
        // Not-yet-initialized viewport texture (first frame after add)
        return;
    }

    Vector2i size = sub->get_size();
    rs->call_on_render_thread(callable_mp_static(&SpectatorStreamExporter::_dispatch_readback).bind(
            (int64_t)get_instance_id(), rd_tex, (int64_t)++m_frame_index, (int64_t)size.x, (int64_t)size.y));
}

void SpectatorStreamExporter::_dispatch_readback(int64_t p_owner_id, const RID &p_rd_tex, int64_t p_frame_index, int64_t p_width, int64_t p_height) {
    RenderingDevice *rd = RenderingServer::get_singleton()->get_rendering_device();
    if (!rd) {
        // Compatibility renderer with no exposed RenderingDevice
        return;
    }

    uint64_t native_handle = rd->texture_get_native_handle(p_rd_tex);
    if (native_handle == 0) {
        return;
    }

    if (k_debug_log) {
        UtilityFunctions::print(vformat(
                "[SpectatorStreamExporter] frame=%d rd_rid=%d handle=0x%x size=%dx%d",
                p_frame_index, (int64_t)p_rd_tex.get_id(), (int64_t)native_handle, p_width, p_height));
    }

    rd->texture_get_data_async(p_rd_tex, 0, callable_mp_static(&SpectatorStreamExporter::_on_readback_done).bind(
            p_owner_id, p_frame_index, (int64_t)native_handle, p_width, p_height));
}

void SpectatorStreamExporter::_on_readback_done(const PackedByteArray &p_data, int64_t p_owner_id, int64_t p_frame_index, int64_t p_native_handle, int64_t p_width, int64_t p_height) {
    SpectatorStreamExporter *self = Object::cast_to<SpectatorStreamExporter>(ObjectDB::get_instance((uint64_t)p_owner_id));
    if (!self || self->m_queue.is_stopped()) {
        return; // Freed or disengaged while the download was in flight
    }

    spectator::FrameToken tok;
    tok.frame_index = (uint64_t)p_frame_index;
    tok.pixels = p_data;
    tok.native_handle = (uint64_t)p_native_handle;
    tok.width = (uint32_t)p_width;
    tok.height = (uint32_t)p_height;
    tok.godot_format = 0;
    tok.valid = true;

    self->m_queue.try_publish(tok);
}

void SpectatorStreamExporter::worker_main() {
    while (true) {
        spectator::FrameToken tok;
        if (!m_queue.wait_pop(tok)) {
            break; // stopped
        }
        if (!tok.valid || !m_backend) {
            continue;
        }

        spectator::ExportedFrame f;
        f.frame_index = tok.frame_index;
        f.pixels = tok.pixels;
        f.native_handle = tok.native_handle;
        f.width = tok.width;
        f.height = tok.height;
        f.godot_format = tok.godot_format;

        m_backend->push_frame(f);
    }
}

std::unique_ptr<spectator::IStreamBackend> SpectatorStreamExporter::create_backend() {
    const char *force_null = std::getenv("SPECTATOR_STREAM_NULL_BACKEND");
    if (force_null && force_null[0] == '1') {
        return std::make_unique<spectator::NullBackend>();
    }

#if defined(SPECTATOR_STREAM_LINUX)
    return std::make_unique<spectator::PipeWireBackend>();
#elif defined(SPECTATOR_STREAM_WINDOWS)
    return std::make_unique<spectator::SpoutBackend>();
#elif defined(SPECTATOR_STREAM_MACOS)
    return std::make_unique<spectator::SyphonBackend>();
#else
    return std::make_unique<spectator::NullBackend>();
#endif
}

void SpectatorStreamExporter::_on_backend_started() {
    emit_signal("stream_started");
}

void SpectatorStreamExporter::_on_backend_error(bool fatal, const String &message) {
    emit_signal("stream_error", message);
    if (fatal) {
        if (m_active) {
            disengage();
            m_active = false;
            emit_signal("stream_stopped");
        }
    }
}
