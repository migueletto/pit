include ../common.mak

PROGRAM=$(LIB)/libdisplay$(SOEXT)

CUSTOMFLAGS=-I../font

OBJS=libdisplay.o

$(PROGRAM): $(OBJS)
	$(CC) -shared -o $(PROGRAM) $(OBJS) -L$(LIB) -lpit

clean:
	rm -f $(PROGRAM) *.o
