CFLAGS += $(shell yed --print-cflags)
CFLAGS += $(shell yed --print-ldflags)
install:
	gcc $(CFLAGS) drill.c -o drill.so
	cp ./drill.so ~/.config/yed/mpy/plugins
