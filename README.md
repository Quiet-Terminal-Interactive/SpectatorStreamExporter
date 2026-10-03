# SpectatorStreamExporter v1.0.0

A Godot 4 GDExtension that publishes the contents of a `SubViewport` as a live video source that other applications can pick up. OBS, Resolume, TouchDesigner, VJ tools, or a second process of your own can use it, with no window or screen capture involved.

Each platform uses its native inter-process video sharing system:

| Platform | Backend  | Consumers                                                   |
|----------|----------|-------------------------------------------------------------|
| Linux    | PipeWire | Anything that reads a PipeWire `Video/Source` node (GStreamer `pipewiresrc`, qpwgraph/Helvum for routing, ...) |
| Windows  | Spout 2  | OBS (Spout2 plugin), Resolume, TouchDesigner, SpoutReceiver, ... |
| macOS    | Syphon   | OBS (built-in Syphon Client), Resolume, VDMX, Syphon Simple Client, ... |

A typical use is a spectator or broadcast camera: a second camera renders into a `SubViewport` at its own resolution and framerate, and a streaming tool receives that view without it ever being on screen.

---

## Contents

- [SpectatorStreamExporter v1.0.0](#spectatorstreamexporter-v100)
  - [Contents](#contents)
  - [Requirements](#requirements)
  - [Installing into a project](#installing-into-a-project)
  - [Demo project](#demo-project)
  - [Usage](#usage)
    - [Scene setup](#scene-setup)
      - [Changing resolution](#changing-resolution)
    - [API reference](#api-reference)
      - [Properties](#properties)
      - [Methods](#methods)
      - [Signals](#signals)
    - [Receiving the stream](#receiving-the-stream)
      - [Linux (PipeWire)](#linux-pipewire)
      - [Windows (Spout)](#windows-spout)
      - [macOS (Syphon)](#macos-syphon)
      - [Testing without a receiver](#testing-without-a-receiver)
  - [Building](#building)
    - [Prerequisites](#prerequisites)
    - [Build scripts](#build-scripts)
    - [Building manually with SCons](#building-manually-with-scons)
    - [SCons options](#scons-options)
    - [Installing Spout](#installing-spout)
    - [Installing Syphon](#installing-syphon)
  - [How it works](#how-it-works)
    - [Adding a backend](#adding-a-backend)
  - [Troubleshooting](#troubleshooting)
  - [Project layout](#project-layout)
  - [License](#license)

---

## Requirements

- **Godot 4.7 or newer.** The extension is built with `api_version=4.7`.
- **The Forward+ or Mobile renderer.** Frames are read through the `RenderingDevice`, which the Compatibility (OpenGL) renderer doesn't expose. Under Compatibility the node engages but never sends a frame.
- One of the following at runtime:
  - **Linux:** a running PipeWire daemon (the default on most current distros).
  - **Windows:** nothing extra. The Spout sources are compiled into the extension DLL (see [Installing Spout](#installing-spout)).
  - **macOS:** `Syphon.framework` has to be loadable by the extension (see [Installing Syphon](#installing-syphon)).

## Installing into a project

1. Build the extension (see [Building](#building)) or use prebuilt binaries.
2. Copy `demo/addons/spectator_stream_exporter/` into your project's `addons/` folder, so you end up with:

   ```
   your_project/
   └── addons/
       └── spectator_stream_exporter/
           ├── spectator_stream_exporter.gdextension
           └── bin/
               ├── libspectator_stream_exporter.linux.template_debug.x86_64.so
               ├── libspectator_stream_exporter.linux.template_release.x86_64.so
               ├── libspectator_stream_exporter.windows.template_debug.x86_64.dll
               ├── libspectator_stream_exporter.windows.template_release.x86_64.dll
               ├── libspectator_stream_exporter.macos.template_debug.framework/
               └── libspectator_stream_exporter.macos.template_release.framework/
   ```

   You only need the binaries for the platforms you target.
3. Restart the editor. `SpectatorStreamExporter` now appears in the **Create New Node** dialog.

When you export your game, Godot copies the matching library automatically. On macOS you also need to ship `Syphon.framework` (see [Installing Syphon](#installing-syphon)).

## Demo project

`demo/` is a small Godot project that uses the extension. The build writes its binaries straight into `demo/addons/spectator_stream_exporter/bin/`, so you can build and then open it:

```sh
./build-all.sh                  # or build-all.bat / build-all.command
godot --path demo               # or import demo/project.godot from the Project Manager
```

The scene is a few spinning shapes. The main camera looks at them from a fixed spot, and a second camera inside a 1280x720 `SubViewport` orbits them. The exporter publishes that orbiting view as **`SpectatorStreamDemo`**, and a preview in the top-right corner shows the same image receivers get. The status text in the top-left shows whether the stream is live, its name and size, the FPS cap, the renderer and the last error.

| Key   | Action |
|-------|--------|
| Space | Toggle streaming (`active`) |
| F     | Cycle `target_fps`: unlimited → 30 → 60 |
| V     | Switch the spectator viewport between 720p and 1080p and call `restart()` |
| R     | `restart()` |

To look at the stream, point a receiver at `SpectatorStreamDemo` (see [Receiving the stream](#receiving-the-stream)). For example, on Linux:

```sh
gst-launch-1.0 pipewiresrc target-object="SpectatorStreamDemo" ! videoconvert ! autovideosink
```

To check the scene without a receiver, run it with `SPECTATOR_STREAM_NULL_BACKEND=1 godot --path demo`.

All the demo code is in `demo/main.gd`.

## Usage

### Scene setup

```
Main (Node3D)
├── Player ...
├── SpectatorViewport (SubViewport)        size = 1920x1080, render_target_update_mode = Always
│   └── SpectatorCamera (Camera3D)
└── SpectatorStreamExporter                 target_viewport = ../SpectatorViewport
                                            stream_name     = "MyGame Spectator"
                                            target_fps      = 60
```

1. Add a `SubViewport` and put a camera inside it. Give it the resolution you want to stream at.
2. Set the SubViewport's **Render Target -> Update Mode** to **Always**. Godot doesn't redraw a SubViewport that isn't displayed anywhere unless you set this, and a viewport that isn't redrawn produces no new frames.
3. If the SubViewport shares the main scene's world, it renders the same scene. Enable **Own World 3D** if the spectator should see a separate world.
4. Add a `SpectatorStreamExporter` node and point `target_viewport` at the SubViewport.
5. Turn it on from a script once the scene is ready:

```gdscript
extends Node3D

@onready var exporter: SpectatorStreamExporter = $SpectatorStreamExporter

func _ready() -> void:
    exporter.stream_started.connect(func(): print("Spectator stream is live"))
    exporter.stream_stopped.connect(func(): print("Spectator stream stopped"))
    exporter.stream_error.connect(func(msg): push_warning("Spectator stream: " + msg))
    exporter.active = true
```

> Set `active` from code, not from the Inspector. Setting `active = true` looks up `target_viewport` right away. A value saved in the scene file is applied while the scene loads, before the node is in the tree, so the lookup fails and `stream_error` fires. Setting it in `_ready()` (or later) avoids this.

#### Changing resolution

The stream's dimensions are fixed when it starts. If you resize the SubViewport while streaming, call `restart()` so the backend is re-created at the new size:

```gdscript
$SpectatorViewport.size = Vector2i(1280, 720)
exporter.restart()
```

### API reference

**`SpectatorStreamExporter`** (inherits `Node`)

#### Properties

| Property          | Type       | Default            | Description |
|-------------------|------------|--------------------|-------------|
| `target_viewport` | `NodePath` | `""`               | Path to the `SubViewport` to stream. Must resolve to a `SubViewport` when `active` is set to `true`. |
| `stream_name`     | `String`   | `"GodotSpectator"` | Name receivers see: the PipeWire node name/description, the Spout sender name, or the Syphon server name. Changes apply the next time the stream starts (`restart()` or toggling `active`). |
| `active`          | `bool`     | `false`            | Starts or stops the stream. If starting fails, it stays `false` and `stream_error` is emitted. |
| `target_fps`      | `int`      | `0`                | Upper limit on frames sent per second. `0` sends every rendered frame. Negative values are clamped to `0`. Use this when the game runs at 144 Hz but the stream only needs 30 or 60. |

#### Methods

| Method      | Description |
|-------------|-------------|
| `restart()` | Tears down and re-creates the backend if `active` is `true`, picking up the current `stream_name` and SubViewport size. Does nothing when inactive. If starting again fails, `active` becomes `false` and `stream_stopped` is emitted. |

#### Signals

| Signal                         | Description |
|--------------------------------|-------------|
| `stream_started()`             | The backend is up and publishing. Emitted deferred, at the end of the frame in which `active` was set. |
| `stream_stopped()`             | The stream stopped: `active` was set to `false`, `restart()` failed, or the backend hit a fatal error. |
| `stream_error(message: String)`| Something went wrong. Fatal backend errors also stop the stream (you'll get `stream_stopped` afterwards). Errors are also written to the Godot error log. |

The stream also stops, without emitting a signal, when the node leaves the scene tree or is freed.

### Receiving the stream

#### Linux (PipeWire)

The exporter creates a PipeWire node with `media.class = Video/Source`, named after `stream_name`. It offers exactly one fixed format: raw `BGRA` video at the SubViewport's resolution and 60/1 fps, with no ranges or alternatives for the consumer to choose from. This is the offer verified to negotiate with OBS's PipeWire camera source. The advertised framerate is nominal: frames are pushed as they're rendered, subject to `target_fps`.

To check that it works with GStreamer:

```sh
gst-launch-1.0 pipewiresrc target-object="GodotSpectator" ! videoconvert ! autovideosink
```

You can also see and route the node in `qpwgraph` or `Helvum`, or list it with `pw-cli ls Node`.

To save work, pixels are only read back from the GPU while a consumer is connected and the format has been negotiated. Until then the node is visible but no frames flow.

#### Windows (Spout)

The exporter creates a Spout sender named after `stream_name`, sharing a `BGRA8` D3D11 texture. If a sender with that name already exists, Spout appends `_1`, `_2`, ... (the actual name is printed to the Godot log when the stream starts). Any Spout receiver will list it:

- **OBS:** install the [OBS Spout2 plugin](https://github.com/Off-World-Live/obs-spout2-plugin) and add a *Spout2 Capture* source.
- **Test:** `SpoutReceiver.exe` from the Spout distribution.

#### macOS (Syphon)

The exporter creates a `SyphonMetalServer` named after `stream_name`. The app name shown in clients is your Godot game/editor.

- **OBS:** add a *Syphon Client* source (built in on macOS).
- **Test:** [Syphon Simple Apps](https://github.com/Syphon/Simple/releases).

#### Testing without a receiver

Set `SPECTATOR_STREAM_NULL_BACKEND=1` in the environment before launching Godot. The platform backend is replaced by a null backend that logs the start and stop, plus one line every 60 frames. Use it to check that the scene is wired up correctly and frames are flowing, separately from the streaming side:

```sh
SPECTATOR_STREAM_NULL_BACKEND=1 godot --path your_project
```

---

## Building

### Prerequisites

All platforms:

- **Python 3** and **SCons** (`pip install scons`, or your package manager).
- **godot-cpp** cloned into the repo root as `godot-cpp/`. The build is tested against the `10.0.0-stable` tag:

  ```sh
  git clone --recursive https://github.com/godotengine/godot-cpp.git
  ```

  (Or `git submodule add https://github.com/godotengine/godot-cpp.git` if you prefer a submodule.)

Per host platform:

| Host    | Needs |
|---------|-------|
| Linux   | GCC or Clang, `pkg-config`, PipeWire dev headers (`libpipewire-0.3`), Vulkan headers/loader (`vulkan`). <br>Arch: `pacman -S base-devel scons pipewire libpipewire vulkan-headers vulkan-icd-loader` <br>Debian/Ubuntu: `apt install build-essential scons pkg-config libpipewire-0.3-dev libvulkan-dev` <br>Fedora: `dnf install gcc-c++ scons pkgconf pipewire-devel vulkan-loader-devel` |
| Windows | Visual Studio 2022 (Desktop C++ workload) run from an *x64 Native Tools* prompt, or MinGW-w64 with `USE_MINGW=1`. Plus the Spout SDK (see [Installing Spout](#installing-spout)). |
| macOS   | Xcode command-line tools (`xcode-select --install`). Plus `Syphon.framework` (see [Installing Syphon](#installing-syphon)). |

### Build scripts

Each host has a script that builds debug and release for the native platform and then attempts the other platforms. Any cross-build whose toolchain or SDK is missing is skipped with a message rather than failing. Extra arguments are passed through to every `scons` call (for example `./build-all.sh debug_log=yes`).

All output goes to `demo/addons/spectator_stream_exporter/bin/`.

| Script              | Native build | Windows              | Linux                          | macOS |
|---------------------|--------------|----------------------|--------------------------------|-------|
| `build-all.sh` (Linux)       | Linux x86_64 | MinGW-w64 cross (needs `x86_64-w64-mingw32-gcc` and `SPOUT_SDK`) | —                  | osxcross, universal (needs `OSXCROSS_ROOT` and a Syphon SDK) |
| `build-all.bat` (Windows)    | Windows x86_64 (MSVC, or MinGW with `USE_MINGW=1`) | — | Runs `build-all.sh` inside WSL2 | Skipped: not practical from Windows |
| `build-all.command` (macOS)  | macOS universal (x86_64 + arm64) | MinGW-w64 cross (`brew install mingw-w64`, needs `SPOUT_SDK`) | Arch Linux Docker container (needs `docker`) | — |

```sh
# Linux
./build-all.sh

# macOS (or double-click in Finder)
./build-all.command
```

```bat
:: Windows, from an x64 Native Tools Command Prompt
set SPOUT_SDK=C:\dev\Spout2
build-all.bat

:: or with MinGW-w64
set USE_MINGW=1
build-all.bat
```

Notes:

- On Windows, the Linux build runs `build-all.sh` inside WSL, so that distro needs the Linux prerequisites installed. Because of this, `build-all.sh` must keep LF line endings. `.gitattributes` enforces that.
- The Docker Linux build writes into your checkout as root. Afterwards you may need `sudo chown -R $USER .`.

### Building manually with SCons

From the repo root:

```sh
# Linux
scons platform=linux target=template_debug
scons platform=linux target=template_release

# Windows (MSVC)
scons platform=windows target=template_release arch=x86_64 spout_sdk=C:/dev/Spout2

# Windows (MinGW, including cross-compiling from Linux/macOS)
scons platform=windows use_mingw=yes target=template_release arch=x86_64 spout_sdk=/path/to/Spout2

# macOS
scons platform=macos target=template_release arch=universal

# macOS via osxcross (enabled automatically when OSXCROSS_ROOT is set;
# osxcross_sdk must match your osxcross target, e.g. darwin25.4)
scons platform=macos osxcross_sdk=darwin25.4 target=template_release arch=universal
```

Add `-j$(nproc)` (Linux), `-j$(sysctl -n hw.ncpu)` (macOS) or `-j%NUMBER_OF_PROCESSORS%` (Windows) to build in parallel.

`template_debug` builds are what the editor and debug exports load. `template_release` builds are used by release exports. You need both for a working project.

To clean: `scons -c` with the same arguments, or delete `demo/addons/spectator_stream_exporter/bin/`, `build/`, `src/**/*.os` and `.sconsign.dblite`.

### SCons options

These are specific to this project. All the standard [godot-cpp options](https://docs.godotengine.org/en/stable/tutorials/scripting/cpp/gdextension_cpp_example.html) (`platform`, `target`, `arch`, `use_mingw`, `osxcross_sdk`, `dev_build`, `api_version`, ...) also work.

| Option        | Default                | Description |
|---------------|------------------------|-------------|
| `spout_sdk`   | `$SPOUT_SDK`           | Windows: path to the Spout2 SDK root. |
| `syphon_sdk`  | `$SYPHON_SDK`, then `third_party/` if it contains `Syphon.framework` | macOS: directory that contains `Syphon.framework`. |
| `debug_log`   | `no`                   | `yes` defines `SPECTATOR_STREAM_DEBUG_LOG`, which prints engage/disengage and one line **per frame** (frame index, texture RID, native handle, size), and turns on the backends' verbose logging (see `SPECTATOR_STREAM_DEBUG_LOG=1` below). Very noisy; only for debugging. |
| `api_version` | `4.7`                  | Godot API version to build against. Lowering it lets the extension load on older Godot versions, if the APIs it uses exist there. |

| Environment variable | Used by | Description |
|----------------------|---------|-------------|
| `SPOUT_SDK`          | SConstruct, all build scripts | Same as `spout_sdk=`. |
| `SYPHON_SDK`         | SConstruct, `build-all.sh`, `build-all.command` | Same as `syphon_sdk=`. |
| `USE_MINGW=1`        | `build-all.bat` | Build Windows with MinGW-w64 instead of MSVC. |
| `OSXCROSS_ROOT`      | godot-cpp, `build-all.sh` | Root of your osxcross install. Setting it makes `platform=macos` use osxcross. `build-all.sh` skips the macOS cross-build unless `$OSXCROSS_ROOT/target/bin` exists, and builds with `osxcross_sdk=darwin25.4`. |
| `SPECTATOR_STREAM_NULL_BACKEND=1` | Runtime | Use the logging null backend instead of the platform backend (see [Testing without a receiver](#testing-without-a-receiver)). |
| `SPECTATOR_STREAM_DEBUG_LOG=1` | Runtime | Verbose backend logging without a `debug_log=yes` build. PipeWire: advertised format pods, negotiation and buffer setup. Spout: Spout's own log (console + `%AppData%\Spout\SpectatorStreamSpout.log`), dumped with adapter info if the sender fails to start. Doesn't enable the node's per-frame lines. |

### Installing Spout

The Windows backend uses [Spout2](https://github.com/leadedge/Spout2)'s DirectX-only `spoutDX` sender, so it needs a D3D11 device but no OpenGL context. That matters because Godot's Forward+ and Mobile renderers run on Vulkan or D3D12, and SpoutGL's sender can't start without an OpenGL context.

1. Get the SDK:

   ```sh
   git clone https://github.com/leadedge/Spout2.git C:/dev/Spout2
   ```

2. Point the build at the SDK root with `SPOUT_SDK=C:/dev/Spout2` (or `spout_sdk=...` on the scons command line).

You don't need to build Spout itself. None of Spout's prebuilt libraries contain `spoutDX`, and linking `Spout_static` would also pull in its OpenGL code. So SConstruct compiles into the extension exactly the sources that SpoutDX's own CMake target uses:

- `SPOUTSDK/SpoutDirectX/SpoutDX/SpoutDX.cpp`
- `SPOUTSDK/SpoutGL/` `SpoutCopy.cpp`, `SpoutDirectX.cpp`, `SpoutFrameCount.cpp`, `SpoutSenderNames.cpp`, `SpoutSharedMemory.cpp` and `SpoutUtils.cpp`

These files don't use OpenGL; the resulting DLL imports `d3d11.dll` and `dxgi.dll`, not `opengl32.dll`. They're compiled with exceptions enabled (Spout uses `try`/`catch`) and, on MinGW x86_64, with `-msse4` as in Spout's own CMake. Their objects go to `build/spout/`, so nothing is written into the SDK tree.

If `SPOUT_SDK` isn't set, the Windows build still compiles, but the Spout backend is a stub. Setting `active = true` then fails with *"Spout SDK not linked in this build"*.

### Installing Syphon

The macOS backend uses [Syphon](https://github.com/Syphon/Syphon-Framework)'s `SyphonMetalServer`. The official Syphon project doesn't publish a prebuilt framework, so use the universal (x86_64 + arm64) build that [node-syphon](https://github.com/benoitlahoz/node-syphon) publishes as a release asset.

1. Download `SyphonFramework.zip` from [node-syphon v1.1.5](https://github.com/benoitlahoz/node-syphon/releases/tag/v1.1.5) and unzip it into `third_party/`. The archive has `Syphon.framework/` at its root:

   ```sh
   curl -L -o /tmp/SyphonFramework.zip \
        https://github.com/benoitlahoz/node-syphon/releases/download/v1.1.5/SyphonFramework.zip
   unzip -o /tmp/SyphonFramework.zip -d third_party/ -x '__MACOSX/*'
   ```

   You should end up with `third_party/Syphon.framework/Versions/A/Syphon`. SConstruct and the build scripts pick it up from there automatically. To keep it somewhere else, set `SYPHON_SDK` (or `syphon_sdk=`) to the directory that **contains** `Syphon.framework`.

   > The framework relies on symlinks (`Versions/Current`, `Headers`, `Syphon`, ...). `unzip` on Linux and macOS preserves them. Don't extract it with a tool or onto a filesystem that turns them into plain files, or the build won't find the headers.

   Because the binary contains both x86_64 and arm64, it works for `arch=universal` builds, both natively on a Mac and through osxcross. You can confirm with `lipo -info third_party/Syphon.framework/Versions/A/Syphon`.

2. **At runtime** the extension has to be able to find `Syphon.framework`. This build's install name is `@rpath/Syphon.framework/Versions/A/Syphon`, so the extension library needs an rpath that points at a folder containing `Syphon.framework`. For example, to load it from `bin/` next to the extension:

   ```sh
   cp -R third_party/Syphon.framework demo/addons/spectator_stream_exporter/bin/
   for t in template_debug template_release; do
       install_name_tool -add_rpath @loader_path/.. \
           demo/addons/spectator_stream_exporter/bin/libspectator_stream_exporter.macos.$t.framework/libspectator_stream_exporter.macos.$t
   done
   ```

   (`@loader_path` is the extension's own `.framework` folder, so `@loader_path/..` is `bin/`. On Linux, osxcross provides the tool as `x86_64-apple-darwin25.4-install_name_tool`.) Changing the binary invalidates its code signature, so re-sign it on a Mac with `codesign -f -s - <path>`.

   For exported apps, put `Syphon.framework` in `YourGame.app/Contents/Frameworks/`, add `@executable_path/../Frameworks` as an rpath the same way, and re-sign the bundle. Run `otool -L` and `otool -l | grep -A2 LC_RPATH` on the extension binary to check what it will look for.

If no Syphon SDK is found, the macOS build still compiles, but the Syphon backend is a stub. Setting `active = true` then fails with *"Syphon framework not linked in this build"*.

---

## How it works

```
 main thread                     render thread                        worker thread              platform
 ───────────                     ─────────────                        ─────────────              ────────
 RenderingServer.frame_post_draw
   └─ on_frame_post_draw()
        ├─ target_fps throttle
        ├─ backend.wants_frames()?
        ├─ SubViewport → ViewportTexture
        │    → RD texture RID
        └─ call_on_render_thread ──► _dispatch_readback()
                                       ├─ native handle
                                       └─ RD.texture_get_data_async()
                                            (GPU → CPU, RGBA8)
                                     ... a few frames later ...
                                     _on_readback_done(pixels)
                                       └─ FrameQueue.try_publish ──►  wait_pop(token)
                                          (ring of 3; drops oldest      └─ backend.push_frame() ──►  PipeWire / Spout / Syphon
                                           when the worker falls behind)
```

- The node hooks `RenderingServer.frame_post_draw` while active, so it grabs a frame after each render and never interrupts the render itself.
- Before doing any GPU work it asks the backend `wants_frames()`. PipeWire answers no until a consumer is connected and a format has been negotiated, so an idle stream costs nothing.
- The readback runs on the render thread, as `RenderingDevice` requires, using `texture_get_data_async` so the GPU isn't stalled waiting for the copy. The completion callback looks the node up by instance ID, so a node freed while a download is in flight is handled safely.
- Frames go through a small lock-protected ring buffer (`src/frame_queue.h`, capacity 3). If the backend can't keep up, the oldest frame is dropped and the game never blocks.
- A dedicated worker thread hands the pixels to the backend, which converts them if needed (RGBA → BGRA for Spout and PipeWire) and publishes them. PipeWire additionally runs its own `pw_thread_loop`, and pixels are copied into PipeWire's buffers in its `process` callback.
- Backend callbacks (`started`, `error`) reach the node through `call_deferred`, so signals always fire on the main thread.
- All three backends currently copy pixels through the CPU. A 1080p RGBA frame is about 8 MB, so at 60 fps that is roughly 500 MB/s of GPU→CPU bandwidth. Use `target_fps` and a sensible SubViewport size to keep it in check. The native texture handle is already carried through to the backends (`ExportedFrame::native_handle`) as groundwork for zero-copy sharing later.

### Adding a backend

Implement `spectator::IStreamBackend` (`src/backends/i_stream_backend.h`): `start`, `stop`, `push_frame` and `name`. `push_frame` receives tightly packed RGBA8 pixels in `ExportedFrame::pixels`. Optionally override `wants_frames()` to return `false` while nobody is receiving, which skips the GPU readback. Call `callbacks.on_started()` once publishing, and `callbacks.on_error(fatal, msg)` on failure. Then add it to `create_backend()` in `src/spectator_stream_exporter.cpp` and to the source list for its platform in `SConstruct`.

---

## Troubleshooting

| Symptom | Cause / fix |
|---------|-------------|
| `SpectatorStreamExporter` isn't in the Create Node dialog | The library for your platform/target is missing from `bin/`, or the `.gdextension` file isn't in the project. Check the editor Output panel for GDExtension load errors. You need a `template_debug` build for the editor. |
| `target_viewport does not resolve to a SubViewport` | The path is wrong or points at a non-`SubViewport`, or `active` was set before the node was in the tree (for example ticked in the Inspector). Set it from `_ready()`. |
| `SubViewport has zero-sized rendering area` | Give the SubViewport a non-zero `size`. |
| Stream starts but receivers show a black or frozen image | Set the SubViewport's *Update Mode* to **Always**, make sure it has an active camera, and check you're not on the **Compatibility** renderer. Run with `SPECTATOR_STREAM_NULL_BACKEND=1` to confirm frames are flowing. |
| Image is garbled or cut off after resizing the viewport | Call `restart()` after changing the SubViewport size. |
| Linux: `pw_context_connect failed (is PipeWire running?)` | Start PipeWire (`systemctl --user start pipewire`). Inside Flatpak or other sandboxes, the app needs access to the PipeWire socket. |
| Linux: node exists but no frames arrive | Frames are only produced once a consumer connects and negotiates the format. Make sure the consumer accepts `BGRA` at the SubViewport's size. Run with `SPECTATOR_STREAM_DEBUG_LOG=1` to see the advertised formats and what the consumer negotiated. |
| Linux: image is cropped or has black borders | The consumer negotiated a size different from the SubViewport's. Ask the consumer for the viewport's exact size, or resize the SubViewport and call `restart()`. |
| Windows: `Spout SDK not linked in this build` | The DLL was built without `SPOUT_SDK`. Rebuild with it set (see [Installing Spout](#installing-spout)). |
| Windows: `spoutDX::...` error when starting | Spout couldn't create a D3D11 device or the sender. Run with `SPECTATOR_STREAM_DEBUG_LOG=1`, which turns on Spout's own logging and dumps its log (`%AppData%\Spout\SpectatorStreamSpout.log`) on failure. |
| macOS: `Syphon framework not linked in this build` | Rebuild with `third_party/Syphon.framework` present or `SYPHON_SDK` set (see [Installing Syphon](#installing-syphon)). |
| macOS: extension fails to load, `Library not loaded: ...Syphon.framework...` | The extension has no rpath that leads to `Syphon.framework`. See step 2 of [Installing Syphon](#installing-syphon). |
| macOS: the extension is blocked by Gatekeeper | Unsigned downloaded binaries are quarantined. Run `xattr -dr com.apple.quarantine addons/spectator_stream_exporter/bin`, or sign them. |

---

## Project layout

```
.
├── SConstruct                     # Build definition (wraps godot-cpp's SConstruct)
├── build-all.sh                   # Linux host: native + mingw + osxcross
├── build-all.bat                  # Windows host: native + WSL
├── build-all.command              # macOS host: native + mingw + Docker
├── godot-cpp/                     # godot-cpp checkout (not committed)
├── third_party/                   # Drop Syphon.framework here (not committed)
├── build/spout/                   # Spout SDK objects from Windows builds (generated)
├── src/
│   ├── register_types.{h,cpp}     # GDExtension entry point
│   ├── spectator_stream_exporter.{h,cpp}  # The node: frame capture, async readback, worker thread, signals
│   ├── frame_queue.h              # Drop-oldest ring buffer between render and worker threads
│   └── backends/
│       ├── i_stream_backend.h     # Backend interface
│       ├── null_backend.*         # Logging backend (SPECTATOR_STREAM_NULL_BACKEND=1)
│       ├── pipewire_backend.*     # Linux
│       ├── spout_backend.*        # Windows
│       └── syphon_backend.{h,mm}  # macOS
└── demo/                          # Demo Godot project (see "Demo project")
    ├── project.godot
    ├── main.tscn                  # Scene: props, player camera, spectator SubViewport, exporter, UI
    ├── main.gd                    # Orbiting spectator camera, key controls, status display
    └── addons/spectator_stream_exporter/
        ├── spectator_stream_exporter.gdextension
        └── bin/                   # Build output
```

## License

[MIT](LICENSE) © 2026 Quiet Terminal Interactive.

Third-party components keep their own licenses: [godot-cpp](https://github.com/godotengine/godot-cpp) (MIT), [Spout2](https://github.com/leadedge/Spout2) and [Syphon](https://github.com/Syphon/Syphon-Framework) (both BSD-style). If you ship binaries that link them, include their license texts.
