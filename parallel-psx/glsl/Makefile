ifeq ($(platform),)
	platform = unix
	ifeq ($(shell uname -s),)
		platform = win
	else ifneq ($(findstring MINGW,$(shell uname -s)),)
		platform = win
	endif
endif

INCFLAGS += -Ideps

ifeq ($(platform), win)
	LDFLAGS += -static
endif

FLAGS += $(INCFLAGS)
LINKOUT = -o
TARGET_NAME = rebuild_shaders

ifeq ($(platform), win)
	TARGET = $(TARGET_NAME).exe
else
	TARGET = $(TARGET_NAME)
endif

SOURCES_C = $(TARGET_NAME).c
CFLAGS = $(FLAGS)

IN_CONF = $(TARGET_NAME).conf
# relative to OUT_DIR
IN_DIR = ..
OUT_DIR = prebuilt
OUT_MAKE = $(OUT_DIR)/Makefile
OUT_HPP = $(OUT_DIR)/renderer_pipelines.hpp

CC = gcc
ifeq ($(CC), gcc)
	CFLAGS += -std=c99 -Wall
	CFLAGS += -Wextra -Wpedantic
	# CFLAGS += -O2
	CFLAGS += -ggdb
endif

all: $(OUT_MAKE) $(OUT_HPP)
	$(MAKE) -C $(OUT_DIR)

$(OUT_MAKE) $(OUT_HPP) &: $(TARGET) $(IN_CONF)
	./$(TARGET) $(IN_CONF) $(OUT_MAKE) $(OUT_HPP) $(IN_DIR)

clean:
	rm -f $(TARGET)

$(TARGET): $(SOURCES_C)
	$(CC) $(LINKOUT) $@ $^ $(CFLAGS) $(LDFLAGS) $(LIBS)

.PHONY: clean all
