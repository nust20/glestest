NDK_PATH=/mnt/d/Android/android-ndk-r13

TOOLCHAINS_PATH = $(NDK_PATH)/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/

CC=$(TOOLCHAINS_PATH)/aarch64-linux-android-gcc --sysroot $(NDK_PATH)/platforms/android-21/arch-arm64/

INCLUDE_DIR = -isystem $(NDK_PATH)/platforms/android-21/arch-arm64/usr/include
INCLUDE_DIR += -isystem $(NDK_PATH)/sources/cxx-stl/gnu-libstdc++/4.9/include
INCLUDE_DIR += -isystem $(NDK_PATH)/sources/cxx-stl/gnu-libstdc++/4.9/libs/arm64-v8a/include

LIBS_DIR = -L $(NDK_PATH)/sources/cxx-stl/gnu-libstdc++/4.9/libs/arm64-v8a
CFLAGS = -g -std=c++11 -fPIE -pie -Wl,-allow-shlib-undefined -DHAVE_ANDROID_OS

all:gltest glyuv2rgb glyuv2nv12

gltest:glestest/glestest.cpp
	$(CC) $(INCLUDE_DIR) $(LIBS_DIR) $(CFLAGS)  -g glestest/glestest.cpp -o gltest -lEGL -lGLESv3

glyuv2rgb: yuv2rgb/main.cpp yuv2rgb/GLESConvert.cpp
	$(CC) $(INCLUDE_DIR) $(LIBS_DIR) $(CFLAGS)  -g yuv2rgb/main.cpp yuv2rgb/GLESConvert.cpp -o glyuv2rgb -lEGL -lGLESv3 -lgnustl_static

glyuv2nv12: yuv2nv12/main.cpp yuv2nv12/GLESConvert.cpp
	$(CC) $(INCLUDE_DIR) $(LIBS_DIR) $(CFLAGS)  -g yuv2nv12/main.cpp yuv2nv12/GLESConvert.cpp -o glyuv2nv12 -lEGL -lGLESv3 -lgnustl_static
clean:
	rm gltest glyuv2rgb glyuv2nv12
