N64_INST ?= /opt/libdragon
export N64_INST
BUILD_DIR := build/transfer-diagnostic
include $(N64_INST)/include/n64.mk
PORT := ../../..
CFLAGS += -I$(PORT)/gameplay -I$(PORT)/src
all: transfer-test.z64
$(BUILD_DIR)/transfer-test.elf: $(BUILD_DIR)/test.o $(BUILD_DIR)/import.o $(BUILD_DIR)/gameplay.o $(BUILD_DIR)/data.o
$(BUILD_DIR)/test.o: tests/transfer_import_test.c
	@mkdir -p $(dir $@)
	$(N64_CC) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/import.o: transfer_import.c
	$(N64_CC) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/gameplay.o: $(PORT)/gameplay/gameplay.c
	$(N64_CC) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/data.o: $(PORT)/gameplay/gen/data.c
	$(N64_CC) $(CFLAGS) -c $< -o $@
transfer-test.z64: N64_ROM_TITLE="Transfer test"
