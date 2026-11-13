// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <telephony/ril.h>
#include <cstring>
#include <climits>

namespace tilapia {
inline RIL_RadioState radioState(int state) {
    // Pre-RIL-v6 GSM/RUIM states mean powered-on, not HIDL radio enum values.
    return static_cast<RIL_RadioState>(state >= 2 && state <= 9 ? 10 : state);
}
inline bool cardStatus(const RIL_CardStatus_v5& old, RIL_CardStatus_v6* out) {
    if (old.num_applications < 0 || old.num_applications > RIL_CARD_MAX_APPS) return false;
    auto index = [](int value) { return value == RIL_CARD_MAX_APPS ? -1 : value; };
    const int gsm = index(old.gsm_umts_subscription_app_index);
    const int cdma = index(old.cdma_subscription_app_index);
    if (gsm < -1 || gsm >= old.num_applications || cdma < -1 || cdma >= old.num_applications) return false;
    *out = {};
    out->card_state = old.card_state;
    out->universal_pin_state = old.universal_pin_state;
    out->gsm_umts_subscription_app_index = gsm;
    out->cdma_subscription_app_index = cdma;
    out->ims_subscription_app_index = -1;
    out->num_applications = old.num_applications;
    std::memcpy(out->applications, old.applications, sizeof(old.applications));
    return true;
}
inline bool signalStrength(const void* response, size_t length, RIL_SignalStrength_v10* out) {
    if (!response || (length != sizeof(RIL_SignalStrength_v5) && length != sizeof(RIL_SignalStrength_v6))) return false;
    *out = {};
    RIL_SignalStrength_v5 old;
    std::memcpy(&old, response, sizeof(old));
    out->GW_SignalStrength = old.GW_SignalStrength;
    out->CDMA_SignalStrength = old.CDMA_SignalStrength;
    out->EVDO_SignalStrength = old.EVDO_SignalStrength;
    out->LTE_SignalStrength = {99, INT_MAX, INT_MAX, INT_MAX, INT_MAX, INT_MAX};
    out->TD_SCDMA_SignalStrength.rscp = INT_MAX;
    if (length == sizeof(RIL_SignalStrength_v6)) {
        RIL_SignalStrength_v6 full;
        std::memcpy(&full, response, sizeof(full));
        const auto& lte = full.LTE_SignalStrength;
        out->LTE_SignalStrength.signalStrength = lte.signalStrength;
        out->LTE_SignalStrength.rsrp = lte.rsrp;
        out->LTE_SignalStrength.rsrq = lte.rsrq;
        out->LTE_SignalStrength.rssnr = lte.rssnr;
        out->LTE_SignalStrength.cqi = lte.cqi;
    }
    return true;
}
template<size_t count>
inline bool registration(void* response, size_t length, char* (&out)[count]) {
    if (!response || (length != 3 * sizeof(char*) && length != 4 * sizeof(char*))) return false;
    std::memset(out, 0, sizeof(out));
    std::memcpy(out, response, length);
    return true;
}
} // namespace tilapia
