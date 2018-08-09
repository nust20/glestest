#include "GLESConvert.h"
#include <stdlib.h>
#include <stdio.h>


GLESConvert::GLESConvert(uint32_t width, uint32_t height, uint32_t uv_stride):
    mWidth(width), mHeight(height), mUVStride(uv_stride), display(EGL_NO_DISPLAY), context(EGL_NO_CONTEXT){
    num_groups_x = (mWidth / 4 + 31) / 32; //process 4 pixels together
    num_groups_y = (mHeight/2 + 31) / 32;  //uv height is half of y

    mOutBufSize = mUVStride * mHeight / 2;

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
    initFBO();
    

    mThreadRun = true;
    sem_post(&mCustSem);
    for(;;){
        sem_wait(&mGLSem);
        if (!mThreadRun)
            break;
        performCompute(cu, cv);
        
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(0, 0, mUVStride / 4, mHeight / 2, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, 0);
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
            "layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;\n"
            "layout(binding = 0, rgba8ui) readonly uniform  uimage2D u_image; \n"
            "layout(binding = 1, rgba8ui) readonly uniform  uimage2D v_image; \n"
            "layout(binding = 2, rgba8ui) writeonly uniform  uimage2D output_image;\n"
            "void main(void){\n"
            "    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);\n"
            "    ivec2 index = pos;\n"
            "    index.y *= 2;\n"
            "    vec4 u = vec4(imageLoad(u_image, index));\n"
            "    vec4 v = vec4(imageLoad(v_image, index));\n"
            "    index.y += 1;\n"
            "    u += vec4(imageLoad(u_image, index));\n"
            "    v += vec4(imageLoad(v_image, index));\n"
            "    u.x += u.y;\n"
            "    u.z += u.w;\n"
            "    u.y = v.x + v.y;\n"
            "    u.w = v.z + v.w;\n"
            "    u = u * 0.25 ;\n"
			"    imageStore(output_image, pos, uvec4(u));\n"
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
    return 0;
}

int GLESConvert::initFBO(void){
    glGenFramebuffers(1, &fboid);
    glBindFramebuffer(GL_FRAMEBUFFER, fboid);

    glGenTextures(2, texIn); 
    printf("texIn:%d,%d\n", texIn[0], texIn[1]);
    glBindTexture(GL_TEXTURE_2D, texIn[0]);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8UI, mWidth / 4, mHeight);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    printf("line:%d glError:%x\n", __LINE__, glGetError()); 

    glBindTexture(GL_TEXTURE_2D, texIn[1]);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8UI, mWidth / 4, mHeight);
    printf("line:%d glError:%x\n", __LINE__, glGetError());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    printf("line:%d glError:%x\n", __LINE__, glGetError()); 

    glGenTextures(1, &texOut);  
    glBindTexture(GL_TEXTURE_2D, texOut);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8UI, mUVStride / 4, mHeight / 2);
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

    glGenBuffers(1, &pboid);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pboid);
    glBufferData(GL_PIXEL_PACK_BUFFER, mOutBufSize, NULL, GL_DYNAMIC_READ);
    return 0;
}

void GLESConvert::performCompute(uint8_t *u, uint8_t *v){

    glUseProgram(program);

    

    glBindTexture(GL_TEXTURE_2D, texIn[0]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,  mWidth / 4, mHeight, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, u);
    printf("line:%d glError:%x\n", __LINE__, glGetError());

    glBindTexture(GL_TEXTURE_2D, texIn[1]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,  mWidth / 4, mHeight, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, v);
    printf("line:%d glError:%x\n", __LINE__, glGetError());

    glBindImageTexture(0, texIn[0], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8UI);
    glBindImageTexture(1, texIn[1], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8UI);
    glBindImageTexture(2, texOut, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8UI);
    printf("line:%d glError:%x\n", __LINE__, glGetError());

    glDispatchCompute(num_groups_x, num_groups_y, 1);
    printf("line:%d glError:%x\n", __LINE__, glGetError());

    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

int GLESConvert::convert(uint8_t *u, uint8_t *v, uint8_t * dst){
    if(!mThreadRun)
        return -1;
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

    glDeleteTextures(2, texIn);
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
