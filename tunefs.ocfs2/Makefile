TOPDIR = ..

include $(TOPDIR)/Preamble.make

LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a

LIBO2DLM_LIBS = -L$(TOPDIR)/libo2dlm -lo2dlm $(DL_LIBS)
LIBO2DLM_DEPS = $(TOPDIR)/libo2dlm/libo2dlm.a

LIBO2CB_LIBS = -L$(TOPDIR)/libo2cb -lo2cb
LIBO2CB_DEPS = $(TOPDIR)/libo2cb/libo2cb.a

UNINST_LIBRARIES = libocfs2ne.a

OCFS2NE_FEATURES =			\
	feature_backup_super		\
	feature_extended_slotmap	\
	feature_inline_data		\
	feature_local			\
	feature_sparse_files		\
	feature_unwritten_extents

OCFS2NE_OPERATIONS =			\
	op_features			\
	op_list_sparse_files		\
	op_query			\
	op_reset_uuid			\
	op_resize_volume		\
	op_set_label			\
	op_set_journal_size		\
	op_set_slot_count		\
	op_update_cluster_stack

sbindir = $(root_sbindir)
SBIN_PROGRAMS = tunefs.ocfs2 ocfs2ne

INCLUDES = -I$(TOPDIR)/include -I.
DEFINES = -DVERSION=\"$(VERSION)\"

MANS = tunefs.ocfs2.8

ifneq ($(OCFS2_DEBUG_EXE),)
DEBUG_EXE_FILES = $(shell awk '/DEBUG_EXE/{if (k[FILENAME] == 0) {print FILENAME; k[FILENAME] = 1;}}' $(CFILES))
DEBUG_EXE_PROGRAMS = $(addprefix debug_,$(subst .c,,$(DEBUG_EXE_FILES)))

.SECONDARY:

UNINST_PROGRAMS += $(filter-out debug_op_features,$(DEBUG_EXE_PROGRAMS))

debug_%.o : %.c
	$(CC) $(CFLAGS) $(LOCAL_CFLAGS) $(CPPFLAGS) $(LOCAL_CPPFLAGS) \
		$(INCLUDES) $(DEFINES) \
		-DDEBUG_EXE -o $@ -c $<

debug_op_features: debug_op_features.o $(OCFS2NE_FEATURE_OBJS) libocfs2ne.a $(LIBOCFS2_DEPS) $(LIBO2DLM_DEPS) $(LIBO2CB_DEPS)
	$(LINK) $(LIBOCFS2_LIBS) $(UUID_LIBS) $(LIBO2DLM_LIBS) \
		$(LIBO2CB_LIBS) $(COM_ERR_LIBS)

debug_%: debug_%.o libocfs2ne.a $(LIBOCFS2_DEPS) $(LIBO2DLM_DEPS) $(LIBO2CB_DEPS)
	$(LINK) $(LIBOCFS2_LIBS) $(UUID_LIBS) $(LIBO2DLM_LIBS) \
		$(LIBO2CB_LIBS) $(COM_ERR_LIBS)
endif

LIBOCFS2NE_CFILES = libocfs2ne.c
HFILES_GEN = o2ne_err.h

o2ne_err.c o2ne_err.h: o2ne_err.et
	compile_et o2ne_err.et


TUNEFS_CFILES =		\
	tunefs.c	\
	query.c		\
	remove_slot.c	\
	sparse_file.c	\
	features.c	\
	resize.c	\
	format_slotmap.c \
	inline_data.c

OCFS2NE_OPERATION_CFILES = $(patsubst %,%.c,$(OCFS2NE_OPERATIONS))
OCFS2NE_FEATURE_CFILES = $(patsubst %,%.c,$(OCFS2NE_FEATURES))
OCFS2NE_CFILES =			\
	ocfs2ne.c			\
	$(OCFS2NE_OPERATION_CFILES)	\
	$(OCFS2NE_FEATURE_CFILES)

CFILES = $(TUNFES_CFILES) $(LIBOCFS2NE_CFILES) $(OCFS2NE_CFILES)
HFILES = tunefs.h libocfs2ne.h

TUNEFS_OBJS = $(subst .c,.o,$(TUNEFS_CFILES))
LIBOCFS2NE_OBJS = $(subst .c,.o,$(LIBOCFS2NE_CFILES)) o2ne_err.o
OCFS2NE_OBJS = $(subst .c,.o,$(OCFS2NE_CFILES))

$(LIBOCFS2NE_OBJS): $(HFILES_GEN)
$(OCFS2NE_OBJS): $(HFILES_GEN)

DIST_FILES = $(CFILES) $(HFILES) tunefs.ocfs2.8.in o2ne_err.et

libocfs2ne.a: $(LIBOCFS2NE_OBJS)
	rm -f $@
	$(AR) r $@ $^
	$(RANLIB) $@

tunefs.ocfs2: $(TUNEFS_OBJS) $(LIBOCFS2_DEPS) $(LIBO2DLM_DEPS) $(LIBO2CB_DEPS)
	$(LINK) $(LIBOCFS2_LIBS) $(UUID_LIBS) $(LIBO2DLM_LIBS) $(LIBO2CB_LIBS) $(COM_ERR_LIBS)

ocfs2ne: $(OCFS2NE_OBJS) libocfs2ne.a $(LIBOCFS2_DEPS) $(LIBO2DLM_DEPS) $(LIBO2CB_DEPS)
	$(LINK) $(LIBOCFS2_LIBS) $(UUID_LIBS) $(LIBO2DLM_LIBS) \
		$(LIBO2CB_LIBS) $(COM_ERR_LIBS)

CLEAN_RULES = clean-err

clean-err:
	rm -f o2ne_err.c o2ne_err.h

include $(TOPDIR)/Postamble.make
