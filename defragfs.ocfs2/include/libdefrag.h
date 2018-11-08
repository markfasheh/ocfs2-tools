#ifndef __LIB_DEFERAG_H__
#define __LIB_DEFERAG_H__

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <linux/limits.h>
#include <linux/types.h>

#define PRINT_ERR(msg)	\
	fprintf(stderr, "[ERROR]\t%s\n", (msg))

#define PRINT_FILE_MSG(file, msg) \
	fprintf(stdout, "\"%s\":%s\n", (file), msg)

#define PRINT_FILE_ERRNO(file)	\
	fprintf(stderr, "[ERROR]\"%s\":%s\n", (file), strerror(errno))

#define PRINT_FILE_MSG_ERRNO(file, msg) \
	fprintf(stderr, "[ERROR]%s:\"%s\" - %s\n", msg, file, strerror(errno))

#define PRINT_FILE_ERR(file, msg) \
	fprintf(stderr, "[ERROR]\"%s\":%s\n", (file), msg)

void *do_malloc(size_t size);

int do_read(int fd, void *bytes, size_t count);

int do_write(int fd, const void *bytes, size_t count);

unsigned int do_csum(const unsigned char *buff, int len);


#endif
