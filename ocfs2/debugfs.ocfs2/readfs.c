
#include <main.h>
#include <commands.h>
#include <dump.h>
#include <readfs.h>

/*
 * read_super_block()
 *
 */
int read_super_block(int fd, char *buf, int buflen, __u32 *bits)
{
	int ret = -1;
	__u64 off;
	ocfs1_vol_disk_hdr *hdr;
	ocfs2_dinode *di;

	if ((ret = pread64(fd, buf, buflen, 0)) == -1) {
		LOG_INTERNAL("%s", strerror(errno));
		goto bail;
	}

	hdr = (ocfs1_vol_disk_hdr *)buf;
	if (memcmp(hdr->signature, OCFS1_VOLUME_SIGNATURE,
		   strlen (OCFS1_VOLUME_SIGNATURE)) == 0) {
		printf("OCFS1 detected. Use debugocfs.\n");
		goto bail;
	}

	/*
	 * Now check at magic offset for 512, 1024, 2048, 4096
	 * blocksizes.  4096 is the maximum blocksize because it is
	 * the minimum clustersize.
	 */
	for (*bits = 9; *bits < 13; (*bits)++) {
		off = OCFS2_SUPER_BLOCK_BLKNO << *bits;

		if ((ret = pread64(fd, buf, buflen, off)) == -1) {
			LOG_INTERNAL("%s", strerror(errno));
			goto bail;
		}

		di = (ocfs2_dinode *) buf;
		if (memcmp(di->i_signature, OCFS2_SUPER_BLOCK_SIGNATURE,
			   strlen(OCFS2_SUPER_BLOCK_SIGNATURE))) {
			printf("Not an OCFS2 volume.\n");
			goto bail;
		} else {
			ret = 0;
			break;
		}
	}
bail:
	return ret;
}				/* read_super_block */
