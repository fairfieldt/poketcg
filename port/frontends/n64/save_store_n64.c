/*
 * save_store.h for the Nintendo 64: the cartridge's 16 Kbit EEPROM (256 blocks
 * of 8 bytes), through libdragon's EEPROM API (eeprom.h). See README.md
 * ("Saving") for why EEPROM, and save_store_n64.h for the write-back.
 *
 * Layout (blocks):
 *   0                 signature "TCGN64" + layout version
 *   then, per file    a header block: 'F', valid (0/1), size (2 bytes, big
 *                     endian), CRC-32 of the data (4 bytes, big endian),
 *                     followed by the file's data blocks
 * The files are fixed (kFiles below); the duel save (1660 bytes) takes 1 + 208
 * blocks. libdragon's eepromfs would give the same kind of layout, but its
 * eepfs_write always rewrites the whole file (208 blocks, ~3.1 s for the duel
 * save), while the duel save changes in ~25 blocks per turn on average.
 *
 * The EEPROM is mirrored in RAM (`chip`: what the EEPROM holds, `want`: what
 * it should hold). Reads come from `want`. save_store_poll writes the changed
 * data blocks of a file first and its header last, so a write cut short by a
 * power-off leaves data that does not match the header's CRC: the file then
 * reads as missing, never as a mix of two versions.
 */
#include <string.h>

#include <libdragon.h>

#include "../common/save_store.h"
#include "save_store_n64.h"

#define BLK 8
#define NUM_BLOCKS 256

static const struct {
	const char *name;
	uint16_t size;
} kFiles[] = {
	{ SAVE_STORE_DUEL, 1660 }, /* TCG_SAVE_SIZE */
	{ "duelmode", 8 },         /* tcgn64.c: how the saved duel's sides are controlled */
	{ "settings", 16 },        /* tcgn64.c: last title screen choices */
};
#define NUM_FILES ((int)(sizeof(kFiles) / sizeof(kFiles[0])))

static const uint8_t kSignature[BLK] = { 'T', 'C', 'G', 'N', '6', '4', 1, 0 };

static uint8_t chip[NUM_BLOCKS][BLK];
static uint8_t want[NUM_BLOCKS][BLK];
static bool opened;
static uint32_t writes;

static int blocks_of(int size) { return (size + BLK - 1) / BLK; }

static int file_index(const char *name)
{
	for (int i = 0; i < NUM_FILES; i++)
		if (!strcmp(kFiles[i].name, name))
			return i;
	return -1;
}

/* header block of file i */
static int file_base(int i)
{
	int b = 1;
	for (int j = 0; j < i; j++)
		b += 1 + blocks_of(kFiles[j].size);
	return b;
}

static uint32_t crc32(const uint8_t *p, uint32_t n)
{
	uint32_t c = 0xffffffffu;
	while (n--) {
		c ^= *p++;
		for (int k = 0; k < 8; k++)
			c = c & 1 ? (c >> 1) ^ 0xedb88320u : c >> 1;
	}
	return ~c;
}

/* next block to write: the signature, then per file its data blocks, then its header */
static int next_changed(void)
{
	if (memcmp(chip[0], want[0], BLK))
		return 0;
	for (int i = 0; i < NUM_FILES; i++) {
		int base = file_base(i), n = blocks_of(kFiles[i].size);
		for (int b = base + 1; b <= base + n; b++)
			if (memcmp(chip[b], want[b], BLK))
				return b;
		if (memcmp(chip[base], want[base], BLK))
			return base;
	}
	return -1;
}

void save_store_poll(void)
{
	if (!opened)
		return;
	int b = next_changed();
	if (b < 0)
		return;
	eeprom_write((uint8_t)b, want[b]);
	memcpy(chip[b], want[b], BLK);
	writes++;
}

void save_store_flush(void)
{
	while (opened && next_changed() >= 0)
		save_store_poll();
}

int save_store_pending(void)
{
	int n = 0;
	if (opened)
		for (int b = 0; b < NUM_BLOCKS; b++)
			n += memcmp(chip[b], want[b], BLK) != 0;
	return n;
}

uint32_t save_store_blocks_written(void) { return writes; }

bool save_store_open(const char *where)
{
	(void)where;
	if (opened)
		return true;
	/* a 4 Kbit EEPROM (64 blocks) cannot hold the duel save */
	if (eeprom_present() != EEPROM_16K || file_base(NUM_FILES) > NUM_BLOCKS)
		return false;
	for (int b = 0; b < NUM_BLOCKS; b++)
		eeprom_read((uint8_t)b, chip[b]);
	memcpy(want, chip, sizeof(want));
	opened = true;
	if (memcmp(chip[0], kSignature, BLK)) {
		/* blank, or used by another program: start an empty store (signature and
		 * invalid file headers only, the data blocks are left alone) */
		memcpy(want[0], kSignature, BLK);
		for (int i = 0; i < NUM_FILES; i++)
			memset(want[file_base(i)], 0, BLK);
		save_store_flush();
	}
	return true;
}

int save_store_read(const char *name, uint8_t *buf, uint32_t size)
{
	int i = file_index(name);
	if (!opened || i < 0)
		return -1;
	const uint8_t *h = want[file_base(i)];
	uint32_t len = (uint32_t)(h[2] << 8 | h[3]);
	uint32_t crc = (uint32_t)h[4] << 24 | (uint32_t)h[5] << 16 | (uint32_t)h[6] << 8 | h[7];
	const uint8_t *data = want[file_base(i) + 1];
	if (h[0] != 'F' || h[1] != 1 || len > kFiles[i].size || crc32(data, len) != crc)
		return -1;
	if (len > size)
		len = size;
	memcpy(buf, data, len);
	return (int)len;
}

bool save_store_write(const char *name, const uint8_t *data, uint32_t size)
{
	int i = file_index(name);
	if (!opened || i < 0 || size > kFiles[i].size)
		return false;
	int base = file_base(i);
	uint8_t *d = want[base + 1];
	memset(d, 0, (size_t)blocks_of(kFiles[i].size) * BLK);
	memcpy(d, data, size);
	uint32_t crc = crc32(d, size);
	const uint8_t h[BLK] = { 'F', 1, (uint8_t)(size >> 8), (uint8_t)size, (uint8_t)(crc >> 24), (uint8_t)(crc >> 16),
	                         (uint8_t)(crc >> 8), (uint8_t)crc };
	memcpy(want[base], h, BLK);
	return true;
}

bool save_store_remove(const char *name)
{
	int i = file_index(name);
	if (!opened || i < 0)
		return false;
	int base = file_base(i);
	/* only the header is written; queued data that was not written yet is dropped */
	memcpy(want[base + 1], chip[base + 1], (size_t)blocks_of(kFiles[i].size) * BLK);
	if (want[base][1] != 0 || chip[base][1] != 0) {
		memcpy(want[base], chip[base], BLK);
		want[base][0] = 'F';
		want[base][1] = 0;
	}
	return true;
}
