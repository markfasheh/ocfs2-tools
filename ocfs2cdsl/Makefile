TOPDIR = ..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall

ifdef OCFS_DEBUG
OPTS = -g
endif

CFLAGS = $(OPTS) $(WARNINGS) 

SBIN_PROGRAMS = ocfs2cdsl

DEFINES = -DG_DISABLE_DEPRECATED
INCLUDES = $(GLIB_CFLAGS)

OPTIMIZE = -g -O2

CFLAGS += $(OPTIMIZE)

VERSION_FILES = ocfs2cdsl.c
VERSION_SRC = ocfs2cdsl.c
VERSION_PREFIX = OCFS2

DIST_RULES = dist-incdir

#MANS = ocfs2cdsl.8

DIST_FILES = $(VERSION_FILES) $(VERSION_SRC) ocfs2cdsl.c #ocfs2cdsl.8.in

ocfs2cdsl: ocfs2cdsl.o
	$(LINK) $(GLIB_LIBS)

include $(TOPDIR)/Postamble.make
