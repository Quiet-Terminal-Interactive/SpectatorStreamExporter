#!/usr/bin/env python
import os
import sys

if "api_version" not in ARGUMENTS:
    ARGUMENTS["api_version"] = "4.7"

env = SConscript("godot-cpp/SConstruct")

env.Append(CPPPATH=["src/"])

sources = [
    "src/register_types.cpp",
    "src/spectator_stream_exporter.cpp",
    "src/backends/null_backend.cpp",
]

platform = env["platform"]

if platform == "linux":
    sources += [
        "src/backends/pipewire_backend.cpp",
    ]
    env.ParseConfig("pkg-config libpipewire-0.3 --cflags --libs")
    env.ParseConfig("pkg-config vulkan --cflags --libs")
    env.Append(CPPDEFINES=["SPECTATOR_STREAM_LINUX"])

elif platform == "windows":
    sources += [
        "src/backends/spout_backend.cpp",
    ]
    spout_root = ARGUMENTS.get("spout_sdk", os.environ.get("SPOUT_SDK", ""))
    if spout_root:
        # The backend uses Spout's DirectX-only spoutDX sender. None of the prebuilt
        # Spout libraries contain spoutDX, and linking libSpout_static also drags in
        # the OpenGL objects (they satisfy libstdc++ template symbols first), so
        # compile exactly the sources SpoutDX's own CMake target uses instead
        spoutgl_dir = os.path.join(spout_root, "SPOUTSDK", "SpoutGL")
        spoutdx_dir = os.path.join(spout_root, "SPOUTSDK", "SpoutDirectX", "SpoutDX")
        env.Append(CPPPATH=[spoutgl_dir, spoutdx_dir])

        spout_sources = [os.path.join(spoutdx_dir, "SpoutDX.cpp")] + [
            os.path.join(spoutgl_dir, name + ".cpp")
            for name in ["SpoutCopy", "SpoutDirectX", "SpoutFrameCount",
                         "SpoutSenderNames", "SpoutSharedMemory", "SpoutUtils"]
        ]
        missing = [p for p in spout_sources if not os.path.isfile(p)]
        if missing:
            print("ERROR: Spout sources not found: {}".format(", ".join(missing)))
            Exit(1)

        # Spout uses try/catch, which godot-cpp compiles out by default
        spout_env = env.Clone()
        if spout_env.get("is_msvc", False):
            spout_env["CPPDEFINES"] = [d for d in spout_env["CPPDEFINES"] if d != ("_HAS_EXCEPTIONS", 0)]
            spout_env.Append(CXXFLAGS=["/EHsc"])
        else:
            spout_env["CXXFLAGS"] = [f for f in spout_env["CXXFLAGS"] if f != "-fno-exceptions"]
            spout_env.Append(CXXFLAGS=["-fexceptions"])
            # Same as Spout's CMake for MinGW: SpoutCopy uses SSSE3/SSE4 intrinsics
            if env["arch"] == "x86_64":
                spout_env.Append(CXXFLAGS=["-msse4"])

        # Objects go under build/ so nothing is written into the SDK tree
        sources += [
            spout_env.SharedObject(os.path.join("build", "spout", os.path.splitext(os.path.basename(src))[0]), src)
            for src in spout_sources
        ]
        env.Append(LIBS=[
            "d3d11", "dxgi", "user32", "gdi32", "advapi32", "shell32",
            "ole32", "comctl32", "version", "winmm", "psapi",
        ])
    else:
        print("WARNING: SPOUT_SDK not set; Spout backend will not link. Pass spout_sdk=PATH or set SPOUT_SDK.")
    env.Append(CPPDEFINES=["SPECTATOR_STREAM_WINDOWS"])

elif platform == "macos":
    sources += [
        "src/backends/syphon_backend.mm",
    ]
    env.Append(CPPDEFINES=["SPECTATOR_STREAM_MACOS"])
    env.Append(LINKFLAGS=[
        "-framework", "Metal",
        "-framework", "Foundation",
        "-framework", "IOSurface",
    ])
    syphon_root = ARGUMENTS.get("syphon_sdk", os.environ.get("SYPHON_SDK", ""))
    if not syphon_root and os.path.isdir(os.path.join("third_party", "Syphon.framework")):
        syphon_root = "third_party"
    if syphon_root:
        syphon_root = os.path.abspath(syphon_root)
        env.Append(CPPPATH=[syphon_root])
        env.Append(FRAMEWORKPATH=[syphon_root])
        # godot-cpp's macOS tool doesn't emit $_FRAMEWORKPATH on the link line so pass -F explicitly to make sure ld finds Syphon.framework.
        env.Append(LINKFLAGS=["-F" + syphon_root, "-framework", "Syphon"])
    else:
        print("WARNING: SYPHON_SDK not set; Syphon backend compiled as stub.")

if ARGUMENTS.get("debug_log", "no") == "yes":
    env.Append(CPPDEFINES=["SPECTATOR_STREAM_DEBUG_LOG"])

if env["platform"] == "macos":
    library = env.SharedLibrary(
        "demo/addons/spectator_stream_exporter/bin/libspectator_stream_exporter.{}.{}.framework/libspectator_stream_exporter.{}.{}".format(
            env["platform"], env["target"], env["platform"], env["target"]
        ),
        source=sources,
    )
else:
    library = env.SharedLibrary(
        "demo/addons/spectator_stream_exporter/bin/libspectator_stream_exporter{}{}".format(
            env["suffix"], env["SHLIBSUFFIX"]
        ),
        source=sources,
    )

Default(library)
