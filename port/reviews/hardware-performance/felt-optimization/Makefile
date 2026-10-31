.DEFAULT_GOAL := all
N64_INST := /opt/libdragon
export N64_INST
BUILD_DIR := .
SOURCE_DIR := .
include $(N64_INST)/include/n64.mk
CFLAGS += -I../../../src
all: felt-bench.z64
$(BUILD_DIR)/felt-bench.elf: $(BUILD_DIR)/bench.o
felt-bench.z64: N64_ROM_TITLE = "Felt benchmark"
felt-bench.z64: N64_ROM_SAVETYPE = none
