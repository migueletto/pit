include ../common.mak

PROGRAM=$(LIB)/libfb$(SOEXT)

CUSTOMFLAGS=-I$(SRC)/font

OBJS=libfb.o

$(PROGRAM): $(OBJS)
	$(CC) -shared -o $(PROGRAM) $(OBJS) -L$(LIB) -lpit -lm

clean:
	rm -f $(PROGRAM) *.o
