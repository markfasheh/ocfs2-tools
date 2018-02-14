/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * mkfs.c
 *
 * OCFS2 format utility
 *
 * Copyright (C) 2004, 2011 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2,  as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

#include "mkfs.h"

static State *get_state(int argc, char **argv);
static void free_state(State *s);
static int get_number(char *arg, uint64_t *res);
static void parse_journal_opts(char *progname, const char *opts,
			       uint64_t *journal_size_in_bytes,
			       int *journal64);
static void usage(const char *progname);
static void version(const char *progname);
static void fill_defaults(State *s);
static int get_bits(State *s, int num);
static uint64_t get_valid_size(uint64_t num, uint64_t lo, uint64_t hi);
static void *do_malloc(State *s, size_t size);
static void do_pwrite(State *s, const void *buf, size_t count, 
		      uint64_t offset);
static AllocBitmap *initialize_bitmap(State *s, uint32_t bits,
				      uint32_t unit_bits, const char *name,
				      SystemFileDiskRecord *bm_record);

static int
find_clear_bits(void *buf, unsigned int size, uint32_t num_bits, uint32_t offset);
static int alloc_bytes_from_bitmap(State *s, uint64_t bytes,
				   AllocBitmap *bitmap, uint64_t *start,
				   uint64_t *num);
static int alloc_from_bitmap(State *s, uint64_t num_bits, AllocBitmap *bitmap,
			     uint64_t *start, uint64_t *num);
static uint64_t alloc_inode(State *s, uint16_t *suballoc_bit);
static DirData *alloc_directory(State *s);
static void free_directory(DirData *dir);
static void add_entry_to_directory(State *s, DirData *dir, char *name,
				   uint64_t byte_off, uint8_t type);
static uint32_t blocks_needed(State *s);
static uint32_t sys_blocks_needed(uint32_t num_slots);
static uint32_t system_dir_blocks_needed(State *s);
static void check_32bit_blocks(State *s);
static void format_superblock(State *s, SystemFileDiskRecord *rec,
			      SystemFileDiskRecord *root_rec,
			      SystemFileDiskRecord *sys_rec);
static void format_file(State *s, SystemFileDiskRecord *rec);
static void write_metadata(State *s, SystemFileDiskRecord *rec, void *src);
static void write_bitmap_data(State *s, AllocBitmap *bitmap);
static void write_directory_data(State *s, DirData *dir);
static void write_group_data(State *s, AllocGroup *group);
static void format_leading_space(State *s);
//static void replacement_journal_create(State *s, uint64_t journal_off);
static void open_device(State *s);
static void close_device(State *s);
static int initial_slots_for_volume(uint64_t size);
static void create_generation(State *s);
static void init_record(State *s, SystemFileDiskRecord *rec, int type, int mode);
static void print_state(State *s);
static void clear_both_ends(State *s);
static int ocfs2_clusters_per_group(int block_size,
				    int cluster_size_bits);
static AllocGroup * initialize_alloc_group(State *s, const char *name,
					   SystemFileDiskRecord *alloc_inode,
					   uint64_t blkno,
					   uint16_t chain, uint16_t cpg,
					   uint16_t bpc);
static void free_alloc_group(AllocGroup *group);
static void index_system_dirs(State *s, ocfs2_filesys *fs);
static void create_lost_found_dir(State *s, ocfs2_filesys *fs);
static void format_journals(State *s, ocfs2_filesys *fs);
static void format_slotmap(State *s, ocfs2_filesys *fs);
static int format_backup_super(State *s, ocfs2_filesys *fs);
static void mkfs_compute_meta_ecc(State *s, void *data,
				  struct ocfs2_block_check *bc);

extern char *optarg;
extern int optind, opterr, optopt;

static SystemFileInfo system_files[] = {
	{ "bad_blocks", SFI_OTHER, 1, S_IFREG | 0644 },
	{ "global_inode_alloc", SFI_CHAIN, 1, S_IFREG | 0644 },
	{ "slot_map", SFI_OTHER, 1, S_IFREG | 0644 },
	{ "heartbeat", SFI_HEARTBEAT, 1, S_IFREG | 0644 },
	{ "global_bitmap", SFI_CLUSTER, 1, S_IFREG | 0644 },
	{ "aquota.user", SFI_QUOTA, 1, S_IFREG | 0644 },
	{ "aquota.group", SFI_QUOTA, 1, S_IFREG | 0644 },
	{ "orphan_dir:%04d", SFI_OTHER, 0, S_IFDIR | 0755 },
	{ "extent_alloc:%04d", SFI_CHAIN, 0, S_IFREG | 0644 },
	{ "inode_alloc:%04d", SFI_CHAIN, 0, S_IFREG | 0644 },
	{ "journal:%04d", SFI_JOURNAL, 0, S_IFREG | 0644 },
	{ "local_alloc:%04d", SFI_LOCAL_ALLOC, 0, S_IFREG | 0644 },
	{ "truncate_log:%04d", SFI_TRUNCATE_LOG, 0, S_IFREG | 0644 },
	{ "aquota.user:%04d", SFI_QUOTA, 0, S_IFREG | 0644 },
	{ "aquota.group:%04d", SFI_QUOTA, 0, S_IFREG | 0644 },
};

struct fs_type_translation {
	const char *ft_str;
	enum ocfs2_mkfs_types ft_type;
};

static struct fs_type_translation ocfs2_mkfs_types_table[] = {
	{"datafiles", OCFS2_MKFSTYPE_DATAFILES},
	{"mail", OCFS2_MKFSTYPE_MAIL},
	{"vmstore", OCFS2_MKFSTYPE_VMSTORE},
	{NULL, OCFS2_MKFSTYPE_DEFAULT},
};

enum {
	BACKUP_SUPER_OPTION = CHAR_MAX + 1,
	FEATURE_LEVEL,
	FEATURES_OPTION,
	CLUSTER_STACK_OPTION,
	CLUSTER_NAME_OPTION,
	GLOBAL_HEARTBEAT_OPTION,
};

static uint64_t align_bytes_to_clusters_ceil(State *s,
					     uint64_t bytes)
{
	uint64_t ret = bytes + s->cluster_size - 1;

	if (ret < bytes) /* deal with wrapping */
		ret = UINT64_MAX;

	ret = ret >> s->cluster_size_bits;
	ret = ret << s->cluster_size_bits;

	return ret;
}

/*
 *	Translate 32 bytes uuid to 36 bytes uuid format.
 *	for example:
 *	32 bytes uuid: 178BDC83D50241EF94EB474A677D498B
 *	36 bytes uuid: 178BDC83-D502-41EF-94EB-474A677D498B
 */
static void translate_uuid(char *uuid_32, char *uuid_36)
{
	int i;
	char *cp = uuid_32;

	for (i = 0; i < 36; i++) {
		if ((i == 8) || (i == 13) || (i == 18) || (i == 23)) {
			uuid_36[i] = '-';
			continue;
		}
		uuid_36[i] = *cp++;
	}
}

static int is_cluster_info_valid(State *s, char *stack_name,
				 char *cluster_name, int globalhb)
{
	if (!stack_name && !cluster_name && !globalhb)
		return 1;

	if (s->mount == MOUNT_LOCAL) {
		if (stack_name || cluster_name || globalhb) {
			com_err(s->progname, O2CB_ET_INVALID_STACK_NAME,
				"; local mount is incompatible with "
				"the specified cluster attribute");
			return 0;
		}
	}

	if (!stack_name || !strlen(stack_name)) {
		com_err(s->progname, O2CB_ET_INVALID_STACK_NAME,
			"; missing cluster stack");
		return 0;
	}

	if (!o2cb_valid_stack_name(stack_name)) {
		com_err(s->progname, O2CB_ET_INVALID_STACK_NAME,
			"; unknown cluster stack '%s'", stack_name);
		return 0;
	}

	if (!cluster_name) {
		com_err(s->progname, O2CB_ET_INVALID_CLUSTER_NAME,
			"; missing cluster name");
		return 0;
	}

	if (!strcmp(stack_name, OCFS2_CLASSIC_CLUSTER_STACK)) {
		if (!o2cb_valid_o2cb_cluster_name(cluster_name)) {
			com_err(s->progname, O2CB_ET_INVALID_CLUSTER_NAME,
				"; max %d alpha-numeric characters",
				OCFS2_CLUSTER_NAME_LEN);
			return 0;
		}
	} else {
		if (!o2cb_valid_cluster_name(cluster_name)) {
			com_err(s->progname, O2CB_ET_INVALID_CLUSTER_NAME,
				"; max %d characters", OCFS2_CLUSTER_NAME_LEN);
			return 0;
		}
	}

	if (globalhb) {
		if (strcmp(stack_name, OCFS2_CLASSIC_CLUSTER_STACK)) {
			com_err(s->progname, O2CB_ET_INVALID_STACK_NAME,
				"; global heartbeat mode is only applicable to "
				"the o2cb cluster stack");
			return 0;
		}
	}

	return 1;
}

static void
handle_signal (int sig)
{
	switch (sig) {
	case SIGTERM:
	case SIGINT:
		printf("\nProcess Interrupted.\n");
		exit(1);
	}

	return ;
}

/* Call this with SIG_BLOCK to block and SIG_UNBLOCK to unblock */
static void
block_signals (int how)
{
     sigset_t sigs;

     sigfillset(&sigs);
     sigdelset(&sigs, SIGTRAP);
     sigdelset(&sigs, SIGSEGV);
     sigprocmask(how, &sigs, (sigset_t *) 0);

     return ;
}

/* Is this something to skip for heartbeat-only devices */
static int hb_dev_skip(State *s, int system_inode)
{
	int ret = 0;

	if (s->hb_dev) {
		switch (system_inode) {
			case GLOBAL_BITMAP_SYSTEM_INODE:
			case GLOBAL_INODE_ALLOC_SYSTEM_INODE:
			case HEARTBEAT_SYSTEM_INODE:
				break;

			default:
				ret = 1;
		}
	}

	return ret;
}

static void fill_fake_fs(State *s, ocfs2_filesys *fake_fs, void *buf)
{
	memset(buf, 0, s->blocksize);
	memset(fake_fs, 0, sizeof(ocfs2_filesys));

	fake_fs->fs_super = buf;
	fake_fs->fs_blocksize = s->blocksize;
	fake_fs->fs_clustersize = s->cluster_size;

	OCFS2_RAW_SB(fake_fs->fs_super)->s_feature_incompat =
		s->feature_flags.opt_incompat;
	OCFS2_RAW_SB(fake_fs->fs_super)->s_feature_ro_compat =
		s->feature_flags.opt_ro_compat;
	OCFS2_RAW_SB(fake_fs->fs_super)->s_feature_compat =
		s->feature_flags.opt_compat;
}

static void mkfs_init_dir_trailer(State *s, DirData *dir, void *buf)
{
	char super_buf[OCFS2_MAX_BLOCKSIZE];
	ocfs2_filesys fake_fs;
	struct ocfs2_dir_entry *de;
	struct ocfs2_dinode fake_di = {
		.i_blkno = dir->record->fe_off >> s->blocksize_bits,
	};
	uint64_t blkno = dir->record->extent_off;

	/* Find out how far we are in our directory */
	blkno += ((char *)buf) - ((char *)dir->buf);
	blkno >>= s->blocksize_bits;

	fill_fake_fs(s, &fake_fs, super_buf);

	if (ocfs2_supports_dir_trailer(&fake_fs)) {
		de = buf;
		de->rec_len = ocfs2_dir_trailer_blk_off(&fake_fs);
		ocfs2_init_dir_trailer(&fake_fs, &fake_di, blkno, buf);
	}
}

/* Should we skip this inode because of features enabled / disabled? */
static int feature_skip(State *s, int system_inode)
{
	switch (system_inode) {
		case USER_QUOTA_SYSTEM_INODE:
		case LOCAL_USER_QUOTA_SYSTEM_INODE:
			return !(s->feature_flags.opt_ro_compat &
					OCFS2_FEATURE_RO_COMPAT_USRQUOTA);
		case GROUP_QUOTA_SYSTEM_INODE:
		case LOCAL_GROUP_QUOTA_SYSTEM_INODE:
			return !(s->feature_flags.opt_ro_compat &
					OCFS2_FEATURE_RO_COMPAT_GRPQUOTA);
		default:
			return 0;
	}
}

static inline uint32_t system_dir_bytes_needed(State *s)
{
	int each = OCFS2_DIR_REC_LEN(SYSTEM_FILE_NAME_MAX);

	return each * sys_blocks_needed(s->initial_slots);
}

static void format_quota_files(State *s, ocfs2_filesys *fs)
{
	errcode_t ret;
	ocfs2_quota_hash *usr_hash = NULL, *grp_hash = NULL;

	/* Write correct data into quota files */
	if (!feature_skip(s, USER_QUOTA_SYSTEM_INODE)) {
		ret = ocfs2_init_fs_quota_info(fs, USRQUOTA);
		if (ret) {
			com_err(s->progname, ret,
				"while looking up global user quota file");
			goto error;
		}
		fs->qinfo[USRQUOTA].flags = 0;
		fs->qinfo[USRQUOTA].qi_info.dqi_syncms = OCFS2_DEF_QUOTA_SYNC;
		fs->qinfo[USRQUOTA].qi_info.dqi_bgrace = OCFS2_DEF_BLOCK_GRACE;
		fs->qinfo[USRQUOTA].qi_info.dqi_igrace = OCFS2_DEF_INODE_GRACE;

		ret = ocfs2_new_quota_hash(&usr_hash);
		if (ret) {
			com_err(s->progname, ret,
				"while creating user quota hash.");
			goto error;
		}
		ret = ocfs2_init_global_quota_file(fs, USRQUOTA);
		if (ret) {
			com_err(s->progname, ret, "while creating global user "
				"quota file");
			goto error;
		}
		ret = ocfs2_init_local_quota_files(fs, USRQUOTA);
		if (ret) {
			com_err(s->progname, ret,
				"while initializing local user quota files");
			goto error;
		}
	}
	if (!feature_skip(s, GROUP_QUOTA_SYSTEM_INODE)) {
		ret = ocfs2_init_fs_quota_info(fs, GRPQUOTA);
		if (ret) {
			com_err(s->progname, ret,
				"while looking up global group quota file");
			goto error;
		}
		fs->qinfo[GRPQUOTA].flags = 0;
		fs->qinfo[GRPQUOTA].qi_info.dqi_syncms = OCFS2_DEF_QUOTA_SYNC;
		fs->qinfo[GRPQUOTA].qi_info.dqi_bgrace = OCFS2_DEF_BLOCK_GRACE;
		fs->qinfo[GRPQUOTA].qi_info.dqi_igrace = OCFS2_DEF_INODE_GRACE;
		ret = ocfs2_new_quota_hash(&grp_hash);
		if (ret) {
			com_err(s->progname, ret,
				"while creating group quota hash.");
			goto error;
		}
		ret = ocfs2_init_global_quota_file(fs, GRPQUOTA);
		if (ret) {
			com_err(s->progname, ret, "while creating global group "
				"quota file");
			goto error;
		}

		ret = ocfs2_init_local_quota_files(fs, GRPQUOTA);
		if (ret) {
			com_err(s->progname, ret,
				"while initializing local group quota files");
			goto error;
		}
	}

	ret = ocfs2_compute_quota_usage(fs, usr_hash, grp_hash);
	if (ret) {
		com_err(s->progname, ret, "while computing quota usage");
		goto error;
	}
	if (usr_hash) {
		ret = ocfs2_write_release_dquots(fs, USRQUOTA, usr_hash);
		if (ret) {
			com_err(s->progname, ret,
				"while writing user quota usage");
			goto error;
		}
		ret = ocfs2_free_quota_hash(usr_hash);
		if (ret) {
			com_err(s->progname, ret,
				"while releasing user quota hash");
			goto error;
		}
	}
	if (grp_hash) {
		ret = ocfs2_write_release_dquots(fs, GRPQUOTA, grp_hash);
		if (ret) {
			com_err(s->progname, ret,
				"while writing group quota usage");
			goto error;
		}
		ret = ocfs2_free_quota_hash(grp_hash);
		if (ret) {
			com_err(s->progname, ret,
				"while releasing group quota hash");
			goto error;
		}
	}
	return;
error:
	clear_both_ends(s);
	exit(1);
}

static void grow_extent_allocator(State *s, ocfs2_filesys *fs)
{
	errcode_t ret;
	int i;

	for (i = 0; i < OCFS2_RAW_SB(fs->fs_super)->s_max_slots; i++) {
		ret = ocfs2_grow_chain_allocator(fs, EXTENT_ALLOC_SYSTEM_INODE,
						 i, s->extent_alloc_size_in_clusters);
		if (ret) {
			com_err(s->progname, ret, "while growing the extent "
				"allocator for slot %d by %d clusters",
				i, s->extent_alloc_size_in_clusters);
			goto error;
		}
	}
	return;

error:
	clear_both_ends(s);
	exit(1);
}

static void finish_normal_format(State *s)
{
	errcode_t ret;
	int num;
	ocfs2_filesys *fs;

	/* These routines use libocfs2 to do their work. */

	ret = ocfs2_open(s->device_name, OCFS2_FLAG_RW, 0, 0, &fs);
	if (ret) {
		com_err(s->progname, ret,
			"while opening file system for final "
			"operations.");
		clear_both_ends(s);
		exit(1);
	}

	/* 8MB should cover an allocator and some other stuff */
	ret = io_init_cache_size(fs->fs_io, 8 * 1024 * 1024);
	if (ret)
		com_err(s->progname, ret,
			"while initializing the I/O cache.  Continuing "
			"without a cache (safe, but slower)");

	if (!s->no_backup_super) {
		if (!s->quiet)
			printf("Writing backup superblock: ");
		num = format_backup_super(s, fs);
		if (!s->quiet)
			printf("%d block(s)\n", num);
	}

	if (!s->quiet)
		printf("Formatting Journals: ");
	format_journals(s, fs);
	if (!s->quiet)
		printf("done\n");

	if (!s->quiet)
		printf("Growing extent allocator: ");
	grow_extent_allocator(s, fs);
	if (!s->quiet)
		printf("done\n");

	if (!s->quiet)
		printf("Formatting slot map: ");
	format_slotmap(s, fs);
	if (!s->quiet)
		printf("done\n");

	if (!s->quiet)
		printf("Formatting quota files: ");

	format_quota_files(s, fs);

	if (!s->quiet)
		printf("done\n");

	if (s->dx_dirs && !s->inline_data) {
		/*
		 * We want to do this after quota, but before adding
		 * any new entries to directories.
		 */
		if (!s->quiet)
			printf("Indexing system directories: ");
		index_system_dirs(s, fs);
		if (!s->quiet)
			printf("done\n");
	}

	if (!s->quiet)
		printf("Writing lost+found: ");
	create_lost_found_dir(s, fs);
	if (!s->quiet)
		printf("done\n");

	ocfs2_close(fs);
}

static inline int discard_blocks(State *s, uint64_t from,
		uint64_t count)
{
	uint64_t range[2];

	range[0] = from << s->blocksize_bits;
	range[1] = count << s->blocksize_bits;

	return ioctl(s->fd, BLKDISCARD, &range);
}

static int discard_device_blocks(State *s)
{
	uint64_t blocks = s->volume_size_in_blocks;
	uint64_t count = DISCARD_STEP_MB;
	uint64_t cur = 0;
	int retval = 0;

	count *= (1024 * 1024);
	count >>= s->blocksize_bits;

	while (cur < blocks) {
		if (cur + count > blocks)
			count = blocks - cur;

		retval = discard_blocks(s, cur, count);
		if (retval) {
			if (!s->quiet && errno != EOPNOTSUPP)
				com_err(s->progname, 0, "Discard device blocks: %s",
					strerror(errno));
			break;
		}
		cur += count;
	}

	return retval;
}

int
main(int argc, char **argv)
{
	State *s;
	SystemFileDiskRecord *record[NUM_SYSTEM_INODES];
	SystemFileDiskRecord crap_rec;
	SystemFileDiskRecord superblock_rec;
	SystemFileDiskRecord root_dir_rec;
	SystemFileDiskRecord system_dir_rec;
	int i, j, num;
	DirData *orphan_dir[OCFS2_MAX_SLOTS];
	DirData *root_dir;
	DirData *system_dir;
	uint64_t need;
	SystemFileDiskRecord *tmprec;
	char fname[SYSTEM_FILE_NAME_MAX];

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	if (signal(SIGTERM, handle_signal) == SIG_ERR) {
		fprintf(stderr, "Could not set SIGTERM\n");
		exit(1);
	}

	if (signal(SIGINT, handle_signal) == SIG_ERR) {
		fprintf(stderr, "Could not set SIGINT\n");
		exit(1);
	}

	initialize_ocfs_error_table();
	initialize_o2dl_error_table();
	initialize_o2cb_error_table();

	s = get_state(argc, argv);

	/* bail if volume already mounted on cluster, etc. */
	switch (ocfs2_check_volume(s)) {
	case -1:
		free_state(s);
		return 1;
	case 1:
		if (s->prompt) {
			fprintf(stdout, "Proceed (y/N): ");
			if (toupper(getchar()) != 'Y') {
				printf("Aborting operation.\n");
				free_state(s);
				return 1;
			}
		}
		break;
	case 0:
	default:
		break;
	}

	open_device(s);

	fill_defaults(s);

	create_generation(s);

	print_state (s);

	check_32bit_blocks(s);

	if (s->dry_run) {
		close_device(s);
		free_state(s);
		return 0;
	}

	if (s->discard_blocks)
		discard_device_blocks(s);

	clear_both_ends(s);

	init_record(s, &superblock_rec, SFI_OTHER, S_IFREG | 0644);
	init_record(s, &root_dir_rec, SFI_OTHER, S_IFDIR | 0755);
	init_record(s, &system_dir_rec, SFI_OTHER, S_IFDIR | 0755);

	for (i = 0; i < NUM_SYSTEM_INODES; i++) {
		num = system_files[i].global ? 1 : s->initial_slots;
		record[i] = do_malloc(s, sizeof(SystemFileDiskRecord) * num);

		for (j = 0; j < num; j++) {
			init_record(s, &record[i][j],
				    system_files[i].type, system_files[i].mode);
		}
	}

	root_dir = alloc_directory(s);
	system_dir = alloc_directory(s);
	for (i = 0; i < s->initial_slots; ++i)
		orphan_dir[i] = alloc_directory(s);

	need = (s->volume_size_in_clusters + 7) >> 3;
	need = ((need + s->cluster_size - 1) >> s->cluster_size_bits) << s->cluster_size_bits;

	if (!s->quiet)
		printf("Creating bitmaps: ");

	tmprec = &(record[GLOBAL_BITMAP_SYSTEM_INODE][0]);
	tmprec->extent_off = 0;
	tmprec->extent_len = need;

	s->global_bm = initialize_bitmap (s, s->volume_size_in_clusters,
					  s->cluster_size_bits,
					  "global bitmap", tmprec);

	/*
	 * Now allocate the global inode alloc group
	 */
	tmprec = &(record[GLOBAL_INODE_ALLOC_SYSTEM_INODE][0]);

	need = blocks_needed(s);
	alloc_bytes_from_bitmap(s, need << s->blocksize_bits,
                                s->global_bm,
                                &(crap_rec.extent_off),
                                &(crap_rec.extent_len));

	s->system_group =
		initialize_alloc_group(s, "system inode group", tmprec,
				       crap_rec.extent_off >> s->blocksize_bits,
				       0,
                                       crap_rec.extent_len >> s->cluster_size_bits,
				       s->cluster_size / s->blocksize);

	tmprec->group = s->system_group;
	tmprec->chain_off =
		tmprec->group->gd->bg_blkno << s->blocksize_bits;

	fsync(s->fd);
	if (!s->quiet)
		printf("done\n");

	if (!s->quiet)
		printf("Initializing superblock: ");

	superblock_rec.fe_off = (uint64_t)OCFS2_SUPER_BLOCK_BLKNO << s->blocksize_bits;

	if (!s->inline_data) {
		alloc_from_bitmap(s, 1, s->global_bm,
				  &root_dir_rec.extent_off,
				  &root_dir_rec.extent_len);
		root_dir_rec.dir_data = NULL;
	} else
		root_dir_rec.dir_data = root_dir;

	root_dir_rec.fe_off = alloc_inode(s, &root_dir_rec.suballoc_bit);
	root_dir->record = &root_dir_rec;

	add_entry_to_directory(s, root_dir, ".", root_dir_rec.fe_off, OCFS2_FT_DIR);
	add_entry_to_directory(s, root_dir, "..", root_dir_rec.fe_off, OCFS2_FT_DIR);

	need = system_dir_bytes_needed(s);
	if (!s->inline_data ||
	    need > ocfs2_max_inline_data_with_xattr(s->blocksize, NULL)) {
		need = system_dir_blocks_needed(s) << s->blocksize_bits;
		alloc_bytes_from_bitmap(s, need, s->global_bm,
					&system_dir_rec.extent_off,
					&system_dir_rec.extent_len);
		system_dir_rec.dir_data = NULL;
	} else
		system_dir_rec.dir_data = system_dir;

	system_dir_rec.fe_off = alloc_inode(s, &system_dir_rec.suballoc_bit);
	system_dir->record = &system_dir_rec;
	add_entry_to_directory(s, system_dir, ".", system_dir_rec.fe_off, OCFS2_FT_DIR);
	add_entry_to_directory(s, system_dir, "..", system_dir_rec.fe_off, OCFS2_FT_DIR);

	for (i = 0; i < NUM_SYSTEM_INODES; i++) {
		if (hb_dev_skip(s, i))
			continue;
		if (feature_skip(s, i))
			continue;

		num = (system_files[i].global) ? 1 : s->initial_slots;
		for (j = 0; j < num; j++) {
			record[i][j].fe_off = alloc_inode(s, &(record[i][j].suballoc_bit));
			sprintf(fname, system_files[i].name, j);
			add_entry_to_directory(s, system_dir, fname,
					       record[i][j].fe_off,
					       S_ISDIR(system_files[i].mode) ? OCFS2_FT_DIR
							           : OCFS2_FT_REG_FILE);
		}
	}

	/* back when we initialized the alloc group we hadn't allocated
	 * an inode for the global allocator yet */
	tmprec = &(record[GLOBAL_INODE_ALLOC_SYSTEM_INODE][0]);
	s->system_group->gd->bg_parent_dinode =
		tmprec->fe_off >> s->blocksize_bits;

	tmprec = &(record[HEARTBEAT_SYSTEM_INODE][0]);
	need = (O2NM_MAX_NODES + 1) << s->blocksize_bits;

	alloc_bytes_from_bitmap(s, need, s->global_bm, &tmprec->extent_off, &tmprec->extent_len);
	tmprec->file_size = need;

	if (!hb_dev_skip(s, ORPHAN_DIR_SYSTEM_INODE)) {
		for (i = 0; i < s->initial_slots; ++i) {
			tmprec = &record[ORPHAN_DIR_SYSTEM_INODE][i];
			orphan_dir[i]->record = tmprec;
			if (!s->inline_data) {
				alloc_from_bitmap(s, 1, s->global_bm,
						  &tmprec->extent_off,
						  &tmprec->extent_len);
				tmprec->dir_data = NULL;
			} else
				tmprec->dir_data = orphan_dir[i];

			add_entry_to_directory(s, orphan_dir[i], ".",
					       tmprec->fe_off,
					       OCFS2_FT_DIR);
			add_entry_to_directory(s, orphan_dir[i], "..",
					       system_dir_rec.fe_off,
					       OCFS2_FT_DIR);
		}
	}

	fsync(s->fd);
	if (!s->quiet)
		printf("done\n");

	if (!s->quiet)
		printf("Writing system files: ");

	format_file(s, &root_dir_rec);
	format_file(s, &system_dir_rec);

	for (i = 0; i < NUM_SYSTEM_INODES; i++) {
		if (hb_dev_skip(s, i))
			continue;
		if (feature_skip(s, i))
			continue;

		num = system_files[i].global ? 1 : s->initial_slots;
		for (j = 0; j < num; j++) {
			tmprec = &(record[i][j]);
			format_file(s, tmprec);
		}
	}

	/* OHMYGODTHISISTHEWORSTCODEEVER: We write out the bitmap here
	 * *again* because we did a bunch of allocs above after our
	 * initial write-out. */
	tmprec = &(record[GLOBAL_BITMAP_SYSTEM_INODE][0]);
	format_file(s, tmprec);

	write_bitmap_data(s, s->global_bm);

	write_group_data(s, s->system_group);

	write_directory_data(s, root_dir);
	write_directory_data(s, system_dir);

	if (!hb_dev_skip(s, ORPHAN_DIR_SYSTEM_INODE)) {
		for (i = 0; i < s->initial_slots; ++i)
			write_directory_data(s, orphan_dir[i]);
	}

	tmprec = &(record[HEARTBEAT_SYSTEM_INODE][0]);
	write_metadata(s, tmprec, NULL);

	fsync(s->fd);
	if (!s->quiet)
		printf("done\n");

	if (!s->quiet)
		printf("Writing superblock: ");

	block_signals(SIG_BLOCK);
	format_leading_space(s);
	format_superblock(s, &superblock_rec, &root_dir_rec, &system_dir_rec);
	block_signals(SIG_UNBLOCK);

	if (!s->quiet)
		printf("done\n");

	if (!s->hb_dev)
		finish_normal_format(s);

	close_device(s);

	if (!s->quiet)
		printf("%s successful\n\n", s->progname);

	free_directory(root_dir);
	free_directory(system_dir);
	for (i = 0; i < s->initial_slots; i++)
		free_directory(orphan_dir[i]);

	for (i = 0; i < NUM_SYSTEM_INODES; i++)
		ocfs2_free(&record[i]);

	free_state(s);

	return 0;
}

static void
parse_fs_type_opts(char *progname, const char *typestr,
		   enum ocfs2_mkfs_types *fs_type)
{
	int i;

	*fs_type = OCFS2_MKFSTYPE_DEFAULT;

	for(i = 0; ocfs2_mkfs_types_table[i].ft_str; i++) {
		if (strcmp(typestr, ocfs2_mkfs_types_table[i].ft_str) == 0) {
			*fs_type = ocfs2_mkfs_types_table[i].ft_type;
			break;
		}
	}

	if (*fs_type == OCFS2_MKFSTYPE_DEFAULT) {
		com_err(progname, 0, "Bad fs type option specified.");
		exit(1);
	}
}

static State *
get_state(int argc, char **argv)
{
	char *progname;
	unsigned int blocksize = 0;
	unsigned int cluster_size = 0;
	char *vol_label = NULL;
	char *stack_name = NULL;
	char *cluster_name = NULL;
	int globalhb = 0;
	unsigned int initial_slots = 0;
	char *dummy;
	State *s;
	int c;
	int verbose = 0, quiet = 0, force = 0, xtool = 0, hb_dev = 0;
	int show_version = 0, dry_run = 0;
	char *device_name;
	char *uuid = NULL, uuid_36[37] = {'\0'}, *uuid_p;
	int ret;
	uint64_t val;
	uint64_t journal_size_in_bytes = 0;
	int journal64 = 0;
	enum ocfs2_mkfs_types fs_type = OCFS2_MKFSTYPE_DEFAULT;
	int mount = -1;
	int no_backup_super = -1;
	enum ocfs2_feature_levels level = OCFS2_FEATURE_LEVEL_DEFAULT;
	ocfs2_fs_options feature_flags = {0,0,0}, reverse_flags = {0,0,0};
	int discard_blocks = 1;

	static struct option long_options[] = {
		{ "block-size", 1, 0, 'b' },
		{ "cluster-size", 1, 0, 'C' },
		{ "label", 1, 0, 'L' },
		{ "node-slots", 1, 0, 'N' },
		{ "verbose", 0, 0, 'v' },
		{ "quiet", 0, 0, 'q' },
		{ "version", 0, 0, 'V' },
		{ "journal-options", 1, 0, 'J'},
		{ "heartbeat-device", 0, 0, 'H'},
		{ "force", 0, 0, 'F'},
		{ "mount", 1, 0, 'M'},
		{ "dry-run", 0, 0, 'n' },
		{ "nodiscard", 0, 0, 'o'},
		{ "discard", 0, 0, 'O'},
		{ "no-backup-super", 0, 0, BACKUP_SUPER_OPTION },
		{ "fs-feature-level=", 1, 0, FEATURE_LEVEL },
		{ "fs-features=", 1, 0, FEATURES_OPTION },
		{ "cluster-stack=", 1, 0, CLUSTER_STACK_OPTION },
		{ "cluster-name=", 1, 0, CLUSTER_NAME_OPTION },
		{ "global-heartbeat", 0, 0, GLOBAL_HEARTBEAT_OPTION },
		{ 0, 0, 0, 0}
	};

	if (argc && *argv)
		progname = basename(argv[0]);
	else
		progname = "mkfs.ocfs2";

	while (1) {
		c = getopt_long(argc, argv, "b:C:L:N:J:M:vnqVFHxT:U:",
				long_options, NULL);

		if (c == -1)
			break;

		switch (c) {
		case 'b':
			ret = get_number(optarg, &val);

			if (ret ||
			    val < OCFS2_MIN_BLOCKSIZE ||
			    val > OCFS2_MAX_BLOCKSIZE) {
				com_err(progname, 0,
					"Specify a blocksize between %d and %d "
					"in powers of 2", OCFS2_MIN_BLOCKSIZE,
					OCFS2_MAX_BLOCKSIZE);
				exit(1);
			}

			blocksize = (unsigned int)
					get_valid_size(val, OCFS2_MIN_BLOCKSIZE,
						       OCFS2_MAX_BLOCKSIZE);
			break;

		case 'C':
			ret = get_number(optarg, &val);

			if (ret ||
			    val < OCFS2_MIN_CLUSTERSIZE ||
			    val > OCFS2_MAX_CLUSTERSIZE) {
				com_err(progname, 0,
					"Specify a clustersize between %d and "
					"%d in powers of 2", OCFS2_MIN_CLUSTERSIZE,
					OCFS2_MAX_CLUSTERSIZE);
				exit(1);
			}

			cluster_size = (unsigned int)
					get_valid_size(val, OCFS2_MIN_CLUSTERSIZE,
						       OCFS2_MAX_CLUSTERSIZE);
			break;

		case 'L':
			vol_label = strdup(optarg);

			if (strlen(vol_label) >= OCFS2_MAX_VOL_LABEL_LEN) {
				com_err(progname, 0,
					"Volume label too long: must be less "
					"than %d characters",
					OCFS2_MAX_VOL_LABEL_LEN);
				exit(1);
			}

			break;

		case 'M':
			if (!strncasecmp(optarg, MOUNT_LOCAL_STR,
					 strlen(MOUNT_LOCAL_STR)))
				mount = MOUNT_LOCAL;
			else if (!strncasecmp(optarg, MOUNT_CLUSTER_STR,
					      strlen(MOUNT_CLUSTER_STR)))
				mount = MOUNT_CLUSTER;
			else {
				com_err(progname, 0,
					"Invalid mount type %s", optarg);
				exit(1);
			}
			break;

		case 'N':
			initial_slots = strtoul(optarg, &dummy, 0);

			if (initial_slots > OCFS2_MAX_SLOTS || *dummy != '\0') {
				com_err(progname, 0,
					"Initial node slots must be no more "
					"than %d",
					OCFS2_MAX_SLOTS);
				exit(1);
			} else if (initial_slots < 1) {
				com_err(progname, 0,
					"Initial node slots must be at "
					"least 1");
				exit(1);
			}

			break;

		case 'J':
			parse_journal_opts(progname, optarg,
					   &journal_size_in_bytes,
					   &journal64);
			break;

		case 'U':
			uuid = strdup(optarg);
			break;

		case 'H':
			hb_dev = 1;
			break;

		case 'v':
			verbose = 1;
			break;

		case 'n':
			dry_run = 1;
			break;

		case 'q':
			quiet = 1;
			break;

		case 'V':
			show_version = 1;
			break;

		case 'F':
			force = 1;
			break;

		case 'x':
			xtool = 1;
			break;

		case 'T':
			parse_fs_type_opts(progname, optarg, &fs_type);
			break;

		case BACKUP_SUPER_OPTION:
			no_backup_super = 1;
			break;

		case FEATURE_LEVEL:
			ret = ocfs2_parse_feature_level(optarg, &level);
			if (ret) {
				com_err(progname, ret,
					"when parsing fs-feature-level string");
				exit(1);
			}
			break;

		case FEATURES_OPTION:
			ret = ocfs2_parse_feature(optarg,
						  &feature_flags,
						  &reverse_flags);
			if (ret) {
				com_err(progname, ret,
					"when parsing fs-features string");
				exit(1);
			}
			break;

		case CLUSTER_STACK_OPTION:
			if (!optarg || !strlen(optarg)) {
				com_err(progname, 0,
					"Option --cluster-stack requires an argument");
				exit(1);
			}
			if (!o2cb_valid_stack_name(optarg)) {
				com_err(progname, O2CB_ET_INVALID_STACK_NAME,
					"; unknown cluster stack '%s'", optarg);
				exit(1);
			}
			if (stack_name)
				free(stack_name);
			stack_name = strdup(optarg);
			break;

		case CLUSTER_NAME_OPTION:
			if (!optarg || !strlen(optarg)) {
				com_err(progname, 0,
					"Option --cluster-name requires an argument");
				exit(1);
			}
			if (cluster_name)
				free(cluster_name);
			cluster_name = strdup(optarg);
			break;

		case GLOBAL_HEARTBEAT_OPTION:
			globalhb = 1;
			break;

		case 'O':
			discard_blocks = 1;
			break;

		case 'o':
			discard_blocks = 0;
			break;

		default:
			usage(progname);
			break;
		}
	}

	if ((optind == argc) && !show_version)
		usage(progname);

	srand48(time(NULL));

	device_name = argv[optind];
	optind++;

	s = malloc(sizeof(State));
	memset(s, 0, sizeof(State));

	if (optind < argc) {
		s->specified_size_in_blocks = strtoull(argv[optind], &dummy,
						       0);
		if ((*dummy)) {
			com_err(progname, 0, "Block count bad - %s",
				argv[optind]);
			exit(1);
		}

		optind++;
	}

	if (optind < argc)
		usage(progname);

	if (!quiet || show_version)
		version(progname);

	if (show_version)
		exit(0);


	s->progname      = progname;

	s->verbose       = verbose;
	s->quiet         = quiet;
	s->force         = force;
	s->dry_run       = dry_run;
	s->discard_blocks = discard_blocks;

	s->prompt        = xtool ? 0 : 1;

	s->blocksize     = blocksize;
	s->cluster_size  = cluster_size;
	s->vol_label     = vol_label;
	s->initial_slots = initial_slots;

	s->device_name   = strdup(device_name);

	s->fd            = -1;

	s->format_time   = time(NULL);

	s->journal_size_in_bytes = journal_size_in_bytes;
	s->journal64 = journal64;

	s->hb_dev = hb_dev;

	s->fs_type = fs_type;

	ret = ocfs2_merge_feature_flags_with_level(&s->feature_flags,
						   fs_type,
						   level,
						   &feature_flags,
						   &reverse_flags);
	if (ret) {
		com_err(s->progname, ret,
			"while reconciling specified features with chosen "
			"defaults");
		exit(1);
	}

	if (s->feature_flags.opt_incompat & OCFS2_FEATURE_INCOMPAT_LOCAL_MOUNT)
		s->mount = MOUNT_LOCAL;
	else
		s->mount = MOUNT_CLUSTER;
 	if (s->feature_flags.opt_incompat & OCFS2_FEATURE_INCOMPAT_INLINE_DATA)
 		s->inline_data = 1;
 	else
 		s->inline_data = 0;
	if (s->feature_flags.opt_compat & OCFS2_FEATURE_COMPAT_BACKUP_SB)
		s->no_backup_super = 0;
	else
		s->no_backup_super = 1;
	if (s->feature_flags.opt_incompat & OCFS2_FEATURE_INCOMPAT_INDEXED_DIRS)
		s->dx_dirs = 1;
	else
		s->dx_dirs = 0;

	/* uuid */
	if (!uuid)
		uuid_generate(s->uuid);
	else {
		if (strlen(uuid) == 32) {
			translate_uuid(uuid, uuid_36);
			uuid_p = uuid_36;
		} else
			uuid_p = uuid;

		/*uuid_parse only support 36 bytes uuid*/
		if (uuid_parse(uuid_p, s->uuid)) {
			com_err(s->progname, 0, "Invalid UUID specified");
			exit(1);
		}
		printf("\nWARNING!!! OCFS2 uses the UUID to uniquely identify "
		       "a file system.\nHaving two OCFS2 file systems with "
		       "the same UUID could, in the least,\ncause erratic "
		       "behavior, and if unlucky, cause file system damage.\n"
		       "Please choose the UUID with care.\n\n");
		free(uuid);
	}

	/* Here if the user set these flags explicitly, we will use them and
	 * discard the setting in the features set.
	 */
	if (mount != -1)
		s->mount = mount;

	if (!is_cluster_info_valid(s, stack_name, cluster_name, globalhb))
		exit(1);

	s->cluster_stack = stack_name;
	s->cluster_name = cluster_name;
	if (globalhb)
		s->stack_flags |= OCFS2_CLUSTER_O2CB_GLOBAL_HEARTBEAT;
	s->global_heartbeat = globalhb;

	if (no_backup_super != -1)
		s->no_backup_super = no_backup_super;

	return s;
}

static void
free_state(State *s)
{
	int i;

	ocfs2_free(&s->vol_label);
	ocfs2_free(&s->device_name);
	ocfs2_free(&s->cluster_stack);
	ocfs2_free(&s->cluster_name);

	if (s->global_bm) {
		for (i = 0; i < s->nr_cluster_groups; i++)
			free_alloc_group(s->global_bm->groups[i]);
		ocfs2_free(&s->global_bm->groups);
		ocfs2_free(&s->global_bm->name);
		ocfs2_free(&s->global_bm);
	}

	free_alloc_group(s->system_group);
	ocfs2_free(&s);
}

static int
get_number(char *arg, uint64_t *res)
{
	char *ptr = NULL;
	uint64_t num;

	num = strtoull(arg, &ptr, 0);

	if ((ptr == arg) || (num == UINT64_MAX))
		return(-EINVAL);

	switch (*ptr) {
	case '\0':
		break;

	case 'g':
	case 'G':
		num *= 1024;
		/* FALL THROUGH */

	case 'm':
	case 'M':
		num *= 1024;
		/* FALL THROUGH */

	case 'k':
	case 'K':
		num *= 1024;
		/* FALL THROUGH */

	case 'b':
	case 'B':
		break;

	default:
		return -EINVAL;
	}

	*res = num;

	return 0;
}

/* derived from e2fsprogs */
static void
parse_journal_opts(char *progname, const char *opts,
		   uint64_t *journal_size_in_bytes, int *journal64)
{
	char *options, *token, *next, *p, *arg;
	int ret, journal_usage = 0;
	uint64_t val;
	int invert;

	options = strdup(opts);

	for (token = options; token && *token; token = next) {
		p = strchr(token, ',');
		next = NULL;
		invert = 0;

		if (p) {
			*p = '\0';
			next = p + 1;
		}

		arg = strstr(token, "no");
		if (arg == token) {
			invert = 1;
			token += strlen("no");
		}

		arg = strchr(token, '=');

		if (arg) {
			*arg = '\0';
			arg++;
		}

		if (strcmp(token, "size") == 0) {
			if (!arg || invert) {
				journal_usage++;
				continue;
			}

			ret = get_number(arg, &val);

			if (ret ||
			    val < OCFS2_MIN_JOURNAL_SIZE) {
				com_err(progname, 0,
					"Invalid journal size: %s\nSize must "
					"be greater than %d bytes",
					arg, OCFS2_MIN_JOURNAL_SIZE);
				exit(1);
			}

			*journal_size_in_bytes = val;
		} else if (strcmp(token, "block32") == 0) {
			if (arg) {
				journal_usage++;
				continue;
			}
			*journal64 = invert;
		} else if (strcmp(token, "block64") == 0) {
			if (arg) {
				journal_usage++;
				continue;
			}
			*journal64 = !invert;
		} else
			journal_usage++;
	}

	if (journal_usage) {
		com_err(progname, 0,
			"Bad journal options specified. Valid journal "
			"options are:\n"
			"\tsize=<journal size>\n"
			"\t[no]block32\n"
			"\t[no]block64\n");
		exit(1);
	}

	free(options);
}

static void
usage(const char *progname)
{
	fprintf(stderr, "usage: %s [-b block-size] [-C cluster-size] "
		"[-J journal-options]\n\t\t[-L volume-label] [-M mount-type] "
		"[-N number-of-node-slots]\n\t\t[-T filesystem-type] [-U uuid]"
		"[-HFnqvV] [--dry-run]"
		"\n\t\t[--fs-feature-level=[default|max-compat|max-features]] "
		"\n\t\t[--fs-features=[[no]sparse,...]] [--global-heartbeat]"
		"\n\t\t[--cluster-stack=stackname] [--cluster-name=clustername]"
		"\n\t\t[--no-backup-super] device [blocks-count]\n", progname);
	exit(1);
}

static void
version(const char *progname)
{
	fprintf(stderr, "%s %s\n", progname, VERSION);
}

static unsigned int journal_size_default(State *s)
{
	unsigned int j_blocks;

	if (s->volume_size_in_blocks < 32768)
		j_blocks = OCFS2_MIN_JOURNAL_SIZE / s->blocksize;
	else if (s->volume_size_in_blocks < 262144)
		j_blocks = 4096;
	else {
		/* Each journal gets ~.625% of the blocks in the file
		 * system, with a min of 16384 and a max of 65536 */
		j_blocks = s->volume_size_in_blocks / 160;
		if (j_blocks < 16384)
			j_blocks = 16384;
		else if (j_blocks > 65536)
			j_blocks = 65536;
	}
	return j_blocks;
}

static unsigned int journal_size_datafiles(void)
{
	return 8192;
}

static unsigned int journal_size_mail(State *s)
{
	if (s->volume_size_in_blocks < 262144)
		return 16384;
	else if (s->volume_size_in_blocks < 524288)
		return 32768;
	return 65536;
}

static unsigned int journal_size_vmstore(State *s)
{
	if (s->volume_size_in_blocks < 262144)
		return 8192;
	else if (s->volume_size_in_blocks < 524288)
		return 16384;
	return 32768;
}

static int journal_size_valid(unsigned int j_blocks, State *s)
{
	return (j_blocks * s->initial_slots + 1024) <=
		s->volume_size_in_blocks;
}

/* For operations such as mkdir that can require more than a cluster worth
 * of journal credits, journal size should be greater than cluster size * 8.
 * The kernel allows the maximum transaction buffer to be 1\4 th of the
 * journal size and this is further divided by 2 for transaction
 * reservation support. We calculate minimum journal size here
 * accordingly and and ceil w.r.t to the cluster size.*/
static unsigned int journal_min_size(uint32_t cluster_size)
{
	return (cluster_size << OCFS2_MIN_CLUSTER_TO_JOURNAL_SIZE_SHIFT)
		+ cluster_size;
}

/* stolen from e2fsprogs, modified to fit ocfs2 patterns */
static uint64_t figure_journal_size(uint64_t size, State *s)
{
	unsigned int j_blocks;
	uint64_t ret;
	unsigned int min_journal_size;

	if (s->hb_dev)
		return 0;

	if (s->volume_size_in_blocks < 2048) {
		fprintf(stderr,	"Filesystem too small for a journal\n");
		exit(1);
	}

	min_journal_size = journal_min_size(s->cluster_size);
	if (size > 0) {
		j_blocks = size >> s->blocksize_bits;
		/* mke2fs knows about free blocks at this point, but
		 * we don't so lets just take a wild guess as to what
		 * the fs overhead we're looking at will be. */
		if (!journal_size_valid(j_blocks, s)) {
			fprintf(stderr, 
				"Journal size too big for filesystem.\n");
			exit(1);
		}

		ret = align_bytes_to_clusters_ceil(s, size);
		/* It is better to fail mkfs than to create a non-functional
		 * filesystem.*/
		if (ret < min_journal_size) {
			fprintf(stderr,
				"Journal size too small for filesystem.\n");
			exit(1);
		}
		return ret;
	}

	switch (s->fs_type) {
	case OCFS2_MKFSTYPE_DATAFILES:
		j_blocks = journal_size_datafiles();
		break;
	case OCFS2_MKFSTYPE_MAIL:
		j_blocks = journal_size_mail(s);
		break;
	case OCFS2_MKFSTYPE_VMSTORE:
		j_blocks = journal_size_vmstore(s);
		break;
	default:
		j_blocks = journal_size_default(s);
		break;
	}

	ret = align_bytes_to_clusters_ceil(s, j_blocks << s->blocksize_bits);
	/* If the default journal size is less than the minimum required
	 * size, set the default to the minimum size. Then fail if
	 * the journal size is not valid*/
	if (ret < min_journal_size) {
		ret = min_journal_size;
		j_blocks = ret >> s->blocksize_bits;
		if (!journal_size_valid(j_blocks, s)) {
			fprintf(stderr,
				"Volume size too small for required "
				"configuration.\nIncrease volume size or "
				"reduce cluster size\n");
			exit(1);
		}
	}
	return ret;
}

static uint32_t cluster_size_default(State *s)
{
	uint32_t cluster_size, cluster_size_bits;
	uint64_t volume_size;

	for (cluster_size = OCFS2_MIN_CLUSTERSIZE;
	     cluster_size < OCFS2_MAX_CLUSTERSIZE;
	     cluster_size <<= 1) {
		cluster_size_bits = get_bits(s, cluster_size);

		volume_size =
			s->volume_size_in_bytes >> cluster_size_bits;

		if (volume_size <= CLUSTERS_MAX)
			break;
	}

	return cluster_size;
}

static uint32_t cluster_size_datafiles(State *s)
{
	uint32_t cluster_size;
	uint64_t volume_gigs = s->volume_size_in_bytes / (1024 * 1024 * 1024);

	if (volume_gigs < 2) {
		com_err(s->progname, 0,
			"Selected file system type requires a device of at "
			"least 2 gigabytes\n");
		exit(1);
	}

	if (volume_gigs < 64)
		cluster_size = 128;
	else if (volume_gigs < 96)
		cluster_size = 256;
	else if (volume_gigs < 128)
		cluster_size = 512;
	else
		cluster_size = 1024;

	return cluster_size * 1024;
}

static uint32_t figure_extent_alloc_size(State *s)
{
	uint32_t cpg;
	int numgroups;
	uint64_t unitsize, totalsize;
	double curr_percent, target_percent;

	if (!s->initial_slots)
		return 0;

	switch (s->fs_type) {
	case OCFS2_MKFSTYPE_DATAFILES:
	case OCFS2_MKFSTYPE_VMSTORE:
		target_percent = 0.3;
		break;
	case OCFS2_MKFSTYPE_MAIL:
	default:
		target_percent = 0.1;
		break;
	}

	cpg = ocfs2_clusters_per_group(s->blocksize, s->cluster_size_bits);

	/* size of the allocator across all slots with one group */
	unitsize = cpg * s->cluster_size * s->initial_slots;

	totalsize = unitsize;
	for (numgroups = 1; ; ++numgroups) {
		curr_percent = (double)totalsize * 100 / s->volume_size_in_bytes;
		if (curr_percent >= target_percent)
			break;
		totalsize += unitsize;
	}

	if (curr_percent > MAX_EXTALLOC_RESERVE_PERCENT)
		--numgroups;

	assert(numgroups >= 0);

	return cpg * numgroups;
}

static void
fill_defaults(State *s)
{
	size_t pagesize;
	errcode_t err;
	uint32_t blocksize;
	int sectsize;
	uint64_t ret;
	struct ocfs2_cluster_group_sizes cgs;
	uint64_t tmp;

	pagesize = getpagesize();

	s->pagesize_bits = get_bits(s, pagesize);

	err = ocfs2_get_device_sectsize(s->device_name, &sectsize);
	if (err) {
		if (err == OCFS2_ET_CANNOT_DETERMINE_SECTOR_SIZE)
			sectsize = 0;
		else {
			com_err(s->progname, err,
				"while getting hardware sector size of "
				"device %s", s->device_name);
			exit(1);
		}
	}
	if (!sectsize)
		sectsize = OCFS2_MIN_BLOCKSIZE;

	/* Heartbeat devices use the minimum size, unless specified */
	if (!s->blocksize && s->hb_dev)
		s->blocksize = sectsize;

	if (s->blocksize)
		blocksize = s->blocksize;
	else
		blocksize = OCFS2_MAX_BLOCKSIZE;

	if (blocksize < sectsize) {
		com_err(s->progname, 0,
			"the block device %s has a hardware sector size (%d) "
			"that is larger than the selected block size (%u)",
			s->device_name, sectsize, blocksize);
		exit(1);
	}

	if (!s->volume_size_in_blocks) {
		err = ocfs2_get_device_size(s->device_name, blocksize, &ret);
		if (err) {
			com_err(s->progname, err,
				"while getting size of device %s",
				s->device_name);
			exit(1);
		}
		
		if (s->hb_dev) {
			uint64_t dev_size = 0;

			if ((ret * blocksize) > (2 * 1024 * 1024)) {
				fprintf(stderr,
					"%s: Warning: Volume larger than required for a heartbeat device\n",
					s->progname);
			}

			/* Blocks for system dir, root dir,
			 * global allocator*/
			dev_size = 4;
			/* Blocks for hb region */
			dev_size += OCFS2_MAX_SLOTS;
			/* Slop for superblock + cluster bitmap */
			dev_size += 10;

			/* Convert to bytes */
			dev_size *= blocksize;

			/* Convert to megabytes */
			dev_size = (dev_size + (1024 * 1024) - 1) >> ONE_MB_SHIFT;
			dev_size <<= ONE_MB_SHIFT;

			dev_size /= blocksize;

			if (ret > dev_size)
				ret = dev_size;
		}

		s->volume_size_in_blocks = ret;
		if (s->specified_size_in_blocks) {
			if (s->specified_size_in_blocks > 
			    s->volume_size_in_blocks) {
				com_err(s->progname, 0,
					"%"PRIu64" blocks were specified and "
					"this is greater than the %"PRIu64" "
					"blocks that make up %s.\n", 
					s->specified_size_in_blocks,
					s->volume_size_in_blocks, 
					s->device_name);
				exit(1);
			}
			s->volume_size_in_blocks = s->specified_size_in_blocks;
		}
	}

	s->volume_size_in_bytes = s->volume_size_in_blocks * blocksize;

	if (!s->blocksize) {
		if (s->volume_size_in_bytes <= 1024 * 1024 * 3) {
			s->blocksize = OCFS2_MIN_BLOCKSIZE;
		} else {
			int shift = 30;

			s->blocksize = OCFS2_MAX_BLOCKSIZE;

			while (s->blocksize > 1024) {
				if (s->volume_size_in_bytes >= 1U << shift)
					break;
				s->blocksize >>= 1;
				shift--;
			}
		}

		if (!s->specified_size_in_blocks) {
			err = ocfs2_get_device_size(s->device_name,
						    s->blocksize, &ret);
			s->volume_size_in_blocks = ret;
		} else
			s->volume_size_in_blocks = s->specified_size_in_blocks;

		s->volume_size_in_bytes =
			s->volume_size_in_blocks * s->blocksize;
	}

	s->blocksize_bits = get_bits(s, s->blocksize);

	if (!s->cluster_size) {
		switch (s->fs_type) {
		case OCFS2_MKFSTYPE_DATAFILES:
		case OCFS2_MKFSTYPE_VMSTORE:
			s->cluster_size = cluster_size_datafiles(s);
			break;
		default:
			s->cluster_size = cluster_size_default(s);
			break;
		}
	}

	s->cluster_size_bits = get_bits(s, s->cluster_size);

	/* volume size needs to be cluster aligned */
	s->volume_size_in_clusters = s->volume_size_in_bytes >> s->cluster_size_bits;
	tmp = (uint64_t)s->volume_size_in_clusters;
	s->volume_size_in_bytes = tmp << s->cluster_size_bits;
	s->volume_size_in_blocks = s->volume_size_in_bytes >> s->blocksize_bits;
	
	s->reserved_tail_size = 0;

	ocfs2_calc_cluster_groups(s->volume_size_in_clusters, s->blocksize,
				  &cgs);
	s->global_cpg = cgs.cgs_cpg;
	s->nr_cluster_groups = cgs.cgs_cluster_groups;
	s->tail_group_bits = cgs.cgs_tail_group_bits;

#if 0
	printf("volume_size_in_clusters = %u\n", s->volume_size_in_clusters);
	printf("global_cpg = %u\n", s->global_cpg);
	printf("nr_cluster_groups = %u\n", s->nr_cluster_groups);
	printf("tail_group_bits = %u\n", s->tail_group_bits);
#endif

	if (s->hb_dev)
		s->initial_slots = 0;

	if (!s->hb_dev && !s->initial_slots) {
		if (s->mount == MOUNT_LOCAL)
			s->initial_slots = 1;
		else
			s->initial_slots =
				initial_slots_for_volume(s->volume_size_in_bytes);
	}

	if (!s->vol_label) {
		  s->vol_label = strdup("");
	}

	s->journal_size_in_bytes = figure_journal_size(s->journal_size_in_bytes, s);

	s->extent_alloc_size_in_clusters = figure_extent_alloc_size(s);
}

static int
get_bits(State *s, int num)
{
	int i, bits = 0;

	for (i = 32; i >= 0; i--) {
		if (num == (1U << i))
			bits = i;
	}

	if (bits == 0) {
		com_err(s->progname, 0,
			"Could not get bits for number %d", num);
		exit(1);
	}

	return bits;
}

static uint64_t
get_valid_size(uint64_t num, uint64_t lo, uint64_t hi)
{
	uint64_t tmp = lo;

	for ( ; lo <= hi; lo <<= 1) {
		if (lo == num)
			return num;

		if (lo < num)
			tmp = lo;
		else
			break;
	}

	return tmp;
}

static void *
do_malloc(State *s, size_t size)
{
	void *buf;
	int ret;

	ret = posix_memalign(&buf, OCFS2_MAX_BLOCKSIZE, size);

	if (ret != 0) {
		com_err(s->progname, 0,
			"Could not allocate %lu bytes of memory",
			(unsigned long)size);
		exit(1);
	}

	return buf;
}

static void
do_pwrite(State *s, const void *buf, size_t count, uint64_t offset)
{
	ssize_t ret;

	ret = pwrite64(s->fd, buf, count, offset);

	if (ret == -1) {
		com_err(s->progname, 0, "Could not write: %s",
			strerror(errno));
		exit(1);
	}
}

static AllocGroup *
initialize_alloc_group(State *s, const char *name,
		       SystemFileDiskRecord *alloc_inode,
		       uint64_t blkno, uint16_t chain,
		       uint16_t cpg, uint16_t bpc)
{
	AllocGroup *group;

	group = do_malloc(s, sizeof(AllocGroup));
	memset(group, 0, sizeof(AllocGroup));

	group->gd = do_malloc(s, s->blocksize);
	memset(group->gd, 0, s->blocksize);

	strcpy((char *)group->gd->bg_signature, OCFS2_GROUP_DESC_SIGNATURE);
	group->gd->bg_generation = s->vol_generation;
	group->gd->bg_size =
			(uint32_t)ocfs2_group_bitmap_size(s->blocksize, 0, 0);
	group->gd->bg_bits = cpg * bpc;
	group->gd->bg_chain = chain;
	group->gd->bg_parent_dinode = alloc_inode->fe_off >> 
					s->blocksize_bits;
	group->gd->bg_blkno = blkno;

	/* First bit set to account for the descriptor block */
	ocfs2_set_bit(0, group->gd->bg_bitmap);
	group->gd->bg_free_bits_count = group->gd->bg_bits - 1;

	alloc_inode->bi.total_bits += group->gd->bg_bits;
	alloc_inode->bi.used_bits++;
	group->alloc_inode = alloc_inode;

	group->name = strdup(name);

	return group;
}

static void
free_alloc_group(AllocGroup *group)
{
	if (group) {
		ocfs2_free(&group->name);
		ocfs2_free(&group->gd);
		ocfs2_free(&group);
	}
}

static AllocBitmap *
initialize_bitmap(State *s, uint32_t bits, uint32_t unit_bits,
		  const char *name, SystemFileDiskRecord *bm_record)
{
	AllocBitmap *bitmap;
	uint64_t blkno;
	int i, j, cpg, chain, c_to_b_bits;
	int recs_per_inode = ocfs2_chain_recs_per_inode(s->blocksize);
	int wrapped = 0;

	bitmap = do_malloc(s, sizeof(AllocBitmap));
	memset(bitmap, 0, sizeof(AllocBitmap));

	bitmap->valid_bits = bits;
	bitmap->unit_bits = unit_bits;
	bitmap->unit = 1 << unit_bits;
	bitmap->name = strdup(name);

	bm_record->file_size = s->volume_size_in_bytes;
	bm_record->fe_off = 0ULL;

	bm_record->bi.used_bits = 0;

	/* this will be set as we add groups. */
	bm_record->bi.total_bits = 0;

	bm_record->bitmap = bitmap;

	bitmap->bm_record = bm_record;

	bitmap->groups = do_malloc(s, s->nr_cluster_groups * 
				   sizeof(AllocGroup *));
	memset(bitmap->groups, 0, s->nr_cluster_groups * 
	       sizeof(AllocGroup *));

	c_to_b_bits = s->cluster_size_bits - s->blocksize_bits;

	/* to the next aligned cluster */
	s->first_cluster_group = (OCFS2_SUPER_BLOCK_BLKNO + 1);
	s->first_cluster_group += ((1 << c_to_b_bits) - 1);
	s->first_cluster_group >>= c_to_b_bits;

	s->first_cluster_group_blkno = (uint64_t)s->first_cluster_group << c_to_b_bits;
	bitmap->groups[0] = initialize_alloc_group(s, "stupid", bm_record,
						   s->first_cluster_group_blkno,
						   0, s->global_cpg, 1);
	/* The first bit is set by initialize_alloc_group, hence
	 * we start at 1.  For this group (which contains the clusters
	 * containing the superblock and first group descriptor), we
	 * have to set these by hand. */
	for (i = 1; i <= s->first_cluster_group; i++) {
		ocfs2_set_bit(i, bitmap->groups[0]->gd->bg_bitmap);
		bitmap->groups[0]->gd->bg_free_bits_count--;
		bm_record->bi.used_bits++;
	}
	bitmap->groups[0]->chain_total = s->global_cpg;
	bitmap->groups[0]->chain_free = 
		bitmap->groups[0]->gd->bg_free_bits_count;

	chain = 1;
	blkno = (uint64_t) s->global_cpg << (s->cluster_size_bits - s->blocksize_bits);
	cpg = s->global_cpg;
	for(i = 1; i < s->nr_cluster_groups; i++) {
		if (i == (s->nr_cluster_groups - 1))
			cpg = s->tail_group_bits;
		bitmap->groups[i] = initialize_alloc_group(s, "stupid",
							   bm_record, blkno,
							   chain, cpg, 1);
		if (wrapped) {
			/* link the previous group to this guy. */
			j = i - recs_per_inode;
			bitmap->groups[j]->gd->bg_next_group = blkno;
			bitmap->groups[j]->next = bitmap->groups[i];
		}

		bitmap->groups[chain]->chain_total += 
			bitmap->groups[i]->gd->bg_bits;
		bitmap->groups[chain]->chain_free += 
			bitmap->groups[i]->gd->bg_free_bits_count;

		blkno += (uint64_t) s->global_cpg << (s->cluster_size_bits - s->blocksize_bits);
		chain++;
		if (chain >= recs_per_inode) {
			chain = 0;
			wrapped = 1;
		}
	}
	if (!wrapped)
		bitmap->num_chains = chain;
	else
		bitmap->num_chains = recs_per_inode;

	/* by now, this should be accurate. */
	if (bm_record->bi.total_bits != s->volume_size_in_clusters) {
		fprintf(stderr, "bitmap total and num clusters don't "
			"match! %u, %u\n", bm_record->bi.total_bits,
			s->volume_size_in_clusters);
		exit(1);
	}

	return bitmap;
}

#if 0
static void
destroy_bitmap(AllocBitmap *bitmap)
{
	free(bitmap->buf);
	free(bitmap);
}
#endif

static int
find_clear_bits(void *buf, unsigned int size, uint32_t num_bits, uint32_t offset)
{
	uint32_t next_zero, off, count = 0, first_zero = -1;

	off = offset;

	while ((size - off + count >= num_bits) &&
	       (next_zero = ocfs2_find_next_bit_clear(buf, size, off)) != size) {
		if (next_zero >= size)
			break;

		if (next_zero != off) {
			first_zero = next_zero;
			off = next_zero + 1;
			count = 0;
		} else {
			off++;
			if (count == 0)
				first_zero = next_zero;
		}

		count++;

		if (count == num_bits)
			goto bail;
	}

	first_zero = -1;

bail:
	if (first_zero != (uint32_t)-1 && first_zero > size) {
		fprintf(stderr, "erf... first_zero > bitmap->valid_bits "
				"(%d > %d)", first_zero, size);
		first_zero = -1;
	}

	return first_zero;
}

static int
alloc_bytes_from_bitmap(State *s, uint64_t bytes, AllocBitmap *bitmap,
			uint64_t *start, uint64_t *num)
{
	uint32_t num_bits = 0;

	num_bits = (bytes + bitmap->unit - 1) >> bitmap->unit_bits;

	return alloc_from_bitmap(s, num_bits, bitmap, start, num);
}

static int
alloc_from_bitmap(State *s, uint64_t num_bits, AllocBitmap *bitmap,
		  uint64_t *start, uint64_t *num)
{
	uint32_t start_bit = (uint32_t) - 1;
	void *buf = NULL;
	int i, found, chain;
	AllocGroup *group;
	struct ocfs2_group_desc *gd = NULL;
	unsigned int size;

	found = 0;
	for(i = 0; i < bitmap->num_chains && !found; i++) {
		group = bitmap->groups[i];
		do {
			gd = group->gd;
			if (gd->bg_free_bits_count >= num_bits) {
				buf = gd->bg_bitmap;
				size = gd->bg_bits;
				start_bit = find_clear_bits(buf, size,
							    num_bits, 0);
				found = 1;
				break;
			}
			group = group->next;
		} while (group);
	}

	if (start_bit == (uint32_t)-1) {
		com_err(s->progname, 0,
			"Could not allocate %"PRIu64" bits from %s bitmap",
			num_bits, bitmap->name);
		exit(1);
	}

	if (gd->bg_blkno == s->first_cluster_group_blkno)
		*start = (uint64_t) start_bit;
	else
		*start = (uint64_t) start_bit +
			((gd->bg_blkno << s->blocksize_bits) >> s->cluster_size_bits);

	*start = *start << bitmap->unit_bits;
	*num = ((uint64_t)num_bits) << bitmap->unit_bits;
	gd->bg_free_bits_count -= num_bits;
	chain = gd->bg_chain;
	bitmap->groups[chain]->chain_free -= num_bits;

	bitmap->bm_record->bi.used_bits += num_bits;

#if 0
	printf("alloc requested %"PRIu64" bits, given len = %"PRIu64", at "
	       "start = %"PRIu64". used_bits = %u\n", num_bits, *num, *start,
	       bitmap->bm_record->bi.used_bits);
#endif

	while (num_bits--) {
		ocfs2_set_bit(start_bit, buf);
		start_bit++;
	}

	return 0;
}

static int alloc_from_group(State *s, uint16_t count,
			    AllocGroup *group, uint64_t *start_blkno,
			    uint16_t *num_bits)
{
	uint16_t start_bit, end_bit;

	start_bit = ocfs2_find_first_bit_clear(group->gd->bg_bitmap,
					       group->gd->bg_bits);

	while (start_bit < group->gd->bg_bits) {
		end_bit = ocfs2_find_next_bit_set(group->gd->bg_bitmap,
						  group->gd->bg_bits,
						  start_bit);
		if ((end_bit - start_bit) >= count) {
			*num_bits = count;
			for (*num_bits = 0; *num_bits < count; *num_bits += 1) {
				ocfs2_set_bit(start_bit + *num_bits,
					      group->gd->bg_bitmap);
			}
			group->gd->bg_free_bits_count -= *num_bits;
			group->alloc_inode->bi.used_bits += *num_bits;
			*start_blkno = group->gd->bg_blkno + start_bit;
			return 0;
		}
		start_bit = end_bit;
	}

	com_err(s->progname, 0,
		"Could not allocate %"PRIu16" bits from %s alloc group",
		count, group->name);
	exit(1);

	return 1;
}

static uint64_t
alloc_inode(State *s, uint16_t *suballoc_bit)
{
	uint64_t ret;
	uint16_t num;

	alloc_from_group(s, 1, s->system_group,
			 &ret, &num);

        *suballoc_bit = (int)(ret - s->system_group->gd->bg_blkno);

        /* Did I mention I hate this code? */
	return (ret << s->blocksize_bits);
}

static DirData *
alloc_directory(State *s)
{
	DirData *dir;

	dir = do_malloc(s, sizeof(DirData));
	memset(dir, 0, sizeof(DirData));

	return dir;
}

static void
free_directory(DirData *dir)
{
	ocfs2_free(&dir->buf);
	ocfs2_free(&dir);
}

static void
add_entry_to_directory(State *s, DirData *dir, char *name, uint64_t byte_off,
		       uint8_t type)
{
	struct ocfs2_dir_entry *de, *de1;
	int new_rec_len;
	void *new_buf, *p;
	int new_size, rec_len, real_len;

	new_rec_len = OCFS2_DIR_REC_LEN(strlen(name));

	if (dir->buf) {
		de = (struct ocfs2_dir_entry *)(dir->buf + dir->last_off);
		rec_len = de->rec_len;
		real_len = OCFS2_DIR_REC_LEN(de->name_len);

		if ((de->inode == 0 && rec_len >= new_rec_len) ||
		    (rec_len >= real_len + new_rec_len)) {
			if (de->inode) {
				de1 =(struct ocfs2_dir_entry *) ((char *) de + real_len);
				de1->rec_len = de->rec_len - real_len;
				de->rec_len = real_len;
				de = de1;
			}

			goto got_it;
		}

		new_size = dir->record->file_size + s->blocksize;
	} else {
		new_size = s->blocksize;
	}

	new_buf = memalign(s->blocksize, new_size);

	if (new_buf == NULL) {
		com_err(s->progname, 0, "Failed to grow directory");
		exit(1);
	}

	if (dir->buf) {
		memcpy(new_buf, dir->buf, dir->record->file_size);
		free(dir->buf);
		p = new_buf + dir->record->file_size;
		memset(p, 0, s->blocksize);
	} else {
		p = new_buf;
		memset(new_buf, 0, new_size);
	}

	dir->buf = new_buf;
	dir->record->file_size = new_size;

	de = (struct ocfs2_dir_entry *)p;
	de->inode = 0;
	de->rec_len = s->blocksize;
	if (!s->inline_data || !dir->record->dir_data)
		mkfs_init_dir_trailer(s, dir, p);

got_it:
	de->name_len = strlen(name);

	de->inode = byte_off >> s->blocksize_bits;

	de->file_type = type;

        strcpy(de->name, name);

        dir->last_off = ((char *)de - (char *)dir->buf);

        if (type == OCFS2_FT_DIR)
                dir->record->links++;
}

static uint32_t
blocks_needed(State *s)
{
	uint32_t num;

	num = SUPERBLOCK_BLOCKS;
	num += ROOTDIR_BLOCKS;
	num += SYSDIR_BLOCKS;
	num += LOSTDIR_BLOCKS;
	num += sys_blocks_needed(MAX(32, s->initial_slots));

	return num;
}

static uint32_t
sys_blocks_needed(uint32_t num_slots)
{
	uint32_t num = 0;
	uint32_t cnt = sizeof(system_files) / sizeof(SystemFileInfo);
	int i;

	for (i = 0; i < cnt; ++i) {
		if (system_files[i].global)
			++num;
		else
			num += num_slots;
	}

	return num;
}

static uint32_t
system_dir_blocks_needed(State *s)
{
	int each = OCFS2_DIR_REC_LEN(SYSTEM_FILE_NAME_MAX);
	int entries_per_block = s->blocksize / each;

	return (sys_blocks_needed(s->initial_slots) +
			entries_per_block - 1) / entries_per_block;
}
#if 0
/* This breaks stuff that depends on volume_size_in_clusters and
 * volume_size_in_blocks, and I'm not even sure it's necessary. If
 * needed, this sort of calculation should be done before
 * fill_defaults where we calculate a bunch of other things based on
 * #blocks and #clusters. */
static void
adjust_volume_size(State *s)
{
	uint32_t max;
	uint64_t vsize = s->volume_size_in_bytes -
		(MIN_RESERVED_TAIL_BLOCKS << s->blocksize_bits);

	max = MAX(s->pagesize_bits, s->blocksize_bits);
	max = MAX(max, s->cluster_size_bits);

	vsize >>= max;
	vsize <<= max;

	s->volume_size_in_blocks = vsize >> s->blocksize_bits;
	s->volume_size_in_clusters = vsize >> s->cluster_size_bits;
	s->reserved_tail_size = s->volume_size_in_bytes - vsize;
	s->volume_size_in_bytes = vsize;
}
#endif
/* this will go away once we have patches to jbd to support 64bit blocks.
 * ocfs2 will only fail mounts when it finds itself asked to mount a large
 * device in a kernel that doesn't have a smarter jbd. */
static void
check_32bit_blocks(State *s)
{
	uint64_t max = UINT32_MAX;
       
	if (s->journal64)
		return;

	if (s->volume_size_in_blocks <= max)
		return;

	fprintf(stderr, "ERROR: jbd can only store block numbers in 32 bits. "
		"%s can hold %"PRIu64" blocks which overflows this limit. If "
		"you have a new enough Ocfs2 with JBD2 support, you can try "
		"formatting with the \"-Jblock64\" option to turn on support "
		"for this size block device.\n"
		"Otherwise, consider increasing the block size or "
		"decreasing the device size.\n",
		s->device_name, s->volume_size_in_blocks);

	exit(1);
}

static void mkfs_swap_inode_from_cpu(State *s, struct ocfs2_dinode *di)
{
	ocfs2_filesys fake_fs;
	char super_buf[OCFS2_MAX_BLOCKSIZE];

	fill_fake_fs(s, &fake_fs, super_buf);
	ocfs2_swap_inode_from_cpu(&fake_fs, di);
}

static void mkfs_swap_group_desc_from_cpu(State *s, struct ocfs2_group_desc *gd)
{
	ocfs2_filesys fake_fs;
	char super_buf[OCFS2_MAX_BLOCKSIZE];

	fill_fake_fs(s, &fake_fs, super_buf);
	ocfs2_swap_group_desc_from_cpu(&fake_fs, gd);
}

static void mkfs_swap_group_desc_to_cpu(State *s, struct ocfs2_group_desc *gd)
{
	ocfs2_filesys fake_fs;
	char super_buf[OCFS2_MAX_BLOCKSIZE];

	fill_fake_fs(s, &fake_fs, super_buf);
	ocfs2_swap_group_desc_to_cpu(&fake_fs, gd);
}

static void
format_superblock(State *s, SystemFileDiskRecord *rec,
		  SystemFileDiskRecord *root_rec, SystemFileDiskRecord *sys_rec)
{
	struct ocfs2_dinode *di;
	uint64_t super_off = rec->fe_off;

	di = do_malloc(s, s->blocksize);
	memset(di, 0, s->blocksize);

	strcpy((char *)di->i_signature, OCFS2_SUPER_BLOCK_SIGNATURE);
	di->i_suballoc_slot = (__u16)OCFS2_INVALID_SLOT;
	di->i_suballoc_bit = (__u16)-1;
	di->i_generation = s->vol_generation;
	di->i_fs_generation = s->vol_generation;

	di->i_atime = 0;
	di->i_ctime = s->format_time;
	di->i_mtime = s->format_time;
	di->i_blkno = super_off >> s->blocksize_bits;
	di->i_flags = OCFS2_VALID_FL | OCFS2_SYSTEM_FL | OCFS2_SUPER_BLOCK_FL;
	di->i_clusters = s->volume_size_in_clusters;
	di->id2.i_super.s_major_rev_level = OCFS2_MAJOR_REV_LEVEL;
	di->id2.i_super.s_minor_rev_level = OCFS2_MINOR_REV_LEVEL;
	di->id2.i_super.s_root_blkno = root_rec->fe_off >> s->blocksize_bits;
	di->id2.i_super.s_system_dir_blkno = sys_rec->fe_off >> s->blocksize_bits;
	di->id2.i_super.s_mnt_count = 0;
	di->id2.i_super.s_max_mnt_count = OCFS2_DFL_MAX_MNT_COUNT;
	di->id2.i_super.s_state = 0;
	di->id2.i_super.s_errors = 0;
	di->id2.i_super.s_lastcheck = s->format_time;
	di->id2.i_super.s_checkinterval = OCFS2_DFL_CHECKINTERVAL;
	di->id2.i_super.s_creator_os = OCFS2_OS_LINUX;
	di->id2.i_super.s_blocksize_bits = s->blocksize_bits;
	di->id2.i_super.s_clustersize_bits = s->cluster_size_bits;
	di->id2.i_super.s_max_slots = s->initial_slots;
	di->id2.i_super.s_first_cluster_group = s->first_cluster_group_blkno;

	if (s->hb_dev) {
		s->feature_flags.opt_incompat =
				 	OCFS2_FEATURE_INCOMPAT_HEARTBEAT_DEV;
		s->feature_flags.opt_compat = OCFS2_FEATURE_COMPAT_JBD2_SB;
		s->feature_flags.opt_ro_compat = 0;
	}

	if (s->mount == MOUNT_LOCAL)
		s->feature_flags.opt_incompat |=
			OCFS2_FEATURE_INCOMPAT_LOCAL_MOUNT;

	if (s->cluster_stack) {
		s->feature_flags.opt_incompat |=
			OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP;

		/* Selectively enable clusterinfo or userspace stack */
		if (!(s->feature_flags.opt_incompat &
		      OCFS2_FEATURE_INCOMPAT_CLUSTERINFO)) {
			if (!is_classic_stack(s->cluster_stack))
				s->feature_flags.opt_incompat |=
					OCFS2_FEATURE_INCOMPAT_USERSPACE_STACK;
			else
				s->feature_flags.opt_incompat |=
					OCFS2_FEATURE_INCOMPAT_CLUSTERINFO;
		}

		memcpy(di->id2.i_super.s_cluster_info.ci_stack,
		       s->cluster_stack, OCFS2_STACK_LABEL_LEN);
		memcpy(di->id2.i_super.s_cluster_info.ci_cluster,
		       s->cluster_name, OCFS2_CLUSTER_NAME_LEN);
		di->id2.i_super.s_cluster_info.ci_stackflags = s->stack_flags;
	}

	/*
	 * we clear the "backup_sb" here since it should be written by
	 * format_backup_super, not by us. And we have already set the
	 * "s->no_backup_super" according to the features in get_state,
	 * so it is safe to clear the flag here.
	 */
	s->feature_flags.opt_compat &= ~OCFS2_FEATURE_COMPAT_BACKUP_SB;

	if (s->feature_flags.opt_incompat & OCFS2_FEATURE_INCOMPAT_XATTR)
		di->id2.i_super.s_xattr_inline_size =
						OCFS2_MIN_XATTR_INLINE_SIZE;

	di->id2.i_super.s_feature_incompat = s->feature_flags.opt_incompat;
	di->id2.i_super.s_feature_compat = s->feature_flags.opt_compat;
	di->id2.i_super.s_feature_ro_compat = s->feature_flags.opt_ro_compat;

	strcpy((char *)di->id2.i_super.s_label, s->vol_label);
	memcpy(di->id2.i_super.s_uuid, s->uuid, OCFS2_VOL_UUID_LEN);

	/* s_uuid_hash is also used by Indexed Dirs */
	if (s->feature_flags.opt_incompat & OCFS2_FEATURE_INCOMPAT_XATTR ||
	    s->feature_flags.opt_incompat & OCFS2_FEATURE_INCOMPAT_INDEXED_DIRS)
		di->id2.i_super.s_uuid_hash = ocfs2_xattr_uuid_hash(s->uuid);

	if (s->feature_flags.opt_incompat & OCFS2_FEATURE_INCOMPAT_INDEXED_DIRS) {
		di->id2.i_super.s_dx_seed[0] = mrand48();
		di->id2.i_super.s_dx_seed[1] = mrand48();
		di->id2.i_super.s_dx_seed[2] = mrand48();
	}

	mkfs_swap_inode_from_cpu(s, di);
	mkfs_compute_meta_ecc(s, di, &di->i_check);
	do_pwrite(s, di, s->blocksize, super_off);
	free(di);
}


/*
 * This function is in libocfs2/alloc.c. Needless to add, when
 * changing code here, update the same in alloc.c too.
 */
static int 
ocfs2_clusters_per_group(int block_size, int cluster_size_bits)
{
	int megabytes;

	switch (block_size) {
	case 4096:
	case 2048:
		megabytes = 4;
		break;
	case 1024:
		megabytes = 2;
		break;
	case 512:
	default:
		megabytes = 1;
		break;
	}

	return (megabytes << ONE_MB_SHIFT) >> cluster_size_bits;
}

static void
format_file(State *s, SystemFileDiskRecord *rec)
{
	struct ocfs2_dinode *di;
	int mode, i;
	uint32_t clusters;
	AllocBitmap *bitmap;

	mode = rec->mode;

	clusters = (rec->extent_len + s->cluster_size - 1) >> s->cluster_size_bits;

	di = do_malloc(s, s->blocksize);
	memset(di, 0, s->blocksize);

	strcpy((char *)di->i_signature, OCFS2_INODE_SIGNATURE);
	di->i_generation = s->vol_generation;
	di->i_fs_generation = s->vol_generation;
	di->i_suballoc_slot = (__u16)OCFS2_INVALID_SLOT;
        di->i_suballoc_bit = rec->suballoc_bit;
	di->i_blkno = rec->fe_off >> s->blocksize_bits;
	di->i_uid = 0;
	di->i_gid = 0;
	di->i_size = rec->file_size;
	di->i_mode = mode;
	di->i_links_count = rec->links;
	di->i_flags = rec->flags;
	di->i_atime = di->i_ctime = di->i_mtime = s->format_time;
	di->i_dtime = 0;
	di->i_clusters = clusters;

	if (rec->flags & OCFS2_LOCAL_ALLOC_FL) {
		di->id2.i_lab.la_size =
			ocfs2_local_alloc_size(s->blocksize);
		goto write_out;
	}

	if (rec->flags & OCFS2_DEALLOC_FL) {
		di->id2.i_dealloc.tl_count =
			ocfs2_truncate_recs_per_inode(s->blocksize);
		goto write_out;
	}

	if (rec->flags & OCFS2_BITMAP_FL) {
		di->id1.bitmap1.i_used = rec->bi.used_bits;
		di->id1.bitmap1.i_total = rec->bi.total_bits;
	}

	if (rec->cluster_bitmap) {
		di->id2.i_chain.cl_count = 
			ocfs2_chain_recs_per_inode(s->blocksize);
		di->id2.i_chain.cl_cpg =
			 ocfs2_group_bitmap_size(s->blocksize, 0, 0) * 8;
		di->id2.i_chain.cl_bpc = 1;
		if (s->nr_cluster_groups > 
		    ocfs2_chain_recs_per_inode(s->blocksize)) {
			di->id2.i_chain.cl_next_free_rec = di->id2.i_chain.cl_count;
		} else
			di->id2.i_chain.cl_next_free_rec =
				s->nr_cluster_groups;
		di->i_clusters = s->volume_size_in_clusters;

		bitmap = rec->bitmap;
		for(i = 0; i < bitmap->num_chains; i++) {
			di->id2.i_chain.cl_recs[i].c_blkno = 
				bitmap->groups[i]->gd->bg_blkno;
			di->id2.i_chain.cl_recs[i].c_free = 
				bitmap->groups[i]->chain_free;
			di->id2.i_chain.cl_recs[i].c_total = 
				bitmap->groups[i]->chain_total;
		}
		goto write_out;
	}
	if (rec->flags & OCFS2_CHAIN_FL) {
		di->id2.i_chain.cl_count = 
			ocfs2_chain_recs_per_inode(s->blocksize);

		di->id2.i_chain.cl_cpg = 
			ocfs2_clusters_per_group(s->blocksize, 
						 s->cluster_size_bits);
		di->id2.i_chain.cl_bpc = s->cluster_size / s->blocksize;
		di->id2.i_chain.cl_next_free_rec = 0;

		if (rec->chain_off) {
			di->id2.i_chain.cl_next_free_rec = 1;
			di->id2.i_chain.cl_recs[0].c_free =
				rec->group->gd->bg_free_bits_count;
			di->id2.i_chain.cl_recs[0].c_total =
				rec->group->gd->bg_bits;
			di->id2.i_chain.cl_recs[0].c_blkno =
				rec->chain_off >> s->blocksize_bits;
                        di->id2.i_chain.cl_cpg =
				rec->group->gd->bg_bits /
				di->id2.i_chain.cl_bpc;
			di->i_clusters = di->id2.i_chain.cl_cpg;
			di->i_size = di->i_clusters << s->cluster_size_bits;
		}
		goto write_out;
	}
	di->id2.i_list.l_count = ocfs2_extent_recs_per_inode(s->blocksize);
	di->id2.i_list.l_next_free_rec = 0;
	di->id2.i_list.l_tree_depth = 0;

	if (rec->extent_len) {
		di->id2.i_list.l_next_free_rec = 1;
		di->id2.i_list.l_recs[0].e_cpos = 0;
		ocfs2_set_rec_clusters(0, &di->id2.i_list.l_recs[0], clusters);
		di->id2.i_list.l_recs[0].e_blkno =
			rec->extent_off >> s->blocksize_bits;
	} else if (S_ISDIR(di->i_mode) && s->inline_data && rec->dir_data) {
		DirData *dir = rec->dir_data;
		struct ocfs2_dir_entry *de =
			(struct ocfs2_dir_entry *)(dir->buf + dir->last_off);
		int dir_len = dir->last_off + OCFS2_DIR_REC_LEN(de->name_len);

		if (dir_len >
		    ocfs2_max_inline_data_with_xattr(s->blocksize, di)) {
			com_err(s->progname, 0,
				"Inline a dir which shouldn't be inline.\n");
			clear_both_ends(s);
			exit(1);
		}
		de->rec_len -= s->blocksize -
			       ocfs2_max_inline_data_with_xattr(s->blocksize,
								di);
		memset(&di->id2, 0,
		       s->blocksize - offsetof(struct ocfs2_dinode, id2));

		di->id2.i_data.id_count =
			ocfs2_max_inline_data_with_xattr(s->blocksize, di);
		memcpy(di->id2.i_data.id_data, dir->buf, dir_len);
		di->i_dyn_features |= OCFS2_INLINE_DATA_FL;
		di->i_size = ocfs2_max_inline_data_with_xattr(s->blocksize,
							      di);
	}

write_out:
	mkfs_swap_inode_from_cpu(s, di);
	mkfs_compute_meta_ecc(s, di, &di->i_check);
	do_pwrite(s, di, s->blocksize, rec->fe_off);
	free(di);
}

static void
write_metadata(State *s, SystemFileDiskRecord *rec, void *src)
{
	void *buf;

	buf = do_malloc(s, rec->extent_len);
	memset(buf, 0, rec->extent_len);

	if (src)
		memcpy(buf, src, rec->file_size);

	do_pwrite(s, buf, rec->extent_len, rec->extent_off);

	free(buf);
}

static void
write_bitmap_data(State *s, AllocBitmap *bitmap)
{
	int i;
	uint64_t parent_blkno;
	struct ocfs2_group_desc *gd, *gd_buf;
	char *buf = NULL;

	buf = do_malloc(s, s->cluster_size);
	memset(buf, 0, s->cluster_size);

	parent_blkno = bitmap->bm_record->fe_off >> s->blocksize_bits;
	for(i = 0; i < s->nr_cluster_groups; i++) {
		gd = bitmap->groups[i]->gd;
		if (strcmp((char *)gd->bg_signature, OCFS2_GROUP_DESC_SIGNATURE)) {
			fprintf(stderr, "bad group descriptor!\n");
			exit(1);
		}
		/* Ok, we didn't get a chance to fill in the parent
		 * blkno until now. */
		gd->bg_parent_dinode = parent_blkno;
		memcpy(buf, gd, s->blocksize);
		gd_buf = (struct ocfs2_group_desc *)buf;
		mkfs_swap_group_desc_from_cpu(s, gd_buf);
		mkfs_compute_meta_ecc(s, buf, &gd_buf->bg_check);
		do_pwrite(s, buf, s->cluster_size,
			  gd->bg_blkno << s->blocksize_bits);
	}
	free(buf);
}

static void
write_group_data(State *s, AllocGroup *group)
{
	uint64_t blkno = group->gd->bg_blkno;
	mkfs_swap_group_desc_from_cpu(s, group->gd);
	mkfs_compute_meta_ecc(s, group->gd, &group->gd->bg_check);
	do_pwrite(s, group->gd, s->blocksize, blkno << s->blocksize_bits);
	mkfs_swap_group_desc_to_cpu(s, group->gd);
}

static void mkfs_swap_dir(State *s, DirData *dir,
			  errcode_t (*swap_entry_func)(void *buf,
						       uint64_t bytes))
{
	char *p = dir->buf;
	unsigned int offset = 0;
	unsigned int end = s->blocksize;
	char super_buf[OCFS2_MAX_BLOCKSIZE];
	ocfs2_filesys fake_fs;
	struct ocfs2_dir_block_trailer *trailer;

	if (!dir->record->extent_len)
		return;

	fill_fake_fs(s, &fake_fs, super_buf);
	if (!s->inline_data || !dir->record->dir_data)
		if (ocfs2_supports_dir_trailer(&fake_fs))
			end = ocfs2_dir_trailer_blk_off(&fake_fs);

	while (offset < dir->record->file_size) {
		trailer = ocfs2_dir_trailer_from_block(&fake_fs, p);
		swap_entry_func(p, end);
		if (end != s->blocksize)
			ocfs2_swap_dir_trailer(trailer);
		/* Remember, this does nothing if the feature isn't set */
		ocfs2_compute_meta_ecc(&fake_fs, p, &trailer->db_check);
		offset += s->blocksize;
		p += s->blocksize;
	}
}

static void mkfs_swap_dir_from_cpu(State *s, DirData *dir)
{
	mkfs_swap_dir(s, dir, ocfs2_swap_dir_entries_from_cpu);
}

static void mkfs_swap_dir_to_cpu(State *s, DirData *dir)
{
	mkfs_swap_dir(s, dir, ocfs2_swap_dir_entries_to_cpu);
}

static void
write_directory_data(State *s, DirData *dir)
{
	if (!dir->record->extent_len)
		return;

	if (dir->buf)
		mkfs_swap_dir_from_cpu(s, dir);
	write_metadata(s, dir->record, dir->buf);
	if (dir->buf)
		mkfs_swap_dir_to_cpu(s, dir);
}

static void
format_leading_space(State *s)
{
	int num_blocks = 2, size;
	struct ocfs1_vol_disk_hdr *hdr;
	struct ocfs1_vol_label *lbl;
	void *buf;
	char *p;

	size = num_blocks << s->blocksize_bits;

	p = buf = do_malloc(s, size);
	memset(buf, 2, size);

	hdr = buf;
	strcpy((char *)hdr->signature, "this is an ocfs2 volume");
	strcpy((char *)hdr->mount_point, "this is an ocfs2 volume");

	p += 512;
	lbl = (struct ocfs1_vol_label *)p;
	strcpy((char *)lbl->label, "this is an ocfs2 volume");
	strcpy((char *)lbl->cluster_name, "this is an ocfs2 volume");

	do_pwrite(s, buf, size, 0);
	free(buf);
}

static void
open_device(State *s)
{
	s->fd = open64(s->device_name, O_RDWR | O_DIRECT);

	if (s->fd == -1) {
		com_err(s->progname, 0,
		        "Could not open device %s: %s",
			s->device_name, strerror (errno));
		exit(1);
	}
}

static void
close_device(State *s)
{
	fsync(s->fd);
	close(s->fd);
	s->fd = -1;
}

static int
initial_slots_for_volume(uint64_t size)
{
	size >>= ONE_GB_SHIFT;

	if (size < 2)
		return 2;
	else if (size < 10)
		return 4;
	else if (size < 1024)
		return 8;
	else
		return 16;
}

static void create_generation(State *s)
{
	int randfd = 0;
	int readlen = sizeof(s->vol_generation);

	if ((randfd = open("/dev/urandom", O_RDONLY)) == -1) {
		com_err(s->progname, 0,
			"Error opening /dev/urandom: %s", strerror(errno));
		exit(1);
	}

	if (read(randfd, &s->vol_generation, readlen) != readlen) {
		com_err(s->progname, 0,
			"Error reading from /dev/urandom: %s",
			strerror(errno));
		exit(1);
	}

	close(randfd);
}

static void
init_record(State *s, SystemFileDiskRecord *rec, int type, int mode)
{
	memset(rec, 0, sizeof(SystemFileDiskRecord));

	rec->mode = mode;

	rec->links = S_ISDIR(mode) ? 0 : 1;

	rec->bi.used_bits = rec->bi.total_bits = 0;
	rec->flags = (OCFS2_VALID_FL | OCFS2_SYSTEM_FL);

	switch (type) {
	case SFI_JOURNAL:
		rec->flags |= OCFS2_JOURNAL_FL;
		break;
	case SFI_LOCAL_ALLOC:
		rec->flags |= (OCFS2_BITMAP_FL|OCFS2_LOCAL_ALLOC_FL);
		break;
	case SFI_HEARTBEAT:
		rec->flags |= OCFS2_HEARTBEAT_FL;
		break;
	case SFI_CLUSTER:
		rec->cluster_bitmap = 1;
	case SFI_CHAIN:
		rec->flags |= (OCFS2_BITMAP_FL|OCFS2_CHAIN_FL);
		break;
	case SFI_TRUNCATE_LOG:
		rec->flags |= OCFS2_DEALLOC_FL;
		break;
	case SFI_QUOTA:
		rec->flags |= OCFS2_QUOTA_FL;
		break;
	case SFI_OTHER:
		break;
	}
}

static void
print_state(State *s)
{
	int i;
	char buf[PATH_MAX] = "\0";
	uint64_t extsize = 0;
	uint32_t numgrps = 0;

	if (s->quiet)
		return;

	if (s->extent_alloc_size_in_clusters) {
		numgrps = s->extent_alloc_size_in_clusters /
			ocfs2_clusters_per_group(s->blocksize,
						 s->cluster_size_bits);
		extsize = (uint64_t)s->extent_alloc_size_in_clusters *
			s->cluster_size;
	}

	ocfs2_snprint_feature_flags(buf, PATH_MAX, &s->feature_flags);

	if (s->fs_type != OCFS2_MKFSTYPE_DEFAULT) {
		for(i = 0; ocfs2_mkfs_types_table[i].ft_str; i++) {
			if (ocfs2_mkfs_types_table[i].ft_type == s->fs_type) {
				printf("Filesystem Type of %s\n",
				       ocfs2_mkfs_types_table[i].ft_str);
				break;
			}
		}
	}
	printf("Label: %s\n", s->vol_label);
	printf("Features: %s\n", buf);
	printf("Block size: %u (%u bits)\n", s->blocksize, s->blocksize_bits);
	printf("Cluster size: %u (%u bits)\n", s->cluster_size, s->cluster_size_bits);
	printf("Volume size: %"PRIu64" (%u clusters) (%"PRIu64" blocks)\n",
	       s->volume_size_in_bytes, s->volume_size_in_clusters,
	       s->volume_size_in_blocks);
	printf("Cluster groups: %u (tail covers %u clusters, rest cover %u "
	       "clusters)\n", s->nr_cluster_groups, s->tail_group_bits,
	       s->global_cpg);
	printf("Extent allocator size: %"PRIu64" (%u groups)\n",
	       extsize, numgrps);
	if (s->hb_dev)
		printf("Heartbeat device\n");
	else
		printf("Journal size: %"PRIu64"\n",
		       s->journal_size_in_bytes);
	printf("Node slots: %u\n", s->initial_slots);
}

static void
clear_both_ends(State *s)
{
	char *buf = NULL;

	buf = do_malloc(s, CLEAR_CHUNK);

	memset(buf, 0, CLEAR_CHUNK);

	/* start of volume */
	do_pwrite(s, buf, CLEAR_CHUNK, 0);

	/* end of volume */
	do_pwrite(s, buf, CLEAR_CHUNK, (s->volume_size_in_bytes - CLEAR_CHUNK));

	free(buf);

	return ;
}

static void index_system_dirs(State *s, ocfs2_filesys *fs)
{
	errcode_t ret;
	int i, num_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;
	uint64_t orphan_dir_blkno;


	/* Start with the root directory */
	ret = ocfs2_dx_dir_build(fs, fs->fs_root_blkno);
	if (ret) {
		com_err(s->progname, ret, "while indexing root directory");
		goto bail;
	}

	for (i = 0; i < num_slots; i++) {
		ret = ocfs2_lookup_system_inode(fs, ORPHAN_DIR_SYSTEM_INODE,
						i, &orphan_dir_blkno);
		if (ret) {
			com_err(s->progname, ret,
				"while looking up orphan dir %d for indexing",
				i);
			goto bail;
		}

		ret = ocfs2_dx_dir_build(fs, orphan_dir_blkno);
		if (ret) {
			com_err(s->progname, ret, "while indexing root directory");
			goto bail;
		}
	}

	return;

bail:
	clear_both_ends(s);
	exit(1);
}

static void create_lost_found_dir(State *s, ocfs2_filesys *fs)
{
	errcode_t ret;
	uint64_t lost_found_blkno;

	ret = ocfs2_new_inode(fs, &lost_found_blkno, S_IFDIR | 0755);
	if (ret) {
		com_err(s->progname, ret, "while creating lost+found");
		goto bail;
	}

	ret = ocfs2_init_dir(fs, lost_found_blkno, fs->fs_root_blkno);
	if (ret) {
		com_err(s->progname, ret, "while adding lost+found dir data");
		goto bail;
	}

	ret = ocfs2_link(fs, fs->fs_root_blkno, "lost+found", lost_found_blkno,
			 OCFS2_FT_DIR);
	if (ret) {
		com_err(s->progname, ret, "while linking lost+found to the "
			"root directory");
		goto bail;
	}

	return ;

bail:
	clear_both_ends(s);
	exit(1);
}

static void format_journals(State *s, ocfs2_filesys *fs)
{
	errcode_t ret;
	int i;
	uint32_t journal_size_in_clusters;
	uint64_t blkno;
	char jrnl_file[40];
	ocfs2_fs_options features = {
		.opt_incompat =
			s->journal64 ? JBD2_FEATURE_INCOMPAT_64BIT : 0,
	};

	journal_size_in_clusters = s->journal_size_in_bytes >>
		OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;

	for(i = 0; i < OCFS2_RAW_SB(fs->fs_super)->s_max_slots; i++) {
		snprintf (jrnl_file, sizeof(jrnl_file),
			  ocfs2_system_inodes[JOURNAL_SYSTEM_INODE].si_name, i);
		ret = ocfs2_lookup(fs, fs->fs_sysdir_blkno, jrnl_file,
				   strlen(jrnl_file), NULL, &blkno);
		if (ret) {
			com_err(s->progname, ret,
				"while looking up journal filename \"%.*s\"",
				(int)strlen(jrnl_file), jrnl_file);
			goto error;
		}

		ret = ocfs2_make_journal(fs, blkno, journal_size_in_clusters,
					 &features);
		if (ret) {
			com_err(s->progname, ret,
				"while formatting journal \"%.*s\"",
				(int)strlen(jrnl_file), jrnl_file);
			goto error;
		}
	}

	return;

error:
	clear_both_ends(s);
	exit(1);
}

static void format_slotmap(State *s, ocfs2_filesys *fs)
{
	errcode_t ret;

	ret = ocfs2_format_slot_map(fs);
	if (ret) {
		com_err(s->progname, ret, "while formatting the slot map");
		clear_both_ends(s);
		exit(1);
	}
}

static int format_backup_super(State *s, ocfs2_filesys *fs)
{
	errcode_t ret;
	size_t len;
	uint64_t blocks[OCFS2_MAX_BACKUP_SUPERBLOCKS];

	len = ocfs2_get_backup_super_offsets(fs, blocks, ARRAY_SIZE(blocks));

	ret = ocfs2_set_backup_super_list(fs, blocks, len);
	if (ret) {
		com_err(s->progname, ret, "while backing up superblock.");
		goto error;
	}

	OCFS2_SET_COMPAT_FEATURE(OCFS2_RAW_SB(fs->fs_super),
				 OCFS2_FEATURE_COMPAT_BACKUP_SB);

	/* ocfs2_set_backup_super_list() wrote the backups */
	ret = ocfs2_write_primary_super(fs);
	if (ret) {
		com_err(s->progname, ret, "while updating superblock.");
		goto error;
	}

	return len;

error:
	clear_both_ends(s);
	exit(1);
}

static void mkfs_compute_meta_ecc(State *s, void *data,
				  struct ocfs2_block_check *bc)
{
	if (s->feature_flags.opt_incompat & OCFS2_FEATURE_INCOMPAT_META_ECC)
		ocfs2_block_check_compute(data, s->blocksize, bc);
}
