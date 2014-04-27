GSTREAMER_SDK_ROOT_ANDROID ?= $(HOME)/cerbero/dist/android_armv7

build: setup
	GSTREAMER_SDK_ROOT_ANDROID=$(GSTREAMER_SDK_ROOT_ANDROID) ndk-build NDK_DEBUG=1
	ant debug

setup: build.xml

build.xml: AndroidManifest.xml
	android update project -p . -s  --target android-16
	touch -c build.xml # project update won't change timestamp if no change

install:
	adb install -r bin/AndroidAurena-debug.apk

clean:
	rm setup.stamp
