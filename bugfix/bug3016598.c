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


int do_bugfix()
{
	int ret;
	int found = 0;

	ret = scan_directory(vdh->root_off, &found);
	
	if (found)
		printf("Undeletable directory was found %d times.\n", found);
	else
		printf("Undeletable directory was not found!  OK.\n");

	return ret;
}

void print_bugfix_string()
{
	fprintf(stderr, "\nThis utility fixes bug#3016598, the undeletable directory bug.\n");
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

	if (!found)
		printf("Undeletable directory was not found!  OK.\n");		
	return ret;
}

bool find_the_bug(ocfs_dir_node *dir)
{
	int deleted_files, i;
	
	/* check the undeletable dir bug, BUG #3016598 */
	for (i=0, deleted_files=0; i < dir->num_ent_used; i++) {
		if ((FILEENT(dir, i))->sync_flags & OCFS_SYNC_FLAG_MARK_FOR_DELETION)
			deleted_files++;
	}
	if (dir->num_ent_used && dir->num_ent_used == deleted_files) {
		/* we hit the bug... fix by zeroing num_ent_used */
		return true;
	}
	return false;
}

int fix_the_bug(ocfs_dir_node *dir, __u64 offset, __u64 lock_id)
{
	int ret = 0, tmpret;
	ocfs_lock_res *lockres = NULL;
	ocfs_file_entry *lock_fe = NULL;
	__u8 num_ent_used = dir->num_ent_used;


	/* if we are changing the block being locked   */
	/* we need to make sure to use the same buffer */
	if (offset != lock_id) {
		memset(fe, 0, 512);
		lock_fe = fe;
	} else
		lock_fe = (ocfs_file_entry *)dir;

	if (offset != lock_id) {
		// it seems that the bug only happens on the topmost
		return 0;
	}
	
	printf("Undeletable directory found. Fixing.\n");

	// locking the toplevel dir
	ret = ocfs_acquire_lock (osb, lock_id, OCFS_DLM_EXCLUSIVE_LOCK, FLAG_DIR, 
				 &lockres, lock_fe);

	if (ret < 0) {
		fprintf(stderr, "failed to lock directory\n");
		goto done;
	}
	
	dir->num_ent_used = 0;
	ret = ocfs_write_disk(osb, dir, 512, offset);
	if (ret == -1) {
		fprintf(stderr, "failed to write at offset %u/%u\n", offset);
		dir->num_ent_used = num_ent_used;
	}
	tmpret = ocfs_release_lock(osb, lock_id, OCFS_DLM_EXCLUSIVE_LOCK, FLAG_DIR, 
			      lockres, lock_fe);
	if (tmpret < 0) {
		fprintf(stderr, "failed to release lock\n");
		if (ret == 0)
			ret = tmpret;
	}
done:
	printf("Undeletable directory : %s!\n", ret==0 ? "FIXED" : "NOT FIXED");
	return ret;
}
