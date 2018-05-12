target = oshfs

debug:
	gcc -D_FILE_OFFSET_BITS=64  -o $(target) $(target).c -lfuse -g
	gdb $(target)

EndAndDebug:end
	gcc -D_FILE_OFFSET_BITS=64  -o $(target) $(target).c -lfuse -g
	gdb $(target)

EndMount:end
	gcc -D_FILE_OFFSET_BITS=64  -o $(target) $(target).c -lfuse
	./$(target) mountpoint

mount:
	gcc -D_FILE_OFFSET_BITS=64  -o $(target) $(target).c -lfuse
	./$(target) mountpoint

end:
	sudo umount mountpoint