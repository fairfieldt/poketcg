.DEFAULT_GOAL := all
N64_INST ?= /opt/libdragon
export N64_INST
BUILD_DIR := build
ROM ?= cast
CAPTURE ?= 0
N64_MKDFS_ROOT := $(BUILD_DIR)/filesystem
LIBDRAGON_PREVIEW := 2
include $(N64_INST)/include/n64.mk
CFLAGS += -DCAST_CAPTURE=$(CAPTURE)
TEXTURES := $(wildcard ../assets/textures/*.png)
SPRITES := $(TEXTURES:../assets/textures/%.png=$(N64_MKDFS_ROOT)/textures/%.sprite)
.PHONY: all capture
all: $(ROM).z64
$(N64_MKDFS_ROOT)/textures/%.sprite: ../assets/textures/%.png
	@mkdir -p $(dir $@)
	$(N64_MKSPRITE) -f RGBA16 -o $(dir $@) $<
$(N64_MKDFS_ROOT)/scene.l64: assets/scene.l64
	@mkdir -p $(dir $@)
	cp $< $@
$(BUILD_DIR)/$(ROM).dfs: $(SPRITES) $(N64_MKDFS_ROOT)/scene.l64
$(BUILD_DIR)/$(ROM).elf: $(BUILD_DIR)/main.o $(BUILD_DIR)/scene.o
$(ROM).z64: N64_ROM_TITLE="TCG Character Models"
$(ROM).z64: $(BUILD_DIR)/$(ROM).dfs
capture:
	$(MAKE) BUILD_DIR=build/capture ROM=cast-capture CAPTURE=1
	python3 ../tools/capture_gallery.py cast-capture.z64
-include $(wildcard $(BUILD_DIR)/*.d)
