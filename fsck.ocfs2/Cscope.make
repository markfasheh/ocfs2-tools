.PHONY: cscope
cscope:
	rm -f cscope.*
	echo "-k" >> cscope.files
	echo "-I include" >> cscope.files
	find . -maxdepth 2 -name '*.c' -print >>cscope.files
	find . -maxdepth 2 -name '*.h' -print >>cscope.files
	cscope -b
