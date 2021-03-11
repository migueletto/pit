include ../common.mak

PROGRAM=$(LIB)/libstream$(SOEXT)

OBJS=libstream.o

$(PROGRAM): $(OBJS)
	$(CC) -shared -o $(PROGRAM) $(OBJS) -L$(LIB) -lpit

clean:
	rm -f $(PROGRAM) *.o
