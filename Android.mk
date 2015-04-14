LOCAL_PATH:= $(call my-dir)

common_cflags := -Wall -Werror -DANDROID
# XXX: nexus?
ifeq ($(BOARD_USES_QCOM_HARDWARE),true)
common_cflags += -DQCOM
endif

include $(CLEAR_VARS)
LOCAL_MODULE := libgpt
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS := $(common_cflags)
LOCAL_SRC_FILES := util.c readline/readline.c crc32.c gpt.c
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := gpted
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := gpted.c
LOCAL_CFLAGS := $(common_cflags)
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_STATIC_LIBRARIES := libc libgpt
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := gpted_recovery
LOCAL_MODULE_STEM := gpted
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_CFLAGS := $(common_cflags)
LOCAL_SRC_FILES := gpted.c
LOCAL_STATIC_LIBRARIES := libc libgpt
include $(BUILD_EXECUTABLE)
