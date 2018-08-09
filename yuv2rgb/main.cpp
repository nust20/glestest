#include "GLESConvert.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

void usage(char *name){
	printf("offscreen render\n");
	printf("%s texfile savefile width height cnt\n", name);
	exit(0);
}
int main(int argc, char *argv[]){
	FILE *fin, *fout;
	int width, height;
    int size;
    uint8_t *bufin, *bufout;
    uint8_t *y, *u, *v;
    int count;
	void *src;
	if (argc != 6)
		usage(argv[0]);
		
    fin = fopen(argv[1], "rb");
    fout = fopen(argv[2], "wb+");
  	width = atoi(argv[3]);
	height = atoi(argv[4]);
    count = atoi(argv[5]);
    
    size = width * height;
    bufin = (uint8_t *)malloc(size * 3);
    bufout = (uint8_t *)malloc(size *4 );

	GLESConvert *mConvert = new GLESConvert(width, height, width);
	mConvert->waitGLInit();
	
	memset(bufout, 0, size * 4);
	y = bufin;
    u = y + size;
    v = u + size;
	while(fread(bufin, size, 3, fin) && count-- > 0){
		mConvert->convert(y, u, v, bufout);
		fwrite(bufout, size, 4, fout);
	}
	
	fclose(fin);
    fclose(fout);
}
