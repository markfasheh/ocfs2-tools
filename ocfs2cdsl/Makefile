TOPDIR = ..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall

ifdef OCFS_DEBUG
OPTS = -g
endif

CFLAGS = $(OPTS) $(WARNINGS) 

SBIN_PROGRAMS = ocfs2cdsl

DEFINES = -DG_DISABLE_DEPRECATED -DVERSION=\"$(VERSION)\"
INCLUDES = $(GLIB_CFLAGS)

OPTIMIZE = -g -O2

CFLAGS += $(OPTIMIZE)

CFILES = ocfs2cdsl.c
OBJS = $(subst .c,.o,$(CFILES))

DIST_RULES = dist-incdir

#MANS = ocfs2cdsl.8

DIST_FILES = $(CFILES) #ocfs2cdsl.8.in

ocfs2cdsl: $(OBJS)
	$(LINK) $(GLIB_LIBS)

include $(TOPDIR)/Postamble.make
