all:findpng2

CFLAGS_XML2 = $(shell xml2-config --cflags)
CFLAGS_CURL = $(shell curl-config --cflags)
LDLIBS_XML2 = $(shell xml2-config --libs)
LDLIBS_CURL = $(shell curl-config --libs)
LDLIBS = $(LDLIBS_XML2) $(LDLIBS_CURL) -lpthread -lz
CFLAGS =  $(CFLAGS_XML2) $(CFLAGS_CURL) -std=gnu99 -g #-Wall

fingpng2: findpng2.c 
	gcc $(CFLAGS) findpng2.c -o findpng2 $(LDLIBS)

clean:
	rm -f *.o findpng2



