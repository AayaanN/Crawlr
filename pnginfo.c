#include "pnginfo.h"
#include <arpa/inet.h>

int is_png(U8 *buf, size_t n){
    
    //make sure that buffer is 8 bytes
    if (n != 8){
        return 0;
    }

    U8 png_header[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

    //ensure that buffer is png_signature
    for (int i = 0; i < 8; i++){
        if (buf[i]!=png_header[i]){
            return 0;
        }
    }

    return 1;
}

int get_png_height(struct data_IHDR *buf){
    return buf->height;
}

int get_png_width(struct data_IHDR *buf){
    return buf->width;
}

int get_png_data_IHDR(struct data_IHDR *out, FILE *fp, long offset, int whence){
    //seek to offset and wence in file 
    fseek(fp, offset, whence);

    //array of bytes to store 
    U8 data[DATA_IHDR_SIZE];

    //read the file data
    fread(data, 1, DATA_IHDR_SIZE, fp);

    //set the output IHDR to the byte data array values that we read
    out->width = ntohl(*(U32 *)&data[0]);
    out->height = ntohl(*(U32 *)&data[4]);
    out->bit_depth = data[8];
    out->color_type = data[9];
    out->compression = data[10];
    out->filter = data[11];
    out->interlace = data[12];

    return 1;
}


