#include <libocfs.h>

extern int fd;
extern ocfs_super *osb;
extern struct super_block sb;
extern ocfs_vol_disk_hdr *vdh;
extern ocfs_file_entry *fe;
extern __u32 debug_context;
extern __u32 debug_level;
extern __u32 debug_exclude;
extern ocfs_global_ctxt OcfsGlobalCtxt;



int do_bugfix(void);
void print_bugfix_string(void);
bool find_the_bug(ocfs_dir_node *dir);
int fix_the_bug(ocfs_dir_node *dir, __u64 offset, __u64 lock_id);
int scan_directory(__u64 top, int *found);
static int fe_compare_func(const void *m1, const void *m2);


__u8 *dirindex = NULL, *idxtmp = NULL;
ocfs_dir_node *globaldir = NULL;


int do_bugfix()
{
	int ret;
	int found = 0;

	if ((dirindex = malloc(256)) == NULL) {
		return ENOMEM;
	}
	if ((idxtmp = malloc(256)) == NULL) {
		free(dirindex);
		return ENOMEM;
	}
	ret = scan_directory(vdh->root_off, &found);
	if (dirindex)
		free(dirindex);
	
	if (found)
		printf("Bad dir index was found %d times.\n", found);
	else
		printf("Bad dir index was not found!  OK.\n");

	return ret;
}

void print_bugfix_string()
{
	fprintf(stderr, "\nThis utility fixes bug#3123542, the bad directory index bug.\n");
}

int scan_directory(__u64 top, int *found)
{
	int ret, i;
	__u64 off;
	ocfs_dir_node *dir = NULL;

	if ((dir = malloc_aligned(OCFS_DEFAULT_DIR_NODE_SIZE)) == NULL) {
		fprintf(stderr, "failed to alloc %d bytes!  exiting!\n", OCFS_DEFAULT_DIR_NODE_SIZE);
		exit(ENOMEM);
	}
	memset(dir, 0, OCFS_DEFAULT_DIR_NODE_SIZE);

	off = top;
	while (1) {
		ret = ocfs_read_dir_node(osb, dir, off);
		if (ret < 0) {
			fprintf(stderr, "error during read of %llu : %s\n", off, strerror(errno));
			return ret;
		}
		
		if (find_the_bug(dir)) {
			(*found)++;
			ret = fix_the_bug(dir, off, top);
		}

		for (i=0; i < dir->num_ent_used; i++)
		{
			ocfs_file_entry *fe;
			fe = FILEENT(dir, i);
			if (fe->sync_flags && !(fe->sync_flags & DELETED_FLAGS) &&
			    	fe->attribs & OCFS_ATTRIB_DIRECTORY)
			{
				ret = scan_directory(fe->extents[0].disk_off, found);
			}
		}

			
		if (dir->next_node_ptr == -1)
			break;
		else
			off = dir->next_node_ptr;
	}
	if (dir)
		free_aligned(dir);

	return ret;
}

bool find_the_bug(ocfs_dir_node *dir)
{
	/* check the dir->index integrity */
	globaldir = dir;
	memset(dirindex, 0, 256);
	memcpy(dirindex, dir->index, dir->num_ent_used);
	qsort(dirindex, dir->num_ent_used, sizeof(__u8), fe_compare_func);

	if (memcmp(dirindex, dir->index, dir->num_ent_used) != 0)
		return true;

	return false;
}

int fix_the_bug(ocfs_dir_node *dir, __u64 offset, __u64 lock_id)
{
	int ret = 0, tmpret;
	ocfs_lock_res *lockres = NULL;
	ocfs_file_entry *lock_fe = NULL;
   
	/* if we are changing the block being locked   */
	/* we need to make sure to use the same buffer */
	if (offset != lock_id) {
		memset(fe, 0, 512);
		lock_fe = fe;
	} else
		lock_fe = (ocfs_file_entry *)dir;

	printf("Bad dir index found. Fixing.\n");

	// locking the toplevel dir
	ret = ocfs_acquire_lock (osb, lock_id, OCFS_DLM_EXCLUSIVE_LOCK, FLAG_DIR, 
				 &lockres, lock_fe);

	if (ret < 0) {
		fprintf(stderr, "failed to lock directory\n");
		goto done;
	}
	
	memcpy(idxtmp, dir->index, 256);
	memcpy(dir->index, dirindex, dir->num_ent_used);
	ret = ocfs_write_disk(osb, dir, 512, offset);
	if (ret == -1) {
		fprintf(stderr, "failed to write at offset %u/%u\n", offset);
		memcpy(dir->index, idxtmp, 256);
	}
	tmpret = ocfs_release_lock(osb, lock_id, OCFS_DLM_EXCLUSIVE_LOCK, FLAG_DIR, 
			      lockres, lock_fe);
	if (tmpret < 0) {
		fprintf(stderr, "failed to release lock\n");
		if (ret == 0)
			ret = tmpret;
	}
done:
	printf("Bad dir index : %s!\n", ret==0 ? "FIXED" : "NOT FIXED");
	return ret;
}

static int fe_compare_func(const void *m1, const void *m2)
{
	ocfs_file_entry *fe1, *fe2;
	__u8 idx1, idx2;
	int ret;

	if (globaldir == NULL) {
		fprintf(stderr, "globaldir is null!  exiting!\n");
		exit(EINVAL);
	}

	idx1 = *(__u8 *)m1;
	idx2 = *(__u8 *)m2;

	fe1 = (ocfs_file_entry *) ((char *)FIRST_FILE_ENTRY(globaldir) + (idx1 * OCFS_SECTOR_SIZE));
	fe2 = (ocfs_file_entry *) ((char *)FIRST_FILE_ENTRY(globaldir) + (idx2 * OCFS_SECTOR_SIZE));
	if (fe1->sync_flags & OCFS_SYNC_FLAG_NAME_DELETED ||
	    (!(fe1->sync_flags & OCFS_SYNC_FLAG_VALID)) ||
	    fe2->sync_flags & OCFS_SYNC_FLAG_NAME_DELETED ||
	    (!(fe2->sync_flags & OCFS_SYNC_FLAG_VALID))) {
		return 0;
	}
	ret = strncmp(fe1->filename, fe2->filename, 255);
	
	return -ret;
}


