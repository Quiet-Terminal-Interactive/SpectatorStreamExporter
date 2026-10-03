A GDExtension that publishes the contents of a `SubViewport` as a live video source other applications can pick up. OBS, Resolume, TouchDesigner, VJ tools, or a second process of your own can receive it, with no window or screen capture involved.

A typical use is a spectator or broadcast camera: a second camera renders into a `SubViewport` at its own resolution and frame rate, and a streaming tool receives that view without it ever appearing on screen.

Each platform uses its native video sharing system:

| Platform | Backend | Consumers |
|----------|---------|-----------|
| Linux | PipeWire | OBS (Video Capture Device), GStreamer, qpwgraph/Helvum, any PipeWire `Video/Source` reader |
| Windows | Spout 2 | OBS (Spout2 plugin), Resolume, TouchDesigner, SpoutReceiver |
| macOS | Syphon | OBS (Syphon Client), Resolume, VDMX (untested, see below) |

**Usage**

1. Add a `SubViewport` with a camera inside it, at the resolution you want to stream. Set its update mode to Always.
2. Add a `SpectatorStreamExporter` node and point `target_viewport` at the SubViewport.
3. Set `active = true` from code (in `_ready()` or later):

```gdscript
@onready var exporter: SpectatorStreamExporter = $SpectatorStreamExporter

func _ready() -> void:
    exporter.stream_error.connect(func(msg): push_warning("Spectator stream: " + msg))
    exporter.active = true
```

**Properties:** `target_viewport`, `stream_name` (what receivers see), `active`, `target_fps` (cap the stream rate independently of the game).
**Methods:** `restart()` (call after resizing the SubViewport).
**Signals:** `stream_started`, `stream_stopped`, `stream_error(message)`.

**Features**
- One node, no scene rewiring
- Independent resolution and frame rate from the game
- Drop-oldest frame queue, so a slow receiver never stalls your game
- Null backend for testing without a receiver (`SPECTATOR_STREAM_NULL_BACKEND=1`)
- Optional debug logging (`SPECTATOR_STREAM_DEBUG_LOG=1`)
- Demo project included

**Requirements**
- Godot 4.7 or newer
- Forward+ or Mobile renderer. The Compatibility (OpenGL) renderer isn't supported, because frames are read through the `RenderingDevice`.
- Linux: a running PipeWire daemon. Windows: a D3D11-capable GPU (no extra DLLs needed, Spout is compiled in). macOS: `Syphon.framework` available to the extension.

**Platform status**
- Linux (PipeWire): tested with OBS
- Windows (Spout): tested
- macOS (Syphon): built against the SDK but not tested on real hardware, so bug reports are welcome

**Performance note.** Frames are read back through the CPU (about 8 MB per 1080p frame, roughly 500 MB/s at 1080p60). Use `target_fps` and a sensible viewport size on modest hardware.