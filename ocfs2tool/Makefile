TOPDIR = ..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall

ifdef OCFS_DEBUG
OPTS = -g
endif
OPTS = -g

CFLAGS = $(OPTS) $(WARNINGS) -fPIC

LIBOCFS2_CFLAGS = -I$(TOPDIR)/libocfs2/include
LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a

DEFINES = -DOCFS2_FLAT_INCLUDES -DG_DISABLE_DEPRECATED
INCLUDES = $(LIBOCFS2_CFLAGS) $(PYTHON_INCLUDES) $(GLIB_CFLAGS)

OPTIMIZE = -O2
OPTIMIZE = -O0

CFLAGS += $(OPTIMIZE)

CBITS = \
	ocfsplist.c

HBITS = $(subst .c,.h,$(CBITS))

CFILES = $(CBITS) ocfs2module.c
HFILES = $(HBITS)

OBJS = $(subst .c,.o,$(CFILES))

MANS = ocfs2tool.8

SBIN_EXTRA = ocfs2tool

LIBRARIES = ocfs2module.so

PYSRC = \
	about.py \
	browser.py \
	clconfig.py \
	format.py \
	general.py \
	guiutil.py \
	interface.py \
	menu.py \
	nodemap.py \
	process.py

PYLIB = $(LIBRARIES) $(PYSRC)

INSTALL_RULES = install-pylib

DIST_FILES = $(CFILES) $(HFILES) $(PYSRC) ocfs2tool ocfs2tool.8.in

ocfs2module.so: $(OBJS) $(LIBOCFS2_DEPS)
	$(LINK) -shared $(LIBOCFS2_LIBS) $(COM_ERR_LIBS) $(GLIB_LIBS)

install-pylib:
	$(SHELL) $(TOPDIR)/mkinstalldirs $(DESTDIR)$(pyexecdir)/ocfs2tool
	for f in $(PYLIB); do \
	  $(INSTALL_DATA) $$f $(DESTDIR)$(pyexecdir)/ocfs2tool/$$f; \
	done

include $(TOPDIR)/Postamble.make
