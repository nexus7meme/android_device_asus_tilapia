// SPDX-License-Identifier: Apache-2.0
// The legacy GPS daemon allocates its old SensorManager layout. Keep that
// allocation opaque and forward every manager operation to the platform instance.
#include <sensor/SensorManager.h>
#include <sensor/SensorEventQueue.h>
#include <utils/String16.h>
using namespace android;
static SensorManager& manager() {
    return SensorManager::getInstanceForPackage(String16("gps.tegra3"));
}
void* legacySingleton asm("_ZN7android9SingletonINS_13SensorManagerEE9sInstanceE") = nullptr;
void legacyConstruct(void*) asm("_ZN7grouper13SensorManagerC1Ev");
void legacyConstruct(void*) {}
ssize_t legacyList(void*, Sensor const* const**) asm("_ZN7grouper13SensorManager13getSensorListEPPKPKNS_6SensorE");
ssize_t legacyList(void*, Sensor const* const** list) { return manager().getSensorList(list); }
Sensor const* legacyDefault(void*, int) asm("_ZN7grouper13SensorManager16getDefaultSensorEi");
Sensor const* legacyDefault(void*, int type) { return manager().getDefaultSensor(type); }
sp<SensorEventQueue> legacyQueue(void*) asm("_ZN7grouper13SensorManager16createEventQueueEv");
sp<SensorEventQueue> legacyQueue(void*) { return manager().createEventQueue(); }
