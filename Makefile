CFLAGS = -O3 -Wall
FORMATDEFS = -DRWIMG_JPEG -DRWIMG_PNG -DRWIMG_GIF

export CFLAGS FORMATDEFS

all : libbwproc.so

bwproc : librwimg bwproc.c procfunc.h
	$(CC) $(CFLAGS) $(FORMATDEFS) -DBW_COMMANDLINE -o bwproc bwproc.c rwimg/librwimg.a -lpng -ljpeg -lungif

librwimg :
	$(MAKE) -C rwimg

libbwproc.so : bwproc.c procfunc.h
	$(CC) $(CFLAGS) -fPIC -shared -o libbwproc.so bwproc.c
