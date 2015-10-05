LOCAL_PATH := $(call my-dir)
APP_PLATFORM := android-16

include $(CLEAR_VARS)


ifndef GSTREAMER_ROOT
ifndef GSTREAMER_SDK_ROOT_ANDROID
$(error GSTREAMER_SDK_ROOT_ANDROID is not defined!)
endif
GSTREAMER_ROOT        := $(GSTREAMER_SDK_ROOT_ANDROID)
endif

LOCAL_MODULE    := android-aurena
LOCAL_SRC_FILES := android-aurena.c ../../src/common/aur-component.c \
    ../../src/common/aur-event.c ../../src/common/aur-json.c ../../src/client/aur-client.c
LOCAL_SHARED_LIBRARIES := gstreamer_android
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../src
LOCAL_LDLIBS := -landroid
include $(BUILD_SHARED_LIBRARY)

GSTREAMER_NDK_BUILD_PATH  := $(GSTREAMER_ROOT)/share/gst-android/ndk-build/
include $(GSTREAMER_NDK_BUILD_PATH)/plugins.mk
GSTREAMER_PLUGINS         := $(GSTREAMER_PLUGINS_CORE) $(GSTREAMER_PLUGINS_PLAYBACK) $(GSTREAMER_PLUGINS_CODECS) $(GSTREAMER_PLUGINS_NET) $(GSTREAMER_PLUGINS_SYS)
G_IO_MODULES              := gnutls
GSTREAMER_EXTRA_DEPS      := glib-2.0 json-glib-1.0 libsoup-2.4 gstreamer-net-1.0
include $(GSTREAMER_NDK_BUILD_PATH)/gstreamer-1.0.mk
