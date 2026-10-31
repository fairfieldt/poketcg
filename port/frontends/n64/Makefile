# Nintendo 64 frontend (libdragon) for the portable Pokemon TCG duel engine.
#
#   make             build adventure/poketcg-adventure.z64
#   make duel        build tcgn64.z64
#   make hosttest    build tcgn64.c against mock/libdragon.h on the host and run
#                    the scripted UI tests in tests/*.txt (logs and screenshots
#                    in build/host/)
#   make felttest    compare the cached felt pixels with the scalar reference
#   make boardtest   compare board caching and selection against fresh drawing
#   make rastertest  compare scaled glyphs, pictures and backs pixel for pixel
#   make inputtest   verify VI-rate taps and frontend one-press consumption
#   make endiancheck run tools/endian_check.c (36 deterministic duels, digest of
#                    every event/request/answer and of the final WRAM/HRAM)
#                    natively and as a big-endian MIPS32 Linux binary under
#                    qemu-mips (QEMU_MIPS), and compare the outputs
#   make clean
#
# Needs N64_INST pointing at a libdragon toolchain with libdragon and its tools
# installed (n64.mk, mksprite, mkdfs, n64tool), e.g.
#   N64_INST=/home/user/tools/n64/opt/libdragon make
#
# Engine sources
#   port/src/*.c and port/src/gen/*.c are always built (-DTCG_NO_DEFAULT_FATAL:
#   tcgn64.c provides tcg_fatal, which shows the message and halts).
#   port/src/effects/*.c and port/src/ai/*.c are being ported concurrently and
#   are optional: each one is compiled on its own and excluded if it does not
#   compile; then tools/select_optional.sh trial-links the ROM and drops the
#   objects the linker blames (undefined references to helpers not ported yet,
#   duplicate definitions) until it links. The weak stubs (gen/effect_stubs.c,
#   ai_stubs.c) stand in for whatever is excluded. The outcome is printed as
#   "[OPT]" lines, detailed in build/optional.log and shown on the title screen.
#
# Saves: save_store_n64.c keeps the duel save on the cartridge's 16 Kbit EEPROM
# (N64_ROM_SAVETYPE = eeprom16k), see README.md.
#
# Card art: src/gfx/cards/*.png of the disassembly are converted with mksprite
# (RGBA16) into the ROM filesystem; tools/cardart.awk maps the cards' gfx
# pointers to file names. Without the disassembly the ROM builds without art.

.DEFAULT_GOAL := all
N64_INST ?= /opt/libdragon
export N64_INST
ifeq ($(wildcard $(N64_INST)/include/n64.mk),)
$(error N64_INST ($(N64_INST)) does not contain include/n64.mk: install libdragon first)
endif

BUILD_DIR := build
N64_MKDFS_ROOT := $(BUILD_DIR)/filesystem
include $(N64_INST)/include/n64.mk

PORT := ../..
ENGINE := $(PORT)/src
REPO := $(PORT)/..
CARD_GFX := $(REPO)/src/gfx/cards
GFX_ASM := $(REPO)/src/gfx.asm
ROM := tcgn64.z64

all: adventure
adventure:
	$(MAKE) -C adventure N64_INST=$(N64_INST)
duel: $(ROM)
.PHONY: all adventure duel clean hosttest felttest cointest endiancheck FORCE
.DELETE_ON_ERROR:

# ---------------------------------------------------------------- engine

CORE_SRCS := $(wildcard $(ENGINE)/*.c) $(wildcard $(ENGINE)/gen/*.c)
OPT_SRCS := $(wildcard $(ENGINE)/effects/*.c) $(wildcard $(ENGINE)/ai/*.c)
CORE_OBJS := $(CORE_SRCS:$(ENGINE)/%.c=$(BUILD_DIR)/engine/%.o)
OPT_STAMPS := $(OPT_SRCS:$(ENGINE)/%.c=$(BUILD_DIR)/engine/%.ok)

# the engine is C11 (like port/Makefile); warnings are not errors there
ENGINE_CFLAGS = $(filter-out -Werror -std=%,$(N64_CFLAGS)) -std=c11 -Wextra -Wno-unused-parameter \
	-DTCG_NO_DEFAULT_FATAL -I$(ENGINE)

$(BUILD_DIR)/engine/%.o: $(ENGINE)/%.c
	@mkdir -p $(dir $@)
	@echo "    [CC] engine/$*.c"
	$(N64_CC) -c $(ENGINE_CFLAGS) -MMD -o $@ $<

# optional engine sources: a compile error only excludes the file
$(BUILD_DIR)/engine/%.ok: $(ENGINE)/%.c
	@mkdir -p $(dir $@)
	@if $(N64_CC) -c $(ENGINE_CFLAGS) -MMD -MT $@ -MF $(@:.ok=.d) -o $(@:.ok=.o) $< 2> $(@:.ok=.err); then \
		echo "    [CC] engine/$*.c"; cat $(@:.ok=.err); echo ok > $@; \
	else \
		echo "    [CC] engine/$*.c: does not compile, excluded (see $(@:.ok=.err))"; \
		rm -f $(@:.ok=.o); echo fail > $@; \
	fi

# ---------------------------------------------------------------- card art

CARD_PNGS := $(wildcard $(CARD_GFX)/*.png)
ifneq ($(CARD_PNGS),)
ifneq ($(wildcard $(GFX_ASM)),)
CARD_ART := 1
endif
endif
ifeq ($(CARD_ART),1)
CARD_SPRITES := $(CARD_PNGS:$(CARD_GFX)/%.png=$(N64_MKDFS_ROOT)/cards/%.sprite)
ART_HDR := $(BUILD_DIR)/cardart_table.h
ART_DEF := -DTCGN64_CARD_ART
endif

$(N64_MKDFS_ROOT)/cards/%.sprite: $(CARD_GFX)/%.png
	@mkdir -p $(dir $@)
	@echo "    [SPRITE] $@"
	$(N64_MKSPRITE) -f RGBA16 -o $(dir $@) "$<" > /dev/null

$(BUILD_DIR)/cardart_table.h: $(GFX_ASM) tools/cardart.awk
	@mkdir -p $(dir $@)
	awk -f tools/cardart.awk $(GFX_ASM) > $@.tmp && mv $@.tmp $@

# ---------------------------------------------------------------- frontend and ROM

FRONT_CFLAGS = $(N64_CFLAGS) -I$(ENGINE) -I$(BUILD_DIR) $(ART_DEF)

$(BUILD_DIR)/tcgn64.o: tcgn64.c $(ART_HDR)
	@mkdir -p $(dir $@)
	@echo "    [CC] $<"
	$(N64_CC) -c $(FRONT_CFLAGS) -MMD -o $@ $<

$(BUILD_DIR)/save_store_n64.o: save_store_n64.c
	@mkdir -p $(dir $@)
	@echo "    [CC] $<"
	$(N64_CC) -c $(FRONT_CFLAGS) -MMD -o $@ $<

FRONT_OBJS := $(BUILD_DIR)/tcgn64.o $(BUILD_DIR)/save_store_n64.o

# rerun the selection when the set of optional sources changes
$(BUILD_DIR)/optional.set: FORCE
	@mkdir -p $(dir $@)
	@echo "$(OPT_SRCS)" | cmp -s - $@ || echo "$(OPT_SRCS)" > $@

OPT_LINK_PRE = $(N64_CXX) -o $(BUILD_DIR)/trial.elf $(CORE_OBJS) $(FRONT_OBJS)
OPT_LINK_POST = -lc -mabi=o64 $(patsubst %,-Wl$(COMMA)%,$(N64_LDFLAGS))

$(BUILD_DIR)/optional.o: $(OPT_STAMPS) $(CORE_OBJS) $(FRONT_OBJS) $(BUILD_DIR)/optional.set tools/select_optional.sh
	NM=$(N64_NM) sh tools/select_optional.sh $(BUILD_DIR)/optional $(OPT_STAMPS) -- $(OPT_LINK_PRE) -- $(OPT_LINK_POST)
	@rm -f $(BUILD_DIR)/trial.elf
	@if [ -s $(BUILD_DIR)/optional.lst ]; then \
		$(N64_LD) -r -o $@ $$(cat $(BUILD_DIR)/optional.lst); \
	else \
		$(N64_CC) $(N64_CFLAGS) -x c -c -o $@ - < /dev/null; \
	fi

$(N64_MKDFS_ROOT)/buildinfo.txt: $(BUILD_DIR)/optional.o
	@mkdir -p $(dir $@)
	cp $(BUILD_DIR)/optional.info $@

$(BUILD_DIR)/tcgn64.dfs: $(CARD_SPRITES) $(N64_MKDFS_ROOT)/buildinfo.txt
$(BUILD_DIR)/tcgn64.elf: $(CORE_OBJS) $(FRONT_OBJS) $(BUILD_DIR)/optional.o

$(ROM): N64_ROM_TITLE = "Pokemon TCG Duel"
# the saved duel (save_store_n64.c): 16 Kbit EEPROM, declared in the ROM header
$(ROM): N64_ROM_SAVETYPE = eeprom16k
$(ROM): $(BUILD_DIR)/tcgn64.dfs

# ---------------------------------------------------------------- host build with the libdragon mock

HOST_CC ?= cc
HOST_BUILD := $(BUILD_DIR)/host
HOST_ENGINE_CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Wno-unused-parameter -DTCG_NO_DEFAULT_FATAL -I$(ENGINE)
HOST_FRONT_CFLAGS := -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -Werror -Imock -I$(ENGINE) -I$(BUILD_DIR) $(ART_DEF) \
	-DTCGN64_HOST_TEST
HOST_CORE_OBJS := $(CORE_SRCS:$(ENGINE)/%.c=$(HOST_BUILD)/engine/%.o)
HOST_OPT_STAMPS := $(OPT_SRCS:$(ENGINE)/%.c=$(HOST_BUILD)/engine/%.ok)
MOCK_FONT := $(N64_INST)/mips64-elf/include/font.h
HOST_ROMFS := $(HOST_BUILD)/filesystem
HOST_TESTS := $(wildcard tests/*.txt)

$(HOST_BUILD)/tcgn64.o $(HOST_BUILD)/felt_cache $(HOST_BUILD)/raster_paths \
$(HOST_BUILD)/board_cache.o $(HOST_BUILD)/retained_ui.o $(HOST_BUILD)/duel_ack_flow.o \
$(HOST_BUILD)/check_selector.o $(HOST_BUILD)/input_frontend $(HOST_BUILD)/coin_animation.o: ../control_text.h

# Compare the actual felt renderer at both display resolutions without linking
# the rest of the engine; unreachable frontend sections are discarded.
HOST_DEAD_STRIP := $(if $(filter Darwin,$(shell uname -s)),-Wl$(COMMA)-dead_strip,-Wl$(COMMA)--gc-sections)
$(HOST_BUILD)/felt_cache: tests/felt_cache.c tcgn64.c mock/libdragon.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter -Werror \
		-ffunction-sections -fdata-sections -Imock -I$(ENGINE) $< $(HOST_DEAD_STRIP) -o $@
felttest: $(HOST_BUILD)/felt_cache
	./$(HOST_BUILD)/felt_cache

$(HOST_BUILD)/raster_paths: tests/raster_paths.c tcgn64.c mock/libdragon.h $(ART_HDR)
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter -Werror \
		-ffunction-sections -fdata-sections -Imock -I$(ENGINE) -I$(BUILD_DIR) $< $(HOST_DEAD_STRIP) -o $@
rastertest: $(HOST_BUILD)/raster_paths
	./$(HOST_BUILD)/raster_paths

$(HOST_BUILD)/board_cache.o: tests/board_cache.c tcgn64.c mock/libdragon.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -c $(HOST_FRONT_CFLAGS) -UTCGN64_CARD_ART -ffunction-sections -fdata-sections -o $@ $<
$(HOST_BUILD)/board_cache: $(HOST_BUILD)/board_cache.o $(HOST_CORE_OBJS) $(HOST_BUILD)/save_store_n64.o $(HOST_BUILD)/mock_libdragon.o $(HOST_BUILD)/optional.lst
	$(HOST_CC) -o $@ $(HOST_BUILD)/board_cache.o $(HOST_CORE_OBJS) $(HOST_BUILD)/save_store_n64.o $(HOST_BUILD)/mock_libdragon.o $$(cat $(HOST_BUILD)/optional.lst) $(HOST_DEAD_STRIP)
boardtest: $(HOST_BUILD)/board_cache
	./$(HOST_BUILD)/board_cache
.PHONY: boardtest rastertest

$(HOST_BUILD)/duel_board_focus: tests/duel_board_focus.c duel_board_focus.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror $< -o $@
focustest: $(HOST_BUILD)/duel_board_focus
	./$(HOST_BUILD)/duel_board_focus
.PHONY: focustest

$(HOST_BUILD)/retained_ui.o: tests/retained_ui.c tcgn64.c mock/libdragon.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -c $(HOST_FRONT_CFLAGS) -UTCGN64_CARD_ART -ffunction-sections -fdata-sections -o $@ $<
$(HOST_BUILD)/retained_ui: $(HOST_BUILD)/retained_ui.o $(HOST_CORE_OBJS) $(HOST_BUILD)/save_store_n64.o $(HOST_BUILD)/mock_libdragon.o $(HOST_BUILD)/optional.lst
	$(HOST_CC) -o $@ $(HOST_BUILD)/retained_ui.o $(HOST_CORE_OBJS) $(HOST_BUILD)/save_store_n64.o $(HOST_BUILD)/mock_libdragon.o $$(cat $(HOST_BUILD)/optional.lst) $(HOST_DEAD_STRIP)
retainedtest: $(HOST_BUILD)/retained_ui
	./$(HOST_BUILD)/retained_ui
.PHONY: retainedtest

$(HOST_BUILD)/duel_ack_flow.o: tests/duel_ack_flow.c tcgn64.c duel_input.h mock/libdragon.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -c $(HOST_FRONT_CFLAGS) -UTCGN64_CARD_ART -ffunction-sections -fdata-sections -o $@ $<
$(HOST_BUILD)/duel_ack_flow: $(HOST_BUILD)/duel_ack_flow.o $(HOST_CORE_OBJS) $(HOST_BUILD)/save_store_n64.o $(HOST_BUILD)/mock_libdragon.o $(HOST_BUILD)/optional.lst
	$(HOST_CC) -o $@ $(HOST_BUILD)/duel_ack_flow.o $(HOST_CORE_OBJS) $(HOST_BUILD)/save_store_n64.o $(HOST_BUILD)/mock_libdragon.o $$(cat $(HOST_BUILD)/optional.lst) $(HOST_DEAD_STRIP)
acktest: $(HOST_BUILD)/duel_ack_flow
	@mkdir -p $(HOST_BUILD)/shots/ack-flow-320 $(HOST_BUILD)/shots/ack-flow-480
	MOCK_SCRIPT=tests/duel_ack_flow.script MOCK_SHOTS=$(HOST_BUILD)/shots/ack-flow-320 ./$(HOST_BUILD)/duel_ack_flow
	MOCK_SCRIPT=tests/duel_ack_flow.script MOCK_SHOTS=$(HOST_BUILD)/shots/ack-flow-480 MOCK_HIGH_RES=1 ./$(HOST_BUILD)/duel_ack_flow
.PHONY: acktest

$(HOST_BUILD)/check_selector.o: tests/check_selector.c tcgn64.c duel_input.h mock/libdragon.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -c $(HOST_FRONT_CFLAGS) -UTCGN64_CARD_ART -ffunction-sections -fdata-sections -o $@ $<
$(HOST_BUILD)/check_selector: $(HOST_BUILD)/check_selector.o $(HOST_CORE_OBJS) $(HOST_BUILD)/save_store_n64.o $(HOST_BUILD)/mock_libdragon.o $(HOST_BUILD)/optional.lst
	$(HOST_CC) -o $@ $(HOST_BUILD)/check_selector.o $(HOST_CORE_OBJS) $(HOST_BUILD)/save_store_n64.o $(HOST_BUILD)/mock_libdragon.o $$(cat $(HOST_BUILD)/optional.lst) $(HOST_DEAD_STRIP)
checktest: $(HOST_BUILD)/check_selector
	@mkdir -p $(HOST_BUILD)/shots/check-selector-320 $(HOST_BUILD)/shots/check-selector-480
	MOCK_SCRIPT=tests/check_selector.script MOCK_SHOTS=$(HOST_BUILD)/shots/check-selector-320 ./$(HOST_BUILD)/check_selector
	MOCK_SCRIPT=tests/check_selector.script MOCK_SHOTS=$(HOST_BUILD)/shots/check-selector-480 MOCK_HIGH_RES=1 ./$(HOST_BUILD)/check_selector
.PHONY: checktest

$(HOST_BUILD)/power_flow.o: tests/power_flow.c tcgn64.c duel_input.h mock/libdragon.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -c $(HOST_FRONT_CFLAGS) -UTCGN64_CARD_ART -ffunction-sections -fdata-sections -o $@ $<
$(HOST_BUILD)/power_flow: $(HOST_BUILD)/power_flow.o $(HOST_CORE_OBJS) $(HOST_BUILD)/save_store_n64.o $(HOST_BUILD)/mock_libdragon.o $(HOST_BUILD)/optional.lst
	$(HOST_CC) -o $@ $(HOST_BUILD)/power_flow.o $(HOST_CORE_OBJS) $(HOST_BUILD)/save_store_n64.o $(HOST_BUILD)/mock_libdragon.o $$(cat $(HOST_BUILD)/optional.lst) $(HOST_DEAD_STRIP)
powertest: $(HOST_BUILD)/power_flow
	@mkdir -p $(HOST_BUILD)/shots/power-flow-320 $(HOST_BUILD)/shots/power-flow-480
	MOCK_SCRIPT=tests/power_flow.script MOCK_SHOTS=$(HOST_BUILD)/shots/power-flow-320 ./$(HOST_BUILD)/power_flow
	MOCK_SCRIPT=tests/power_flow.script MOCK_SHOTS=$(HOST_BUILD)/shots/power-flow-480 MOCK_HIGH_RES=1 ./$(HOST_BUILD)/power_flow
.PHONY: powertest

$(HOST_BUILD)/input_capture: tests/input_capture.c duel_input.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror $< -o $@
$(HOST_BUILD)/input_frontend: tests/input_frontend.c tcgn64.c duel_input.h mock/libdragon.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter -Werror \
		-ffunction-sections -fdata-sections -Imock -I$(ENGINE) $< $(HOST_DEAD_STRIP) -lm -o $@
.PHONY: inputtest
inputtest: $(HOST_BUILD)/input_capture $(HOST_BUILD)/input_frontend
	./$(HOST_BUILD)/input_capture
	./$(HOST_BUILD)/input_frontend

$(HOST_BUILD)/coin_animation.o: tests/coin_animation.c tcgn64.c coin_art.h mock/libdragon.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -c $(HOST_FRONT_CFLAGS) -UTCGN64_CARD_ART -ffunction-sections -fdata-sections -o $@ $<
$(HOST_BUILD)/coin_animation: $(HOST_BUILD)/coin_animation.o $(HOST_CORE_OBJS) $(HOST_BUILD)/save_store_n64.o $(HOST_BUILD)/mock_libdragon.o $(HOST_BUILD)/optional.lst
	$(HOST_CC) -o $@ $(HOST_BUILD)/coin_animation.o $(HOST_CORE_OBJS) $(HOST_BUILD)/save_store_n64.o $(HOST_BUILD)/mock_libdragon.o $$(cat $(HOST_BUILD)/optional.lst) $(HOST_DEAD_STRIP)
cointest: $(HOST_BUILD)/coin_animation
	@printf 'wait 100000\n' > $(HOST_BUILD)/coin-idle.script
	MOCK_SCRIPT=$(HOST_BUILD)/coin-idle.script ./$(HOST_BUILD)/coin_animation
	MOCK_SCRIPT=$(HOST_BUILD)/coin-idle.script MOCK_HIGH_RES=1 ./$(HOST_BUILD)/coin_animation

$(HOST_BUILD)/engine/%.o: $(ENGINE)/%.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -c $(HOST_ENGINE_CFLAGS) -MMD -o $@ $<

$(HOST_BUILD)/engine/%.ok: $(ENGINE)/%.c
	@mkdir -p $(dir $@)
	@if $(HOST_CC) -c $(HOST_ENGINE_CFLAGS) -MMD -MT $@ -MF $(@:.ok=.d) -o $(@:.ok=.o) $< 2> $(@:.ok=.err); then \
		echo ok > $@; else rm -f $(@:.ok=.o); echo fail > $@; fi

$(HOST_BUILD)/tcgn64.o: tcgn64.c mock/libdragon.h $(ART_HDR)
	@mkdir -p $(dir $@)
	$(HOST_CC) -c $(HOST_FRONT_CFLAGS) -MMD -o $@ $<

$(HOST_BUILD)/save_store_n64.o: save_store_n64.c save_store_n64.h mock/libdragon.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -c $(HOST_FRONT_CFLAGS) -MMD -o $@ $<

$(HOST_BUILD)/mock_libdragon.o: mock/mock_libdragon.c mock/libdragon.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -c $(HOST_FRONT_CFLAGS) $(if $(wildcard $(MOCK_FONT)),-DMOCK_FONT_H='"$(MOCK_FONT)"') -o $@ $<

$(HOST_BUILD)/optional.set: FORCE
	@mkdir -p $(dir $@)
	@echo "$(OPT_SRCS)" | cmp -s - $@ || echo "$(OPT_SRCS)" > $@

HOST_FRONT_OBJS := $(HOST_BUILD)/tcgn64.o $(HOST_BUILD)/save_store_n64.o $(HOST_BUILD)/mock_libdragon.o

$(HOST_BUILD)/optional.lst: $(HOST_OPT_STAMPS) $(HOST_CORE_OBJS) $(HOST_FRONT_OBJS) $(HOST_BUILD)/optional.set tools/select_optional.sh
	sh tools/select_optional.sh $(HOST_BUILD)/optional $(HOST_OPT_STAMPS) -- \
		$(HOST_CC) -o $(HOST_BUILD)/trial $(HOST_CORE_OBJS) $(HOST_FRONT_OBJS) --
	@rm -f $(HOST_BUILD)/trial

$(HOST_BUILD)/tcgn64_host: $(HOST_BUILD)/optional.lst $(HOST_CORE_OBJS) $(HOST_FRONT_OBJS)
	$(HOST_CC) -o $@ $(HOST_CORE_OBJS) $(HOST_FRONT_OBJS) $$(cat $(HOST_BUILD)/optional.lst)

$(HOST_ROMFS)/buildinfo.txt: $(HOST_BUILD)/optional.lst $(CARD_SPRITES)
	@mkdir -p $(dir $@)
	cp $(HOST_BUILD)/optional.info $@
	@if [ -n "$(CARD_SPRITES)" ]; then rm -rf $(HOST_ROMFS)/cards; ln -s ../../filesystem/cards $(HOST_ROMFS)/cards; fi

hosttest: felttest inputtest cointest boardtest rastertest focustest retainedtest acktest checktest powertest $(HOST_BUILD)/tcgn64_host $(HOST_ROMFS)/buildinfo.txt
	@fail=0; for t in $(HOST_TESTS); do \
		n=$$(basename $$t .txt); mkdir -p $(HOST_BUILD)/shots/$$n; \
		printf '    [TEST] %-24s' $$n; \
		rm -f $(HOST_BUILD)/$$n.eep; \
		env MOCK_SCRIPT=$$t MOCK_ROMFS=$(HOST_ROMFS) MOCK_SHOTS=$(HOST_BUILD)/shots/$$n MOCK_EEPROM=$(HOST_BUILD)/$$n.eep \
			$$(sed -n 's/^#env //p' $$t) ./$(HOST_BUILD)/tcgn64_host > $(HOST_BUILD)/$$n.log 2>&1; \
		rc=$$?; \
		if [ $$rc = 0 ]; then echo "ok   ($$(grep -c '^TURN' $(HOST_BUILD)/$$n.log) turns, $$(grep -c '^REQ' $(HOST_BUILD)/$$n.log) requests, $$(tail -n 1 $(HOST_BUILD)/$$n.log))"; \
		else echo "FAIL (exit $$rc, see $(HOST_BUILD)/$$n.log)"; tail -n 3 $(HOST_BUILD)/$$n.log; fail=1; fi; \
	done; exit $$fail

# ---------------------------------------------------------------- byte order check (big-endian MIPS under qemu)

QEMU_MIPS ?= $(firstword $(wildcard /home/user/tools/n64/qemu/usr/bin/qemu-mips-static) \
	$(shell command -v qemu-mips-static 2> /dev/null) $(shell command -v qemu-mips 2> /dev/null))
EC_DIR := $(BUILD_DIR)/endian
EC_MIPS_FLAGS := -std=c11 -O2 -EB -mabi=32 -march=mips32r2 -G0 -mno-abicalls -fno-pic -ffreestanding \
	-fno-builtin -fno-tree-loop-distribute-patterns -fno-stack-protector -nostdlib -static \
	-DENDIAN_CHECK_FREESTANDING -DTCG_NO_DEFAULT_FATAL -I$(ENGINE) -Wl,-T,tools/endian_check.ld

endiancheck: $(HOST_BUILD)/optional.lst tools/endian_check.c tools/endian_check.ld
	@test -n "$(QEMU_MIPS)" || { echo "endiancheck needs qemu-mips (set QEMU_MIPS)"; exit 1; }
	@mkdir -p $(EC_DIR)
	@srcs="$(CORE_SRCS) $$(sed 's|^$(HOST_BUILD)/engine/|$(ENGINE)/|; s|\.o$$|.c|' $(HOST_BUILD)/optional.lst)"; \
	set -e; \
	echo "    [CC] $(EC_DIR)/endian_check_host"; \
	$(HOST_CC) -std=c11 -O2 -DTCG_NO_DEFAULT_FATAL -I$(ENGINE) -o $(EC_DIR)/endian_check_host tools/endian_check.c $$srcs; \
	echo "    [CC] $(EC_DIR)/endian_check_mipsbe"; \
	$(N64_CC) $(EC_MIPS_FLAGS) -o $(EC_DIR)/endian_check_mipsbe tools/endian_check.c $$srcs -lgcc; \
	./$(EC_DIR)/endian_check_host > $(EC_DIR)/host.txt; \
	$(QEMU_MIPS) $(EC_DIR)/endian_check_mipsbe > $(EC_DIR)/mipsbe.txt; \
	if cmp -s $(EC_DIR)/host.txt $(EC_DIR)/mipsbe.txt; then \
		echo "    [ENDIAN] identical on $$(uname -m) and big-endian MIPS32: $$(tail -n 1 $(EC_DIR)/host.txt)"; \
	else \
		echo "    [ENDIAN] DIFFERENT, see: diff $(EC_DIR)/host.txt $(EC_DIR)/mipsbe.txt"; exit 1; \
	fi

clean:
	rm -rf $(BUILD_DIR) $(ROM)
	$(MAKE) -C adventure clean

-include $(wildcard $(BUILD_DIR)/*.d $(BUILD_DIR)/engine/*.d $(BUILD_DIR)/engine/*/*.d)
-include $(wildcard $(HOST_BUILD)/*.d $(HOST_BUILD)/engine/*.d $(HOST_BUILD)/engine/*/*.d)
