include ../common.mak

PROGRAM=$(LIB)/libinput.so

CUSTOMFLAGS=-I../font

OBJS=libinput.o

$(PROGRAM): $(OBJS)
	$(CC) -shared -o $(PROGRAM) $(OBJS) -L$(LIB) -lpit

clean:
	rm -f $(PROGRAM) *.o
