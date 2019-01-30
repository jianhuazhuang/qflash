LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)
LOCAL_SRC_FILES := protocol.c engine.c fastboot.c 
LOCAL_SRC_FILES += usb_linux.c util_linux.c
LOCAL_CFLAGS += -pie -fPIE
LOCAL_LDFLAGS += -pie -fPIE
LOCAL_MODULE_TAGS:= optional
LOCAL_MODULE := fastboot
include $(BUILD_EXECUTABLE)
