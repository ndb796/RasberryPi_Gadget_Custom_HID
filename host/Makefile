PROG = custom_hid

TARGET = $(PROG)
CC = gcc
STRIP = strip
CFLAGS = -Wall -O2 -DOS_$(OS)
LIBS = -lusb

OBJS = $(PROG).o hid.o

all: $(TARGET)

$(PROG): $(OBJS)
	$(CC) -o $(PROG) $(OBJS) $(LIBS)
	$(STRIP) $(PROG)

$(PROG).exe: $(PROG)
	cp $(PROG) $(PROG).exe

hid.o: hid_LINUX.c hid.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o $(PROG) $(PROG).exe $(PROG).dmg
	rm -rf tmp
