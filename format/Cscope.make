.PHONY: cscope
cscope:
	rm -f cscope.*
	echo "-k" >> cscope.files
	echo "-I $(TOPDIR)/ocfs2/Common/inc" >> cscope.files
	echo "-I inc" >> cscope.files
	find . -maxdepth 2 -name '*.c' -print >>cscope.files
	find . -maxdepth 2 -name '*.h' -print >>cscope.files
	find ../libocfs -maxdepth 2 -name '*.h' -print >>cscope.files
	find ../../ocfs2/Common -maxdepth 2 -name ocfsheartbeat\* -print >>cscope.files
	find $(TOPDIR)/ocfs2/Common/inc -maxdepth 2 -name 'ocfscom.h' -print >>cscope.files
	find $(TOPDIR)/ocfs2/Common/inc -maxdepth 2 -name 'ocfsvol.h' -print >>cscope.files
	find $(TOPDIR)/ocfs2/Common/inc -maxdepth 2 -name 'ocfstrace.h' -print >>cscope.files
	cscope -b
