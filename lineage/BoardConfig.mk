# SPDX-License-Identifier: Apache-2.0
# Standalone Android 11 board configuration: do not inherit the Nougat GCC flags.
TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_CPU_VARIANT := cortex-a9
TARGET_BOARD_PLATFORM := tegra3
TARGET_TEGRA_VERSION := t30
# The tilapia init file is deliberately installed as init.grouper.rc by device.mk.
TARGET_BOOTLOADER_BOARD_NAME := grouper
TARGET_NO_BOOTLOADER := true
TARGET_NO_RADIOIMAGE := true
TARGET_BOARD_INFO_FILE := device/asus/tilapia/board-info.txt

TARGET_KERNEL_SOURCE := kernel/asus/grouper
TARGET_KERNEL_CONFIG := lineageos_tilapia_defconfig
BOARD_KERNEL_IMAGE_NAME := zImage
BOARD_KERNEL_CMDLINE := androidboot.hardware=grouper
# Match CONFIG_ANDROID_BINDER_IPC_32BIT=n in lineageos_tilapia_defconfig.
TARGET_USES_64_BIT_BINDER := true
# The kernel provides real memfd-backed shared mappings for ART's W^X JIT.
TARGET_HAS_MEMFD_BACKPORT := true

TARGET_USERIMAGES_USE_EXT4 := true
TARGET_USERIMAGES_USE_F2FS := true
TARGET_COPY_OUT_VENDOR := system/vendor
# First-stage init pivots into the system-root image; retain the proven ramdisk
# boot protocol rather than asking this legacy bootloader to mount root itself.
BOARD_BUILD_SYSTEM_ROOT_IMAGE := true
BOARD_INCLUDE_BOOT_RAMDISK := true
TARGET_COPY_OUT_PRODUCT := product
TARGET_COPY_OUT_SYSTEM_EXT := product/system_ext
BOARD_PRODUCTIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_PRODUCTIMAGE_PARTITION_SIZE := 464519168
BOARD_PRODUCTIMAGE_JOURNAL_SIZE := 0
BOARD_ROOT_EXTRA_SYMLINKS += /product/system_ext:system_ext
# Measured read-only on the connected 32 GB Tilapia (bootloader 4.23).
BOARD_BOOTIMAGE_PARTITION_SIZE := 8388608
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 12582912
BOARD_KERNEL_BASE := 0x10000000
BOARD_KERNEL_PAGESIZE := 2048
BOARD_BOOT_HEADER_VERSION := 0
BOARD_RAMDISK_USE_XZ := true
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 681574400
BOARD_SYSTEMIMAGE_JOURNAL_SIZE := 0
BOARD_FLASH_BLOCK_SIZE := 4096
# No userdata image size is guessed: tilapia storage capacity must be measured.
TARGET_RECOVERY_FSTAB := device/asus/tilapia/lineage/recovery.fstab
TARGET_RECOVERY_PIXEL_FORMAT := RGBX_8888
TARGET_RECOVERY_DENSITY := tvdpi

BOARD_BLUETOOTH_BDROID_BUILDCFG_INCLUDE_DIR := device/asus/tilapia/lineage/bluetooth
BOARD_HAVE_BLUETOOTH := true
BOARD_HAVE_BLUETOOTH_BCM := true
BOARD_WLAN_DEVICE := bcmdhd
WPA_SUPPLICANT_VERSION := VER_0_8_X
BOARD_WPA_SUPPLICANT_DRIVER := NL80211
BOARD_HOSTAPD_DRIVER := NL80211
BOARD_HOSTAPD_PRIVATE_LIB := lib_driver_cmd_bcmdhd
BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_bcmdhd
WIFI_DRIVER_FW_PATH_PARAM := "/sys/module/bcmdhd/parameters/firmware_path"
WIFI_DRIVER_FW_PATH_STA := "/vendor/firmware/fw_bcmdhd.bin"
WIFI_DRIVER_FW_PATH_AP := "/vendor/firmware/fw_bcmdhd_apsta.bin"

USE_OPENGL_RENDERER := true
NUM_FRAMEBUFFER_SURFACE_BUFFERS := 3
MALLOC_SVELTE := true
WITH_DEXPREOPT := true
WITH_DEXPREOPT_BOOT_IMG_ONLY := true
# Android 11 policy keeps neverallow checks enabled.
BOARD_VENDOR_SEPOLICY_DIRS += device/asus/tilapia/lineage/sepolicy

TARGET_SYSTEM_PROP += device/asus/tilapia/lineage/system.prop

DEVICE_MANIFEST_FILE := device/asus/tilapia/lineage/manifest.xml
BOARD_CUSTOM_BT_CONFIG := device/asus/tilapia/lineage/vnd_tilapia.txt
WIFI_HIDL_UNIFIED_SUPPLICANT_SERVICE_RC_ENTRY := true

# Keep legacy host mke2fs independent of the host distro ext4 feature defaults.
BUILD_BROKEN_NINJA_USES_ENV_VARS += MKE2FS_CONFIG

# CAC is Product; guard against old recoveries mounting it as cache.
TARGET_RELEASETOOLS_EXTENSIONS := device/asus/tilapia/lineage

