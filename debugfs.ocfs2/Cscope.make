.PHONY: cscope
cscope:
	rm -f cscope.*
	echo "-k" >> cscope.files
	echo "-I inc" >> cscope.files
	find . -name '*.c' -print >>cscope.files
	find . -name '*.h' -print >>cscope.files
	find ../libocfs2/ -name '*.h' -print >>cscope.files
	find ../libocfs2/ -name '*.c' -print >>cscope.files
	cscope -b
