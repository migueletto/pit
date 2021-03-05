include ../common.mak

PROGRAM=$(LIB)/libsd1306$(SOEXT)

CUSTOMFLAGS=-I$(SRC)/libdisplay -I$(SRC)/font

OBJS=libsd1306.o sd1306.o

all: $(PROGRAM)

$(PROGRAM): $(OBJS)
	$(CC) -shared -o $(PROGRAM) $(OBJS) -L$(LIB) -lpit

clean:
	rm -f $(PROGRAM) *.o
