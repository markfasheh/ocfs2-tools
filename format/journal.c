#include <format.h>
#include <signal.h>
#include <libgen.h>

#include <netinet/in.h>

#include "kernel-jbd.h"

int ocfs_replacement_journal_create(int file, __u64 journal_off)
{
	int status = 0;
	journal_superblock_t *sb;

	/* zero out all 8mb and stamp this little sb header on it */
	sb = (journal_superblock_t *) MemAlloc(OCFS_JOURNAL_DEFAULT_SIZE);
	if (sb == NULL)
		return 0;
	
	memset(sb, 0, OCFS_JOURNAL_DEFAULT_SIZE);

	sb->s_header.h_magic	 = htonl(JFS_MAGIC_NUMBER);
	sb->s_header.h_blocktype = htonl(JFS_SUPERBLOCK_V2);
	sb->s_blocksize	= htonl(512);
	sb->s_maxlen	= htonl(OCFS_JOURNAL_DEFAULT_SIZE / 512);
	sb->s_first	= htonl(1);
	sb->s_start     = htonl(1);
	sb->s_sequence  = htonl(1);
	sb->s_errno     = htonl(0);

	if (SetSeek(file, journal_off)) {
                if (Write(file, OCFS_JOURNAL_DEFAULT_SIZE, (void *) sb)) {
			status = 1;
			fsync(file);
		}
	}
	safefree(sb);
	return status;
}
