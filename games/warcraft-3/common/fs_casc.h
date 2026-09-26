#ifndef WC3_FS_CASC_H
#define WC3_FS_CASC_H

#include <stdbool.h>
#include <stdint.h>

typedef struct fsCascStorage_s fsCascStorage_t;
typedef struct fsCascFile_s fsCascFile_t;

bool FS_CascOpenStorage(char const *root, char const *product, fsCascStorage_t **out);
void FS_CascCloseStorage(fsCascStorage_t *storage);
bool FS_CascOpenFile(fsCascStorage_t *storage, char const *path, fsCascFile_t **out);
bool FS_CascRead(fsCascFile_t *file, void *buffer, uint32_t length, uint32_t *bytes_read);
bool FS_CascSeek(fsCascFile_t *file, int64_t distance, uint32_t origin);
bool FS_CascSize(fsCascFile_t *file, uint64_t *size);
void FS_CascCloseFile(fsCascFile_t *file);
uint32_t FS_CascLastError(void);

#endif
