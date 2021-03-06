#ifndef _GLESCONVERT_H_
#define _GLESCONVERT_H_
#include <stdint.h>
#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <pthread.h>
#include <semaphore.h>


// Some platform can't do eglMakeCurrent with NULL surface
// So use pbuffer to create a 1x1 surface
#define USE_PBUFFER 1

class GLESConvert{
public:
    GLESConvert(uint32_t width, uint32_t height, uint32_t uv_stride);
    ~GLESConvert();
    int convert(uint8_t *u, uint8_t *v, uint8_t *dst);
	void waitGLInit(void);

private:
	static void *gles_entry(void *data);
	void glesMain(void);

	int initEgl(void);
	int initProgram(void);
	int initFBO(void);
	void performCompute(uint8_t *u, uint8_t *v);

	void cleanGLES(void);
private:
	uint32_t mWidth;
	uint32_t mHeight;
	uint32_t mUVStride;

	pthread_t mThread;
	uint8_t *cu;
	uint8_t *cv;
	uint8_t *cdst;
	sem_t mGLSem;
	sem_t mCustSem;
	
	bool mThreadRun;
	
	// OPENGL ES 3.1 ComputeShader
	EGLDisplay display;
	EGLContext context;
#ifdef USE_PBUFFER
	EGLSurface surface; 
#endif
	//framebuffer object
	GLuint fboid;
    GLuint texIn[2];
    GLuint texOut;

    //Vertex Buffer Object
    GLuint pboid;
	
	// computer program
    GLuint program;

	GLuint num_groups_x;
	GLuint num_groups_y;
	GLsizeiptr mOutBufSize;
};
#endif
