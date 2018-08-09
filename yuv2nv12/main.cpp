#include "GLESConvert.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

void usage(char *name){
	printf("offscreen render\n");
	printf("%s texfile savefile width height stride cnt\n", name);
	exit(0);
}
int main(int argc, char *argv[]){
	FILE *fin, *fout;
	int width, height, stride;
    int size;
    uint8_t *bufin, *bufout;
    uint8_t *y, *u, *v, *uv;
    int count;
	void *src;
	if (argc != 7)
		usage(argv[0]);
		
    fin = fopen(argv[1], "rb");
    fout = fopen(argv[2], "wb+");
  	width = atoi(argv[3]);
	height = atoi(argv[4]);
    stride = atoi(argv[5]);
    count = atoi(argv[6]);
    
    size = width * height;
    bufin = (uint8_t *)malloc(size * 3);
    bufout = (uint8_t *)malloc(stride * height * 3 / 2 );

	GLESConvert *mConvert = new GLESConvert(width, height, stride);
	mConvert->waitGLInit();
	
	memset(bufout, 0, stride * height * 3 / 2);
	y = bufin;
    u = y + size;
    v = u + size;
    uv = bufout + stride * height;
	while(fread(bufin, size, 3, fin) && count-- > 0){
        for (int i = 0; i < height; i++){
            memcpy(bufout + i *stride, bufin + i * width, width);
        }
		mConvert->convert(u, v, uv);
        
		fwrite(bufout, stride, height, fout);
        fwrite(uv, stride / 2, height, fout);
	}
	
	fclose(fin);
    fclose(fout);
}
