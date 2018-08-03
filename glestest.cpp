#include <stdio.h>
#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <stdlib.h>
#include <string.h>

// Some platform can't do eglMakeCurrent with NULL surface
// So use pbuffer to create a 1x1 surface
#define USE_PBUFFER 1

struct ESContext{
	EGLDisplay display;
	EGLContext context;
	int width;
	int height;

	//framebuffer object
	GLuint fboid;
    GLuint texIn;
    GLuint texOut;
    // computer program
    GLuint program;
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

#ifdef USE_PBUFFER
    EGLint attrib_pb[] = {
        EGL_WIDTH, 1, 
        EGL_HEIGHT, 1, 
        EGL_NONE
    };
    
    EGLSurface surface = eglCreatePbufferSurface(display, config, attrib_pb);
    if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE){
        printf("Initilize error at eglMakeCurrent, error:%d\n", eglGetError());
        return -1;
    }
#else
	if (eglMakeCurrent(display, NULL, NULL, context) == EGL_FALSE){
		printf("Initilize error at eglMakeCurrent, error:%d\n", eglGetError());
		return -1;
	}
#endif

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


int initProgram(){
    GLuint program;
    GLuint computeShader;
    GLint linked;
    const char *shader_source = 
            "#version 310 es      \n"
            "layout (local_size_x = 32, local_size_y = 32, local_size_z = 1) in; \n"
            "layout(binding = 0, rgba8ui) readonly uniform  uimage2D input_image; \n"
            "layout(binding = 1, rgba8ui) writeonly uniform  uimage2D output_image;\n"
            "void main(void)\n"
            "{\n"
            "    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);\n"
            "    uvec4 data = imageLoad(input_image, pos);\n"
            "    imageStore(output_image, pos.xy, data);\n"
            "}\n";
    
    // Load the vertex/fragment shaders
    computeShader = loadShader(GL_COMPUTE_SHADER, shader_source);

    // Create the program object
    program = glCreateProgram();
    glAttachShader(program, computeShader);
    // Link the program
    glLinkProgram(program);

    // Check the link status
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if(!linked){
        GLint infoLen = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 1){
            char *infoLog = (char *)malloc(sizeof (char) * infoLen);

            glGetProgramInfoLog(program, infoLen, NULL, infoLog);
            printf("Error linking program:\n%s\n", infoLog);
            free(infoLog);
        }
        glDeleteProgram(program);
        return -1;
    }
    esContext.program = program; 
   return 0;
}

int initFBO(void){
    GLuint fboid;
    GLuint texIn, texOut;
    
    glGenFramebuffers(1, &fboid);
    glBindFramebuffer(GL_FRAMEBUFFER, fboid);
    
    glGenTextures(1, &texIn);  
    glBindTexture(GL_TEXTURE_2D, texIn);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8UI, esContext.width, esContext.height);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    printf("line:%d glError:%x\n", __LINE__, glGetError());    
    
    glGenTextures(1, &texOut);  
    glBindTexture(GL_TEXTURE_2D, texOut);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8UI, esContext.width, esContext.height);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texOut, 0);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if(status != GL_FRAMEBUFFER_COMPLETE){
        printf("failed  %x\n", status);
    }
    esContext.fboid = fboid;
    esContext.texIn = texIn;
    esContext.texOut = texOut;
    return 0;
}

int main(int argc, char *argv[]){
	FILE *fin, *fout;
	int width, height;
    int size;
    char *bufin, *bufout;
    
	if (argc != 5)
		usage(argv[0]);
    fin = fopen(argv[1], "rb");
    fout = fopen(argv[2], "wb+");
  	width = atoi(argv[3]);
	height = atoi(argv[4]);

    size = width * height;
    bufin = (char *)malloc(size * 4);
    bufout = (char *)malloc(size *4 );
    
    fread(bufin, size, 4, fin);
    
	initEgl(width, height);
    
    initProgram();
    
    initFBO();
    
    glBindTexture(GL_TEXTURE_2D, esContext.texIn);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,  width, height, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, bufin);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    
    
    
    glUseProgram(esContext.program);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    glBindImageTexture(0, esContext.texIn, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8UI);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    glBindImageTexture(1, esContext.texOut, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8UI);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    glDispatchCompute(60, 34, 1);   // process 1920 1080
    printf("line:%d glError:%x\n", __LINE__, glGetError());

    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    //glReadBuffer(GL_COLOR_ATTACHMENT0);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    glReadPixels(0, 0, width, height, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, bufout);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    fwrite(bufout, size, 4, fout);
    GLint value;
    glGetIntegerv(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &value);
    printf("MAX_SHADER_STORAGE_BLOCK_SIZE:%d\n", value);
    glGetIntegerv(GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS, &value);
    printf("MAX_COMPUTE_SHADER_STORAGE_BLOCKS:%d\n", value);
    fclose(fin);
    fclose(fout);
	return 0;
}
