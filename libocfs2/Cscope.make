.PHONY: cscope
cscope:
	rm -f cscope.*
	echo "-k" >> cscope.files
	echo "-I include" >> cscope.files
	find . -maxdepth 2 -name '*.c' -print >>cscope.files
	find . -maxdepth 2 -name '*.h' -print >>cscope.files
	find ../libo2dlm -name '*.c' >>cscope.files
	find ../libo2dlm -name '*.h' >>cscope.files
	find ../libo2cb -name '*.c' >>cscope.files
	find ../libo2cb -name '*.h' >>cscope.files
	cscope -b
