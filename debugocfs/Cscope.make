cscope:
	rm -f cscope.*
	echo "-k" >> cscope.files
	echo "-I ../../ocfs2/Common/inc" >> cscope.files
	echo "-I ../../ocfs2/Linux/inc" >> cscope.files
	find . -name '*.c' -print >>cscope.files
	find . -name '*.h' -print >>cscope.files
	cscope -b
