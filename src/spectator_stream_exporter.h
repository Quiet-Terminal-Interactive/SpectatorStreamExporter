#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/string.hpp>

#include "backends/i_stream_backend.h"
#include "frame_queue.h"

namespace godot {

class SpectatorStreamExporter : public Node {
    GDCLASS(SpectatorStreamExporter, Node)

public:
    SpectatorStreamExporter();
    ~SpectatorStreamExporter() override;

    void _notification(int p_what);

    void set_target_viewport(const NodePath &p_path);
    NodePath get_target_viewport() const;

    void set_stream_name(const String &p_name);
    String get_stream_name() const;

    void set_active(bool p_active);
    bool is_active() const;

    void set_target_fps(int p_fps);
    int get_target_fps() const;

    void restart();

protected:
    static void _bind_methods();

private:
    NodePath m_target_viewport_path;
    String m_stream_name = "GodotSpectator";
    bool m_active = false;
    int m_target_fps = 0;
    bool m_engaged = false;
    uint64_t m_frame_index = 0;
    uint64_t m_last_publish_usec = 0;
    uint32_t m_last_width = 0;
    uint32_t m_last_height = 0;
    uint64_t m_target_viewport_id = 0;

    spectator::FrameQueue m_queue;
    std::thread m_worker;
    std::unique_ptr<spectator::IStreamBackend> m_backend;

    bool engage();
    void disengage();
    void on_frame_post_draw();
    static void _dispatch_readback(int64_t p_owner_id, const RID &p_rd_tex, int64_t p_frame_index, int64_t p_width, int64_t p_height);
    static void _on_readback_done(const PackedByteArray &p_data, int64_t p_owner_id, int64_t p_frame_index, int64_t p_native_handle, int64_t p_width, int64_t p_height);
    void worker_main();

    std::unique_ptr<spectator::IStreamBackend> create_backend();

    void _on_backend_started();
    void _on_backend_error(bool fatal, const String &message);
};

} // namespace godot
