.DEFAULT_GOAL := all
N64_INST ?= /opt/libdragon
export N64_INST
BUILD_DIR := build
ROM ?= lightning
CAPTURE ?= 0
N64_MKDFS_ROOT := $(BUILD_DIR)/filesystem
LIBDRAGON_PREVIEW := 2
include $(N64_INST)/include/n64.mk
CFLAGS += -DLIGHTNING_CAPTURE=$(CAPTURE)

BLENDER ?= /Applications/Blender.app/Contents/MacOS/Blender
PYTHON ?= python3
ASSETS := assets
TEXTURES := $(wildcard $(ASSETS)/textures/*.png)
SPRITES := $(TEXTURES:$(ASSETS)/textures/%.png=$(N64_MKDFS_ROOT)/textures/%.sprite)

.PHONY: all assets test clean
all: $(ROM).z64

# Export is explicit: ordinary ROM builds do not require Blender.
assets:
	"$(BLENDER)" --background --python tools/build_scene.py

.PHONY: export
export:
	"$(BLENDER)" assets/lightning_club.blend --background --python tools/export_scene.py

$(N64_MKDFS_ROOT)/textures/%.sprite: $(ASSETS)/textures/%.png
	@mkdir -p $(dir $@)
	$(N64_MKSPRITE) -f RGBA16 -o $(dir $@) $<

$(N64_MKDFS_ROOT)/scene.l64: $(ASSETS)/scene.l64
	@mkdir -p $(dir $@)
	cp $< $@

$(BUILD_DIR)/$(ROM).dfs: $(SPRITES) $(N64_MKDFS_ROOT)/scene.l64
$(BUILD_DIR)/$(ROM).elf: $(BUILD_DIR)/main.o $(BUILD_DIR)/scene.o $(BUILD_DIR)/world.o
$(ROM).z64: N64_ROM_TITLE="Lightning Club"
$(ROM).z64: $(BUILD_DIR)/$(ROM).dfs

.PHONY: capture
capture:
	$(MAKE) BUILD_DIR=build/capture ROM=lightning-capture CAPTURE=1
	$(PYTHON) tools/capture.py lightning-capture.z64

test:
	$(PYTHON) tools/check_scene.py
	@mkdir -p $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -I. tests/world_test.c world.c -lm -o $(BUILD_DIR)/world_test
	./$(BUILD_DIR)/world_test

clean:
	rm -rf $(BUILD_DIR) lightning.z64 lightning-capture.z64

-include $(wildcard $(BUILD_DIR)/*.d)
