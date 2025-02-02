/*
 * Copyright (c) 2024 tarball-a-davis
 * Copyright (c) 2017 rxi
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef MTAR_CHUNK_SIZE
#define MTAR_CHUNK_SIZE 4096
#endif

/* 64 bit off_t in stdio.h */
#if !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 64
#elif _FILE_OFFSET_BITS != 64
#error "microtar requires _FILE_OFFSET_BITS=64"
#endif

/* fseeko() in stdio.h */
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#elif _POSIX_C_SOURCE < 200809L
#error "microtar requires _POSIX_C_SOURCE>=200809L"
#endif

#include <stdio.h>

#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>

#include "microtar.h"

/* 077777777777 == ~8GB */
#define MTAR_FILE_SIZE_MAX ((uint64_t)0x1FFFFFFFF)

typedef struct {
    char name[100];
    char mode[8];
    char owner[8];
    char group[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char typeflag;
    char linkname[100];
    char _padding[255];
} mtar_raw_header_t;

static uint64_t round_up(uint64_t n) {
    const int TAR_BLOCK_SIZE = 512;
    return n + ((TAR_BLOCK_SIZE - (n % TAR_BLOCK_SIZE)) % TAR_BLOCK_SIZE);
}

static uint64_t checksum(const mtar_raw_header_t *rh) {
    uint64_t i;
    unsigned char *p = (unsigned char *)rh;
    uint64_t res = 256;

    for (i = 0; i < offsetof(mtar_raw_header_t, checksum); i++) {
        res += p[i];
    }

    for (i = offsetof(mtar_raw_header_t, typeflag); i < sizeof(*rh); i++) {
        res += p[i];
    }

    return res;
}


static int tread(mtar_t *tar, void *data, uint64_t size) {
    int err;

    if (size > MTAR_FILE_SIZE_MAX) {
        /* over max size to fit in tarball */
        return MTAR_EREADFAIL;
    }


    err = tar->read(tar, data, size);
    tar->pos += size;

    return err;
}


static int twrite(mtar_t *tar, const void *data, uint64_t size) {
    int err;

    if (size > MTAR_FILE_SIZE_MAX) {
        /* over max size to fit in tarball */
        return MTAR_EWRITEFAIL;
    }

    err = tar->write(tar, data, size);
    tar->pos += size;

    return err;
}


static int write_null_bytes(mtar_t *tar, int n) {
    int i;
    int err;
    char nul = '\0';

    for (i = 0; i < n; i++) {
        err = twrite(tar, &nul, 1);

        if (err) {
            return err;
        }
    }

    return MTAR_ESUCCESS;
}


static int raw_to_header(mtar_header_t *h, const mtar_raw_header_t *rh) {
    uint64_t chksum1;
    uint64_t chksum2;

    /* If the checksum starts with a null byte we assume the record is NULL */
    if (*rh->checksum == '\0') {
        return MTAR_ENULLRECORD;
    }

    /* Build and compare checksum */
    chksum1 = checksum(rh);
    sscanf(rh->checksum, "%" PRIo64, &chksum2);
    if (chksum1 != chksum2) {
        return MTAR_EBADCHKSUM;
    }

    /* Load raw header into header */
    sscanf(rh->mode, "%" PRIo64, &h->mode);
    sscanf(rh->owner, "%" PRIo64, &h->owner);
    sscanf(rh->size, "%" PRIo64, &h->size);
    sscanf(rh->mtime, "%" PRIo64, &h->mtime);
    h->typeflag = rh->typeflag;
    strncpy(h->name, rh->name, sizeof(h->name));
    strncpy(h->linkname, rh->linkname, sizeof(h->linkname));

    return MTAR_ESUCCESS;
}


static void header_to_raw(mtar_raw_header_t *rh, const mtar_header_t *h) {
    uint64_t chksum;

    /* Load header into raw header */
    memset(rh, 0, sizeof(*rh));
    sprintf(rh->mode, "%" PRIo64, h->mode);
    sprintf(rh->owner, "%" PRIo64, h->owner);
    sprintf(rh->size, "%" PRIo64, h->size);
    sprintf(rh->mtime, "%" PRIo64, h->mtime);
    rh->typeflag = (char)(h->typeflag ? h->typeflag : MTAR_TREG);
    strcpy(rh->name, h->name);
    strcpy(rh->linkname, h->linkname);

    /* Calculate and write checksum */
    chksum = checksum(rh);
    sprintf(rh->checksum, "%06" PRIo64, chksum);
    rh->checksum[7] = ' ';
}


const char *mtar_strerror(int err) {
    switch (err) {
    case MTAR_ESUCCESS:
        return "success";
    case MTAR_EFAILURE:
        return "failure";
    case MTAR_EOPENFAIL:
        return "could not open";
    case MTAR_EREADFAIL:
        return "could not read";
    case MTAR_EWRITEFAIL:
        return "could not write";
    case MTAR_ESEEKFAIL:
        return "could not seek";
    case MTAR_EBADCHKSUM:
        return "bad checksum";
    case MTAR_ENULLRECORD:
        return "null record";
    case MTAR_ENOTFOUND:
        return "file not found";
    }

    return "unknown error";
}


static int file_write(mtar_t *tar, const void *data, uint64_t size) {
    uint64_t res = fwrite(data, 1, size, tar->stream);
    uint64_t w_sz = MTAR_CHUNK_SIZE;

    while (res < size) {
        if (w_sz + res > size) {
            w_sz = size - res;
        }

        res += fwrite(data, 1, w_sz, tar->stream);
        data = (char *)data + w_sz;

        if (ferror(tar->stream)) {
            break;
        }
    }


    return (res == size) ? MTAR_ESUCCESS : MTAR_EWRITEFAIL;
}

static int file_read(mtar_t *tar, void *data, uint64_t size) {
    uint64_t res = 0;
    uint64_t r_sz = MTAR_CHUNK_SIZE;

    while (res < size) {
        if (r_sz + res > size) {
            r_sz = size - res;
        }

        res += fread(data, 1, r_sz, tar->stream);
        data = (char *)data + r_sz;

        if (ferror(tar->stream)) {
            break;
        }
    }

    return (res == size) ? MTAR_ESUCCESS : MTAR_EREADFAIL;
}

static int file_seek(mtar_t *tar, uint64_t offset) {
    int res = fseeko(tar->stream, (int64_t)offset, SEEK_SET);
    return (res == 0) ? MTAR_ESUCCESS : MTAR_ESEEKFAIL;
}

static int file_close(mtar_t *tar) {
    return fclose(tar->stream) == 0 ? MTAR_ESUCCESS : MTAR_EFAILURE;
}


int mtar_open(mtar_t *tar, const char *filename, const char *mode) {
    int err;
    mtar_header_t h;

    /* Init tar struct and functions */
    memset(tar, 0, sizeof(*tar));
    tar->write = file_write;
    tar->read = file_read;
    tar->seek = file_seek;
    tar->close = file_close;

    /* Assure mode is always binary */
    if (strchr(mode, 'r')) {
        mode = "rb";
    }

    if (strchr(mode, 'w')) {
        mode = "wb";
    }

    if (strchr(mode, 'a')) {
        mode = "ab";
    }

    /* Open file */
    tar->stream = fopen(filename, mode);
    if (!tar->stream) {
        return MTAR_EOPENFAIL;
    }

    /* Read first header to check it is valid if mode is `r` */
    if (*mode == 'r') {
        err = mtar_read_header(tar, &h);

        if (err != MTAR_ESUCCESS) {
            mtar_close(tar);
            return err;
        }
    }

    /* Return ok */
    return MTAR_ESUCCESS;
}


int mtar_close(mtar_t *tar) {
    return tar->close(tar);
}


int mtar_seek(mtar_t *tar, uint64_t pos) {
    int err = tar->seek(tar, pos);

    if (!err) {
        tar->pos = pos;
    }

    return err;
}


int mtar_rewind(mtar_t *tar) {
    tar->remaining_data = 0;
    tar->last_header = 0;

    return mtar_seek(tar, 0);
}


int mtar_next(mtar_t *tar) {
    int err;
    uint64_t n;
    mtar_header_t h;

    /* Load header */
    err = mtar_read_header(tar, &h);
    if (err) {
        return err;
    }

    /* Seek to next record */
    n = round_up(h.size) + sizeof(mtar_raw_header_t);

    return mtar_seek(tar, tar->pos + n);
}


int mtar_find(mtar_t *tar, const char *name, mtar_header_t *h) {
    int err;
    mtar_header_t header;

    /* Start at beginning */
    err = mtar_rewind(tar);
    if (err) {
        return err;
    }

    /* Iterate all files until we hit an error or find the file */
    while ((err = mtar_read_header(tar, &header)) == MTAR_ESUCCESS) {
        if (!strcmp(header.name, name)) {
            if (h) {
                *h = header;
            }

            return MTAR_ESUCCESS;
        }

        mtar_next(tar);
    }

    /* Return error */
    if (err == MTAR_ENULLRECORD) {
        err = MTAR_ENOTFOUND;
    }

    return err;
}


int mtar_read_header(mtar_t *tar, mtar_header_t *h) {
    int err;
    mtar_raw_header_t rh;

    /* Save header position */
    tar->last_header = tar->pos;

    /* Read raw header */
    err = tread(tar, &rh, sizeof(rh));
    if (err) {
        return err;
    }

    /* Seek back to start of header */
    err = mtar_seek(tar, tar->last_header);
    if (err) {
        return err;
    }

    /* Load raw header into header struct and return */
    return raw_to_header(h, &rh);
}


int mtar_read_data(mtar_t *tar, void *ptr, uint64_t size) {
    int err;

    /* If we have no remaining data then this is the first read,
     * we get the size, set the remaining data
     * and seek to the beginning of the data */
    if (tar->remaining_data == 0) {
        mtar_header_t h;

        /* Read header */
        err = mtar_read_header(tar, &h);
        if (err) {
            return err;
        }

        /* Seek past header and init remaining data */
        err = mtar_seek(tar, tar->pos + sizeof(mtar_raw_header_t));
        if (err) {
            return err;
        }

        tar->remaining_data = h.size;
    }

    /* Read data */
    err = tread(tar, ptr, size);
    if (err) {
        return err;
    }

    tar->remaining_data -= size;

    /* If there is no remaining data we've finished reading and seek back to the
     * header */
    if (tar->remaining_data == 0) {
        return mtar_seek(tar, tar->last_header);
    }

    return MTAR_ESUCCESS;
}


int mtar_write_header(mtar_t *tar, const mtar_header_t *h) {
    mtar_raw_header_t rh;

    /* Build raw header and write */
    header_to_raw(&rh, h);
    tar->remaining_data = h->size;

    return twrite(tar, &rh, sizeof(rh));
}


int mtar_write_file_header(mtar_t *tar, const char *name, uint64_t size) {
    mtar_header_t h;

    /* Build header */
    memset(&h, 0, sizeof(h));
    strcpy(h.name, name);

    h.size = size;
    h.typeflag = MTAR_TREG;
    h.mode = 0664;

    /* Write header */
    return mtar_write_header(tar, &h);
}


int mtar_write_dir_header(mtar_t *tar, const char *name) {
    mtar_header_t h;

    /* Build header */
    memset(&h, 0, sizeof(h));
    strcpy(h.name, name);
    h.typeflag = MTAR_TDIR;
    h.mode = 0775;

    /* Write header */
    return mtar_write_header(tar, &h);
}


int mtar_write_data(mtar_t *tar, const void *data, uint64_t size) {
    int err;

    /* Write data */
    err = twrite(tar, data, size);
    if (err) {
        return err;
    }

    tar->remaining_data -= size;

    /* Write padding if we've written all the data for this file */
    if (tar->remaining_data == 0) {
        return write_null_bytes(tar, (int)(round_up(tar->pos) - tar->pos));
    }

    return MTAR_ESUCCESS;
}


int mtar_finalize(mtar_t *tar) {
    /* Write two NULL records */
    return write_null_bytes(tar, sizeof(mtar_raw_header_t) * 2);
}
