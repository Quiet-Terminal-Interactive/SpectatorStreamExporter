#include "syphon_backend.h"

#include <cstring>

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#if __has_include(<Syphon/Syphon.h>)
#import <Syphon/Syphon.h>
#define SPECTATOR_SYPHON_AVAILABLE 1
#else
#define SPECTATOR_SYPHON_AVAILABLE 0
#endif

#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace spectator {

SyphonBackend::SyphonBackend() = default;

SyphonBackend::~SyphonBackend() {
    stop();
}

bool SyphonBackend::start(const StreamConfig &config, const BackendCallbacks &callbacks) {
    m_config = config;
    m_callbacks = callbacks;

#if !SPECTATOR_SYPHON_AVAILABLE
    report_error(true, "Syphon framework not linked in this build (SYPHON_SDK was unset)");
    return false;
#else
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (!dev) {
            report_error(true, "MTLCreateSystemDefaultDevice returned nil");
            return false;
        }
        m_metal_device = (__bridge_retained void *)dev;

        id<MTLCommandQueue> q = [dev newCommandQueue];
        if (!q) {
            report_error(true, "newCommandQueue failed");
            return false;
        }
        m_metal_queue = (__bridge_retained void *)q;

        MTLTextureDescriptor *desc = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                             width:m_config.width
                                            height:m_config.height
                                         mipmapped:NO];
        desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        desc.storageMode = MTLStorageModeManaged;
        id<MTLTexture> tex = [dev newTextureWithDescriptor:desc];
        if (!tex) {
            report_error(true, "newTextureWithDescriptor failed");
            return false;
        }
        m_metal_texture = (__bridge_retained void *)tex;

        NSString *nsname = [NSString stringWithUTF8String:m_config.stream_name.c_str()];
        SyphonMetalServer *server = [[SyphonMetalServer alloc]
                initWithName:nsname
                      device:dev
                     options:nil];
        if (!server) {
            report_error(true, "SyphonMetalServer alloc/init failed");
            return false;
        }
        m_syphon_server = (__bridge_retained void *)server;
    }

    m_started = true;
    UtilityFunctions::print(String("[SyphonBackend] started as '") +
                            String(m_config.stream_name.c_str()) + "' " +
                            String::num_int64(m_config.width) + "x" +
                            String::num_int64(m_config.height));
    if (m_callbacks.on_started) {
        m_callbacks.on_started();
    }
    return true;
#endif
}

void SyphonBackend::stop() {
#if SPECTATOR_SYPHON_AVAILABLE
    if (m_syphon_server) {
        SyphonMetalServer *server = (__bridge_transfer SyphonMetalServer *)m_syphon_server;
        [server stop];
        m_syphon_server = nullptr;
        (void)server;
    }
    if (m_metal_texture) {
        id<MTLTexture> tex = (__bridge_transfer id<MTLTexture>)m_metal_texture;
        m_metal_texture = nullptr;
        (void)tex;
    }
    if (m_metal_queue) {
        id<MTLCommandQueue> q = (__bridge_transfer id<MTLCommandQueue>)m_metal_queue;
        m_metal_queue = nullptr;
        (void)q;
    }
    if (m_metal_device) {
        id<MTLDevice> dev = (__bridge_transfer id<MTLDevice>)m_metal_device;
        m_metal_device = nullptr;
        (void)dev;
    }
#endif
    m_started = false;
    m_scratch.clear();
    m_scratch.shrink_to_fit();
}

bool SyphonBackend::push_frame(const ExportedFrame &frame) {
#if !SPECTATOR_SYPHON_AVAILABLE
    (void)frame;
    return false;
#else
    if (!m_started.load()) {
        return false;
    }
    const PackedByteArray &data = frame.pixels;
    const size_t need = (size_t)frame.width * (size_t)frame.height * 4;
    if ((size_t)data.size() < need) {
        return false;
    }
    m_scratch.resize(need);
    std::memcpy(m_scratch.data(), data.ptr(), need);

    @autoreleasepool {
        id<MTLTexture> tex = (__bridge id<MTLTexture>)m_metal_texture;
        MTLRegion region = MTLRegionMake2D(0, 0, frame.width, frame.height);
        [tex replaceRegion:region
               mipmapLevel:0
                 withBytes:m_scratch.data()
               bytesPerRow:frame.width * 4];

        SyphonMetalServer *server = (__bridge SyphonMetalServer *)m_syphon_server;
        id<MTLCommandQueue> q = (__bridge id<MTLCommandQueue>)m_metal_queue;
        id<MTLCommandBuffer> cb = [q commandBuffer];
        [server publishFrameTexture:tex
                    onCommandBuffer:cb
                        imageRegion:region
                  flipped:NO];
        [cb commit];
    }
    return true;
#endif
}

void SyphonBackend::report_error(bool fatal, const std::string &msg) {
    UtilityFunctions::push_error(String("[SyphonBackend] ") + String(msg.c_str()));
    if (m_callbacks.on_error) {
        m_callbacks.on_error(fatal, msg);
    }
}

} // namespace spectator
