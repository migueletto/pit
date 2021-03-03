include ../common.mak

PROGRAM=$(LIB)/liblcd.so

CUSTOMFLAGS=-I$(SRC)/libdisplay -I$(SRC)/libi2c -I$(SRC)/font

OBJS=liblcd.o lcdc.o

$(PROGRAM): $(OBJS)
	$(CC) -shared -o $(PROGRAM) $(OBJS) -L$(LIB) -lpit

clean:
	rm -f $(PROGRAM) *.o
