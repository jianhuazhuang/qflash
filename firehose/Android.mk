LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)
LOCAL_SRC_FILES:= qfirehose.c sahara_protocol.c firehose_protocol.c usb_linux.c
LOCAL_CFLAGS += -pie -fPIE -Wall
LOCAL_LDFLAGS += -pie -fPIE
LOCAL_MODULE_TAGS:= optional
LOCAL_MODULE:= qfirehose
include $(BUILD_EXECUTABLE)
