#include <libocfs.h>


void usage(int argc, char **argv);
void init_global_context(void);
void version(char *progname);
bool disclaimer(void);


extern void print_bugfix_string(void);
extern int do_bugfix(void);


int fd = -1;
ocfs_super *osb = NULL;
struct super_block sb;
ocfs_vol_disk_hdr *vdh = NULL;
ocfs_file_entry *fe = NULL;
__u32 debug_context = 0;
__u32 debug_level = 0;
__u32 debug_exclude = 0;
ocfs_global_ctxt OcfsGlobalCtxt;



#define DISCLAIMER_MESSAGE      \
"WARNING:       This utility fixes a particular bug with OCFS.  Only run this utility \n"\
"               if directed to do so by Oracle personnel.  \n"\
"!!! NOTE !!!:  DO NOT run more than one instance of this command, or any other bugfix \n"\
"               commands at the same time on this volume!  Doing so MAY CORRUPT YOUR \n"\
"               FILESYSTEM!  Also, please attempt to limit the I/O being performed on \n"\
"               this partition at the time that you run the command.\n"\
"\n"\
"Are you sure you want to run this utility? (yes, [no]) "


void usage(int argc, char **argv)
{
	fprintf(stderr, "\n");
	fprintf(stderr, "usage: %s /dev/device\n", argv[0]);
	fprintf(stderr, "       where /dev/device is a raw device bound to your ocfs block device\n");
	fprintf(stderr, "       (please see the raw(8) manpage for more information) or an ocfs \n");
	fprintf(stderr, "       block device which supports direct-I/O\n");
	fprintf(stderr, "\n");
	print_bugfix_string();
}

void init_global_context()
{
	char *tmp;

	memset(&OcfsGlobalCtxt, 0, sizeof(ocfs_global_ctxt));
	OcfsGlobalCtxt.obj_id.type = OCFS_TYPE_GLOBAL_DATA;
	OcfsGlobalCtxt.obj_id.size = sizeof (ocfs_global_ctxt);
	OcfsGlobalCtxt.pref_node_num = 31;
	OcfsGlobalCtxt.node_name = "user-tool";
	OcfsGlobalCtxt.comm_info.type = OCFS_UDP;
	OcfsGlobalCtxt.comm_info.ip_addr = "0.0.0.0";
	OcfsGlobalCtxt.comm_info.ip_port = OCFS_IPC_DEFAULT_PORT;
	OcfsGlobalCtxt.comm_info.ip_mask = NULL;
	OcfsGlobalCtxt.comm_info_read = true;
	memset(&OcfsGlobalCtxt.guid.id.host_id, 'f', HOSTID_LEN);
	memset(&OcfsGlobalCtxt.guid.id.mac_id,  '0', MACID_LEN);

	tmp = getenv("debug_level");
	if (tmp)
		debug_level = atoi(tmp);
	tmp = getenv("debug_context");
	if (tmp)
		debug_context = atoi(tmp);
	tmp = getenv("debug_exclude");
	if (tmp)
		debug_exclude = atoi(tmp);
}


bool disclaimer()
{
	char response[10];
	int i;

	print_bugfix_string();
	printf(DISCLAIMER_MESSAGE);

	if (fgets(response, 10, stdin) == NULL)
		return false;
	
	for (i=0; i<10; i++)
		if (response[i] == '\n' || response[i] == '\r')
			response[i] = '\0';

	if (strcasecmp(response, "yes")==0) {
		return true;
	}
	return false;
}


int main(int argc, char **argv)
{
	int ret = 0;
	bool mounted = false;
	int flags;
	struct stat st;

	init_global_context();
	version(argv[0]);

	if (argc < 2 || stat(argv[1], &st) != 0 ||
	    !(S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) ) {
		usage(argc, argv);
		exit(EINVAL);
	}

	if (S_ISCHR(st.st_mode))
		flags = O_RDWR | O_LARGEFILE | O_SYNC;
	else
		flags = O_RDWR | O_LARGEFILE | O_DIRECT | O_SYNC;

	if (!disclaimer()) 
		exit(EINVAL);
	
	fd = open(argv[1], flags);
	if (fd == -1) {
		usage(argc, argv);
		exit(EINVAL);
	}
	
	if ((vdh = malloc_aligned(1024)) == NULL) {
		fprintf(stderr, "failed to alloc %d bytes!  exiting!\n", 1024);
		exit(ENOMEM);
	}
	if ((fe = malloc_aligned(512)) == NULL) {
		fprintf(stderr, "failed to alloc %d bytes!  exiting!\n", 512);
		exit(ENOMEM);
	}

	memset(vdh, 0, 1024);
	memset(fe, 0, 512);
	memset(&sb, 0, sizeof(struct super_block));
	sb.s_dev = fd;

	ret = ocfs_read_disk_header ((__u8 **)&vdh, &sb);
	if (ret < 0) {
		fprintf(stderr, "failed to read header\n");
		goto done;
	}

	ret = ocfs_mount_volume (&sb, false);
	if (ret < 0) {
		fprintf(stderr, "failed to mount\n");
		goto done;
	}
	
	mounted = true;
	osb = sb.u.generic_sbp;

	ret = do_bugfix();


done:
	if (mounted) {
		int tmp;
		if ((tmp = ocfs_dismount_volume (&sb)) < 0) {
			fprintf(stderr, "dismount failed, ret = %d\n", tmp);
			if (ret == 0)
				ret = tmp;
		}
	}
	if (fe)
		free_aligned(fe);
	if (vdh)
		free_aligned(vdh);
	if (fd != -1)
		close(fd);

	exit(ret);
}
