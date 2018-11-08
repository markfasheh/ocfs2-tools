#ifndef __O2DEFRAG_H__
#define __O2DEFRAG_H__

#define FS_OCFS2 "ocfs2"

#define DETAIL			0x01
#define STATISTIC		0x02
#define GO_ON			0x04
#define LOW_IO			0x08

#define DEVNAME			0
#define DIRNAME			1
#define FILENAME		2

#define FTW_OPEN_FD		2000

#define ROOT_UID		0

#define SHOW_FRAG_FILES	5
#define SCHEDULE_TIME_LIMIT	5

#define RECORD_EVERY_N_FILES	50

#ifndef OCFS2_IOC_MOVE_EXT
#define OCFS2_IOC_MOVE_EXT	_IOW('o', 6, struct ocfs2_move_extents)
#endif

struct o2defrag_opt {
	int o_num;
	char *o_str;
};

#define declare_opt(n, s)  {\
	.o_num = n,\
	.o_str = s\
}

#define PROGRAME_NAME "defragfs.ocfs2"

#endif
