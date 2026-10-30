/* save_store.h for desktop systems: one file per name (<dir>/<name>.sav). */
#include "save_store.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#define SEP "\\"
#else
#include <sys/stat.h>
#define make_dir(p) mkdir(p, 0755)
#define SEP "/"
#endif

static char dir[1024];

/* mkdir -p */
static bool make_dirs(const char *path)
{
	char tmp[sizeof(dir)];
	snprintf(tmp, sizeof(tmp), "%s", path);
	for (char *p = tmp + 1; *p; p++) {
		if (*p != '/' && *p != '\\')
			continue;
		char c = *p;
		*p = 0;
		if (make_dir(tmp) != 0 && errno != EEXIST)
			return false;
		*p = c;
	}
	return make_dir(tmp) == 0 || errno == EEXIST;
}

bool save_store_open(const char *where)
{
	const char *env;
	if (where)
		snprintf(dir, sizeof(dir), "%s", where);
	else if ((env = getenv("TCG_SAVE_DIR")) && *env)
		snprintf(dir, sizeof(dir), "%s", env);
#ifdef _WIN32
	else if ((env = getenv("APPDATA")) && *env)
		snprintf(dir, sizeof(dir), "%s\\poketcg-port", env);
#else
	else if ((env = getenv("XDG_DATA_HOME")) && *env)
		snprintf(dir, sizeof(dir), "%s/poketcg-port", env);
	else if ((env = getenv("HOME")) && *env)
		snprintf(dir, sizeof(dir), "%s/.local/share/poketcg-port", env);
#endif
	else
		snprintf(dir, sizeof(dir), "saves");
	return make_dirs(dir);
}

static void file_path(char *out, size_t n, const char *name, const char *ext)
{
	snprintf(out, n, "%s" SEP "%s%s", dir, name, ext);
}

int save_store_read(const char *name, uint8_t *buf, uint32_t size)
{
	char path[sizeof(dir) + 64];
	file_path(path, sizeof(path), name, ".sav");
	FILE *f = fopen(path, "rb");
	if (!f)
		return -1;
	size_t n = fread(buf, 1, size, f);
	fclose(f);
	return (int)n;
}

bool save_store_write(const char *name, const uint8_t *data, uint32_t size)
{
	/* write a temporary file, then put it in place */
	char path[sizeof(dir) + 64], tmp[sizeof(dir) + 64];
	file_path(path, sizeof(path), name, ".sav");
	file_path(tmp, sizeof(tmp), name, ".tmp");
	FILE *f = fopen(tmp, "wb");
	if (!f)
		return false;
	bool ok = fwrite(data, 1, size, f) == size;
	ok = fclose(f) == 0 && ok;
	if (!ok) {
		remove(tmp);
		return false;
	}
#ifdef _WIN32
	remove(path); /* rename doesn't replace on Windows */
#endif
	return rename(tmp, path) == 0;
}

bool save_store_remove(const char *name)
{
	char path[sizeof(dir) + 64];
	file_path(path, sizeof(path), name, ".sav");
	return remove(path) == 0 || errno == ENOENT;
}
