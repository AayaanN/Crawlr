all:paster

paster: paster.c 
	gcc -o paster paster.c catpng.c zutil.c crc.c -lcurl -lz -g -lpthread
clean:
	rm -f *.o paster