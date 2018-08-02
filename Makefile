NDK_PATH=/mnt/d/Android/android-ndk-r13

TOOLCHAINS_PATH = $(NDK_PATH)/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/

CC=$(TOOLCHAINS_PATH)/aarch64-linux-android-gcc --sysroot $(NDK_PATH)/platforms/android-21/arch-arm64/

INCLUDE_DIR = -isystem $(NDK_PATH)/platforms/android-21/arch-arm64/usr/include
INCLUDE_DIR += -isystem $(NDK_PATH)/sources/cxx-stl/gnu-libstdc++/4.9/include
INCLUDE_DIR += -isystem $(NDK_PATH)/sources/cxx-stl/gnu-libstdc++/4.9/libs/arm64-v8a/include

LIBS_DIR = -L $(NDK_PATH)/sources/cxx-stl/gnu-libstdc++/4.9/libs/arm64-v8a
CFLAGS = -fPIE -pie

gltest:glestest.cpp
	$(CC) $(INCLUDE_DIR) $(LIBS_DIR) $(CFLAGS)  -g glestest.cpp -o gltest -lEGL -lGLESv3

clean:
	rm gltest
