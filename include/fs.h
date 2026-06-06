#ifndef FS_H
#define FS_H

#include <stdint.h>

void fs_init(void);
int  fs_write(const char *name, const char *data, uint32_t size);
int  fs_read(const char *name, char *buf, uint32_t max_size);
int  fs_delete(const char *name);
void fs_list(void);
int  fs_exists(const char *name);

#endif
