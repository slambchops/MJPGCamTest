LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := $(call all-java-files-under, src)

LOCAL_PACKAGE_NAME := CamCodecTest
#LOCAL_SDK_VERSION := current

LOCAL_JNI_SHARED_LIBRARIES := libcamview

LOCAL_REQUIRED_MODULES := libcamview

include $(BUILD_PACKAGE)

ifeq ($(strip $(LOCAL_PACKAGE_OVERRIDES)),)
# Use the following include to make our test apk.
include $(call all-makefiles-under, $(LOCAL_PATH))
endif