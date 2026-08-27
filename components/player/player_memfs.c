/*
 * SPDX-FileCopyrightText: 2026 Lero Voice contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file player_memfs.c
 * @brief Minimal in-RAM virtual file system for WAV playback without SD.
 *
 * Registers an esp_vfs mount at "/mem" exposing a single read-only file
 * "rec.wav". esp_audio_simple_player's file IO then opens
 * "file://mem/rec.wav" through the standard VFS layer, so audio recorded
 * into a PSRAM buffer can be played back with no SD card at all
 * (docs/PLAN.md 6.2 / console "rec" / "play-mem").
 *
 * MISRA notes: single file, single reader (player pipeline task); no heap
 * allocation on our side; all offsets bounded by s_size.
 */

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "esp_vfs.h"
#include "esp_err.h"
#include "esp_log.h"
#include "player_memfs.h"

#define TAG "player_memfs"

#define MEMFS_MOUNT       "/mem"
#define MEMFS_PATH        "/mem/rec.wav"
#define MEMFS_LOCAL_FD    0   /* VFS 层分配的本地 fd（单文件，恒为 0） */

static const uint8_t *s_data;   /* WAV 数据指针（由调用方持有生命周期） */
static size_t s_size;           /* 有效字节数 */
static off_t s_pos;             /* 当前读位置 */

static bool s_is_rec_file(const char *path)
{
    return (path != NULL) && (strcmp(path, MEMFS_PATH) == 0);
}

static int s_vfs_open(const char *path, int flags, int mode)
{
    (void)flags;
    (void)mode;
    if (!s_is_rec_file(path)) {
        errno = ENOENT;
        return -1;
    }
    s_pos = 0;
    return MEMFS_LOCAL_FD;
}

static ssize_t s_vfs_read(int fd, void *dst, size_t size)
{
    if (fd != MEMFS_LOCAL_FD) {
        errno = EBADF;
        return -1;
    }
    if ((s_data == NULL) || (dst == NULL)) {
        errno = EIO;
        return -1;
    }
    if (s_pos >= (off_t)s_size) {
        return 0;   /* EOF */
    }
    const size_t remain = s_size - (size_t)s_pos;
    size_t n = size;
    if (n > remain) {
        n = remain;
    }
    (void)memcpy(dst, &s_data[(size_t)s_pos], n);
    s_pos += (off_t)n;
    return (ssize_t)n;
}

static off_t s_vfs_seek(int fd, off_t offset, int whence)
{
    if (fd != MEMFS_LOCAL_FD) {
        errno = EBADF;
        return -1;
    }
    off_t base = 0;
    if (whence == SEEK_CUR) {
        base = s_pos;
    } else if (whence == SEEK_END) {
        base = (off_t)s_size;
    } else if (whence != SEEK_SET) {
        errno = EINVAL;
        return -1;
    }
    const off_t new_pos = base + offset;
    if ((new_pos < 0) || (new_pos > (off_t)s_size)) {
        errno = EINVAL;
        return -1;
    }
    s_pos = new_pos;
    return s_pos;
}

static int s_vfs_close(int fd)
{
    if (fd != MEMFS_LOCAL_FD) {
        errno = EBADF;
        return -1;
    }
    s_pos = 0;
    return 0;
}

static int s_vfs_fstat(int fd, struct stat *st)
{
    if ((fd != MEMFS_LOCAL_FD) || (st == NULL)) {
        errno = EBADF;
        return -1;
    }
    (void)memset(st, 0, sizeof(*st));
    st->st_size = (off_t)s_size;
    st->st_mode = S_IFREG | 0444;
    return 0;
}

esp_err_t player_memfs_init(void)
{
    static bool s_registered = false;
    if (s_registered) {
        return ESP_OK;
    }
    const esp_vfs_t vfs = {
        .flags = ESP_VFS_FLAG_DEFAULT,
        .open = s_vfs_open,
        .read = s_vfs_read,
        .seek = s_vfs_seek,
        .close = s_vfs_close,
        .fstat = s_vfs_fstat,
    };
    const esp_err_t err = esp_vfs_register(MEMFS_MOUNT, &vfs, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register %s failed: %s", MEMFS_MOUNT, esp_err_to_name(err));
        return err;
    }
    s_registered = true;
    ESP_LOGI(TAG, "registered %s (in-RAM WAV playback, no SD needed)", MEMFS_MOUNT);
    return ESP_OK;
}

esp_err_t player_memfs_set_data(const uint8_t *data, size_t size)
{
    if ((data == NULL) || (size < 44U)) {   /* 至少一个完整 WAV 头 */
        return ESP_ERR_INVALID_ARG;
    }
    s_data = data;
    s_size = size;
    s_pos = 0;
    return ESP_OK;
}
