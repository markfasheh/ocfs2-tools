
#ifndef __READFS_H__
#define __READFS_H__

int read_super_block(int fd, char *buf, int len, __u32 *bits);

#endif		/* __READFS_H__ */
