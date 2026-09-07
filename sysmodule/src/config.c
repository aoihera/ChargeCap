/* SPDX-License-Identifier: MIT
 *
 * Hand-rolled ini reader. A real ini library would be several kilobytes of
 * code and heap for a file that holds two integers, and this sysmodule is
 * optimised for footprint above everything else.
 */

#include <string.h>
#include "config.h"

#define CFG_BUF_SIZE 512

static u32 _parseU32(const char *s) {
    u32 v = 0;
    while (*s == ' ' || *s == '\t')
        s++;
    while (*s >= '0' && *s <= '9') {
        u32 digit = (u32)(*s - '0');
        /* Check for overflow before multiplying */
        if (v > (0xFFFFFFFFU - digit) / 10)
            break;
        v = (v * 10) + digit;
        s++;
        if (v > 100000)
            break;
    }
    return v;
}

/* Returns the value side of `key=` if this line declares it, else NULL. */
static const char *_matchKey(const char *line, const char *key) {
    size_t klen = strlen(key);

    while (*line == ' ' || *line == '\t')
        line++;

    if (strncmp(line, key, klen) != 0)
        return NULL;

    line += klen;
    while (*line == ' ' || *line == '\t')
        line++;

    if (*line != '=')
        return NULL;

    return line + 1;
}

static void _parse(char *buf, ChargeCapConfig *out) {
    char *line = buf;

    while (line && *line) {
        char *next = strchr(line, '\n');
        if (next) {
            *next = '\0';
            next++;
        }

        char *cr = strchr(line, '\r');
        if (cr)
            *cr = '\0';

        if (*line != '#' && *line != ';' && *line != '[') {
            const char *val = _matchKey(line, "enabled");
            if (val) {
                out->enabled = _parseU32(val) ? 1 : 0;
            } else {
                val = _matchKey(line, "limit");
                if (val)
                    out->limit = (u8)_parseU32(val);
            }
        }

        if (!next)
            break;
        line = next;
    }
}

void configSanitize(ChargeCapConfig *cfg) {
    cfg->enabled = cfg->enabled ? 1 : 0;

    if (cfg->limit < CHARGECAP_LIMIT_MIN)
        cfg->limit = CHARGECAP_LIMIT_MIN;
    else if (cfg->limit > CHARGECAP_LIMIT_MAX)
        cfg->limit = CHARGECAP_LIMIT_MAX;

    cfg->reserved[0] = 0;
    cfg->reserved[1] = 0;
}

/* Reads one ini file into *out (only the keys it declares; the rest of *out
 * is left untouched). Returns true if the file could be opened at all. */
static bool _readConfigFile(FsFileSystem *sd, const char *path, ChargeCapConfig *out) {
    FsFile f;
    if (R_FAILED(fsFsOpenFile(sd, path, FsOpenMode_Read, &f)))
        return false;

    char buf[CFG_BUF_SIZE + 1];
    u64  read = 0;

    if (R_SUCCEEDED(fsFileRead(&f, 0, buf, CFG_BUF_SIZE, FsReadOption_None, &read))) {
        if (read > CFG_BUF_SIZE)
            read = CFG_BUF_SIZE;
        buf[read] = '\0';

        /* If the file is longer than the buffer we are holding a
         * truncated tail. Drop the partial last line rather than
         * parsing "limit=8" out of "limit=80". */
        if (read == CFG_BUF_SIZE) {
            char *last = strrchr(buf, '\n');
            if (last)
                last[1] = '\0';
            else
                buf[0] = '\0';
        }

        _parse(buf, out);
    }

    fsFileClose(&f);
    return true;
}

void configLoad(ChargeCapConfig *out) {
    out->enabled     = 0;
    out->limit       = CHARGECAP_LIMIT_DEFAULT;
    out->reserved[0] = 0;
    out->reserved[1] = 0;

    if (R_FAILED(fsInitialize()))
        return;

    FsFileSystem sd;
    if (R_SUCCEEDED(fsOpenSdCardFileSystem(&sd))) {
        /* Ensure the config directory exists */
        (void)fsFsCreateDirectory(&sd, CHARGECAP_CONFIG_DIR);

        if (!_readConfigFile(&sd, CHARGECAP_CONFIG_PATH, out)) {
            /* Upgrade path: the project used to be called charge-limit-NX
             * and stored its config elsewhere. Fall back to the old file so
             * a rename does not silently reset the user's settings. */
            (void)_readConfigFile(&sd, CHARGECAP_LEGACY_CONFIG_PATH, out);
        }
        fsFsClose(&sd);
    }

    fsExit();
    configSanitize(out);
}
