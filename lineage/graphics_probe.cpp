/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <ui/GraphicBufferMapper.h>
#include <stdint.h>
#include <ui/GraphicBuffer.h>
#include <utils/StrongPointer.h>
#include <hardware/gralloc.h>

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    unsigned failed = 0;
    android::sp<android::GraphicBuffer> retained[8];
    for (unsigned i = 0; i < 8; ++i) {
        retained[i] = new android::GraphicBuffer(39, 41, android::PIXEL_FORMAT_RGBA_8888, 1,
                static_cast<uint64_t>(GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_SW_READ_OFTEN |
                                      GRALLOC_USAGE_SW_WRITE_OFTEN), "tilapia-retained-probe");
        void *address = nullptr;
        if (retained[i]->initCheck() || retained[i]->lock(GRALLOC_USAGE_SW_WRITE_OFTEN, &address)) {
            printf("FAIL: retained allocation/write lock %u\n", i);
            return 1;
        }
        auto *pixels = static_cast<uint32_t *>(address);
        for (unsigned y = 0; y < 41; ++y)
            for (unsigned x = 0; x < 39; ++x)
                pixels[y * retained[i]->getStride() + x] = 0xff123400u + i;
        if (retained[i]->unlock()) return 1;
    }
    for (unsigned i = 0; i < 64; ++i) {
        android::sp<android::GraphicBuffer> buffer = new android::GraphicBuffer(
                39, 41, android::PIXEL_FORMAT_RGBA_8888, 1,
                static_cast<uint64_t>(GRALLOC_USAGE_HW_TEXTURE), "tilapia-graphics-probe");
        android::status_t result = buffer->initCheck();
        if (result != android::NO_ERROR) {
            printf("FAIL: allocation/import %u returned %d\n", i, result);
            ++failed;
        }
    }
    printf("%u/64 graphics allocation/import operations passed\n", 64 - failed);
    for (unsigned i = 0; i < 8; ++i) {
        void *address = nullptr;
        if (retained[i]->lock(GRALLOC_USAGE_SW_READ_OFTEN, &address)) return 1;
        auto *pixels = static_cast<const uint32_t *>(address);
        for (unsigned y = 0; y < 41; ++y)
            for (unsigned x = 0; x < 39; ++x)
                if (pixels[y * retained[i]->getStride() + x] != 0xff123400u + i) ++failed;
        if (retained[i]->unlock()) return 1;
    }
    puts(failed ? "FAIL: retained buffer contents" : "PASS: 8 retained buffers preserve pixel data after allocator release and 64 intervening allocations");
    const hw_module_t* module = nullptr;
    if (hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module)) return 1;
    auto* gralloc = reinterpret_cast<const gralloc_module_t*>(module);
    auto& mapper = android::GraphicBufferMapper::get();
    for (unsigned i = 0; i < 64; ++i) {
        buffer_handle_t imported = nullptr;
        const auto& buffer = retained[i % 8];
        const uint64_t usage = GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_SW_READ_OFTEN |
                               GRALLOC_USAGE_SW_WRITE_OFTEN;
        if (mapper.importBuffer(buffer->handle, 39, 41, 1,
                android::PIXEL_FORMAT_RGBA_8888, usage, buffer->getStride(), &imported)) return 1;
        void* pixels = nullptr;
        if (gralloc->lock(gralloc, imported, GRALLOC_USAGE_SW_READ_OFTEN,
                          0, 0, 39, 41, &pixels)) return 1;
        const int fd = imported->data[0];
        if (mapper.freeBuffer(imported)) return 1;
        // A legacy GPU/HWC lock retains the native handle beyond mapper release.
        if (fcntl(fd, F_GETFD) < 0 || imported->data[35] != 1 ||
            *static_cast<const uint32_t*>(pixels) != 0xff123400u + i % 8) {
            puts("FAIL: mapper destroyed a handle still retained by NVIDIA");
            return 1;
        }
        if (gralloc->unlock(gralloc, imported)) return 1;
        errno = 0;
        if (fcntl(fd, F_GETFD) != -1 || errno != EBADF) {
            puts("FAIL: final NVIDIA reference did not close the handle fd");
            return 1;
        }
    }
    puts("PASS: 64 imported handles survive mapper release until final NVIDIA unlock");
    return failed ? 1 : 0;
}
