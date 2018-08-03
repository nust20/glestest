#include <stdio.h>
#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <stdlib.h>

struct ESContext{
	EGLDisplay display;
	EGLContext context;
	int width;
	int height;
};

static struct ESContext esContext;

void usage(char *name){
	printf("offscreen render\n");
	printf("%s texfile savefile width height\n", name);
	exit(0);
}

int initEgl(int width, int height){
	EGLint major,minor;

	EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (display == EGL_NO_DISPLAY){
		printf("unable to open connection to local windowing system, error:%d\n", eglGetError());
		return -1;
	}

	if (!eglInitialize(display, &major, &minor)){
		printf("unable to initialize EGL, error:%d\n", eglGetError());
		return -1;
	}
	printf("EGL Verion:%d.%d\n", major, minor);

	EGLint attribs [] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_DEPTH_SIZE, 0,
		EGL_NONE
	};

	EGLint numConfigs;
	EGLConfig config;
	if (!eglChooseConfig(display, attribs, &config, 1, &numConfigs)){
		printf("can't find suitable configs, error:%d\n", eglGetError());
		return -1;
	}
	EGLint value;
	eglGetConfigAttrib(display, config, EGL_SURFACE_TYPE, &value);
	printf("EGL_SURFACE_TYPE:%x\n", value);

	eglGetConfigAttrib(display, config, EGL_MAX_PBUFFER_WIDTH, &value);
	printf("EGL_MAX_PBUFFER_WIDTH:%x\n", value);

	EGLint contextAttrib[] = {
		EGL_CONTEXT_CLIENT_VERSION, 3,
		EGL_NONE
	};

	EGLContext context = eglCreateContext(display, config, NULL, contextAttrib);
	if (context == EGL_NO_CONTEXT){
		printf("Can't Create EGLContext, error:%d\n", eglGetError());
		return -1;
	}

	if (eglMakeCurrent(display, NULL, NULL, context) == EGL_FALSE){
		printf("Initilize error at eglMakeCurrent, error:%d\n", eglGetError());
		return -1;
	}


	esContext.display = display;
	esContext.context = context;
	esContext.width = width;
	esContext.height = height;

	return 0;

}

GLuint loadShader(GLenum type, const char *shaderSrc){
	GLuint shader;
	GLint compiled;

	// Create the shader object
	shader = glCreateShader(type);
	if (shader == 0){
		return 0;
	}
	// Load the shader source
	glShaderSource(shader, 1, &shaderSrc, NULL);
	// Compile the shader
	glCompileShader(shader);
	// Check the compile status
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
	if (!compiled){
		GLint infoLen = 0;
		glGetShaderiv ( shader, GL_INFO_LOG_LENGTH, &infoLen );
		if (infoLen > 1){
			char *infoLog = (char *)malloc(sizeof (char) * infoLen);

			glGetShaderInfoLog (shader, infoLen, NULL, infoLog);
			printf("Error compiling shader:\n%s\n", infoLog);
			free ( infoLog );
		}
		glDeleteShader ( shader );
		return 0;
	}
	return shader;
}

int main(int argc, char *argv[]){
	FILE *fin, *fout;
	int width, height;

	if (argc != 5)
		usage(argv[0]);

	width = atoi(argv[3]);
	height = atoi(argv[4]);

	initEgl(width, height);

	return 0;
}
