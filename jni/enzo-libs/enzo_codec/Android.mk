ifeq ($(BOARD_HAVE_VPU),true)
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
       enzo_codec.c \
       enzo_utils.c \
       vpu_common.c \
       vpu_decode.c \
       vpu_encode.c \
       v4l2_camera.c

LOCAL_CFLAGS += -DBUILD_FOR_ANDROID

LOCAL_C_INCLUDES += $(LOCAL_PATH) \
	external/linux-lib/vpu \
	device/fsl-proprietary/include/

LOCAL_LDLIBS := -llog

LOCAL_SHARED_LIBRARIES := libutils libc libvpu libg2d

LOCAL_MODULE := libenzocodec
LOCAL_MODULE_TAGS := tests
LOCAL_MODULE_PATH := $(TARGET_OUT)/lib/
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
include $(BUILD_STATIC_LIBRARY)
endif
