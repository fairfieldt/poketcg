N64_INST ?= /opt/libdragon
export N64_INST
BUILD_DIR := build
LIBDRAGON_PREVIEW := 2
include $(N64_INST)/include/n64.mk
ROM ?= rspq-wakeup-fixed
WAKE_FIX ?= 1
ADVENTURE := ../..
QUEUE_OBJECT ?= $(ADVENTURE)/build/rspq-wake-audit/rspq_fixed.o
OBJECTS := $(BUILD_DIR)/main.o
ifeq ($(WAKE_FIX),1)
OBJECTS += $(QUEUE_OBJECT)
endif
.PHONY: all
all: $(ROM).z64
$(ADVENTURE)/build/rspq-wake-audit/rspq_fixed.o:
	$(MAKE) -C $(ADVENTURE) BUILD_DIR=build/rspq-wake-audit build/rspq-wake-audit/rspq_fixed.o
$(BUILD_DIR)/main.o: main.c
	@mkdir -p $(dir $@)
	$(N64_CC) $(N64_CFLAGS) -MMD -c $< -o $@
$(BUILD_DIR)/$(ROM).elf: $(OBJECTS)
$(ROM).z64: N64_ROM_TITLE="RSP Queue Stress"
-include $(wildcard $(BUILD_DIR)/*.d)
