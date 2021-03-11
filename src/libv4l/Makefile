include ../common.mak

PROGRAM=$(LIB)/libv4l$(SOEXT)

OBJS=libv4l.o

$(PROGRAM): $(OBJS)
	$(CC) -shared -o $(PROGRAM) $(OBJS) -L$(LIB) -lpit

clean:
	rm -f $(PROGRAM) *.o
