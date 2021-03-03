include ../common.mak

PROGRAM=$(LIB)/libinet$(SOEXT)

OBJS=libinet.o

$(PROGRAM): $(OBJS)
	$(CC) -shared -o $(PROGRAM) $(OBJS) -L$(LIB) -lpit

clean:
	rm -f $(PROGRAM) *.o
