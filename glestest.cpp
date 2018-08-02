#include <stdio.h>
#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <stdlib.h>


void usage(char *name){
	printf("offscreen render\n");
	printf("%s texfile savefile width height\n", name);
	exit(0);
}

int main(int argc, char *argv[]){
	FILE *fin, *fout;
	int width, height;

	if (argc != 5)
		usage(argv[0]);

	return 0;
}
