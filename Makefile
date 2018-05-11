target = oshfs

test:
	gcc -D_FILE_OFFSET_BITS=64  -o $(target) $(target).c -lfuse -g

debug:test
	gdb $(target)

EndAndDebug:end
	gcc -D_FILE_OFFSET_BITS=64  -o $(target) $(target).c -lfuse -g
	gdb $(target)

EndAndMount:end
	gcc -D_FILE_OFFSET_BITS=64  -o $(target) $(target).c -lfuse -g
	./$(target) mountpoint

mount:test
	./$(target) -f -s mountpoint

end:
	sudo umount mountpoint