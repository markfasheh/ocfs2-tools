cscope:
	rm -f cscope.*
	echo "-k" >> cscope.files
	echo "-I ../libocfs/Common/inc" >> cscope.files
	echo "-I ../libocfs/Linux/inc" >> cscope.files
	find . -name '*.c' -print >>cscope.files
	find . -name '*.h' -print >>cscope.files
	find ../libocfs -name '*.h' -print >>cscope.files
	find ../libocfs -name '*.c' -print >>cscope.files
	cscope -b
