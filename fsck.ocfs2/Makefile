TOPDIR = ..

include $(TOPDIR)/Preamble.make

SBIN_PROGRAMS = fsck.ocfs2

INCLUDES = -Iinclude -I$(TOPDIR)/libocfs2/include
LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2

CFLAGS := -g -Wall -Wstrict-prototypes -Wmissing-prototypes \
           -Wmissing-declarations

DEFINES = -DOCFS2_FLAT_INCLUDES

CFILES =	fsck.c		\
		dirblocks.c 	\
		dirparents.c 	\
		icount.c 	\
		pass1.c 	\
		pass2.c 	\
		pass3.c 	\
		problem.c 	\
		strings.c 	\
		util.c

HFILES = 	include/fsck.h		\
		include/dirblocks.h	\
		include/dirparents.h	\
		include/icount.h	\
		include/pass1.h		\
		include/pass2.h		\
		include/pass3.h		\
		include/problem.h	\
		include/strings.h	\
		include/util.h

OBJS = $(subst .c,.o,$(CFILES))

DIST_FILES = $(CFILES) $(HFILES) 
DIST_RULES = dist-subdircreate

dist-subdircreate:
	$(TOPDIR)/mkinstalldirs $(DIST_DIR)/include

fsck.ocfs2: $(OBJS)
	$(LINK) $(LIBOCFS2_LIBS) $(COM_ERR_LIBS)

include $(TOPDIR)/Postamble.make
