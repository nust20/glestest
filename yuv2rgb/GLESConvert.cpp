#include "GLESConvert.h"
#include <stdlib.h>
#include <stdio.h>


GLESConvert::GLESConvert(uint32_t width, uint32_t height, uint32_t rgbstride):
    mWidth(width), mHeight(height), mRGBStride(rgbstride), display(EGL_NO_DISPLAY), context(EGL_NO_CONTEXT){
    num_groups_x = (mWidth / 4 + 31) / 32;
    num_groups_y = (mHeight + 31) / 32;

    mInBufSize = mWidth * mHeight;
    mOutBufSize = mRGBStride * mHeight * 4;

    mThreadRun = false;

    sem_init(&mGLSem, 0, 0);
    sem_init(&mCustSem, 0, 0);
    
    if(0 != pthread_create(&mThread, NULL, gles_entry, this)){
        printf("Could not create dispatch thread\n");
    }
}

GLESConvert::~GLESConvert(){    

    mThreadRun = false;
    
    sem_post(&mGLSem);
    sem_post(&mCustSem);

    int status = pthread_join(mThread, NULL);
    if (status != 0) {
       printf("pthread_join error:%d\n", status);
    }

    sem_destroy(&mGLSem);
    sem_destroy(&mCustSem);
}

//static
void *GLESConvert::gles_entry(void *data){
    GLESConvert *me = static_cast<GLESConvert *>(data);
    me->glesMain();
    return NULL;
}

void GLESConvert::glesMain(void){
    void *src;
    
    initEgl();
    initProgram();
    initVBO();
    

    mThreadRun = true;
    sem_post(&mCustSem);
    for(;;){
        sem_wait(&mGLSem);
        if (!mThreadRun)
            break;
        performCompute(cy, cu, cv);
        
        glReadBuffer(GL_COLOR_ATTACHMENT0);
		glReadPixels(0, 0, mRGBStride / 4, mHeight, GL_RGBA_INTEGER, GL_UNSIGNED_INT, 0);
        src = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, mOutBufSize , GL_MAP_READ_BIT);
        memcpy(cdst, src, mOutBufSize);
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        sem_post(&mCustSem);
    }
    cleanGLES();
    return;
}

int GLESConvert::initEgl(){
	EGLint major,minor;

	display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
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

	context = eglCreateContext(display, config, NULL, contextAttrib);
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
    
    surface = eglCreatePbufferSurface(display, config, attrib_pb);
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

int GLESConvert::initProgram(void){
    GLuint computeShader;
    GLint linked;
    const char *shader_source = 
            "#version 310 es\n"
            "\n"
            "struct YUVData{\n"
            "  uint yuv;  \n"
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
            "layout(binding = 1, rgba32ui) writeonly uniform  uimage2D output_image;\n"
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
			"    uvec4 outdata; \n"
            "    outdata.x = packUnorm4x8(rgba[0]);\n"
            "    outdata.y = packUnorm4x8(rgba[1]);\n"
            "    outdata.z = packUnorm4x8(rgba[2]);\n"
            "    outdata.w = packUnorm4x8(rgba[3]);\n"
			"    imageStore(output_image, pos, outdata);\n"
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
    
    glDeleteShader(computeShader);
    stride_index = glGetUniformLocation(program, "stride");
    return 0;
}

int GLESConvert::initVBO(void){
    glGenFramebuffers(1, &fboid);
    glBindFramebuffer(GL_FRAMEBUFFER, fboid);

    glGenTextures(1, &texOut);  
    glBindTexture(GL_TEXTURE_2D, texOut);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32UI, mRGBStride / 4, mHeight);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texOut, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if(status != GL_FRAMEBUFFER_COMPLETE){
        printf("failed  %x\n", status);
    }    
    printf("line:%d glError:%x\n", __LINE__, glGetError());

    glGenBuffers(3,  vbo);
    
    glGenBuffers(1, &pboid);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pboid);
    glBufferData(GL_PIXEL_PACK_BUFFER, mOutBufSize, NULL, GL_DYNAMIC_READ);
    return 0;
}

void GLESConvert::performCompute(uint8_t *y, uint8_t *u, uint8_t *v){
    
    glUseProgram(program);    
    glUniform1i(stride_index, mWidth / 4);
    
    glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
    glBufferData(GL_ARRAY_BUFFER, mInBufSize, y, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, vbo[1]);
    glBufferData(GL_ARRAY_BUFFER, mInBufSize, u, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, vbo[2]);
    glBufferData(GL_ARRAY_BUFFER, mInBufSize, v, GL_DYNAMIC_DRAW);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, vbo[0]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, vbo[1]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, vbo[2]);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    
	glBindImageTexture(1, texOut, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32UI);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    
    glDispatchCompute(num_groups_x, num_groups_y, 1);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

int GLESConvert::convert(uint8_t *y, uint8_t *u, uint8_t *v, uint8_t * dst){
    if(!mThreadRun)
        return -1;
    cy = y;
    cu = u;
    cv = v;
    cdst = dst;
    sem_post(&mGLSem);
    
    sem_wait(&mCustSem);    
}

void GLESConvert::waitGLInit(void){
    sem_wait(&mCustSem);
}

void GLESConvert::cleanGLES(void){    
    glDeleteProgram(program);

    glDeleteBuffers(3, vbo);
    glDeleteTextures(1, &texOut);
    glDeleteFramebuffers(1, &fboid);
    glDeleteBuffers(1, &pboid);    
#ifdef USE_PBUFFER
    eglDestroySurface(display, surface);
#endif
    eglDestroyContext(display, context);
    eglTerminate(display);
    eglReleaseThread();
}
