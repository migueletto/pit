include ../common.mak

PROGRAM=$(PIT)/bin/pit

OBJS=main.o sig.o

$(PROGRAM): $(OBJS)
	$(CC) -o $(PROGRAM) $(OBJS) -L$(LIB) -lpit -lpthread

clean:
	rm -f $(PROGRAM) *.o
