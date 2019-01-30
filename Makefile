export USE_NDK=1
NDK_BUILD=ndk-build
NDK_PROJECT_PATH=`pwd`
NDK_DEBUG=0
APP_ABI=armeabi-v7a,arm64-v8a#,x86,mips,armeabi-v7a,arm64-v8a,mips64,x86_64
APP_BUILD_SCRIPT=Android.mk
NDK_APPLICATION_MK=Application.mk
NDK_OUT=out

all:
	rm -rf *.o
	$(CROSS_COMPILE)gcc -g -c -DPROGRESS_FILE_FAETURE -DUSE_FASTBOOT fastboot/protocol.c fastboot/engine.c fastboot/fastboot.c fastboot/usb_linux_fastboot.c fastboot/util_linux.c	
	$(CROSS_COMPILE)gcc -g -c -DPROGRESS_FILE_FAETURE -DFIREHOSE_ENABLE firehose/qfirehose.c firehose/sahara_protocol.c firehose/firehose_protocol.c firehose/usb_linux_firehose.c	
	$(CROSS_COMPILE)g++ -g -c -DPROGRESS_FILE_FAETURE -DFIREHOSE_ENABLE tinystr.cpp tinyxml.cpp tinyxmlerror.cpp tinyxmlparser.cpp md5.cpp at_tok.cpp atchannel.cpp ril-daemon.cpp download.cpp file.cpp os_linux.cpp serialif.cpp quectel_log.cpp quectel_common.cpp quectel_crc.cpp
	$(CROSS_COMPILE)g++ *.o -lrt -lpthread -o QFlash
android: clean
	$(NDK_BUILD) V=0 NDK_OUT=$(NDK_OUT)  NDK_APPLICATION_MK=$(NDK_APPLICATION_MK) NDK_LIBS_OUT=$(NDK_LIBS_OUT) APP_BUILD_SCRIPT=$(APP_BUILD_SCRIPT) NDK_PROJECT_PATH=$(NDK_PROJECT_PATH) NDK_DEBUG=$(NDK_DEBUG) APP_ABI=$(APP_ABI)
clean:
	rm -rf $(NDK_OUT) libs *.o QFlash QFlash *~
	
