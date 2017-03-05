
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
	
typedef struct _DIR_struct {
	char t;
} DIR;

struct dirent {
  ino_t d_ino;
  off_t d_off;
  unsigned char d_type;
  char d_name[0];
};

enum
  {
    DT_UNKNOWN = 0,
# define DT_UNKNOWN     DT_UNKNOWN
    DT_FIFO = 1,
# define DT_FIFO        DT_FIFO
    DT_CHR = 2,
# define DT_CHR         DT_CHR
    DT_DIR = 4,
# define DT_DIR         DT_DIR
    DT_BLK = 6,
# define DT_BLK         DT_BLK
    DT_REG = 8,
# define DT_REG         DT_REG
    DT_LNK = 10,
# define DT_LNK         DT_LNK
    DT_SOCK = 12,
# define DT_SOCK        DT_SOCK
    DT_WHT = 14
# define DT_WHT         DT_WHT
  };

extern DIR *opendir (const char *__name) __nonnull ((1));
extern struct dirent *readdir (DIR *__dirp) __nonnull ((1));
extern int closedir (DIR *__dirp) __nonnull ((1));


#ifdef __cplusplus
}
#endif
