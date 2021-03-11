include ../common.mak

PROGRAM=$(LIB)/libmedia$(SOEXT)

CUSTOMFLAGS=-I../jpeg-8d

OBJS=libmedia.o stream.o monitor.o delta.o convert.o save.o null.o jpeg.o mono.o pcm.o isolate.o sync.o rtpnode.o

$(PROGRAM): $(OBJS)
	$(CC) -shared -o $(PROGRAM) $(OBJS) -L$(LIB) -lpit -ljpeg8

clean:
	rm -f $(PROGRAM) *.o
