TOPDIR = ..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall -Wstrict-prototypes -Wmissing-prototypes \
           -Wmissing-declarations

ifdef OCFS_DEBUG
OPTS = -g
endif

CFLAGS = $(OPTS) $(WARNINGS) 

UNINST_PROGRAMS = find_hardlinks find_dup_extents find_inode_paths set_random_bits

INCLUDES = -I../libocfs2/include

OPTIMIZE = -O2

DEFINES += -DOCFS2_FLAT_INCLUDES

CFLAGS += $(OPTIMIZE)

FIND_HARDLINKS_CFILES = find_hardlinks.c
FIND_DUP_EXTENTS_CFILES = find_dup_extents.c
FIND_INODE_PATHS_CFILES = find_inode_paths.c
SET_RANDOM_BITS_CFILES = set_random_bits.c

DIST_FILES = $(FIND_HARDLINKS_CFILES) $(FIND_DUP_EXTENTS_CFILES) $(FIND_INODE_PATHS_CFILES) $(SET_RANDOM_BITS_CFILES)

FIND_HARDLINKS_OBJS = $(subst .c,.o,$(FIND_HARDLINKS_CFILES))
FIND_DUP_EXTENTS_OBJS = $(subst .c,.o,$(FIND_DUP_EXTENTS_CFILES))
FIND_INODE_PATHS_OBJS = $(subst .c,.o,$(FIND_INODE_PATHS_CFILES))
SET_RANDOM_BITS_OBJS  = $(subst .c,.o,$(SET_RANDOM_BITS_CFILES))
LIBOCFS2 = ../libocfs2/libocfs2.a
LIBS = $(LIBOCFS2) $(COM_ERR_LIBS)

find_hardlinks: $(FIND_HARDLINKS_OBJS) $(LIBS)
	$(LINK) 

find_dup_extents: $(FIND_DUP_EXTENTS_OBJS) $(LIBS)
	$(LINK) 

find_inode_paths: $(FIND_INODE_PATHS_OBJS) $(LIBS)
	$(LINK) 

set_random_bits: $(SET_RANDOM_BITS_OBJS) $(LIBS)
	$(LINK) 

include $(TOPDIR)/Postamble.make
