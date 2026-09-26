#ifdef BZ_TESTS
#include "shared/test.h"
#include "common/common.h"

#include <unistd.h>
#include <stdlib.h>

static cstring_t fs_test_casc_root(void) {
    return getenv("WC3_CASC_DATA");
}

static bool fs_test_make_mpq(cstring_t path, cstring_t member, cstring_t contents,
                             handle_t *archive) {
    unlink(path);
    if (!SFileCreateArchive(path, 0, 16, archive)) return false;
    if (!SFileAddFileFromBuffer(*archive, member, contents, (uint32_t)strlen(contents))) {
        SFileCloseArchive(*archive);
        *archive = NULL;
        unlink(path);
        return false;
    }
    if (!SFileCloseArchive(*archive)) {
        *archive = NULL;
        unlink(path);
        return false;
    }
    return SFileOpenArchive(path, 0, 0, archive);
}

static bool fs_test_read(cstring_t path, char *contents, uint32_t capacity, uint32_t *size) {
    handle_t file = FS_OpenFile(path);
    bool ok;
    if (!file) return false;
    ok = FS_ReadFileHandle(file, contents, capacity, size);
    FS_CloseFile(file);
    return ok;
}

TEST(wc3_fs, mpq_handle_ops) {
    cstring_t path = "build/tests/wc3-fs-vfs-handle.mpq";
    cstring_t source = "VFS handle";
    handle_t archive = NULL;
    handle_t file;
    char partial[4] = {0};
    char rest[sizeof("VFS handle")] = {0};
    uint32_t bytes_read = 0;
    uint64_t size = 0;

    unlink(path);
    T_ASSERT(SFileCreateArchive(path, 0, 16, &archive));
    if (!archive) return;
    T_ASSERT(SFileAddFileFromBuffer(archive, "vfs-test.txt", source, (uint32_t)strlen(source)));
    T_ASSERT(SFileCloseArchive(archive));
    T_ASSERT(SFileOpenArchive(path, 0, 0, &archive));
    if (!archive) { unlink(path); return; }

    FS_SetPriorityArchive(archive);
    file = FS_OpenFile("vfs-test.txt");
    T_NOT_NULL(file);
    if (file) {
        T_ASSERT(FS_GetFileSize(file, &size));
        T_EQ(size, strlen(source));
        T_ASSERT(FS_ReadFileHandle(file, partial, 3, &bytes_read));
        T_EQ(bytes_read, 3);
        T_STREQ(partial, "VFS");
        T_ASSERT(FS_SetFilePointer(file, 4, FILE_BEGIN));
        T_ASSERT(FS_ReadFileHandle(file, rest, (uint32_t)sizeof(rest) - 1, &bytes_read));
        T_EQ(bytes_read, strlen(source) - 4);
        T_STREQ(rest, "handle");
        FS_CloseFile(file);
    }
    FS_SetPriorityArchive(NULL);
    SFileCloseArchive(archive);
    unlink(path);
}

TEST(wc3_fs, casc_file_exists_and_reads_manifest_path) {
    cstring_t root = fs_test_casc_root();
    cstring_t path = "war3.w3mod:ui\\war3skins.txt";
    handle_t file;
    char sample[17] = {0};
    uint32_t bytes_read = 0;

    if (!root) return;
    T_NOT_NULL(root);
    T_ASSERT(FS_AddDataDirectory(root));
    T_ASSERT(FS_FileExists(path));
    file = FS_OpenFile(path);
    T_NOT_NULL(file);
    if (!file) return;
    T_ASSERT(FS_ReadFileHandle(file, sample, sizeof(sample) - 1, &bytes_read));
    T_EQ(bytes_read, sizeof(sample) - 1);
    T_ASSERT(sample[0] != '\0');
    FS_CloseFile(file);
}

TEST(wc3_fs, map_priority_mpq_overrides_casc) {
    cstring_t root = fs_test_casc_root();
    cstring_t member = "war3.w3mod:ui\\war3skins.txt";
    cstring_t archive_path = "build/tests/wc3-fs-casc-priority.mpq";
    cstring_t expected = "map overlay";
    handle_t archive = NULL;
    char contents[32] = {0};
    uint32_t size = 0;

    if (!root) return;
    T_NOT_NULL(root);
    T_ASSERT(FS_AddDataDirectory(root));
    T_ASSERT(fs_test_make_mpq(archive_path, member, expected, &archive));
    if (!archive) return;
    FS_SetPriorityArchive(archive);
    T_ASSERT(fs_test_read(member, contents, sizeof(contents) - 1, &size));
    T_STREQ(contents, expected);
    T_EQ(size, strlen(expected));
    FS_SetPriorityArchive(NULL);
    SFileCloseArchive(archive);
    unlink(archive_path);
}

TEST(wc3_fs, non_casc_data_directory_stays_mountable) {
    T_ASSERT(FS_AddDataDirectory("build/tests"));
}

TEST(wc3_fs, recognized_invalid_casc_root_fails) {
    cstring_t marker = "build/tests/.build.info";
    FILE *file;

    T_ASSERT(access(marker, F_OK) != 0);
    file = fopen(marker, "wb");
    T_NOT_NULL(file);
    if (!file) return;
    fputs("not a valid CASC storage\n", file);
    fclose(file);
    T_ASSERT(!FS_AddDataDirectory("build/tests"));
    unlink(marker);
}

TEST(wc3_fs, casc_storage_reopens_after_reset) {
    cstring_t root = fs_test_casc_root();
    char sample[2] = {0};
    uint32_t bytes_read = 0;
    handle_t file;

    if (!root) return;
    T_NOT_NULL(root);
    FS_Shutdown();
    T_ASSERT(FS_AddDataDirectory(root));
    file = FS_OpenFile("war3.w3mod:ui\\war3skins.txt");
    T_NOT_NULL(file);
    if (file) {
        T_ASSERT(FS_ReadFileHandle(file, sample, 1, &bytes_read));
        T_EQ(bytes_read, 1);
        FS_CloseFile(file);
    }
    FS_Shutdown();
}
#endif
