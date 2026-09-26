#ifdef BZ_TESTS
#include "shared/test.h"
#include "common/common.h"

#include <unistd.h>

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
#endif
