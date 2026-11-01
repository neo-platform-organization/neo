PLATFORM_SOURCES = neo-vm/platform/win32/tls_selftest.c neo-vm/platform/win32/tls.c neo-vm/platform/win32/network.c neo-vm/platform/x86_64/Windows/platform.c neo-vm/platform/win32/streams.c neo-vm/platform/win32/window.c
ifneq ($(WITH_X11),0)
$(error x86_64/Windows uses Win32; WITH_X11 must be 0)
endif
CPPFLAGS += -DNEO_PLATFORM_WITH_WIN32
LDLIBS += -luser32 -lgdi32 -lshell32 -lws2_32 -lsecur32 -lcrypt32 -lncrypt
