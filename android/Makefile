GSTREAMER_SDK_ROOT_ANDROID ?= $(HOME)/devel/gstreamer/gstreamer-sdk/android

build:
	GSTREAMER_SDK_ROOT_ANDROID=$(GSTREAMER_SDK_ROOT_ANDROID) ndk-build NDK_DEBUG=1
	ant debug

setup:
	android update project -p . -s  --target android-16

install:
	adb install -r bin/AndroidAurena-debug.apk
