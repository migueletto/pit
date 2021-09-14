include ../common.mak

PROGRAM1=$(LIB)/libsd1306.so
PROGRAM2=$(LIB)/libsd1306g.so
PROGRAM3=$(LIB)/libsd1306a.so

CUSTOMFLAGS=-I$(SRC)/libdisplay -I$(SRC)/font

OBJS1=libsd1306.o sd1306.o
OBJS2=libsd1306g.o
OBJS3=libsd1306a.o

all: $(PROGRAM1) $(PROGRAM2) $(PROGRAM3)
#all: $(PROGRAM1)

$(PROGRAM1): $(OBJS1)
	$(CC) -shared -o $(PROGRAM1) $(OBJS1) -L$(LIB) -lpit

$(PROGRAM2): $(OBJS2)
	$(CC) -shared -o $(PROGRAM2) $(OBJS2) -L$(LIB) -lpit

$(PROGRAM3): $(OBJS3)
	$(CC) -shared -o $(PROGRAM3) $(OBJS3) -L$(LIB) -lpit

clean:
	rm -f $(PROGRAM1) $(PROGRAM2) $(PROGRAM3) *.o
