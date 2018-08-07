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

    //Vertex Buffer Object
    GLuint vbo[4];
    
    // computer program
    GLuint program;
    GLint stride_index;
};

static struct ESContext esContext;

void usage(char *name){
	printf("offscreen render\n");
	printf("%s texfile savefile width height cnt\n", name);
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
            "#version 310 es\n"
            "\n"
            "struct YUVData{\n"
            "  uint yuv;  \n"
            "};\n"
            "\n"
            "struct RGBAData{\n"
            "    uint rgba[4];\n"
            "};\n"
            "\n"
            "uniform int stride;\n"
            "\n"
            "const mat4 coef = mat4(\n"
            "    1.164,    0.0,  1.596, 0.0,\n"
            "    1.164, -0.391, -0.813, 0.0,\n"
            "    1.164,  2.018,    0.0, 0.0,\n"
            "    0.0,      0.0,    0.0, 1.0\n"
            ");\n"
            "\n"
            "layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;\n"
            "layout(std430, binding=0) readonly buffer yBuffer{\n"
            "    YUVData data[];\n"
            "}YData;\n"
            "layout(std430, binding=1) readonly buffer uBuffer{\n"
            "    YUVData data[];\n"
            "}UData;\n"
            "layout(std430, binding=2) readonly buffer vBuffer{\n"
            "    YUVData data[];\n"
            "}VData;\n"
            "\n"
            "layout(std430, binding=3) writeonly buffer rgbaBuffer{\n"
            "    RGBAData data[];\n"
            "}outBuffer;\n"
            "\n"
            "void main(void){\n"
            "    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);\n"
            "    int index = pos.y * stride + pos.x;\n"
            "    mat4 yuv;\n"
            "    yuv[0] = unpackUnorm4x8(YData.data[index].yuv) - 16./255.;  // y\n"
            "    yuv[1] = unpackUnorm4x8(UData.data[index].yuv) - 128./255.; // u\n"
            "    yuv[2] = unpackUnorm4x8(VData.data[index].yuv) - 128./255.; // v\n"
            "    yuv[3] = vec4(1.0);\n"
            "    mat4 tmp = yuv * coef;\n"
            "    mat4 rgba = transpose(tmp);\n"
            "    outBuffer.data[index].rgba[0] = packUnorm4x8(rgba[0]);\n"
            "    outBuffer.data[index].rgba[1] = packUnorm4x8(rgba[1]);\n"
            "    outBuffer.data[index].rgba[2] = packUnorm4x8(rgba[2]);\n"
            "    outBuffer.data[index].rgba[3] = packUnorm4x8(rgba[3]);\n"
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
    esContext.stride_index = glGetUniformLocation(program, "stride");
    esContext.program = program; 
   return 0;
}

int initVBO(void){
    glGenBuffers(4,  esContext.vbo);
    printf("line:%d glError:%x\n", __LINE__, glGetError()); 
    
    glBindBuffer(GL_ARRAY_BUFFER, esContext.vbo[3]);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    glBufferData(GL_ARRAY_BUFFER, esContext.width * esContext.height * 4, NULL, GL_DYNAMIC_READ);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    
    return 0;
}

void performCompute(char *y, char *u, char *v){
    glUseProgram(esContext.program);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    glUniform1i(esContext.stride_index, esContext.width / 4);
    
    glBindBuffer(GL_ARRAY_BUFFER, esContext.vbo[0]);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    glBufferData(GL_ARRAY_BUFFER, esContext.width * esContext.height, y, GL_DYNAMIC_DRAW);
    printf("line:%d glError:%x\n", __LINE__, glGetError());

    glBindBuffer(GL_ARRAY_BUFFER, esContext.vbo[1]);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    glBufferData(GL_ARRAY_BUFFER, esContext.width * esContext.height, u, GL_DYNAMIC_DRAW);
    printf("line:%d glError:%x\n", __LINE__, glGetError());

    glBindBuffer(GL_ARRAY_BUFFER, esContext.vbo[2]);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    glBufferData(GL_ARRAY_BUFFER, esContext.width * esContext.height, v, GL_DYNAMIC_DRAW);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, esContext.vbo[0]);
    printf("line:%d glError:%x\n", __LINE__, glGetError());    
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, esContext.vbo[1]);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, esContext.vbo[2]);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, esContext.vbo[3]);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    
    glDispatchCompute((esContext.width/4 + 31) / 32, (esContext.height + 31) /32, 1);   // process 1920/4 1080
    printf("line:%d glError:%x\n", __LINE__, glGetError());

    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}


int main(int argc, char *argv[]){
	FILE *fin, *fout;
	int width, height;
    int size;
    char *bufin, *bufout;
    char *y, *u, *v;
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
    bufin = (char *)malloc(size * 3);
    bufout = (char *)malloc(size *4 );

    initEgl(width, height);
    
    initProgram();
    
    memset(bufout, 0, size * 4);
    initVBO();
    
    y = bufin;
    u = y + size;
    v = u + size;
    
    while(fread(bufin, size, 3, fin) && count-- > 0){
        performCompute(y, u, v);
        
        printf("line:%d glError:%x\n", __LINE__, glGetError());
        glBindBuffer(GL_ARRAY_BUFFER, esContext.vbo[3]);
        printf("line:%d glError:%x\n", __LINE__, glGetError());
        src = glMapBufferRange(GL_ARRAY_BUFFER, 0, size * 4, GL_MAP_READ_BIT);
        printf("line:%d glError:%x src=%p\n", __LINE__, glGetError(), src);
        
        memcpy(bufout, src, size * 4);
        glUnmapBuffer(GL_ARRAY_BUFFER);
        
        fwrite(bufout, size, 4, fout);
    }
    fclose(fin);
    fclose(fout);
    
    // Get info about compute shader
    GLint value;
    glGetIntegerv(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &value);
    printf("MAX_SHADER_STORAGE_BLOCK_SIZE:%d\n", value);
    glGetIntegerv(GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS, &value);
    printf("MAX_COMPUTE_SHADER_STORAGE_BLOCKS:%d\n", value);
    glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &value);
    printf("MAX_COMPUTE_WORK_GROUP_INVOCATIONS:%d\n", value);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &value);
    printf("MAX_COMPUTE_WORK_GROUP_COUNT X:%d\n", value);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1, &value);
    printf("MAX_COMPUTE_WORK_GROUP_COUNT Y:%d\n", value);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2, &value);
    printf("MAX_COMPUTE_WORK_GROUP_COUNT Z:%d\n", value);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0, &value);
    printf("MAX_COMPUTE_WORK_GROUP_SIZE X:%d\n", value);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 1, &value);
    printf("MAX_COMPUTE_WORK_GROUP_SIZE Y:%d\n", value);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 2, &value);
    printf("MAX_COMPUTE_WORK_GROUP_SIZE Z:%d\n", value);

	return 0;
}
