TOPDIR = ..

include $(TOPDIR)/Preamble.make

sbindir = $(root_sbindir)
SBIN_PROGRAMS = defragfs.ocfs2

DEFINES += -DVERSION=\"$(VERSION)\"


INCLUDES = -I$(TOPDIR)/include -I./include
DEFINES = -DVERSION=\"$(VERSION)\"

CFILES = main.c record.c libdefrag.c
HFILES =	\
	include/libdefrag.h	\
	include/o2defrag.h	\
	include/record.h

OBJS = $(subst .c,.o,$(CFILES))


DIST_FILES = $(CFILES) $(HFILES) defragfs.ocfs2.8.in

DIST_RULES = dist-subdircreate
MANS = defragfs.ocfs2.8

defragfs.ocfs2: $(OBJS)
	$(LINK)

dist-subdircreate:
	$(TOPDIR)/mkinstalldirs $(DIST_DIR)/include

CLEAN_RULES = defragfs-clean 

defragfs-clean:
	rm  defragfs.ocfs2.8

.PHONY: defragfs-clean
include $(TOPDIR)/Postamble.make
