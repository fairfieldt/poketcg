.DEFAULT_GOAL := all
N64_INST ?= /opt/libdragon
export N64_INST
BUILD_DIR := build
N64_MKDFS_ROOT := $(BUILD_DIR)/filesystem
LIBDRAGON_PREVIEW := 2
include $(N64_INST)/include/n64.mk
# Keep the real duel/story/UI in diagnostics so 4 MiB checks include the full
# game's resident code/data rather than a smaller renderer-only executable.
N64_LDFLAGS += --undefined=tcg_run_duel --undefined=ow_new_game --undefined=n64_duel_play
PORT := ../../..
REPO := $(PORT)/..
ROM ?= poketcg-adventure
DIAGNOSTIC ?= 0
HIGH_RES ?= 0
ifneq ($(filter 5 6 7 8 12,$(DIAGNOSTIC)),)
SCENE_T3D ?= 0
else
SCENE_T3D ?= 1
endif
VI_FILTER ?= FILTERS_RESAMPLE
ENGINE_SOURCES := $(wildcard $(PORT)/src/*.c $(PORT)/src/gen/*.c $(PORT)/src/effects/*.c $(PORT)/src/ai/*.c)
ENGINE_OBJECTS := $(ENGINE_SOURCES:$(PORT)/src/%.c=$(BUILD_DIR)/engine/%.o)
GAME_SOURCES := $(wildcard $(PORT)/gameplay/*.c $(PORT)/gameplay/gen/*.c $(PORT)/overworld/*.c $(PORT)/overworld/gen/*.c)
GAME_OBJECTS := $(GAME_SOURCES:$(PORT)/%.c=$(BUILD_DIR)/game/%.o)
FRONT_OBJECTS := $(addprefix $(BUILD_DIR)/,main.o menus.o profiles.o extra_ui.o intro.o naming.o presentation.o dialogue.o audio.o wav64_vadpcm_fixed.o scene.o video.o save_sram.o duel_frontend.o)
ifneq ($(DIAGNOSTIC),0)
FRONT_OBJECTS += $(BUILD_DIR)/mips_story.o $(BUILD_DIR)/cinematic.o
endif
ifneq ($(filter 5 6 7,$(DIAGNOSTIC)),)
FRONT_OBJECTS += $(BUILD_DIR)/visual_sweep.o
endif
ifeq ($(DIAGNOSTIC),11)
FRONT_OBJECTS += $(BUILD_DIR)/leads_preview.o
endif
ifeq ($(DIAGNOSTIC),12)
FRONT_OBJECTS += $(BUILD_DIR)/camera_preview.o
endif
ifeq ($(DIAGNOSTIC),17)
FRONT_OBJECTS += $(BUILD_DIR)/audio_reward.o
endif
ifeq ($(DIAGNOSTIC),8)
FRONT_OBJECTS += $(BUILD_DIR)/face_preview.o
.PHONY: face-preview-assets
face-preview-assets:
	python3 tools/pack_face_preview.py --output $(N64_MKDFS_ROOT)/faces --mksprite $(N64_MKSPRITE)
$(BUILD_DIR)/$(ROM).dfs: face-preview-assets
endif
VIDEO_CONFIG := $(BUILD_DIR)/config-$(DIAGNOSTIC)-$(VI_FILTER)-$(HIGH_RES)-$(SCENE_T3D)
INCLUDES := -I$(PORT)/src -I$(PORT)/overworld -I$(PORT)/gameplay -I$(PORT)/world3d -I$(BUILD_DIR)
CFLAGS += $(INCLUDES) -DADVENTURE_DIAGNOSTIC=$(DIAGNOSTIC) -DADVENTURE_VI_FILTER=$(VI_FILTER) -DADVENTURE_HIGH_RES=$(HIGH_RES)
ifeq ($(SCENE_T3D),1)
T3D_ROOT := $(CURDIR)/build/tiny3d-experiment
T3D_CORE := $(T3D_ROOT)/build/libt3d-poketcg.a
CFLAGS += -DSCENE_T3D=1 -I$(T3D_ROOT)/src
$(T3D_CORE):
	python3 tools/fetch_tiny3d.py
$(BUILD_DIR)/$(ROM).elf: $(T3D_CORE)
endif
CARD_PNGS := $(wildcard $(REPO)/src/gfx/cards/*.png)
CARD_SPRITES := $(CARD_PNGS:$(REPO)/src/gfx/cards/%.png=$(N64_MKDFS_ROOT)/cards/%.sprite)
WORLD_TEXTURES := $(wildcard $(PORT)/world3d/generated/textures/*.png)
WORLD_SPRITES := $(WORLD_TEXTURES:$(PORT)/world3d/generated/textures/%.png=$(N64_MKDFS_ROOT)/world/textures/%.sprite)
WORLD_MESHES := $(wildcard $(PORT)/world3d/generated/maps/*.l64 $(PORT)/world3d/generated/lod/*.l64) $(PORT)/world3d/generated/actors.l64
WORLD_FILES := $(WORLD_MESHES:$(PORT)/world3d/generated/%=$(N64_MKDFS_ROOT)/world/%)
AUDIO_ASSETS := $(wildcard $(PORT)/audio/generated/*.wav64)
AUDIO_FILES := $(AUDIO_ASSETS:$(PORT)/audio/generated/%=$(N64_MKDFS_ROOT)/audio/%)
INTRO_PNGS := $(wildcard assets/intro/*.png)
INTRO_SPRITES := $(INTRO_PNGS:assets/intro/%.png=$(N64_MKDFS_ROOT)/intro/%.sprite)

.PHONY: all bundle clean assets diagnostic video-diagnostic review-diagnostic
all:
	python3 $(PORT)/world3d/tools/sync_characters.py
	$(MAKE) bundle
bundle: $(ROM).z64
expansion:
	python3 $(PORT)/world3d/tools/sync_characters.py
	$(MAKE) BUILD_DIR=build/expansion ROM=poketcg-expansion HIGH_RES=1 bundle
assets:
	python3 $(PORT)/world3d/tools/build_world.py
$(VIDEO_CONFIG):
	@mkdir -p $(BUILD_DIR)
	@rm -f $(BUILD_DIR)/config-*
	@touch $@
$(FRONT_OBJECTS): $(VIDEO_CONFIG)
$(BUILD_DIR)/engine/%.o: $(PORT)/src/%.c
	@mkdir -p $(dir $@)
	$(N64_CC) $(filter-out -Werror,$(N64_CFLAGS)) $(INCLUDES) -DTCG_NO_DEFAULT_FATAL -MMD -c $< -o $@
$(BUILD_DIR)/game/%.o: $(PORT)/%.c
	@mkdir -p $(dir $@)
	$(N64_CC) $(N64_CFLAGS) $(INCLUDES) -MMD -c $< -o $@
$(BUILD_DIR)/duel_frontend.o: ../tcgn64.c $(BUILD_DIR)/cardart_table.h
	$(N64_CC) $(N64_CFLAGS) $(INCLUDES) -DTCGN64_ADVENTURE -DTCGN64_CARD_ART -DADVENTURE_DIAGNOSTIC=$(DIAGNOSTIC) -MMD -c $< -o $@
$(BUILD_DIR)/mips_story.o: tests/mips_story.c
	@mkdir -p $(dir $@)
	$(N64_CC) $(N64_CFLAGS) $(INCLUDES) -MMD -c $< -o $@
$(BUILD_DIR)/cinematic.o: tests/cinematic.c
	@mkdir -p $(dir $@)
	$(N64_CC) $(N64_CFLAGS) $(INCLUDES) -DADVENTURE_DIAGNOSTIC=$(DIAGNOSTIC) -MMD -c $< -o $@
$(BUILD_DIR)/face_preview.o: tests/face_preview.c
	@mkdir -p $(dir $@)
	$(N64_CC) $(N64_CFLAGS) $(INCLUDES) -MMD -c $< -o $@
$(BUILD_DIR)/leads_preview.o: tests/leads_preview.c
	@mkdir -p $(dir $@)
	$(N64_CC) $(N64_CFLAGS) $(INCLUDES) -MMD -c $< -o $@
$(BUILD_DIR)/camera_preview.o: tests/camera_preview.c
	@mkdir -p $(dir $@)
	$(N64_CC) $(N64_CFLAGS) $(INCLUDES) -MMD -c $< -o $@
$(BUILD_DIR)/audio_reward.o: tests/audio_reward.c
	@mkdir -p $(dir $@)
	$(N64_CC) $(N64_CFLAGS) $(INCLUDES) -MMD -c $< -o $@
$(BUILD_DIR)/wav64_vadpcm_fixed.o: vendor/libdragon-audio/wav64_vadpcm.c
	@mkdir -p $(dir $@)
	$(N64_CC) $(N64_CFLAGS) $(INCLUDES) -MMD -c $< -o $@
$(BUILD_DIR)/visual_sweep.o: tests/visual_sweep.c
	@mkdir -p $(dir $@)
	$(N64_CC) $(N64_CFLAGS) $(INCLUDES) -DADVENTURE_DIAGNOSTIC=$(DIAGNOSTIC) -MMD -c $< -o $@
$(BUILD_DIR)/cardart_table.h: $(REPO)/src/gfx.asm ../tools/cardart.awk
	@mkdir -p $(dir $@)
	awk -f ../tools/cardart.awk $< > $@
$(N64_MKDFS_ROOT)/cards/%.sprite: $(REPO)/src/gfx/cards/%.png
	@mkdir -p $(dir $@)
	$(N64_MKSPRITE) -f RGBA16 -o $(dir $@) $<
$(N64_MKDFS_ROOT)/intro/%.sprite: assets/intro/%.png
	@mkdir -p $(dir $@)
	$(N64_MKSPRITE) -f RGBA16 -o $(dir $@) $<
$(N64_MKDFS_ROOT)/world/textures/%.sprite: $(PORT)/world3d/generated/textures/%.png
	@mkdir -p $(dir $@)
	$(N64_MKSPRITE) -f RGBA16 -o $(dir $@) $<
$(N64_MKDFS_ROOT)/world/%.l64: $(PORT)/world3d/generated/%.l64
	@mkdir -p $(dir $@)
	cp $< $@
$(N64_MKDFS_ROOT)/audio/%.wav64: $(PORT)/audio/generated/%.wav64
	@mkdir -p $(dir $@)
	cp $< $@
$(N64_MKDFS_ROOT)/buildinfo.txt:
	@mkdir -p $(dir $@)
	@echo ai=1 > $@
$(BUILD_DIR)/$(ROM).dfs: $(CARD_SPRITES) $(WORLD_SPRITES) $(WORLD_FILES) $(AUDIO_FILES) $(INTRO_SPRITES) $(N64_MKDFS_ROOT)/buildinfo.txt
$(BUILD_DIR)/$(ROM).elf: $(ENGINE_OBJECTS) $(GAME_OBJECTS) $(FRONT_OBJECTS)
$(ROM).z64: N64_ROM_TITLE="Pokemon TCG"
$(ROM).z64: N64_ROM_SAVETYPE=sram256k
$(ROM).z64: $(BUILD_DIR)/$(ROM).dfs
diagnostic:
	$(MAKE) BUILD_DIR=build/diagnostic ROM=poketcg-diagnostic DIAGNOSTIC=1
video-diagnostic:
	$(MAKE) BUILD_DIR=build/video ROM=poketcg-video DIAGNOSTIC=2
visual-diagnostic:
	$(MAKE) BUILD_DIR=build/visual ROM=poketcg-visual DIAGNOSTIC=5
review-diagnostic:
	$(MAKE) BUILD_DIR=build/review ROM=poketcg-review DIAGNOSTIC=3
.PHONY: test validate visual-diagnostic expansion
test:
	@mkdir -p $(BUILD_DIR)/host
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror tests/frame_rate.c -o $(BUILD_DIR)/host/test_frame_rate
	./$(BUILD_DIR)/host/test_frame_rate
	$(MAKE) -C $(PORT) practicecheck
	$(MAKE) -C $(PORT)/gameplay test
	$(MAKE) -C $(PORT)/overworld test
	sh tools/test_profiles.sh
	sh tools/test_extra_ui.sh
	@mkdir -p $(BUILD_DIR)/host
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(INCLUDES) tests/presentation.c presentation.c $(PORT)/overworld/build/liboverworld.a -lm -o $(BUILD_DIR)/host/test_presentation
	./$(BUILD_DIR)/host/test_presentation
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Itests/mock tests/sram_test.c -o $(BUILD_DIR)/host/test_sram
	./$(BUILD_DIR)/host/test_sram
	python3 $(PORT)/world3d/tools/check_world.py
	python3 $(PORT)/world3d/tools/check_characters.py
	python3 $(PORT)/world3d/tools/check_character_lods.py
	sh tools/test_visibility.sh
	python3 $(PORT)/characters/tools/check_skin.py
	python3 $(PORT)/audio/tools/check_audio.py
	python3 tools/build_intro.py --check
HOST_CC ?= cc
validate: diagnostic
	python3 tools/validate.py poketcg-diagnostic.z64 --frames 42
clean:
	rm -rf $(BUILD_DIR) $(ROM).z64 poketcg-diagnostic.z64
-include $(wildcard $(BUILD_DIR)/*.d $(BUILD_DIR)/engine/*.d $(BUILD_DIR)/engine/*/*.d $(BUILD_DIR)/game/*/*.d $(BUILD_DIR)/game/*/*/*.d)
