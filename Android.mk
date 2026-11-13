# SPDX-License-Identifier: Apache-2.0
LOCAL_PATH := $(call my-dir)
ifneq ($(filter lineage_tilapia lineage_grouper,$(TARGET_PRODUCT)),)
include $(LOCAL_PATH)/lineage/Android.mk
else
include $(call all-makefiles-under,$(LOCAL_PATH))
endif
