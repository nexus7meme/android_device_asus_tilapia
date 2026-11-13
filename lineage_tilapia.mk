# SPDX-License-Identifier: Apache-2.0
# Android 11 product. See RELEASE.md for build and validation requirements.
$(call inherit-product, vendor/lineage/config/common_mini_tablet_wifionly.mk)
$(call inherit-product, vendor/lineage/config/telephony.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/aosp_base.mk)
$(call inherit-product, device/asus/tilapia/device.mk)
$(call inherit-product, device/asus/tilapia/lineage/hal.mk)

# Android 11 Go's 1 GiB profile: low-RAM framework behavior, profile-guided
# boot image, and in-process network/tethering services.
$(call inherit-product, $(SRC_TARGET_DIR)/product/go_defaults.mk)

# Bound on-device dex compilation memory and parallelism on this 1 GiB tablet.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    dalvik.vm.dex2oat-threads=2 \
    dalvik.vm.background-dex2oat-threads=2


# Nexus 7 3G shipped with Android 4.2 (API 17). This is not a Treble launch device.
PRODUCT_SHIPPING_API_LEVEL := 17
PRODUCT_USE_DYNAMIC_PARTITIONS := false
# Android 11 otherwise leaves the image targets empty when legacy BoardConfig
# has no boot/recovery size declarations. Measure device limits before flashing.
PRODUCT_BUILD_BOOT_IMAGE := true
PRODUCT_BUILD_RECOVERY_IMAGE := true
PRODUCT_BUILD_PRODUCT_IMAGE := true
PRODUCT_BUILD_CACHE_IMAGE := false
PRODUCT_OTA_ENFORCE_VINTF_KERNEL_REQUIREMENTS := false
TARGET_SCREEN_WIDTH := 800
TARGET_SCREEN_HEIGHT := 1280

PRODUCT_BRAND := google
PRODUCT_DEVICE := tilapia
PRODUCT_MANUFACTURER := asus
PRODUCT_MODEL := Nexus 7 3G
PRODUCT_NAME := lineage_tilapia

PRODUCT_SYSTEM_DEFAULT_PROPERTIES += ro.adb.nonblocking_ffs=false sys.usb.ffs.aio_compat=true

# CarrierConfig is required for DcTracker to mark the SIM configuration ready.
# The shared Wi-Fi tablet base intentionally omits this telephony component.
PRODUCT_PACKAGES += libtilapia-ril CarrierConfig
