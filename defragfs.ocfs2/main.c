#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <errno.h>
#include <malloc.h>
#include <time.h>
#include <libgen.h>
#include <netinet/in.h>
#include <inttypes.h>
#include <ctype.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <mntent.h>
#include <sys/vfs.h>
#include <ftw.h>
#include <sched.h>
#include <signal.h>
#include <uuid/uuid.h>

#include "ocfs2/ocfs2.h"
#include "ocfs2/bitops.h"

#include <record.h>
#include <libdefrag.h>
#include <o2defrag.h>




static char	lost_found_dir[PATH_MAX + 1];
static int	mode_flag;
static unsigned int	regular_count;
static unsigned int	succeed_count;
static unsigned int skipped_count;
static unsigned int processed_count;
static uid_t current_uid;
static int should_stop;
struct resume_record rr;



static void usage(char *progname)
{
	printf("usage: %s [-c] [-v] [-l] [-g] [-h] [FILE | DIRECTORY | DEVICE]...\n",
			progname);
	printf("\t-c\t\tCalculate how many files will be processed\n");
	printf("\t-v\t\tVerbose mode\n");
	printf("\t-l\t\tLow io rate mode\n");
	printf("\t-g\t\tResume last defrag progress\n");
	printf("\t-h\t\tShow this help\n");
	exit(0);
}

/*
 * get_dev_mount_point() -	Get device's ocfs2 mount point.
 * @devname:		the device's name.
 * @mount_point:	the mount point.
 *
 * a block device may be mounted on multiple mount points,
 * this function return on first match
 */
static int get_dev_mount_point(const char *devname, char *mount_point)
{
	/* Refer to /etc/mtab */
	const char *mtab = MOUNTED;
	FILE *fp = NULL;
	struct mntent *mnt = NULL;
	struct stat64 sb;

	if (stat64(devname, &sb) < 0) {
		PRINT_FILE_MSG_ERRNO(devname, "While getting mount point");
		return -1;
	}

	fp = setmntent(mtab, "r");
	if (fp == NULL) {
		PRINT_ERR("Couldn't access /etc/mtab");
		return -1;
	}

	while ((mnt = getmntent(fp)) != NULL) {
		struct stat64 ms;

		/*
		 * To handle device symlinks, we see if the
		 * device number matches, not the name
		 */
		if (stat64(mnt->mnt_fsname, &ms) < 0)
			continue;
		if (sb.st_rdev != ms.st_rdev)
			continue;

		endmntent(fp);
		if (strcmp(mnt->mnt_type, FS_OCFS2) == 0) {
			strncpy(mount_point, mnt->mnt_dir,
				PATH_MAX);
			return 0;
		}
		PRINT_FILE_MSG(devname, "Not ocfs2 format");
		return -1;
	}
	endmntent(fp);
	PRINT_FILE_MSG(devname, "Is not mounted");
	return -1;
}

/*
 * get_file_backend_info() - Get file's mount point and block dev that it's on.
 * @file:	the device's name.
 * @dev:	dev path
 * @mount_point:	the mount point.
 *
 * The file mush be on ocfs2 partition
 */

static int get_file_backend_info(const char *file, char *dev, char *mount_point)
{
	int maxlen = 0;
	int	len, ret;
	FILE *fp = NULL;
	/* Refer to /etc/mtab */
	const char *mtab = MOUNTED;
	char real_path[PATH_MAX + 1];
	struct mntent *mnt = NULL;
	struct statfs64	fsbuf;


	/* Get full path */
	if (realpath(file, real_path) == NULL) {
		PRINT_FILE_MSG_ERRNO(file, "Getting realpath failed");
		return -1;
	}

	if (statfs64(real_path, &fsbuf) < 0) {
		PRINT_FILE_MSG_ERRNO(file, "Getting fs state failed");
		return -1;
	}

	if (fsbuf.f_type != OCFS2_SUPER_MAGIC)
		return -1;

	fp = setmntent(mtab, "r");
	if (fp == NULL) {
		PRINT_ERR("Couldn't access /etc/mtab");
		return -1;
	}

	while ((mnt = getmntent(fp)) != NULL) {
		if (mnt->mnt_fsname[0] != '/')
			continue;
		len = strlen(mnt->mnt_dir);
		ret = memcmp(real_path, mnt->mnt_dir, len);
		if (ret != 0)
			continue;

		if (maxlen >= len)
			continue;

		maxlen = len;

		strncpy(mount_point, mnt->mnt_dir, PATH_MAX);
		strncpy(dev, mnt->mnt_fsname, strlen(mnt->mnt_fsname) + 1);
	}

	endmntent(fp);
	return 0;
}

/*
 * is_ocfs2() -	Test whether the file on an ocfs2 filesystem.
 * @file:		the file's name.
 */
static int is_ocfs2(const char *file)
{
	struct statfs64	fsbuf;
	/* Refer to /etc/mtab */
	char	file_path[PATH_MAX + 1];

	/* Get full path */
	if (realpath(file, file_path) == NULL)
		return 0;

	if (statfs64(file_path, &fsbuf) < 0)
		return 0;

	if (fsbuf.f_type != OCFS2_SUPER_MAGIC)
		return 0;

	return 1;
}

/*
 * calc_entry_counts() - Calculate file counts.
 * @file:		file name.
 * @buf:		file info.
 * @flag:		file type.
 * @ftwbuf:		the pointer of a struct FTW.
 */
static int calc_entry_counts(const char *file,
		const struct stat64 *buf, int flag, struct FTW *ftwbuf)
{
	if (lost_found_dir[0] != '\0' &&
	    !memcmp(file, lost_found_dir, strnlen(lost_found_dir, PATH_MAX))) {
		return 0;
	}

	if (S_ISREG(buf->st_mode))
		regular_count++;
	return 0;
}

/*
 * print_progress -	Print defrag progress
 * @file:		file name.
 * @start:		logical offset for defrag target file
 * @file_size:		defrag target filesize
 */
static void print_progress(const char *file, int result)
{
	char *str_res = "Success";

	if (result)
		str_res = "Failed";
	if (mode_flag & DETAIL)
		printf("[%u/%u]%s:%s\n",
			processed_count, regular_count, file, str_res);
	else
		printf("\033[79;0H\033[K[%u/%u]%s:%s\t",
			processed_count, regular_count, file, str_res);
	fflush(stdout);
}

static int do_file_defrag(const char *file, const struct stat64 *buf,
			int flag, struct FTW *ftwbuf)
{
	/* Defrag the file */
	int ret;
	int fd;
	struct ocfs2_move_extents me = {
		.me_start = 0,
		.me_len = buf->st_size,
		.me_flags = OCFS2_MOVE_EXT_FL_AUTO_DEFRAG,
	};

	fd = open64(file, O_RDWR, 0400);
	if (fd < 0) {
		PRINT_FILE_MSG_ERRNO(file, "Open file failed");
		return -1;
	}

	ret = ioctl(fd, OCFS2_IOC_MOVE_EXT, &me);
	if (ret < 0)
		PRINT_FILE_MSG_ERRNO(file,"Move extent failed");

	close(fd);
	return ret;
}


/*
 * check_file() - Check file attributes.
 * @file:		the file's name.
 * @file_stat:		the pointer of the struct stat64.
 */
static int check_file(const char *file, const struct stat64 *file_stat)
{
	const struct stat64 *buf;
	struct stat64 stat_tmp;
	int ret;

	buf = file_stat;

	if (!buf) {
		ret = stat64(file, &stat_tmp);
		if (ret < 0) {
			PRINT_FILE_MSG_ERRNO(file, "Get file stat failed");
			return 0;
		}
		buf = &stat_tmp;
	}

	if (buf->st_size == 0) {
		if (mode_flag & DETAIL)
			PRINT_FILE_MSG(file, "File size is 0... skip");
		return 0;
	}

	/* Has no blocks */
	if (buf->st_blocks == 0) {
		if (mode_flag & DETAIL)
			PRINT_FILE_MSG(file, "File has no blocks");
		return 0;
	}

	if (current_uid != ROOT_UID &&
		buf->st_uid != current_uid) {
		if (mode_flag & DETAIL) {
			PRINT_FILE_MSG(file,
				"File is not current user's file or current user is not root");
		}
		return 0;
	}
	return 1;
}

/*
 * defrag_file_ftw() - Check file attributes and call ioctl to defrag.
 * @file:		the file's name.
 * @buf:		the pointer of the struct stat64.
 * @flag:		file type.
 * @ftwbuf:		the pointer of a struct FTW.
 */
static int defrag_file_ftw(const char *file, const struct stat64 *buf,
			int flag, struct FTW *ftwbuf)
{
	int ret;
	time_t run_limit = SCHEDULE_TIME_LIMIT;
	static time_t sched_clock;
	time_t now, diff;
	static int count;


	if (rr.r_inode_no) {
		if (buf->st_ino != rr.r_inode_no)
			goto skip;
		else
			rr.r_inode_no = 0;
	}

	if (should_stop || count >= RECORD_EVERY_N_FILES) {
		rr.r_inode_no = buf->st_ino;

		if (should_stop) {
			PRINT_FILE_MSG(file, "Interrupted");
			printf("%s ---- interrupted\n", file);
		} else {
			rr.r_inode_no = 0;
			count = 0;
		}

		if (mode_flag & DETAIL)
			printf("\nRecording...\n");
		ret = store_record(&rr);

		if (ret)
			PRINT_ERR("Record failed");
		else if (mode_flag & DETAIL)
				printf("Record successfully\n"
					"Use -g option to resume progress\n");

		if (should_stop)
			exit(0);
	} else
		count++;

	if (mode_flag & LOW_IO) {
		if(sched_clock == 0)
			sched_clock = time(NULL);

		now = time(NULL);
		diff = now - sched_clock;
		if (diff > run_limit) {
			printf("===========\n");
			sched_yield();
			sched_clock = now;
		}
	}

	if (lost_found_dir[0] != '\0' &&
	    !memcmp(file, lost_found_dir, strnlen(lost_found_dir, PATH_MAX))) {
		if (mode_flag & DETAIL)
			PRINT_FILE_MSG(file,
				"In lost+found dir... ignore");
		return 0;
	}

	if (!S_ISREG(buf->st_mode)) {
		if (mode_flag & DETAIL)
			PRINT_FILE_MSG(file, "Not regular file... ignore");
		return 0;
	}

	processed_count++;

	ret = check_file(file, buf);
	if (!ret) {
		skipped_count++;
		goto out;
	}

	ret = do_file_defrag(file, buf, flag, ftwbuf);
	if (ret == 0)
		succeed_count++;
out:
	print_progress(file, ret);
	return 0;
skip:
	if (mode_flag & DETAIL)
		PRINT_FILE_MSG(file, "already done... skip\n");
	processed_count++;
	skipped_count++;
	return 0;
}

static int defrag_dir(const char *dir_path)
{
	//lost+found is skipped!
	char *real_dir_path = do_malloc(PATH_MAX);
	char *dev_path = do_malloc(PATH_MAX);
	char *mount_point = do_malloc(PATH_MAX);
	char *lost_found = lost_found_dir;
	int mount_point_len;
	int flags = FTW_PHYS | FTW_MOUNT;

	int ret;

	if (!is_ocfs2(dir_path)) {
		PRINT_FILE_MSG(dir_path, "Not within ocfs2 fs");
		return 1;
	}

	if (realpath(dir_path, real_dir_path) == NULL) {
		PRINT_FILE_MSG(dir_path, "Couldn't get full path");
		return 1;
	}
	ret = get_file_backend_info(dir_path, dev_path, mount_point);
	if (ret < 0) {
		PRINT_FILE_MSG(dir_path, "can not get file back info");
		return ret;
	}
	if (access(dir_path, R_OK) < 0) {
		perror(dir_path);
		return 1;
	}
	mount_point_len = strnlen(mount_point, PATH_MAX);
	snprintf(lost_found, PATH_MAX, "%s%s", mount_point, "/lost+found");

	/* Not the case("defragfs.ocfs2  mount_point_dir") */
	if (dir_path[mount_point_len] != '\0') {
		/*
		 * "defragfs.ocfs2 mount_point_dir/lost+found"
		 * or "defragfs.ocfs2 mount_point_dir/lost+found/"
		 */
		int lf_len = strnlen(lost_found, PATH_MAX);

		if (strncmp(lost_found, dir_path, lf_len) == 0
			&& (dir_path[lf_len] == '\0'
				|| dir_path[lf_len] == '/')) {
			PRINT_FILE_MSG(dir_path, 
					"defrag won't work within lost+found\n");
		}
	}

	nftw64(real_dir_path, calc_entry_counts, FTW_OPEN_FD, flags);

	if (mode_flag & STATISTIC) {
		printf("%8d files should be defraged in [%s]\n",
				regular_count, real_dir_path);
		return 1;
	}

	nftw64(dir_path, defrag_file_ftw, FTW_OPEN_FD, flags);
	return 0;
}

static int defrag_block_dev(char *dev_path)
{
	char *mnt_point;
	int ret;

	mnt_point = do_malloc(PATH_MAX);
	ret = get_dev_mount_point(dev_path, mnt_point);
	if (ret < 0) {
		PRINT_FILE_MSG(dev_path, "Could not find mount point");
		return ret;
	}

	if (mode_flag & DETAIL)
		printf("ocfs2 defragmentation for device(%s)\n",
			dev_path);

	return defrag_dir(mnt_point);
}

static int defrag_file(char *file_path)
{
	struct stat64 file_stat;
	int ret;

	if (!is_ocfs2(file_path)) {
		PRINT_FILE_MSG(file_path, "Not on ocfs2 fs\n");
		return -1;
	}

	ret = stat64(file_path, &file_stat);
	if (ret < 0) {
		PRINT_FILE_MSG(file_path, "get file stat error");
		return -1;
	}

	if (!S_ISREG(file_stat.st_mode)) {
		if (mode_flag & DETAIL)
			PRINT_FILE_MSG(file_path,
				"Not regular file... ignore");
		return -1;
	}
	regular_count++;

	/* Empty file */
	if (file_stat.st_size == 0) {
		if (mode_flag & DETAIL)
			PRINT_FILE_MSG(file_path,
				"File size is 0... skip");
		skipped_count++;
		return -1;
	}

	/* Has no blocks */
	if (file_stat.st_blocks == 0) {
		if (mode_flag & DETAIL)
			PRINT_FILE_MSG(file_path, "File has no blocks");
		skipped_count++;
		return -1;
	}


	if (current_uid != ROOT_UID &&
		file_stat.st_uid != current_uid) {
		if (mode_flag & DETAIL) {
			PRINT_FILE_MSG(file_path,
				"File is not current user's file or current user is not root");
		}
		return -1;
	}

	ret = do_file_defrag(file_path, &file_stat, 0, 0);
	if (ret < 0) {
		PRINT_FILE_ERRNO(file_path);
		return -1;
	}

	printf("%s: Succeeded\n", file_path);
	return 0;
}

static void handle_signal(int sig)
{
	switch (sig) {
	case SIGTERM:
	case SIGINT:
		printf("\nProcess Interrupted. signale = %d\n", sig);
		should_stop = 1;
	}
}

static struct o2defrag_opt opt_table[] = {
	declare_opt(DETAIL, "-v"),
	declare_opt(STATISTIC, "-c"),
	declare_opt(GO_ON, "-g"),
	declare_opt(LOW_IO, "-o"),
};

static void dump_mode_flag(int mode_flag)
{
	int i = 0;

	for (i = 0; i < sizeof(opt_table)/sizeof(opt_table[0]); i++) {
		if (mode_flag & opt_table[i].o_num)
			printf(" %s ", opt_table[i].o_str);
	}
}

static int parse_opt(int argc, char **argv, int *_mode_flag)
{
	char opt;

	if (argc == 1)
		return 0;

	while ((opt = getopt(argc, argv, "gvclh")) != EOF) {
		switch (opt) {
		case 'v':
			*_mode_flag |= DETAIL;
			break;
		case 'c':
			*_mode_flag |= STATISTIC;
			break;
		case 'g':
			*_mode_flag |= GO_ON;
			break;
		case 'l':
			*_mode_flag |= LOW_IO;
			break;
		case 'h':
			usage(PROGRAME_NAME);
			exit(0);

		default:
			break;
		}
	}
	return optind;
}


static void init_signal_handler(void)
{
	if (signal(SIGTERM, handle_signal) == SIG_ERR) {
		PRINT_ERR("Could not set SIGTERM");
		exit(1);
	}

	if (signal(SIGINT, handle_signal) == SIG_ERR) {
		PRINT_ERR("Could not set SIGINT");
		exit(1);
	}
}


static void print_version(char *progname)
{
	printf("%s %s\n", progname, VERSION);
}



/*
 * main() -		ocfs2 online defrag.
 *
 * @argc:		the number of parameter.
 * @argv[]:		the pointer array of parameter.
 */
int main(int argc, char *argv[])
{
	int ret;
	char	dir_name[PATH_MAX + 1];
	char	dev_name[PATH_MAX + 1];
	struct stat64	buf;
	int _mode_flag = 0;
	int index;
	struct resume_record rr_tmp;
	struct list_head *pos;

	init_signal_handler();

	print_version(PROGRAME_NAME);

	index = parse_opt(argc, argv, &_mode_flag);

	if (_mode_flag & GO_ON) {
		ret = load_record(&rr_tmp);
		if (ret) {
			PRINT_ERR("Load record failed...exit");
			exit(0);
		}
		mv_record(&rr, &rr_tmp);
		printf("Record detected...\n Start as:\n");
		dump_record(PROGRAME_NAME, &rr, dump_mode_flag);
	} else {
		if (index == argc || index == 0) {
			usage(PROGRAME_NAME);
			exit(0);
		}

		fill_resume_record(&rr, _mode_flag, &argv[index], argc - index, 0);
	}

	mode_flag = rr.r_mode_flag & ~GO_ON;
	current_uid = getuid();

	/* Main process */
	list_for_each(pos, &rr.r_argvs) {
		struct argv_node *n = list_entry(pos, struct argv_node, a_list);
		char *path = n->a_path;

		succeed_count = 0;
		regular_count = 0;
		skipped_count = 0;

		memset(dir_name, 0, PATH_MAX + 1);
		memset(dev_name, 0, PATH_MAX + 1);
		memset(lost_found_dir, 0, PATH_MAX + 1);

		if (lstat64(path, &buf) < 0) {
			perror("Failed to get file info:");
			printf("%s\n", path);
			continue;
		}

		/* Handle i.e. lvm device symlinks */
		if (S_ISLNK(buf.st_mode)) {
			struct stat64	buf2;

			if (stat64(path, &buf2) == 0 &&
			    S_ISBLK(buf2.st_mode))
				buf = buf2;
		}

		if (S_ISBLK(buf.st_mode)) {
			/* Block device */
			ret = defrag_block_dev(path);
		} else if (S_ISDIR(buf.st_mode)) {
			/* Directory */
			ret = defrag_dir(path);
		} else if (S_ISREG(buf.st_mode)) {
			/* Regular file */
			ret = defrag_file(path);
			continue;
		} else {
			/* Irregular file */
			printf("irregular file\n");
			continue;
		}

		if (!ret) {
			printf("\n\tSuccess:\t\t\t[ %u/%u ]\n",
					succeed_count, regular_count);
			printf("\n\tSkipped:\t\t\t[ %u/%u ]\n",
					skipped_count, regular_count);
			printf("\n\tFailure:\t\t\t[ %u/%u ]\n",
				regular_count - succeed_count - skipped_count,
				regular_count);
		}

		free_argv_node(n);
	}

	remove_record();
	return 0;
}
