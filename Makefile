install:
	gcc $(yed --print-cflags) $(yed --print-ldflags) selene.c -o selene.so
	cp ./selene.so ~/.config/mpy/plugins
