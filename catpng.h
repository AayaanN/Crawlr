#ifndef CATPNG_H
#define CATPNG_H

#include <stdio.h> 

struct chunk;
struct data_IHDR;

void writeChunk(FILE *fp, struct chunk *chunk_to_write);
void writeIHDR(FILE *fp, struct data_IHDR *ihdr_data_in, struct chunk *ihdr_in);
int catpng(int num_pngs, char* png_names[]);

#endif