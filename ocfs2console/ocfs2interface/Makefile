TOPDIR = ../..

include $(TOPDIR)/Preamble.make

INCLUDES = -I$(TOPDIR)/include

CFLAGS += -fPIC

PYMOD_CFLAGS = -fno-strict-aliasing $(PYTHON_INCLUDES)

LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2 -laio
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a

LIBO2DLM_LIBS = -L$(TOPDIR)/libo2dlm -lo2dlm $(DL_LIBS)
LIBO2DLM_DEPS = $(TOPDIR)/libo2dlm/libo2dlm.a

ifneq ($(BUILD_FSDLM_SUPPORT),)
LIBO2CB_LIBS = -L$(TOPDIR)/libo2cb -lo2cb -ldlm_lt
else
LIBO2CB_LIBS = -L$(TOPDIR)/libo2cb -lo2cb
endif
ifneq ($(BUILD_CMAP_SUPPORT),)
LIBO2CB_LIBS += -lcmap
endif
LIBO2CB_DEPS = $(TOPDIR)/libo2cb/libo2cb.a

ifdef HAVE_BLKID
BLKID_DEPS =
else
BLKID_DEPS = $(TOPDIR)/ocfs2console/blkid/libblkid-internal.a

BLKID_CFLAGS = -I$(TOPDIR)/ocfs2console
BLKID_LIBS = -L$(TOPDIR)/ocfs2console/blkid -lblkid-internal $(UUID_LIBS)
endif

GLIB_CPPFLAGS = $(GLIB_CFLAGS) -DG_DISABLE_DEPRECATED

PLIST_CBITS = ocfsplist.c
PLIST_CFILES = $(PLIST_CBITS) plistmodule.c
PLIST_HFILES = $(subst .c,.h,$(PLIST_CBITS))

ocfsplist_CPPFLAGS = $(GLIB_CPPFLAGS) $(BLKID_CFLAGS)
plistmodule_CPPFLAGS = $(GLIB_CPPFLAGS)
plistmodule_CFLAGS = $(PYMOD_CFLAGS)

GIDLE_CFILES = gidlemodule.c
gidlemodule_CPPFLAGS = $(GLIB_CPPFLAGS)
gidlemodule_CFLAGS = $(PYMOD_CFLAGS)

O2CB_CFILES = o2cbmodule.c
o2cbmodule_CPPFLAGS = $(O2CB_CPPFLAGS)
o2cbmodule_CFLAGS = $(PYMOD_CFLAGS)

OCFS2_CFILES = ocfs2module.c
ocfs2module_CPPFLAGS = $(OCFS2_CPPFLAGS)
ocfs2module_CFLAGS = $(PYMOD_CFLAGS)

PLIST_OBJS = $(subst .c,.o,$(PLIST_CFILES))
GIDLE_OBJS = $(subst .c,.o,$(GIDLE_CFILES))
OCFS2_OBJS = $(subst .c,.o,$(OCFS2_CFILES))
O2CB_OBJS = $(subst .c,.o,$(O2CB_CFILES))

# These are UNINST_ because the python install rule will handle them.
UNINST_LIBRARIES = plistmodule.so gidlemodule.so ocfs2module.so o2cbmodule.so

PYSRC = \
	__init__.py		\
	about.py		\
	bosa.py			\
	classlabel.py		\
	console.py		\
	format.py		\
	fsck.py			\
	fstab.py		\
	fswidgets.py		\
	general.py		\
	guiutil.py		\
	ipwidget.py		\
	ls.py			\
	mount.py		\
	menu.py			\
	nodeconfig.py		\
	o2cb_ctl.py		\
	partitionview.py	\
	process.py		\
	pushconfig.py		\
	terminal.py		\
	toolbar.py		\
	tune.py

BUILT_PYSRC = confdefs.py

PYLIB = $(UNINST_LIBRARIES) $(PYSRC) $(BUILT_PYSRC)

INSTALL_RULES = install-pylib

DIST_FILES = $(PLIST_CFILES) $(PLIST_HFILES) $(GIDLE_CFILES) $(OCFS2_CFILES) $(O2CB_CFILES) $(PYSRC) $(addsuffix .in,$(BUILT_PYSRC))

plistmodule.so: $(PLIST_OBJS) $(LIBOCFS2_DEPS) $(LIBO2DLM_DEPS) $(LIBO2CB_DEPS) $(BLKID_DEPS)
	$(LINK) -shared $(LIBOCFS2_LIBS) $(LIBO2DLM_LIBS) $(LIBO2CB_LIBS) $(BLKID_LIBS) $(COM_ERR_LIBS) $(GLIB_LIBS) $(shell python-config --libs)

gidlemodule.so: $(GIDLE_OBJS)
	$(LINK) -shared $(GLIB_LIBS) $(shell python-config --libs)

ocfs2module.so: $(OCFS2_OBJS) $(LIBOCFS2_DEPS) $(LIBO2DLM_DEPS) $(LIBO2CB_DEPS)
	$(LINK) -shared $(LIBOCFS2_LIBS) $(LIBO2DLM_LIBS) $(LIBO2CB_LIBS) $(COM_ERR_LIBS) $(UUID_LIBS) $(shell python-config --libs)

o2cbmodule.so: $(O2CB_OBJS) $(LIBO2CB_DEPS)
	$(LINK) -shared $(LIBOCFS2_LIBS) $(LIBO2CB_LIBS) $(COM_ERR_LIBS) $(shell python-config --libs)

install-pylib:
	$(SHELL) $(TOPDIR)/mkinstalldirs $(DESTDIR)$(pyexecdir)/ocfs2interface
	for f in $(PYLIB); do \
	  $(INSTALL_DATA) $$f $(DESTDIR)$(pyexecdir)/ocfs2interface/$$f; \
	done

include $(TOPDIR)/Postamble.make
