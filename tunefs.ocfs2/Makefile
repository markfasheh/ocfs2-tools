TOPDIR = ..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall -Wstrict-prototypes -Wno-format -Wmissing-prototypes \
           -Wmissing-declarations

OPTS = -g
ifdef OCFS_DEBUG
OPTS = -g
endif

CFLAGS = $(OPTS) -fno-strict-aliasing $(WARNINGS) 

LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2

SBIN_PROGRAMS = tunefs.ocfs2

INCLUDES = -I$(TOPDIR)/libocfs2/include
DEFINES = -DOCFS2_FLAT_INCLUDES -DVERSION=\"$(VERSION)\"

OPTIMIZE = -O2

ifeq ($(OCFS_PROCESSOR),x86_64)
  CFLAGS += -m64
endif
ifeq ($(OCFS_PROCESSOR),ia64)
endif
ifeq ($(OCFS_PROCESSOR),i686)
  DEFINES += -D__ILP32__
endif

CFLAGS += $(OPTIMIZE)

VERSION_FILES = tunefs.c
VERSION_SRC = tunefs.c
VERSION_PREFIX = OCFS2

#MANS = fsck.ocfs2.8

DIST_FILES = $(VERSION_FILES) $(VERSION_SRC)

tunefs.ocfs2: tunefs.o
	$(LINK) $(LIBOCFS2_LIBS) $(COM_ERR_LIBS) $(VERMAGIC)

include $(TOPDIR)/Postamble.make
