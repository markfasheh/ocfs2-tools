TOPDIR = ../..

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

LIBO2DLM_CFLAGS = -I$(TOPDIR)/libo2dlm/include
LIBO2DLM_LIBS = -L$(TOPDIR)/libo2dlm -lo2dlm
LIBO2DLM_DEPS = $(TOPDIR)/libo2dlm/libo2dlm.a

ifdef HAVE_BLKID
BLKID_DEPS =
else
BLKID_DEPS = $(TOPDIR)/ocfs2console/blkid/libblkid-internal.a

BLKID_CFLAGS = -I$(TOPDIR)/ocfs2console
BLKID_LIBS = -L$(TOPDIR)/ocfs2console/blkid -lblkid-internal -luuid
endif

DEFINES = -DOCFS2_FLAT_INCLUDES -DO2DLM_FLAT_INCLUDES -DG_DISABLE_DEPRECATED
INCLUDES = $(LIBOCFS2_CFLAGS) $(LIBO2DLM_CFLAGS) $(BLKID_CFLAGS) $(PYTHON_INCLUDES) $(GLIB_CFLAGS)

OPTIMIZE = -O2
OPTIMIZE = -O0

CFLAGS += $(OPTIMIZE)

CBITS = \
	ocfsplist.c

HBITS = $(subst .c,.h,$(CBITS))

CFILES = $(CBITS) ocfs2module.c
HFILES = $(HBITS)

OBJS = $(subst .c,.o,$(CFILES))

LIBRARIES = ocfs2module.so

PYSRC = \
	__init__.py \
	about.py \
	browser.py \
	clconfig.py \
	console.py \
	format.py \
	fsck.py \
	fswidgets.py \
	general.py \
	guiutil.py \
	menu.py \
	nodemap.py \
	process.py \
	toolbar.py \
	tune.py

PYLIB = $(LIBRARIES) $(PYSRC)

INSTALL_RULES = install-pylib

DIST_FILES = $(CFILES) $(HFILES) $(PYSRC)

ocfs2module.so: $(OBJS) $(LIBOCFS2_DEPS) $(LIBO2DLM_DEPS) $(BLKID_DEPS)
	$(LINK) -shared $(LIBOCFS2_LIBS) $(LIBO2DLM_LIBS) $(BLKID_LIBS) $(COM_ERR_LIBS) $(GLIB_LIBS)

install-pylib:
	$(SHELL) $(TOPDIR)/mkinstalldirs $(DESTDIR)$(pyexecdir)/ocfs2interface
	for f in $(PYLIB); do \
	  $(INSTALL_DATA) $$f $(DESTDIR)$(pyexecdir)/ocfs2interface/$$f; \
	done

include $(TOPDIR)/Postamble.make
