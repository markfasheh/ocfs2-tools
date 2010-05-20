TOPDIR = ..

include $(TOPDIR)/Preamble.make

UNINST_PROGRAMS = find_hardlinks find_dup_extents find_inode_paths set_random_bits decode_lockres encode_lockres mark_journal_dirty find_allocation_fragments compute_groups check_metaecc

INCLUDES = -I$(TOPDIR)/include

FIND_HARDLINKS_CFILES = find_hardlinks.c
FIND_DUP_EXTENTS_CFILES = find_dup_extents.c
FIND_INODE_PATHS_CFILES = find_inode_paths.c
SET_RANDOM_BITS_CFILES = set_random_bits.c
DECODE_LOCKRES_CFILES = decode_lockres.c
ENCODE_LOCKRES_CFILES = encode_lockres.c
MARK_JOURNAL_DIRTY_CFILES = mark_journal_dirty.c
FIND_ALLOC_FRAG_CFILES = find_allocation_fragments.c
COMPUTE_GROUPS_CFILES = compute_groups.c
CHECK_METAECC_CFILES = check_metaecc.c

DIST_FILES = $(FIND_HARDLINKS_CFILES) $(FIND_DUP_EXTENTS_CFILES) $(FIND_INODE_PATHS_CFILES) $(SET_RANDOM_BITS_CFILES) $(DECODE_LOCKRES_CFILES) $(ENCODE_LOCKRES_CFILES) $(MARK_JOURNAL_DIRTY_CFILES) $(FIND_ALLOC_FRAG_CFILES) $(COMPUTE_GROUPS_CFILES) $(CHECK_METAECC_CFILES)

FIND_HARDLINKS_OBJS = $(subst .c,.o,$(FIND_HARDLINKS_CFILES))
FIND_DUP_EXTENTS_OBJS = $(subst .c,.o,$(FIND_DUP_EXTENTS_CFILES))
FIND_INODE_PATHS_OBJS = $(subst .c,.o,$(FIND_INODE_PATHS_CFILES))
SET_RANDOM_BITS_OBJS  = $(subst .c,.o,$(SET_RANDOM_BITS_CFILES))
DECODE_LOCKRES_OBJS  = $(subst .c,.o,$(DECODE_LOCKRES_CFILES))
ENCODE_LOCKRES_OBJS  = $(subst .c,.o,$(ENCODE_LOCKRES_CFILES))
MARK_JOURNAL_DIRTY_OBJS = $(subst .c,.o,$(MARK_JOURNAL_DIRTY_CFILES))
FIND_ALLOC_FRAG_OBJS = $(subst .c,.o,$(FIND_ALLOC_FRAG_CFILES))
COMPUTE_GROUPS_OBJS = $(subst .c,.o,$(COMPUTE_GROUPS_CFILES))
CHECK_METAECC_OBJS = $(subst .c,.o,$(CHECK_METAECC_CFILES))

LIBOCFS2 = ../libocfs2/libocfs2.a
EXTRAS_LIBS = $(LIBOCFS2) $(COM_ERR_LIBS)

find_hardlinks: $(FIND_HARDLINKS_OBJS) $(LIBOCFS2)
	$(LINK) $(EXTRAS_LIBS)

find_dup_extents: $(FIND_DUP_EXTENTS_OBJS) $(LIBOCFS2)
	$(LINK) $(EXTRAS_LIBS)

find_inode_paths: $(FIND_INODE_PATHS_OBJS) $(LIBOCFS2)
	$(LINK) $(EXTRAS_LIBS)

set_random_bits: $(SET_RANDOM_BITS_OBJS) $(LIBOCFS2)
	$(LINK) $(EXTRAS_LIBS)

decode_lockres: $(DECODE_LOCKRES_OBJS) $(LIBOCFS2)
	$(LINK) $(EXTRAS_LIBS)

encode_lockres: $(ENCODE_LOCKRES_OBJS) $(LIBOCFS2)
	$(LINK) $(EXTRAS_LIBS)

mark_journal_dirty: $(MARK_JOURNAL_DIRTY_OBJS) $(LIBOCFS2)
	$(LINK) $(EXTRAS_LIBS)

find_allocation_fragments: $(FIND_ALLOC_FRAG_OBJS) $(LIBOCFS2)
	$(LINK) $(EXTRAS_LIBS)

compute_groups: $(COMPUTE_GROUPS_OBJS) $(LIBOCFS2)
	$(LINK) $(EXTRAS_LIBS)

check_metaecc: $(CHECK_METAECC_OBJS) $(LIBOCFS2)
	$(LINK) $(EXTRAS_LIBS)
include $(TOPDIR)/Postamble.make
