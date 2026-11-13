// SPDX-License-Identifier: Apache-2.0
#define LOG_TAG "TilapiaRil"
#include "ril_compat.h"
#include <dlfcn.h>
#include <log/log.h>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <string>
#include <vector>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

static const RIL_Env* framework;
static RIL_Env vendorEnv;
static const RIL_RadioFunctions* vendor;
static RIL_RadioFunctions functions;
static std::mutex lock;
static int (*restoreNetworkSelection)();
// Accessed only by the serialized request/timer thread.
static bool simReady;
static bool networkSelectionRestored;
struct Request { int code; int phase = 0; std::string imei; };
static std::unordered_map<RIL_Token, Request> requests;
static_assert(sizeof(RIL_Data_Call_Response_v6) == 36, "legacy data-call ABI");
static std::mutex addressLock;
// Preserve the modem-configured prefix after Android configures the same link.
static std::unordered_map<std::string, std::pair<std::string, std::string>> knownAddresses;
static int maskPrefix(uint32_t mask) {
    const uint32_t inverse = ~mask;
    if (inverse & (inverse + 1)) return -1;
    return __builtin_popcount(mask);
}
static std::string dataAddress(const char* iface, const char* address, bool fresh) {
    if (!address) return {};
    in_addr ip{};
    if (!iface || strnlen(iface, IFNAMSIZ) == IFNAMSIZ || inet_pton(AF_INET, address, &ip) != 1 || !ip.s_addr) return address;
    std::lock_guard<std::mutex> guard(addressLock);
    auto found = knownAddresses.find(iface);
    if (!fresh && found != knownAddresses.end() && found->second.first == address) return found->second.second;
    // The XGold driver configures this interface before returning its v6 reply.
    // Read its actual mask and verify the address; do not guess a carrier subnet.
    ifreq request{};
    std::memcpy(request.ifr_name, iface, std::strlen(iface) + 1);
    const int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return address;
    int prefix = -1;
    if (ioctl(fd, SIOCGIFADDR, &request) == 0 &&
        reinterpret_cast<sockaddr_in*>(&request.ifr_addr)->sin_addr.s_addr == ip.s_addr &&
        ioctl(fd, SIOCGIFNETMASK, &request) == 0) {
        prefix = maskPrefix(ntohl(reinterpret_cast<sockaddr_in*>(&request.ifr_netmask)->sin_addr.s_addr));
    }
    close(fd);
    if (prefix < 0) return address;
    std::string normalized = std::string(address) + "/" + std::to_string(prefix);
    knownAddresses[iface] = {address, normalized};
    ALOGI("Preserved modem IPv4 prefix: /%d", prefix);
    return normalized;
}
static bool dataCalls(const void* response, size_t length, std::vector<RIL_Data_Call_Response_v11>* out,
                      std::vector<std::string>* addresses = nullptr, bool fresh = false) {
    // This modem returns v6, even when its table advertises RIL version 10.
    // Bound the count before allocation; never forward a corrupt vendor length.
    if ((!response && length) || length % sizeof(RIL_Data_Call_Response_v6) ||
        length / sizeof(RIL_Data_Call_Response_v6) > 64) return false;
    auto old = static_cast<const RIL_Data_Call_Response_v6*>(response);
    out->resize(length / sizeof(*old));
    if (addresses) addresses->reserve(out->size());
    for (size_t i = 0; i < out->size(); ++i) {
        auto& current = (*out)[i];
        current = {};
        std::memcpy(&current, &old[i], sizeof(*old));
        if (addresses) {
            addresses->push_back(old[i].active ? dataAddress(old[i].ifname, old[i].addresses, fresh) : (old[i].addresses ? old[i].addresses : ""));
            current.addresses = const_cast<char*>(addresses->back().c_str());
        }
    }
    return true;
}
static std::string digits(void* response, size_t count) {
    if (!response || strnlen(static_cast<char*>(response), count + 1) != count) return {};
    std::string value(static_cast<char*>(response), count);
    return value.find_first_not_of("0123456789") == std::string::npos ? value : std::string{};
}
static_assert(sizeof(RIL_CardStatus_v5) == 276 && sizeof(RIL_CardStatus_v6) == 280, "32-bit legacy SIM ABI");
static_assert(sizeof(RIL_SignalStrength_v6) == 48, "legacy signal-strength ABI");

static void complete(RIL_Token token, RIL_Errno error, void* response, size_t length) {
    Request pending{};
    int code = 0;
    {
        std::lock_guard<std::mutex> guard(lock);
        auto it = requests.find(token);
        if (it != requests.end()) { pending = std::move(it->second); code = pending.code; requests.erase(it); }
    }
    if (code) ALOGI("Legacy response request=%d error=%d bytes=%zu", code, error, length);
    if (code == RIL_REQUEST_DEVICE_IDENTITY) {
        // GSM vendor implements the two pre-v6 requests, not DEVICE_IDENTITY.
        if (pending.phase == 0) {
            pending.imei = error == RIL_E_SUCCESS ? digits(response, 15) : std::string{};
            if (pending.imei.empty()) {
                framework->OnRequestComplete(token, error == RIL_E_SUCCESS ? RIL_E_INVALID_RESPONSE : error, nullptr, 0);
                return;
            }
            pending.phase = 1;
            { std::lock_guard<std::mutex> guard(lock); requests[token] = std::move(pending); }
            vendor->onRequest(RIL_REQUEST_GET_IMEISV, nullptr, 0, token);
            return;
        }
        std::string sv = error == RIL_E_SUCCESS ? digits(response, 2) : std::string{};
        char empty[] = "";
        char* identity[] = {const_cast<char*>(pending.imei.c_str()), const_cast<char*>(sv.c_str()), empty, empty};
        framework->OnRequestComplete(token, RIL_E_SUCCESS, identity, sizeof(identity));
        return;
    }
    if (error == RIL_E_SUCCESS && (code == RIL_REQUEST_DATA_CALL_LIST || code == RIL_REQUEST_SETUP_DATA_CALL)) {
        std::vector<RIL_Data_Call_Response_v11> current;
        std::vector<std::string> addresses;
        if (!dataCalls(response, length, &current, &addresses, code == RIL_REQUEST_SETUP_DATA_CALL)) {
            ALOGE("Rejected malformed legacy data-call response: bytes=%zu", length);
            framework->OnRequestComplete(token, RIL_E_INVALID_RESPONSE, nullptr, 0);
        } else framework->OnRequestComplete(token, error, current.data(), current.size() * sizeof(current[0]));
        return;
    }
    if (error == RIL_E_SUCCESS) {
        char* voice[15];
        char* data[6];
        if (code == RIL_REQUEST_VOICE_REGISTRATION_STATE && tilapia::registration(response, length, voice)) {
            framework->OnRequestComplete(token, error, voice, sizeof(voice)); return;
        }
        if (code == RIL_REQUEST_DATA_REGISTRATION_STATE && tilapia::registration(response, length, data)) {
            framework->OnRequestComplete(token, error, data, sizeof(data)); return;
        }
        RIL_SignalStrength_v10 strength;
        if (code == RIL_REQUEST_SIGNAL_STRENGTH && tilapia::signalStrength(response, length, &strength)) {
            framework->OnRequestComplete(token, error, &strength, sizeof(strength)); return;
        }
    }
    if (code == RIL_REQUEST_GET_SIM_STATUS && error == RIL_E_SUCCESS && response && length == sizeof(RIL_CardStatus_v5)) {
        RIL_CardStatus_v6 current;
        if (tilapia::cardStatus(*static_cast<RIL_CardStatus_v5*>(response), &current)) {
            const int index = current.gsm_umts_subscription_app_index;
            simReady = index >= 0 && current.applications[index].app_state == RIL_APPSTATE_READY;
            ALOGI("Translated legacy SIM status: state=%d applications=%d", current.card_state, current.num_applications);
            framework->OnRequestComplete(token, error, &current, sizeof(current));
            return;
        }
        ALOGE("Rejected malformed legacy SIM status");
    }
    framework->OnRequestComplete(token, error, response, length);
}
static void requestOnEventThread(int code, void* data, size_t length, RIL_Token token) {
    if (code == RIL_REQUEST_RADIO_POWER && data && length == sizeof(int) && *static_cast<int*>(data) == 0) {
        simReady = false;
        networkSelectionRestored = false;
    }
    if (code == RIL_REQUEST_GET_SIM_STATUS || code == RIL_REQUEST_VOICE_REGISTRATION_STATE ||
        code == RIL_REQUEST_DATA_REGISTRATION_STATE || code == RIL_REQUEST_SIGNAL_STRENGTH ||
        code == RIL_REQUEST_DEVICE_IDENTITY || code == RIL_REQUEST_SIM_IO ||
        code == RIL_REQUEST_DATA_CALL_LIST || code == RIL_REQUEST_SETUP_DATA_CALL) {
        std::lock_guard<std::mutex> guard(lock); requests[token] = Request{code, 0, {}};
    }
    vendor->onRequest(code == RIL_REQUEST_DEVICE_IDENTITY ? RIL_REQUEST_GET_IMEI : code, data, length, token);
    // Android's initial power-off/on can leave XGold in COPS=2 after its
    // startup restoration. Reapply its own saved auto/manual choice once the
    // SIM is ready, using the vendor routine and the same serialized thread.
    if (code == RIL_REQUEST_GET_SIM_STATUS && simReady && !networkSelectionRestored && restoreNetworkSelection) {
        const int result = restoreNetworkSelection();
        networkSelectionRestored = result == 0;
        ALOGI("Restored saved network selection after SIM ready: result=%d", result);
    }
}
// The legacy RIL contract runs requests and timed callbacks on one thread.
// HIDL invokes onRequest from Binder threads; retain the old serialization and
// keep its borrowed request payload alive until the event-loop call returns.
struct Dispatch {
    int code; void* data; size_t length; RIL_Token token;
    std::mutex mutex;
    std::condition_variable condition;
    bool done = false;
};
static void dispatch(void* opaque) {
    auto& work = *static_cast<Dispatch*>(opaque);
    requestOnEventThread(work.code, work.data, work.length, work.token);
    std::lock_guard<std::mutex> guard(work.mutex);
    work.done = true;
    work.condition.notify_one();
}
static void request(int code, void* data, size_t length, RIL_Token token) {
    Dispatch work{code, data, length, token, {}, {}, false};
    framework->RequestTimedCallback(dispatch, &work, nullptr);
    std::unique_lock<std::mutex> guard(work.mutex);
    work.condition.wait(guard, [&work] { return work.done; });
}
static RIL_RadioState state() {
    return tilapia::radioState(static_cast<int>(vendor->onStateRequest()));
}
static void cancel(RIL_Token token) {
    { std::lock_guard<std::mutex> guard(lock); requests.erase(token); }
    vendor->onCancel(token);
}
static void unsolicited(int code, const void* response, size_t length) {
    if (code == RIL_UNSOL_DATA_CALL_LIST_CHANGED) {
        ALOGI("Legacy data-call indication: bytes=%zu", length);
        std::vector<RIL_Data_Call_Response_v11> current;
        std::vector<std::string> addresses;
        if (!dataCalls(response, length, &current, &addresses)) {
            ALOGE("Rejected malformed legacy data-call indication: bytes=%zu", length);
            return;
        }
        framework->OnUnsolicitedResponse(code, current.data(), current.size() * sizeof(current[0]));
        return;
    }
    RIL_SignalStrength_v10 strength;
    if (code == RIL_UNSOL_SIGNAL_STRENGTH && tilapia::signalStrength(response, length, &strength)) {
        framework->OnUnsolicitedResponse(code, &strength, sizeof(strength)); return;
    }
    framework->OnUnsolicitedResponse(code, response, length);
}
extern "C" const RIL_RadioFunctions* RIL_Init(const RIL_Env* env, int argc, char** argv) {
    void* library = dlopen("/vendor/lib/libxgold-ril.so", RTLD_NOW | RTLD_LOCAL);
    if (!library) { ALOGE("Loading vendor RIL failed: %s", dlerror()); return nullptr; }
    auto init = reinterpret_cast<const RIL_RadioFunctions* (*)(const RIL_Env*, int, char**)>(dlsym(library, "RIL_Init"));
    if (!init) { ALOGE("Vendor RIL_Init missing"); dlclose(library); return nullptr; }
    restoreNetworkSelection = reinterpret_cast<int (*)()>(dlsym(library, "restoreSavedNetworkSelectionMode"));
    framework = env;
    vendorEnv = *env;
    vendorEnv.OnRequestComplete = complete;
    vendorEnv.OnUnsolicitedResponse = unsolicited;
    vendor = init(&vendorEnv, argc, argv);
    if (!vendor || !vendor->onRequest || !vendor->onStateRequest) return nullptr;
    functions = *vendor;
    functions.onRequest = request;
    functions.onStateRequest = state;
    if (vendor->onCancel) functions.onCancel = cancel;
    ALOGI("Initialized legacy GSM RIL adapter (vendor version %d)", vendor->version);
    return &functions;
}
