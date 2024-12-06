weatherfs : weatherfs.c
	gcc -o $@ $< -D_FILE_OFFSET_BITS=64 -I/usr/include/fuse3 -lfuse3 -lpthread -Wall -lcurl -ljansson

test : weatherfs
	-umount zipcode
	mkdir -p zipcode
	./weatherfs zipcode
	cat zipcode/96701 # Hawaii
	grep -w temp zipcode/99501 # Alaska
