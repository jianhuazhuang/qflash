export USE_NDK=1
NDK_BUILD=ndk-build
NDK_PROJECT_PATH=`pwd`
NDK_DEBUG=0
APP_ABI=armeabi-v7a,arm64-v8a#,x86,mips,armeabi-v7a,arm64-v8a,mips64,x86_64
APP_BUILD_SCRIPT=Android.mk
NDK_APPLICATION_MK=Application.mk
NDK_OUT=out

#CFLAGS+=-Wall
#CFLAGS+=-O2
#CFLAGS+=-Wmissing-declarations
#CFLAGS+=-fno-strict-aliasing
#CFLAGS+=-Wno-deprecated-declarations
#CFLAGS+=-Wint-to-pointer-cast
#CFLAGS+=-Wfloat-equal
#CFLAGS+=-Wno-unused-parameter
#CFLAGS+=-Wno-sign-compare
#CFLAGS+=-Wunused-but-set-variable
#CFLAGS+=-Wundef
#CFLAGS+=-Wpointer-arith
#CFLAGS+=-Winit-self
#CFLAGS+=-Wshadow
#CFLAGS+=-Wmissing-include-dirs
#CFLAGS+=-Waggregate-return
#CFLAGS+=-Wformat-security
#CFLAGS+=-Wtype-limits
## CFLAGS+=-Werror
#CFLAGS+=-Wunreachable-code
#CFLAGS+=-pipe
#CFLAGS+=-fstack-check
#CFLAGS+=-Wredundant-decls
#CFLAGS+=-fstack-protector-all

all: qfastboot qfirehose
	$(CROSS_COMPILE)g++ $(CFLAGS) -g -c -DPROGRESS_FILE_FAETURE -DFIREHOSE_ENABLE tinystr.cpp tinyxml.cpp tinyxmlerror.cpp tinyxmlparser.cpp md5.cpp at_tok.cpp atchannel.cpp ril-daemon.cpp download.cpp file.cpp os_linux.cpp serialif.cpp quectel_log.cpp quectel_common.cpp quectel_crc.cpp
	$(CROSS_COMPILE)g++ *.o -lrt -lpthread -o QFlash

debug: clean qfastboot qfirehose
	$(CROSS_COMPILE)g++ -g -c -DDEBUG -DPROGRESS_FILE_FAETURE -DFIREHOSE_ENABLE tinystr.cpp tinyxml.cpp tinyxmlerror.cpp tinyxmlparser.cpp md5.cpp at_tok.cpp atchannel.cpp ril-daemon.cpp download.cpp file.cpp os_linux.cpp serialif.cpp quectel_log.cpp quectel_common.cpp quectel_crc.cpp
	$(CROSS_COMPILE)g++ *.o -lrt -lpthread -o QFlash

qfirehose:
	if sh tool.sh build_firehose;then exit; fi;

qfastboot:
	if sh tool.sh build_fastboot;then exit; fi;

android: clean
	$(NDK_BUILD) V=0 NDK_OUT=$(NDK_OUT)  NDK_APPLICATION_MK=$(NDK_APPLICATION_MK) NDK_LIBS_OUT=$(NDK_LIBS_OUT) APP_BUILD_SCRIPT=$(APP_BUILD_SCRIPT) NDK_PROJECT_PATH=$(NDK_PROJECT_PATH) NDK_DEBUG=$(NDK_DEBUG) APP_ABI=$(APP_ABI)
clean:
	rm -rf $(NDK_OUT) libs *.o QFlash QFlash *~
	sh tool.sh clean_fastboot
	sh tool.sh clean_firehose
