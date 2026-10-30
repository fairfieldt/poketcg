/*
 * Save files of the frontends: small named blobs kept between sessions (the
 * saved duel for "Continue Duel", ...). One implementation per platform, same
 * calls everywhere:
 *
 *   frontends/common/save_store_file.c   desktop: one file per name in a directory
 *   frontends/n64                        libdragon save memory
 *   frontends/web                        localStorage, with export/import of the
 *                                        files through the File System Access API
 *                                        (the browser one is JavaScript: save_store.js)
 */
#ifndef SAVE_STORE_H
#define SAVE_STORE_H

#include <stdbool.h>
#include <stdint.h>

/* the duel's save file: TcgHost.save data (TCG_SAVE_SIZE bytes) */
#define SAVE_STORE_DUEL "duel"

/* where: the desktop store's directory (NULL = default: $TCG_SAVE_DIR, else
 * $XDG_DATA_HOME/poketcg-port, else ~/.local/share/poketcg-port, or
 * %APPDATA%\poketcg-port on Windows). Other platforms ignore it. */
bool save_store_open(const char *where);
/* reads up to size bytes of name into buf: bytes read, or -1 if there is no such file */
int save_store_read(const char *name, uint8_t *buf, uint32_t size);
/* replaces name with data (the old contents stay if the write fails) */
bool save_store_write(const char *name, const uint8_t *data, uint32_t size);
bool save_store_remove(const char *name);

#endif
