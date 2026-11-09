# Simple Makefile for alock

CC = gcc
CFLAGS = -Wall -O2 -I.
LDFLAGS =
LIBS = -lX11 -lXext -lXrender -lpam -lm

# Optional Xrandr support for multi-monitor (set ENABLE_XRANDR=0 to disable)
ENABLE_XRANDR ?= 1

ifeq ($(ENABLE_XRANDR),1)
CFLAGS += -DENABLE_XRANDR=1
LIBS += -lXrandr
else
CFLAGS += -DENABLE_XRANDR=0
endif

# Optional WebKitGTK support (set ENABLE_WEBKIT=1 to enable)
ENABLE_WEBKIT ?= 0

ifeq ($(ENABLE_WEBKIT),1)
export PKG_CONFIG_PATH := ./local/lib/pkgconfig:$(PKG_CONFIG_PATH)
WEBKIT_CFLAGS := $(shell PKG_CONFIG_PATH=./local/lib/pkgconfig:$(PKG_CONFIG_PATH) pkg-config --cflags webkit2gtk-4.0 gtk+-3.0 2>/dev/null)
WEBKIT_LIBS := $(shell PKG_CONFIG_PATH=./local/lib/pkgconfig:$(PKG_CONFIG_PATH) pkg-config --libs webkit2gtk-4.0 gtk+-3.0 2>/dev/null)
# libsoup headers are needed by webkit2gtk headers
WEBKIT_CFLAGS += -I./local/include/libsoup-2.4
# GTK/Glib/Cairo/Pango includes only needed for WebKit
CFLAGS += -I/usr/include/glib-2.0 -I/usr/lib64/glib-2.0/include -I/usr/include/cairo -I/usr/include/pango-1.0 -I/usr/include/harfbuzz -I/usr/include/gdk-pixbuf-2.0
CFLAGS += $(WEBKIT_CFLAGS) -DENABLE_WEBKIT=1
LIBS += $(WEBKIT_LIBS)
WEBKIT_SRC = bg_web.c
else
WEBKIT_SRC =
endif

# Base source files
SRCS = auth_plaintext.c \
       auth_pam.c \
       input_none.c \
       input_dots.c \
       bg_none.c \
       bg_blank.c \
       bg_shade.c \
       bg_image.c \
       bg_term.c \
       $(WEBKIT_SRC) \
       utils.c \
       main.c

OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)

TARGET = alock++

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

%.o: %.c
	@mkdir -p .deps
	@$(CC) $(CFLAGS) -M $< > .deps/$(*F).d
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
	rm -rf .deps

-include $(patsubst %,.deps/%.d,$(notdir $(SRCS:.c=)))

.PHONY: all clean
