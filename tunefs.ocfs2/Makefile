TOPDIR = ..

include $(TOPDIR)/Preamble.make

LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a

LIBO2DLM_LIBS = -L$(TOPDIR)/libo2dlm -lo2dlm $(DL_LIBS)
LIBO2DLM_DEPS = $(TOPDIR)/libo2dlm/libo2dlm.a

LIBO2CB_LIBS = -L$(TOPDIR)/libo2cb -lo2cb
LIBO2CB_DEPS = $(TOPDIR)/libo2cb/libo2cb.a

UNINST_LIBRARIES = libtunefs.a

OCFS2NE_METHODS =				\
	ocfs2ne_feature_backup_super		\
	ocfs2ne_feature_extended_slotmap	\
	ocfs2ne_list_sparse_files		\
	ocfs2ne_reset_uuid			\
	ocfs2ne_set_label			\
	ocfs2ne_set_journal_size		\
	ocfs2ne_set_mount_type			\
	ocfs2ne_set_slot_count			\
	ocfs2ne_update_cluster_stack

sbindir = $(root_sbindir)
SBIN_PROGRAMS = tunefs.ocfs2 $(OCFS2NE_METHODS)

INCLUDES = -I$(TOPDIR)/include -I.
DEFINES = -DVERSION=\"$(VERSION)\"

MANS = tunefs.ocfs2.8

ifneq ($(OCFS2_DEBUG_EXE),)
DEBUG_EXE_FILES = $(shell awk '/DEBUG_EXE/{if (k[FILENAME] == 0) {print FILENAME; k[FILENAME] = 1;}}' $(CFILES))
DEBUG_EXE_PROGRAMS = $(addprefix debug_,$(subst .c,,$(DEBUG_EXE_FILES)))

.SECONDARY:

UNINST_PROGRAMS += $(DEBUG_EXE_PROGRAMS)

debug_%.o : %.c
	$(CC) $(CFLAGS) $(LOCAL_CFLAGS) $(CPPFLAGS) $(LOCAL_CPPFLAGS) \
		$(INCLUDES) $(DEFINES) \
		-DDEBUG_EXE -o $@ -c $<

debug_%: debug_%.o $(LIBOCFS2_DEPS) $(LIBO2DLM_DEPS) $(LIBO2CB_DEPS) libtunefs.a
	$(LINK) $(LIBOCFS2_LIBS) $(UUID_LIBS) $(LIBO2DLM_LIBS) \
		$(LIBO2CB_LIBS) $(COM_ERR_LIBS)
endif

LIBTUNEFS_CFILES = libtunefs.c foreach.c
HFILES_GEN = libtunefs_err.h

libtunefs_err.c libtunefs_err.h: libtunefs_err.et
	compile_et libtunefs_err.et


TUNEFS_CFILES =		\
	tunefs.c	\
	query.c		\
	remove_slot.c	\
	sparse_file.c	\
	features.c	\
	resize.c	\
	format_slotmap.c

OCFS2NE_CFILES = $(patsubst %,%.c,$(OCFS2NE_METHODS))

CFILES = $(TUNFES_CFILES) $(LIBTUNEFS_CFILES) $(OCFS2NE_CFILES)
HFILES = tunefs.h libtunefs.h

TUNEFS_OBJS = $(subst .c,.o,$(TUNEFS_CFILES))
LIBTUNEFS_OBJS = $(subst .c,.o,$(LIBTUNEFS_CFILES)) libtunefs_err.o
OCFS2NE_OBJS = $(subst .c,.o,$(OCFS2NE_CFILES))

$(LIBTUNEFS_OBJS): $(HFILES_GEN)
$(OCFS2NE_OBJS): $(HFILES_GEN)

DIST_FILES = $(CFILES) $(HFILES) tunefs.ocfs2.8.in libtunefs_err.et

libtunefs.a: $(LIBTUNEFS_OBJS)
	rm -f $@
	$(AR) r $@ $^
	$(RANLIB) $@

tunefs.ocfs2: $(TUNEFS_OBJS) $(LIBOCFS2_DEPS) $(LIBO2DLM_DEPS) $(LIBO2CB_DEPS)
	$(LINK) $(LIBOCFS2_LIBS) $(UUID_LIBS) $(LIBO2DLM_LIBS) $(LIBO2CB_LIBS) $(COM_ERR_LIBS)

ocfs2ne_%: ocfs2ne_%.o $(LIBOCFS2_DEPS) $(LIBO2DLM_DEPS) $(LIBO2CB_DEPS) libtunefs.a
	$(LINK) $(LIBOCFS2_LIBS) $(UUID_LIBS) $(LIBO2DLM_LIBS) \
		$(LIBO2CB_LIBS) $(COM_ERR_LIBS)

CLEAN_RULES = clean-err

clean-err:
	rm -f libtunefs_err.c libtunefs_err.h

include $(TOPDIR)/Postamble.make
