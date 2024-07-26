#include "pnginfo.h"
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include "zutil.h"
#include "crc.h"


void writeChunk(FILE *fp, struct chunk *chunk_to_write){
    fwrite(&(chunk_to_write->length), 4, 1, fp);
    fwrite(&(chunk_to_write->type), 4, 1, fp);

    fwrite(chunk_to_write->p_data, ntohl(chunk_to_write->length), 1, fp);
    fwrite(&(chunk_to_write->crc), 4, 1, fp);
}

void writeIHDR(FILE *fp, struct data_IHDR *ihdr_data_in, struct chunk *ihdr_in){

    unsigned int ihdr_size = htonl(13);

    fwrite(&ihdr_size, 4, 1, fp);
    fwrite(&(ihdr_in->type), 4, 1, fp);
    fwrite(&(ihdr_data_in->width), 4, 1, fp);
    fwrite(&(ihdr_data_in->height), 4, 1, fp);
    fwrite(&(ihdr_data_in->bit_depth), 1, 1, fp);
    fwrite(&(ihdr_data_in->color_type), 1, 1, fp);
    fwrite(&(ihdr_data_in->compression), 1, 1, fp);
    fwrite(&(ihdr_data_in->filter), 1, 1, fp);
    fwrite(&(ihdr_data_in->interlace), 1, 1, fp);

    unsigned char crc_data[17];
    memcpy(crc_data, ihdr_in->type, 4);
    memcpy(crc_data + 4, ihdr_data_in, 13);
    unsigned int ihdr_crc = htonl(crc(crc_data, 17));
    fwrite(&ihdr_crc, 4, 1, fp);
}

// void setIHDRData(U8 *destination, struct data_IHDR ihdr_in){
//     malloc()
// }

int catpng(int num_pngs, char* png_names[]){


    // pointer to memory to be used for concatenated idat data
    unsigned char *concatenated = NULL;
    unsigned int concatenated_length = 0;
    unsigned int total_height = 0;

    //size of ihdr so it can be skipped when reading idat data
    int size_ihdr = 4 + 4 + 13 + 4;

    struct data_IHDR *ihdr_data = malloc(sizeof(struct data_IHDR));

    struct chunk ihdr_chunk;
    struct chunk idat_chunk;
    struct chunk iend_chunk;
    struct simple_PNG overall_png;


    for(int i = 0; i < num_pngs; i++){

        // initialize variable for length
        unsigned char *uncompressed = NULL;
        unsigned int uncompressed_length;
        unsigned char *compressed = NULL;
        unsigned int compressed_length;

        // open png #i
        FILE *fp = fopen(png_names[i], "rb");

        if (fp != NULL) {
            printf("File opened successfully!\n");
        // Don't forget to close the file after you're done with it
        } else {
            printf("Error opening file!\n");
        }

        //move past ihdr chunk to idat chunk
        // fseek(fp, 8+size_ihdr,SEEK_SET);
        fseek(fp, 16, SEEK_SET);

        U8 data[DATA_IHDR_SIZE];
        fread(data, 1, DATA_IHDR_SIZE, fp);


        ihdr_data->width = ntohl(*(U32 *)&data[0]);
        ihdr_data->height = ntohl(*(U32 *)&data[4]);
        ihdr_data->bit_depth = data[8];
        ihdr_data->color_type = data[9];
        ihdr_data->compression = data[10];
        ihdr_data->filter = data[11];
        ihdr_data->interlace = data[12];

        total_height += ihdr_data->height;

        // move past ihdr crc
        fseek(fp, 4, SEEK_CUR);


        // read in uncompressed length for idat chunk
        fread(&uncompressed_length, 4, 1, fp);

        //move past type data
        fseek(fp, 4, SEEK_CUR);

        //convert length to host order
        uncompressed_length = ntohl(uncompressed_length);

        uncompressed = malloc(uncompressed_length);


        //read idat data into uncompressed - all good up til here
        fread(uncompressed, uncompressed_length, 1, fp);

        //inflate idat data - should be good
        compressed = (U8 *)malloc(200000);
        mem_inf(compressed, (unsigned long *)&compressed_length, uncompressed, uncompressed_length);
        
        // compressed = realloc(compressed, compressed_length);

        //increase size of concatenated -- this should stay the same
        unsigned int old_size = concatenated_length;
        concatenated_length += compressed_length;
        // concatenated = (U8 *)malloc(1);
        concatenated = realloc(concatenated, concatenated_length);
        // concatenated = (U8 *)malloc(concatenated_length);

        //add to concatenated
        unsigned char *new_dest = concatenated + old_size; 
        memcpy(new_dest, compressed, compressed_length);
        fclose(fp);

        free(uncompressed);
        free(compressed);


    }
    //now we mem_def the entire concatenated inflated image together
    U8 *deflated_concat;
    deflated_concat = (U8 *) malloc(300000);
    unsigned int inflated_size = concatenated_length;
    unsigned int deflated_size;
    mem_def(deflated_concat, (unsigned long *)&deflated_size, concatenated, inflated_size, Z_DEFAULT_COMPRESSION);
    // deflated_concat = realloc(deflated_concat, deflated_size);
    //use this for ihdr height
    total_height = htonl(total_height);

    ihdr_data->height = total_height;
    ihdr_data->width = htonl(ihdr_data->width);

    //set ihdr chunk
    ihdr_chunk.length = htonl(DATA_IHDR_SIZE);
    ihdr_chunk.p_data = (U8 *)ihdr_data;
    memcpy(ihdr_chunk.type, "IHDR", 4);
    unsigned char *ihdr_buf = malloc(4+13);
    memcpy(ihdr_buf, ihdr_chunk.type, 4);
    memcpy(ihdr_buf+4, ihdr_data, 13);
    ihdr_chunk.crc = htonl(crc(ihdr_buf, 17));
    free(ihdr_buf);

    //set idat chunk
    idat_chunk.length = htonl(deflated_size);
    idat_chunk.p_data = deflated_concat;
    memcpy(idat_chunk.type, "IDAT", 4);
    unsigned char *idat_buf = malloc(4+deflated_size);
    memcpy(idat_buf, idat_chunk.type, 4);
    memcpy(idat_buf+4, deflated_concat, deflated_size);
    idat_chunk.crc = htonl(crc(idat_buf, 4+deflated_size));
    free(idat_buf);

    //set iend chunk
    iend_chunk.length = htonl(0);
    iend_chunk.p_data = NULL;
    memcpy(iend_chunk.type, "IEND", 4);
    unsigned char *iend_buf = malloc(4);
    memcpy(iend_buf, iend_chunk.type, 4);
    iend_chunk.crc = htonl(crc(iend_buf, 4));
    free(iend_buf);

    overall_png.p_IDAT = &idat_chunk;
    overall_png.p_IEND = &iend_chunk;
    overall_png.p_IHDR = &ihdr_chunk;

   
    

    //write to file now
    FILE *fp = fopen("all.png", "wb");
    char signature[] = "\x89\x50\x4E\x47\x0D\x0A\x1A\x0A";
    fwrite(signature, sizeof(char), 8, fp);

    // writeChunk(fp, overall_png.p_IHDR);
    writeIHDR(fp, ihdr_data, overall_png.p_IHDR);
    writeChunk(fp, overall_png.p_IDAT);
    writeChunk(fp, overall_png.p_IEND);

    free(ihdr_data);
    free(concatenated);
    free(deflated_concat);
    fclose(fp);

    return 0;

}


// int main(int argc, char* argv[]){

//     if (argc > 1) {
//         catpng(argc, argv);
//     }

//     return 0;
// }
