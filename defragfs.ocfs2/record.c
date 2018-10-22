#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <linux/limits.h>
#include <linux/types.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <ocfs2-kernel/kernel-list.h>
#include <record.h>
#include <libdefrag.h>


#define MAX_RECORD_FILE_SIZE (2<<20)

static char record_path[PATH_MAX] = "/tmp/"RECORD_FILE_NAME;

void mv_record(struct resume_record *dst, struct resume_record *src)
{
	dst->r_argc = src->r_argc;
	dst->r_inode_no = src->r_inode_no;
	dst->r_mode_flag = src->r_mode_flag;
	INIT_LIST_HEAD(&dst->r_argvs);
	list_splice(&src->r_argvs, &dst->r_argvs);
}

void dump_record(char *base_name, struct resume_record *rr,
		void (*dump_mode_flag)(int mode_flag))
{
		struct list_head *pos;
		struct argv_node *n;
		int mode_flag = rr->r_mode_flag;


		printf("%s", base_name);
		dump_mode_flag(mode_flag);

		list_for_each(pos, &rr->r_argvs) {
			n = list_entry(pos, struct argv_node, a_list);
			printf(" %s ", n->a_path);
		}
		puts("");
}


void free_argv_node(struct argv_node *n)
{
	list_del(&n->a_list);
	if (n->a_path)
		free(n->a_path);
	free(n);
}

static inline
struct argv_node *allocate_argv_node(int len)
{
	struct argv_node *n;

	n = do_malloc(sizeof(struct argv_node));
	n->a_path = do_malloc(len);
	return n;
}

void free_record(struct resume_record *rr)
{
	struct list_head *pos;
	struct argv_node *n;

	list_for_each(pos, &rr->r_argvs) {
		n = list_entry(pos, struct argv_node, a_list);
		free_argv_node(n);
	}
}

static int read_record(int fd, struct resume_record **p, int *len)
{
	struct stat s;
	void *content_buf = NULL;

	if (fstat(fd, &s))
		goto error;

	content_buf = do_malloc(s.st_size);

	if (do_read(fd, content_buf, s.st_size) != s.st_size)
		goto error;

	if (p)
		*p = content_buf;
	if (len)
		*len = s.st_size;
	return 0;

error:
	if (content_buf)
		free(content_buf);
	if (p)
		*p = NULL;
	if (len)
		*len = 0;
	return -errno;

}

static int is_record_file_valid(void *buf, int len)
{
	int data_len = len - sizeof(unsigned int);
	unsigned int check_sum = *(int *)(buf + data_len);

	if (check_sum == do_csum(buf, data_len))
		return 1;
	return 0;
}

static int __store_record(int fd, struct resume_record *rr)
{
	struct list_head *pos;
	struct argv_node *n;
	int index = 0;
	unsigned char *buf = do_malloc(MAX_RECORD_FILE_SIZE);
	int len;
	int ret;

	memcpy(buf, rr, RECORD_HEADER_LEN);
	index += RECORD_HEADER_LEN;

	list_for_each(pos, &rr->r_argvs) {
		n = list_entry(pos, struct argv_node, a_list);
		len = strlen(n->a_path) + 1;
		if (index + len > MAX_RECORD_FILE_SIZE) {
			PRINT_ERR("Arg too long");
			ret = -1;
			goto out;
		}
		strcpy((char *)&buf[index], n->a_path);
		index += len;
	}

	*(int *)&buf[index] = do_csum(buf, index);
	index += sizeof(int);
	ret = do_write(fd, buf, index);

out:
	free(buf);
	if (ret < 0)
		return ret;

	return 0;
}


void fill_resume_record(struct resume_record *rr,
		int mode_flag, char **argv, int argc, ino_t inode_no)
{
	int i;
	int len;
	struct argv_node *node;

	rr->r_mode_flag = mode_flag;
	rr->r_argc = argc;
	rr->r_inode_no = inode_no;
	INIT_LIST_HEAD(&rr->r_argvs);
	for (i = 0; i < argc; i++) {
		len = strlen(argv[i]) + 1;
		node = do_malloc(sizeof(struct argv_node));
		node->a_path = do_malloc(len);
		strcpy(node->a_path, argv[i]);
		list_add_tail(&node->a_list, &rr->r_argvs);
	}
}

int remove_record(void)
{
	int ret;

	ret = unlink(record_path);
	if (ret && errno != ENOENT) {
		perror("while deleting record file");
		return -errno;
	}
	return 0;
}

int store_record(struct resume_record *rr)
{
	int fd;

	fd = open(record_path,
			O_TRUNC | O_CREAT | O_WRONLY, 0600);
	if (fd < 0) {
		perror("while opening record file");
		return -errno;
	}
	if (__store_record(fd, rr))
		goto out;

	if (fsync(fd))
		goto out;

	close(fd);
	return 0;
out:
	return -1;
}

static int __load_record(int fd, struct resume_record *rr)
{
	int ret = -1, i;
	struct resume_record *rr_tmp = NULL;
	char **argv = NULL;
	int argc;
	char *p;
	int len = 0;

	if (read_record(fd, &rr_tmp, &len))
		goto out;

	if (!is_record_file_valid(rr_tmp, len))
		goto out;


	argc = rr_tmp->r_argc;

	argv = do_malloc(sizeof(char *) * argc);

	p = (void *)rr_tmp + RECORD_HEADER_LEN;
	for (i = 0; i < argc; i++, p++) {
		argv[i] = (char *)p;
		p += strlen(p) + 1;
	}

	fill_resume_record(rr, rr_tmp->r_mode_flag,
			argv, argc, rr_tmp->r_inode_no);

	ret = 0;
out:
	if (rr_tmp)
		free(rr_tmp);
	if (argv)
		free(argv);
	return ret;

}

int load_record(struct resume_record *rr)
{
	int ret;
	int record_fd = open(record_path, O_RDONLY, 0700);

	if (record_fd < 0) {
		perror("while opening record file");
		return -errno;
	}
	ret = __load_record(record_fd, rr);
	close(record_fd);
	return ret;
}

