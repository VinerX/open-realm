#include "fs_casc.h"

#include "../../../vendor/casclib/src/CascLib.h"

#ifdef _WIN32
#undef far
#undef near
#undef GetClassName
#undef SetRect
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct fsCascStorage_s {
    HANDLE handle;
};

struct fsCascFile_s {
    HANDLE handle;
};

bool FS_CascOpenStorage(char const *root, char const *product, fsCascStorage_t **out) {
    CASC_OPEN_STORAGE_ARGS args = { sizeof(CASC_OPEN_STORAGE_ARGS) };
    fsCascStorage_t *storage;

    if (out) *out = NULL;
    if (!root || !*root || !product || !*product || !out) return false;
    storage = calloc(1, sizeof(*storage));
    if (!storage) return false;
    args.szLocalPath = root;
    args.szCodeName = product;
    args.dwLocaleMask = CASC_LOCALE_NONE;
    if (!CascOpenStorageEx(NULL, &args, false, &storage->handle)) {
        free(storage);
        return false;
    }
    *out = storage;
    return true;
}

void FS_CascCloseStorage(fsCascStorage_t *storage) {
    if (!storage) return;
    CascCloseStorage(storage->handle);
    free(storage);
}

bool FS_CascOpenFile(fsCascStorage_t *storage, char const *path, fsCascFile_t **out) {
    char namespaced_path[4096];
    HANDLE handle = NULL;
    fsCascFile_t *file;
    bool has_namespace;

    if (out) *out = NULL;
    if (!storage || !path || !*path || !out) return false;
    has_namespace = strchr(path, ':') != NULL;
    if (!CascOpenFile(storage->handle, path, CASC_LOCALE_NONE, CASC_OPEN_BY_NAME, &handle)) {
        if (has_namespace ||
            snprintf(namespaced_path, sizeof(namespaced_path), "war3.w3mod:%s", path) >=
                (int)sizeof(namespaced_path) ||
            !CascOpenFile(storage->handle, namespaced_path, CASC_LOCALE_NONE,
                          CASC_OPEN_BY_NAME, &handle)) {
            return false;
        }
    }
    file = calloc(1, sizeof(*file));
    if (!file) {
        CascCloseFile(handle);
        return false;
    }
    file->handle = handle;
    *out = file;
    return true;
}

bool FS_CascRead(fsCascFile_t *file, void *buffer, uint32_t length, uint32_t *bytes_read) {
    DWORD read = 0;
    if (!file || (length && !buffer) || !CascReadFile(file->handle, buffer, length, &read))
        return false;
    if (bytes_read) *bytes_read = read;
    return true;
}

bool FS_CascSeek(fsCascFile_t *file, int64_t distance, uint32_t origin) {
    ULONGLONG position;
    return file && CascSetFilePointer64(file->handle, distance, &position, origin);
}

bool FS_CascSize(fsCascFile_t *file, uint64_t *size) {
    ULONGLONG file_size;
    if (!file || !size || !CascGetFileSize64(file->handle, &file_size)) return false;
    *size = (uint64_t)file_size;
    return true;
}

void FS_CascCloseFile(fsCascFile_t *file) {
    if (!file) return;
    CascCloseFile(file->handle);
    free(file);
}

uint32_t FS_CascLastError(void) {
    return (uint32_t)GetCascError();
}
