#include "debugocfs.h"
#include <time.h>
#include <sys/raw.h>
#include "glib.h"
#include "libdebugocfs.h"

#define FILE_NAME_SIZE 255

/* XXX: Not really thread safe */
static int rawminor = 0;
static char rawdev[FILE_NAME_SIZE];

int libocfs_init_raw(void)
{
    /* Hmm... is /dev/null really ok? */
    return bind_raw("/dev/null", &rawminor, rawdev, FILE_NAME_SIZE);
}

int libocfs_cleanup_raw(void)
{
    unbind_raw(rawminor);
}

static int libocfs_bind_raw (const char *dev)
{
    struct raw_config_request rcs;
    struct stat statbuf;
    int fd, ret = 0;

    memset(&statbuf, 0, sizeof(struct stat));

    if (stat(dev, &statbuf) == -1)
	return -1;

    if (MAJOR(statbuf.st_rdev) == 0)
	return -1;

    fd = open("/dev/rawctl", O_RDWR);
    if (fd == -1)
	return -1;

    memset(&rcs, 0, sizeof(struct raw_config_request));

    rcs.raw_minor = rawminor;
    rcs.block_major = (__u64)MAJOR(statbuf.st_rdev);
    rcs.block_minor = (__u64)MINOR(statbuf.st_rdev);

    if (ioctl(fd, RAW_SETBIND, &rcs) == -1)
	ret = -1;

    close (fd);

    return ret;
}

static int libocfs_init(const char *dev, ocfs_vol_disk_hdr ** dh, gboolean do_bind)
{
    int fd;

    if (do_bind)
    {
	if (rawminor == 0)
	    return -1;

	if (libocfs_bind_raw (dev) == -1)
	    return -1;

	fd = open(rawdev, O_RDONLY);
    }
    else
        fd = open(dev, O_RDONLY);

    if (fd == -1)
    {
	return fd;
    }
    *dh = (ocfs_vol_disk_hdr *) malloc_aligned(512);
    read_vol_disk_header(fd, *dh);
    return fd;
}

static int libocfs_init_write(const char *dev, ocfs_vol_disk_hdr ** dh)
{
    int fd;

    if (rawminor == 0)
        return -1;

    if (libocfs_bind_raw (dev) == -1)
        return -1;

    fd = open(rawdev, O_RDWR);
    if (fd == -1)
    {
	return fd;
    }
    *dh = (ocfs_vol_disk_hdr *) malloc_aligned(512);
    read_vol_disk_header(fd, *dh);
    return fd;
}

int libocfs_readdir(const char *dev, const char *dir, int recurse,
		    GArray ** arr)
{
    int fd;
    ocfs_vol_disk_hdr *diskHeader;
    __u64 off;
    ocfs_super *vcb;

    if ((fd = libocfs_init(dev, &diskHeader, TRUE)) == -1)
	return 1;

    if (!arr ||
	(*arr = g_array_new(TRUE, TRUE, sizeof(libocfs_stat))) == NULL)
    {
	free_aligned(diskHeader);
	close(fd);
	return 2;
    }

    if (strcmp(dir, "/") == 0)
	off = diskHeader->root_off;
    else
    {
	vcb = get_fake_vcb(fd, diskHeader, DEFAULT_NODE_NUMBER);
	find_file_entry(vcb, diskHeader->root_off, "/", dir, FIND_MODE_DIR,
			(void *) (&off));
	free_aligned(vcb);
    }

    if (off <= 0)
    {
	g_array_free(*arr, FALSE);
	free_aligned(diskHeader);
	close(fd);
	return 3;
    }

    walk_dir_nodes(fd, off, dir, (void *) (*arr));

    close(fd);
    free_aligned(diskHeader);
    return 0;
}

int libocfs_get_bitmap(const char *dev, unsigned char **bmap, int *numbits)
{
    int fd;
    __u64 readlen;
    ocfs_vol_disk_hdr *diskHeader;

    if (!bmap || !numbits)
	return 2;

    if ((fd = libocfs_init(dev, &diskHeader, TRUE)) == -1)
	return 1;

    readlen = 1 << 20;		// now fixed size of 1MB
    *bmap = (void *) malloc_aligned(readlen);

    myseek64(fd, diskHeader->bitmap_off, SEEK_SET);
    read(fd, *bmap, readlen);

    *numbits = diskHeader->num_clusters;

    free_aligned(diskHeader);
    close(fd);
    return 0;
}

int libocfs_get_volume_info(const char *dev, libocfs_volinfo ** info)
{
    int fd;
    ocfs_vol_disk_hdr *diskHeader;

    if (!info)
	return 2;

    if ((fd = libocfs_init(dev, &diskHeader, TRUE)) == -1)
	return 1;

    *info = (libocfs_volinfo *) malloc(sizeof(libocfs_volinfo));
    (*info)->major_ver = diskHeader->major_version;
    (*info)->minor_ver = diskHeader->minor_version;
    memcpy((*info)->signature, diskHeader->signature, 128);
    (*info)->signature[127] = '\0';
    memcpy((*info)->mountpoint, diskHeader->mount_point, 128);
    (*info)->mountpoint[127] = '\0';
    (*info)->length = diskHeader->device_size;
    (*info)->num_extents = diskHeader->num_clusters;
    (*info)->extent_size = diskHeader->cluster_size;
    memset((*info)->mounted_nodes, 0, 32);
    (*info)->protection = diskHeader->prot_bits;
    (*info)->protection |= S_IFDIR;
    (*info)->uid = diskHeader->uid;
    (*info)->gid = diskHeader->gid;

    free_aligned(diskHeader);
    close(fd);
    return 0;
}

int libocfs_is_ocfs_partition(const char *dev)
{
    int fd, ret = 0;
    ocfs_vol_disk_hdr *diskHeader;

    if ((fd = libocfs_init(dev, &diskHeader, FALSE)) == -1)
	return 0;
    if (memcmp(diskHeader->signature,
	       OCFS_VOLUME_SIGNATURE,
	       strlen(OCFS_VOLUME_SIGNATURE)) == 0)
	ret = 1;
    if (diskHeader->major_version >= 9)
        ret = 0;

    free_aligned(diskHeader);
    close(fd);
    return ret;
}

int libocfs_chown_volume(const char *dev, int protection, int uid, int gid)
{
    int fd, ret = 0;
    ocfs_vol_disk_hdr *diskHeader;

    if ((fd = libocfs_init_write(dev, &diskHeader)) == -1)
    {
	ret = 1;
	goto bail;
    }

    diskHeader->prot_bits = (protection & 0007777);	// blank out non-permissions
    diskHeader->uid = uid;
    diskHeader->gid = gid;

    /* XXX totally, completely, utterly UNSAFE!!!!!!!!!!!!!! XXX */
    /* no locking at all, need to figure this out */
    if (write_vol_disk_header(fd, diskHeader) != OCFS_SECTOR_SIZE)
	ret = 2;

    free_aligned(diskHeader);
    close(fd);
  bail:
    return ret;
}


int get_node_config_data(ocfs_super * vcb, GArray * arr)
{
    ocfs_disk_node_config_info *Node;
    void *buffer;
    int i, j, ret = 0;
    libocfs_node libnode;
    int status;
    __u32 tmp;

    buffer = malloc_aligned(vcb->vol_layout.node_cfg_size);

    tmp = LO(vcb->vol_layout.node_cfg_size);
    status =
	ocfs_read_disk(vcb, buffer, tmp, vcb->vol_layout.node_cfg_off);
    if (status < 0)
    {
	ret = 1;
	goto bail;
    }

    for (i = 0; i < OCFS_MAXIMUM_NODES; i++)
    {
	// starts at the 3rd sector
	Node =
	    (ocfs_disk_node_config_info *) ((char *) buffer +
					    ((2 + i) * OCFS_SECTOR_SIZE));

	if (!Node)
	    continue;
	if (Node->node_name[0] == '\0')
	    continue;

	memset(&libnode, 0, sizeof(libocfs_node));
	strncpy(libnode.name, Node->node_name, OCFS_DBGLIB_MAX_NODE_NAME_LENGTH);
	libnode.slot = i;
	strncpy(libnode.addr, Node->ipc_config.ip_addr, OCFS_DBGLIB_IP_ADDR_LEN);
        memcpy(libnode.guid, Node->guid.guid, OCFS_DBGLIB_GUID_LEN);
	g_array_append_val(arr, libnode);
    }
  bail:
    free_aligned(buffer);
    return ret;
}


int libocfs_get_node_map(const char *dev, GArray ** arr)
{
    int fd, ret = 0;
    ocfs_vol_disk_hdr *diskHeader;
    ocfs_super *vcb;

    if ((fd = libocfs_init(dev, &diskHeader, TRUE)) == -1)
    {
	ret = 1;
	goto bail;
    }
    vcb = get_fake_vcb(fd, diskHeader, DEFAULT_NODE_NUMBER);

    if (!arr ||
	(*arr = g_array_new(TRUE, TRUE, sizeof(libocfs_node))) == NULL)
    {
	ret = 2;
	goto free;
    }

    ret = get_node_config_data(vcb, *arr);

  free:
    free_aligned(diskHeader);
    free_aligned(vcb);
    close(fd);
  bail:
    return ret;
}

int libocfs_dump_file(const char *dev, const char *path, const char *file)
{
    return libocfs_dump_file_as_node(dev, path, file, DEFAULT_NODE_NUMBER);
}

int libocfs_dump_file_as_node(const char *dev, const char *path,
			      const char *file, int node)
{
    int fd, ret = 0;
    ocfs_vol_disk_hdr *diskHeader;
    ocfs_super *vcb;

    if ((fd = libocfs_init(dev, &diskHeader, TRUE)) == -1)
    {
	ret = 1;
	goto bail;
    }

    vcb = get_fake_vcb(fd, diskHeader, node);
    ret = suck_file(vcb, path, file);

    free_aligned(diskHeader);
    free_aligned(vcb);
    close(fd);

  bail:
    return ret;
}


void handle_one_file_entry(int fd, ocfs_file_entry *fe, void *buf)
{
    libocfs_stat st;
    int j;
    void *cdslbuf;
    GArray *arr = (GArray *)buf;
        
    memcpy(st.name, fe->filename, 255);
    st.name[254] = '\0';
    st.current_master = fe->disk_lock.curr_master;
    st.size = fe->file_size;
    st.alloc_size = fe->alloc_size;
    st.open_map = (fe->disk_lock.oin_node_map & 0x00000000ffffffffULL);	// uh, this is really still just 32 bits
    st.protection = fe->prot_bits;

    if (fe->attribs & OCFS_ATTRIB_DIRECTORY)
        st.protection |= S_IFDIR;
    else if (fe->attribs & OCFS_ATTRIB_CHAR)
        st.protection |= S_IFCHR;
    else if (fe->attribs & OCFS_ATTRIB_BLOCK)
        st.protection |= S_IFBLK;
    else if (fe->attribs & OCFS_ATTRIB_REG)
        st.protection |= S_IFREG;
    else if (fe->attribs & OCFS_ATTRIB_FIFO)
        st.protection |= S_IFIFO;
    else if (fe->attribs & OCFS_ATTRIB_SYMLINK)
        st.protection |= S_IFLNK;
    else if (fe->attribs & OCFS_ATTRIB_SOCKET)
        st.protection |= S_IFSOCK;
        
    st.uid = fe->uid;
    st.gid = fe->gid;
    st.dir_entries = -1;	// TODO: need to implement or throw it out
        
    st.attribs = 0;
    st.cdsl_bitmap = 0;
#if 0
    if (fe->attribs & OCFS_ATTRIB_FILE_CDSL)
    {
        st.attribs |= OCFS_DBGLIB_ATTRIB_FILE_CDSL;
        cdslbuf = malloc(sizeof(__u64) * MAX_NODES);
        read_cdsl_data(fd, cdslbuf, fe->extents[0].disk_off);
        for (j = 0; j < MAX_NODES; j++)
            if (*(((__u64 *) cdslbuf) + j) != 0)
                st.cdsl_bitmap |= (1 << j);
        free(cdslbuf);
    }
#endif

    g_array_append_val(arr, st);
}
