#include "CascLib.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void PrintCascError(char const *operation, char const *path) {
    fprintf(stderr, "casc_probe: %s %s failed (CascLib error 0x%08lx)\n",
            operation, path, (unsigned long)GetCascError());
}

int main(int argc, char **argv) {
    char storage_path[4096];
    HANDLE storage = NULL;
    CASC_STORAGE_PRODUCT product = { 0 };
    CASC_OPEN_STORAGE_ARGS open_args = { sizeof(CASC_OPEN_STORAGE_ARGS) };
    bool failed = false;

    if (argc < 4) {
        fprintf(stderr, "usage: casc_probe <install-root> <product> <file>... | --find <mask>\n");
        return 2;
    }
    if (snprintf(storage_path, sizeof(storage_path), "%s", argv[1]) >=
        (int)sizeof(storage_path)) {
        fprintf(stderr, "casc_probe: storage path is too long\n");
        return 2;
    }
    open_args.szLocalPath = storage_path;
    open_args.szCodeName = argv[2];
    open_args.dwLocaleMask = CASC_LOCALE_NONE;
    if (!CascOpenStorageEx(NULL, &open_args, false, &storage)) {
        PrintCascError("open storage", storage_path);
        return 1;
    }
    if (!CascGetStorageInfo(storage, CascStorageProduct, &product, sizeof(product), NULL)) {
        PrintCascError("read product metadata", storage_path);
        CascCloseStorage(storage);
        return 1;
    }
    printf("product=%s build=%lu\n", product.szCodeName, (unsigned long)product.BuildNumber);
    if (!strcmp(argv[3], "--find")) {
        CASC_FIND_DATA found = { 0 };
        HANDLE find;
        uint32_t shown = 0;
        if (argc != 5 && argc != 6) {
            fprintf(stderr, "casc_probe: --find requires a mask and optional file name\n");
            CascCloseStorage(storage);
            return 2;
        }
        find = CascFindFirstFile(storage, argv[4], &found, NULL);
        if (find) {
            do {
                printf("entry=%s\n", found.szFileName);
                shown++;
            } while (shown < 100 && CascFindNextFile(find, &found));
            CascFindClose(find);
        } else {
            PrintCascError("enumerate storage mask", argv[4]);
            CascCloseStorage(storage);
            return 1;
        }
        if (argc == 6) {
            HANDLE file = NULL;
            ULONGLONG size = 0;
            uint8_t sample[16];
            DWORD bytes_read = 0;
            DWORD to_read;
            if (!CascOpenFile(storage, argv[5], CASC_LOCALE_NONE, CASC_OPEN_BY_NAME, &file)) {
                PrintCascError("open file", argv[5]);
                CascCloseStorage(storage);
                return 1;
            }
            if (!CascGetFileSize64(file, &size)) {
                PrintCascError("read file size", argv[5]);
                CascCloseFile(file);
                CascCloseStorage(storage);
                return 1;
            }
            to_read = size < sizeof(sample) ? (DWORD)size : (DWORD)sizeof(sample);
            if (to_read && (!CascReadFile(file, sample, to_read, &bytes_read) || bytes_read != to_read)) {
                PrintCascError("read file", argv[5]);
                CascCloseFile(file);
                CascCloseStorage(storage);
                return 1;
            }
            printf("file=%s size=%" PRIu64 " sample_bytes=%lu\n",
                   argv[5], (uint64_t)size, (unsigned long)bytes_read);
            CascCloseFile(file);
        }
        CascCloseStorage(storage);
        return 0;
    }
    if (!strcmp(argv[3], "--cat")) {
        HANDLE file = NULL;
        ULONGLONG size = 0;
        char *buffer;
        DWORD bytes_read = 0;
        if (argc != 5) {
            fprintf(stderr, "casc_probe: --cat requires one file name\n");
            CascCloseStorage(storage);
            return 2;
        }
        if (!CascOpenFile(storage, argv[4], CASC_LOCALE_NONE, CASC_OPEN_BY_NAME, &file)) {
            PrintCascError("open file", argv[4]);
            CascCloseStorage(storage);
            return 1;
        }
        if (!CascGetFileSize64(file, &size)) {
            PrintCascError("read file size", argv[4]);
            CascCloseFile(file);
            CascCloseStorage(storage);
            return 1;
        }
        buffer = malloc((size_t)size + 1);
        if (!buffer || !CascReadFile(file, buffer, (DWORD)size, &bytes_read) || bytes_read != (DWORD)size) {
            PrintCascError("read file", argv[4]);
            free(buffer);
            CascCloseFile(file);
            CascCloseStorage(storage);
            return 1;
        }
        fwrite(buffer, 1, bytes_read, stdout);
        free(buffer);
        CascCloseFile(file);
        CascCloseStorage(storage);
        return 0;
    }
    if (strcmp(product.szCodeName, argv[2])) {
        fprintf(stderr, "casc_probe: opened product '%s', expected '%s'\n",
                product.szCodeName, argv[2]);
        failed = true;
    }

    for (int i = 3; i < argc; i++) {
        HANDLE file = NULL;
        ULONGLONG size = 0;
        uint8_t sample[16];
        DWORD bytes_read = 0;
        DWORD to_read;

        if (!CascOpenFile(storage, argv[i], CASC_LOCALE_NONE, CASC_OPEN_BY_NAME, &file)) {
            PrintCascError("open file", argv[i]);
            failed = true;
            continue;
        }
        if (!CascGetFileSize64(file, &size)) {
            PrintCascError("read file size", argv[i]);
            failed = true;
            CascCloseFile(file);
            continue;
        }
        to_read = size < sizeof(sample) ? (DWORD)size : (DWORD)sizeof(sample);
        if (to_read && (!CascReadFile(file, sample, to_read, &bytes_read) || bytes_read != to_read)) {
            PrintCascError("read file", argv[i]);
            failed = true;
            CascCloseFile(file);
            continue;
        }
        printf("file=%s size=%" PRIu64 " sample_bytes=%lu\n",
               argv[i], (uint64_t)size, (unsigned long)bytes_read);
        CascCloseFile(file);
    }

    CascCloseStorage(storage);
    return failed ? 1 : 0;
}
