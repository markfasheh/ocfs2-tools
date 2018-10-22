#ifndef __RECORD_H__
#define __RECORD_H__

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <linux/limits.h>
#include <linux/types.h>
#include <stdint.h>

#define RECORD_FILE_NAME ".ocfs2.defrag.record" //under /tmp dir

#define offset_of(type, member) (unsigned long)(&((type *)0)->member)

#define calc_record_file_size(argc) (RECORD_HEADER_LEN + argc * PATH_MAX)


/*
 * Because the main way to use this tool is like
 * defrag -a -b -c path1 path2 path3
 * This struct is used to record every path
 */
struct argv_node {
	char *a_path;
	struct list_head a_list;
};

struct resume_record {
	int r_mode_flag;		/* mode flag, the binary of the combination of options */
	ino_t r_inode_no;		/* start from the file as a inode number */
	int r_argc;			/* how many argv_node in the r_argvs list */
	struct list_head r_argvs;	/* the list of argv_node */
};

#define RECORD_HEADER_LEN offset_of(struct resume_record, r_argvs)

extern void dump_record(char *base_name,
			struct resume_record *rr,
			void (*dump_mode_flag)(int mode_flag));

extern void set_record_file_path(char *path);
extern void free_record(struct resume_record *rr);
extern void fill_resume_record(struct resume_record *rr,
		int mode_flag, char **argv, int argc, ino_t inode_no);
extern void free_argv_node(struct argv_node *n);
extern int store_record(struct resume_record *rr);
extern int load_record(struct resume_record *rr);
extern void mv_record(struct resume_record *dst, struct resume_record *src);
extern int remove_record(void);
#endif
