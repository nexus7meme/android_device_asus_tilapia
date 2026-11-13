/* SPDX-License-Identifier: Apache-2.0 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <vector>
static double now() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static int temperature(const char* path) {
    FILE* file = fopen(path,"r"); int value = -1;
    if (file) { if (fscanf(file,"%d",&value) != 1) value = -1; fclose(file); }
    return value;
}
static GLuint shader(GLenum type, const char* text) {
    GLuint s = glCreateShader(type); glShaderSource(s, 1, &text, nullptr); glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[2048]; glGetShaderInfoLog(s, sizeof(log), nullptr, log); puts(log); exit(1); }
    return s;
}
int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    int seconds = argc > 1 ? atoi(argv[1]) : 20;
    if (seconds < 1 || seconds > 60) return 1;
    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(d, nullptr, nullptr)) { printf("eglInitialize: %x\n", eglGetError()); return 1; }
    const EGLint attrs[] = {EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig cfg; EGLint count;
    if (!eglChooseConfig(d, attrs, &cfg, 1, &count) || !count) return 1;
    const EGLint size[] = {EGL_WIDTH,512,EGL_HEIGHT,512,EGL_NONE};
    const EGLint ver[] = {EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLSurface surf = eglCreatePbufferSurface(d, cfg, size);
    EGLContext ctx = eglCreateContext(d, cfg, EGL_NO_CONTEXT, ver);
    if (!eglMakeCurrent(d, surf, surf, ctx)) { printf("eglMakeCurrent: %x\n", eglGetError()); return 1; }
    printf("renderer=%s\n", glGetString(GL_RENDERER));
    const char* vs = "attribute vec2 p; void main(){gl_Position=vec4(p,0.,1.);}";
    const char* fs = "precision highp float; uniform float seed; void main(){"
        "vec3 v=vec3(gl_FragCoord.xy/512.,seed);"
        "for(int i=0;i<32;i++){v=fract(sin(v.yzx*vec3(1.3,1.7,1.9)+v+float(i)*.01)*3.7);}"
        "gl_FragColor=vec4(v,1.);}";
    GLuint prog = glCreateProgram(); GLuint v = shader(GL_VERTEX_SHADER,vs), f = shader(GL_FRAGMENT_SHADER,fs);
    glAttachShader(prog,v); glAttachShader(prog,f); glBindAttribLocation(prog,0,"p"); glLinkProgram(prog);
    GLint linked; glGetProgramiv(prog,GL_LINK_STATUS,&linked); if(!linked) return 1;
    glUseProgram(prog); GLint seed = glGetUniformLocation(prog,"seed");
    const GLfloat vertices[] = {-1,-1,1,-1,-1,1,1,1};
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,vertices); glEnableVertexAttribArray(0);
    glViewport(0,0,512,512); glDisable(GL_DITHER);
    std::vector<unsigned char> pixels(512*512*4);
    uint64_t golden[4] = {}; unsigned frames=0, failures=0;
    double start=now();
    do {
        int die = temperature("/sys/class/thermal/thermal_zone0/temp");
        int battery = temperature("/sys/class/power_supply/battery/temp");
        if (die < 0 || battery < 0 || die >= 55000 || battery >= 400) {
            printf("STOP: thermal guard die_mC=%d battery_deciC=%d\n",die,battery);
            return 2;
        }
        unsigned pattern=frames%4; glUniform1f(seed, .125f*(pattern+1));
        glDrawArrays(GL_TRIANGLE_STRIP,0,4); glFinish();
        glReadPixels(0,0,512,512,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        if (glGetError()!=GL_NO_ERROR) { puts("FAIL: GL error"); return 1; }
        uint64_t hash=14695981039346656037ULL;
        for (auto b:pixels) {hash^=b;hash*=1099511628211ULL;}
        if(frames<4) {golden[pattern]=hash;printf("pattern=%u hash=%016llx\n",pattern,(unsigned long long)hash);}
        else if(hash!=golden[pattern]) ++failures;
        ++frames;
    } while(now()-start<seconds || frames<8);
    printf("frames=%u seconds=%.3f fps=%.3f pixel_mismatches=%u\n",frames,now()-start,frames/(now()-start),failures);
    glDeleteProgram(prog); glDeleteShader(v);glDeleteShader(f);
    eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroyContext(d,ctx);eglDestroySurface(d,surf);eglTerminate(d);
    return failures?1:0;
}
