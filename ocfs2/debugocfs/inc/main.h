
#ifndef __MAIN_H__
#define __MAIN_H__

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>

#include <glib.h>

#include <readline/readline.h>
#include <readline/history.h>

#include <linux/types.h>

#include <ocfs2_fs.h>
#include <ocfs1_fs_compat.h>

#define LOG_INTERNAL(fmt, arg...)					\
	do {								\
		fprintf(stdout, "INTERNAL ERROR: ");			\
		fprintf(stdout, fmt, ## arg);				\
		fprintf(stdout, ", %s, %d\n", __FILE__, __LINE__);	\
		fflush(stdout);						\
	} while (0)

#endif		/* __MAIN_H__ */
