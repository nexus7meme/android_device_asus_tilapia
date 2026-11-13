// SPDX-License-Identifier: Apache-2.0
// Bridge the legacy NVIDIA window API to Android 11 SurfaceControl transactions.
#include <gui/SurfaceComposerClient.h>
#include <gui/SurfaceControl.h>
#include <ui/DisplayConfig.h>
#include <ui/DisplayInfo.h>
#include <ui/DisplayState.h>
#include <memory>
#include <mutex>
using namespace android;
// The old NVIDIA allocation is 24 bytes (RefBase, Mutex, status, client,
// Composer reference). The current class must fit its shared field prefix.
static_assert(sizeof(void*) == 4);
static_assert(sizeof(RefBase) == 8);
static_assert(sizeof(Mutex) == 4);
static_assert(sizeof(SurfaceComposerClient) <= 24);
namespace {
std::mutex transactionMutex;
std::unique_ptr<SurfaceComposerClient::Transaction> pending;
unsigned depth;
bool syncRequested;
template<class F> status_t update(F operation) {
    std::lock_guard<std::mutex> lock(transactionMutex);
    if (depth) { operation(*pending); return OK; }
    SurfaceComposerClient::Transaction transaction;
    operation(transaction);
    return transaction.apply();
}
}
void legacyOpen() asm("_ZN7android21SurfaceComposerClient21openGlobalTransactionEv");
void legacyOpen() {
    std::lock_guard<std::mutex> lock(transactionMutex);
    if (depth++ == 0) {
        pending = std::make_unique<SurfaceComposerClient::Transaction>();
        syncRequested = false;
    }
}
void legacyClose(bool) asm("_ZN7android21SurfaceComposerClient22closeGlobalTransactionEb");
void legacyClose(bool synchronous) {
    std::lock_guard<std::mutex> lock(transactionMutex);
    if (!depth) return;
    syncRequested |= synchronous;
    if (--depth == 0) { pending->apply(syncRequested); pending.reset(); }
}
status_t legacyPosition(SurfaceControl*, int32_t, int32_t) asm("_ZN7android14SurfaceControl11setPositionEii");
status_t legacyPosition(SurfaceControl* self, int32_t x, int32_t y) {
    return update([&](auto& t) { t.setPosition(sp<SurfaceControl>(self), x, y); });
}
status_t legacySize(SurfaceControl*, uint32_t, uint32_t) asm("_ZN7android14SurfaceControl7setSizeEjj");
status_t legacySize(SurfaceControl* self, uint32_t w, uint32_t h) {
    return update([&](auto& t) { t.setSize(sp<SurfaceControl>(self), w, h); });
}
status_t legacyLayer(SurfaceControl*, int32_t) asm("_ZN7android14SurfaceControl8setLayerEi");
status_t legacyLayer(SurfaceControl* self, int32_t layer) {
    return update([&](auto& t) { t.setLayer(sp<SurfaceControl>(self), layer); });
}
sp<SurfaceControl> legacyCreate(SurfaceComposerClient*, const String8&, uint32_t, uint32_t, PixelFormat, uint32_t)
    asm("_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8Ejjij");
sp<SurfaceControl> legacyCreate(SurfaceComposerClient* self, const String8& name, uint32_t w, uint32_t h, PixelFormat format, uint32_t flags) {
    return self->createSurface(name, w, h, format, flags);
}
sp<IBinder> legacyDisplay(int32_t) asm("_ZN7android21SurfaceComposerClient17getBuiltInDisplayEi");
sp<IBinder> legacyDisplay(int32_t id) {
    if (id == 0) return SurfaceComposerClient::getInternalDisplayToken();
    if (id == 1) {
        auto internal = SurfaceComposerClient::getInternalDisplayId();
        for (auto display : SurfaceComposerClient::getPhysicalDisplayIds())
            if (!internal || display != *internal) return SurfaceComposerClient::getPhysicalDisplayToken(display);
    }
    return nullptr;
}

// Android 4.x display information begins with this stable 28-byte prefix.
// Never pass the legacy allocation to Android 11's differently sized structure.
struct LegacyDisplayInfo {
    uint32_t width, height;
    float xDpi, yDpi, refreshRate, density;
    uint8_t orientation;
    bool secure;
    uint8_t reserved[2];
};
static_assert(sizeof(LegacyDisplayInfo) == 28);
status_t legacyInfo(const sp<IBinder>&, LegacyDisplayInfo*)
    asm("_ZN7grouper21SurfaceComposerClient14getDisplayInfoERKNS_2spINS_7IBinderEEEPNS_11DisplayInfoE");
status_t legacyInfo(const sp<IBinder>& display, LegacyDisplayInfo* output) {
    if (!output) return BAD_VALUE;
    DisplayInfo info;
    DisplayConfig config;
    ui::DisplayState state;
    status_t result = SurfaceComposerClient::getDisplayInfo(display, &info);
    if (result != OK) return result;
    result = SurfaceComposerClient::getActiveDisplayConfig(display, &config);
    if (result != OK) return result;
    result = SurfaceComposerClient::getDisplayState(display, &state);
    if (result != OK) return result;
    auto size = state.viewport.isValid() ? state.viewport : config.resolution;
    *output = {static_cast<uint32_t>(size.getWidth()), static_cast<uint32_t>(size.getHeight()),
               config.xDpi, config.yDpi, config.refreshRate, info.density,
               static_cast<uint8_t>(state.orientation), info.secure, {0, 0}};
    return OK;
}
