# SPDX-License-Identifier: Apache-2.0
# Android 11 wrappers around the retained legacy HAL modules.
PRODUCT_PACKAGES += \
    android.hardware.audio@2.0-impl \
    android.hardware.audio.effect@2.0-impl \
    android.hardware.audio.service \
    android.hardware.bluetooth@1.0-impl \
    android.hardware.bluetooth@1.0-service \
    android.hardware.bluetooth.audio@2.0-impl \
    audio.bluetooth.default \
    libbt-vendor \
    android.hardware.graphics.allocator@2.0-impl \
    android.hardware.graphics.mapper@2.0-impl \
    android.hardware.graphics.composer@2.1-service \
    android.hardware.drm@1.0-impl \
    android.hardware.drm@1.0-service \
    libdrmclearkeyplugin \
    android.hardware.gatekeeper@1.0-service.software \
    android.hardware.keymaster@3.0-impl \
    android.hardware.keymaster@3.0-service \
    android.hardware.gnss@1.0-impl \
    android.hardware.gnss@1.0-service \
    android.hardware.health@2.1-impl \
    android.hardware.health@2.1-service \
    android.hardware.light@2.0-impl \
    android.hardware.light@2.0-service \
    android.hardware.media.omx@1.0-service \
    android.hardware.power@1.0-impl \
    android.hardware.power@1.0-service \
    android.hardware.sensors@1.0-impl \
    android.hardware.sensors@1.0-service

PRODUCT_COPY_FILES += \
    device/asus/tilapia/lineage/bt_vendor.conf:$(TARGET_COPY_OUT_VENDOR)/etc/bluetooth/bt_vendor.conf

PRODUCT_COPY_FILES += \
    device/asus/tilapia/lineage/audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_configuration.xml
PRODUCT_COPY_FILES += frameworks/av/services/audiopolicy/config/bluetooth_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/bluetooth_audio_policy_configuration.xml
PRODUCT_COPY_FILES += frameworks/av/services/audiopolicy/config/usb_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/usb_audio_policy_configuration.xml
PRODUCT_COPY_FILES += frameworks/av/services/audiopolicy/config/r_submix_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/r_submix_audio_policy_configuration.xml
PRODUCT_COPY_FILES += frameworks/av/services/audiopolicy/config/audio_policy_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_volumes.xml
PRODUCT_COPY_FILES += frameworks/av/services/audiopolicy/config/default_volume_tables.xml:$(TARGET_COPY_OUT_VENDOR)/etc/default_volume_tables.xml

# Override the platform service capabilities for the legacy wake-lock kernel.
PRODUCT_COPY_FILES += \
    device/asus/tilapia/lineage/bluetooth.tilapia.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/bluetooth.tilapia.rc

# Defer the initial address to BCM4330 OTP, as configured in vnd_tilapia.txt.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += ro.bt.bdaddr_from_controller=true
