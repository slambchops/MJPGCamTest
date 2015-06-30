# Copyright (C) 2009 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE    := libg2d
LOCAL_SRC_FILES := $(LOCAL_PATH)/enzo-libs/libg2d.so
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/enzo-libs/g2d
include $(PREBUILT_SHARED_LIBRARY)

LOCAL_MODULE    := libvpu
LOCAL_SRC_FILES := enzo-libs/libvpu.so
LOCAL_EXPORT_C_INCLUDES := enzo-libs/vpu
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE    := libenzocodec
LOCAL_SRC_FILES := $(LOCAL_PATH)/enzo-libs/libenzocodec.so
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/enzo-libs/enzo_codec

include $(PREBUILT_SHARED_LIBRARY)
include $(CLEAR_VARS)


LOCAL_C_INCLUDES += $(LOCAL_PATH) \
	$(LOCAL_PATH)/enzo-libs/g2d \
	$(LOCAL_PATH)/enzo-libs/vpu \
	$(LOCAL_PATH)/enzo-libs/enzo_codec

LOCAL_MODULE    := libcamview
LOCAL_SRC_FILES := CamView.c
LOCAL_SHARED_LIBRARIES := libvpu libg2d libenzocodec liblog libbinder libjnigraphics
LOCAL_LDLIBS    := -llog -ljnigraphics
LOCAL_CFLAGS += -std=c99

include $(BUILD_SHARED_LIBRARY)
