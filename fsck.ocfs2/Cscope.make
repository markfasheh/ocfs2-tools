.PHONY: cscope
cscope:
	rm -f cscope.*
	echo "-k" >> cscope.files
	echo "-I include" >> cscope.files
	find . -name '*.[ch]' >>cscope.files
	find ../libocfs2/ -name '*.[ch]' >> cscope.files
	find ../libo2dlm/ -name '*.[ch]' >> cscope.files
	cscope -b
