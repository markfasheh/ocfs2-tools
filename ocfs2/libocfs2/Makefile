TOPDIR = ../..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall -Wstrict-prototypes -Wno-format -Wmissing-prototypes \
	-Wmissing-declarations

ifdef OCFS_DEBUG
OPTS += -g
endif

INCLUDES = -I. -Iinclude

LIBRARIES = libocfs.a

CFLAGS = $(OPTS) -fno-strict-aliasing $(WARNINGS)

OPTIMIZE = -O2

ifeq ($(OCFS_PROCESSOR),ppc64)
endif
ifeq ($(OCFS_PROCESSOR),x86_64)
  CFLAGS += -m64
endif
ifeq ($(OCFS_PROCESSOR),ia64)
endif
ifeq ($(OCFS_PROCESSOR),i686)
  DEFINES += -D__ILP32__
endif

CFLAGS += $(OPTIMIZE)

CFILES = 		\
	mounted.c	\

HFILES =				\
	include/kernel-jbh.h		\
	include/kernel-list.h		\
	include/ocfs2_fs.h		\
	include/ocfs1_fs_compat.h
	

$(CFILES): $(HFILES)

OBJS = $(subst .c,.o,$(CFILES))

libocfs.a: $(OBJS)
	rm -f $@
	$(AR) r $@ $^
	$(RANLIB) $@

DIST_FILES = $(CFILES) $(HFILES)

DIST_RULES = dist-subdircreate

dist-subdircreate:
	$(TOPDIR)/mkinstalldirs $(DIST_DIR)/include

include $(TOPDIR)/Postamble.make
