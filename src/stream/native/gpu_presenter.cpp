// SPDX-License-Identifier: GPL-3.0-or-later
// Public ps5-opengl runtime; foreign-memory and HDR extensions are adapted
// from the pinned GPL-2.0-or-later kodi-ps5 implementation (see notices).
#include "gpu_presenter.hpp"
#ifdef OPENNOW_GPU
#include "hardware_decoder.hpp"
#include "hevc_headers.hpp"
#include "surface_fingerprint.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <ps5_opengl_display_modes.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cmath>
extern "C" {
void opennow_media_note(const char*);
void* ps5_opengl_memory_image_create(void*,unsigned,unsigned,unsigned,unsigned,unsigned);
void ps5_opengl_memory_image_destroy(void*);
int ps5_opengl_video_out_handle();
int ps5_opengl_set_scanout_format(std::uint64_t,std::int32_t[4]);
struct ResolutionStatus {std::uint32_t width,height,paneWidth,paneHeight;std::uint64_t rate;float inches;std::uint32_t reserved[4];};
struct OutputStatus {std::uint32_t resolution,range;std::uint64_t rate,flags,reserved[3];};
int sceVideoOutGetResolutionStatus(int,ResolutionStatus*);
int sceVideoOutGetOutputStatus(int,OutputStatus*);
}
static_assert(sizeof(ResolutionStatus)==48&&sizeof(OutputStatus)==48);
namespace opennow::gpu {
namespace {
EGLDisplay display=EGL_NO_DISPLAY;EGLContext context=EGL_NO_CONTEXT;EGLSurface window=EGL_NO_SURFACE;
GLuint program=0,vao=0,uiTexture=0;unsigned width=3840,height=2160,refresh=0;
bool ready=false,hdrScanout=false,videoDrawn=false,qualified[static_cast<unsigned>(StreamProfile::count)]{};
char label[128]="Software video / SDR / Stereo";
using ImageTarget=void(*)(GLenum,void*);ImageTarget imageTarget=nullptr;
constexpr std::uint64_t sdrFormat=0x8000000000000000ULL,hdrFormat=0x8100070422000000ULL;
const char* vertex=R"(#version 330 core
out vec2 uv;
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=vec2(p.x,1.0-p.y);gl_Position=vec4(p*2.0-1.0,0,1);})";
const char* fragment=R"(#version 330 core
in vec2 uv;out vec4 color;
uniform sampler2D yTex;uniform sampler2D uvTex;
uniform vec2 crop;uniform int mode;uniform int fullRange;
void main(){
 if(mode==0){color=texture(yTex,uv);return;}
 if(mode==4||mode==5){
  vec4 ui=texture(yTex,uv);if(ui.a<0.5)discard;
  if(mode==4){color=ui;return;}
  // SDR UI at 203 nits in the HDR scanout, rather than PQ peak white.
  vec3 linear=mix(pow((ui.rgb+0.055)/1.055,vec3(2.4)),ui.rgb/12.92,lessThanEqual(ui.rgb,vec3(0.04045)));
  linear=mat3(0.6274,0.0691,0.0164,0.3293,0.9195,0.0880,0.0433,0.0114,0.8956)*linear;
  vec3 luminance=pow(clamp(linear,0.0,1.0)*0.0203,vec3(0.1593017578125));
  vec3 pq=pow((0.8359375+18.8515625*luminance)/(1.0+18.6875*luminance),vec3(78.84375));
  uvec3 q=uvec3(round(pq*1023.0));uint w=q.r|(q.g<<10u)|(q.b<<20u)|(3u<<30u);
  color=vec4(float((w>>16u)&255u),float((w>>8u)&255u),float(w&255u),float(w>>24u))/255.0;return;
 }
 vec2 pos=uv*crop;float y=texture(yTex,pos).r;vec2 c=texture(uvTex,pos).rg;
 vec3 rgb;
 if(mode==2){y=(y*65535.0-64.0)/876.0;c=(c*65535.0-512.0)/896.0;
 rgb=vec3(y+1.4746*c.y,y-0.164553*c.x-0.571353*c.y,y+1.8814*c.x);
 uvec3 q=uvec3(round(clamp(rgb,0.0,1.0)*1023.0));uint w=q.r|(q.g<<10u)|(q.b<<20u)|(3u<<30u);
 color=vec4(float((w>>16u)&255u),float((w>>8u)&255u),float(w&255u),float(w>>24u))/255.0;
 }else{
 if(mode==3){float raw=y*65535.0;vec2 chroma=c*65535.0;
 y=fullRange==1?raw/1023.0:(raw-64.0)/876.0;c=(chroma-512.0)/(fullRange==1?1023.0:896.0);
 }else{y=fullRange==1?y:(y*255.0-16.0)/219.0;c=(c*255.0-128.0)/(fullRange==1?255.0:224.0);}

 rgb=vec3(y+1.5748*c.y,y-0.187324*c.x-0.468124*c.y,y+1.8556*c.x);color=vec4(clamp(rgb,0.0,1.0),1);}
})";
GLuint shader(GLenum type,const char* source){
 auto id=glCreateShader(type);glShaderSource(id,1,&source,nullptr);glCompileShader(id);GLint ok=0;glGetShaderiv(id,GL_COMPILE_STATUS,&ok);
 if(!ok){char log[1024]{};glGetShaderInfoLog(id,sizeof(log),nullptr,log);opennow_media_note(log);glDeleteShader(id);return 0;}return id;
}
bool setHdr(bool hdr){
 if(hdr==hdrScanout)return true;
 glFinish();std::int32_t results[4]{};const int rc=ps5_opengl_set_scanout_format(hdr?hdrFormat:sdrFormat,results);
 char note[180];std::snprintf(note,sizeof(note),"GPU scanout hdr=%d rc=%x results=%x/%x/%x/%x",hdr,rc,results[0],results[1],results[2],results[3]);opennow_media_note(note);
 if(rc)return false;hdrScanout=hdr;return true;
}
void queryOutput(video::NativeQualification& q){
 const int handle=ps5_opengl_video_out_handle();ResolutionStatus r{};OutputStatus o{};
 const int rr=sceVideoOutGetResolutionStatus(handle,&r),ro=sceVideoOutGetOutputStatus(handle,&o);
 const auto hz=r.rate==0xd?120u:r.rate==3?60u:r.rate==2?50u:r.rate==1?24u:r.rate==4?30u:0u;
 if(rr==0){q.output_width=r.width;q.output_height=r.height;q.output_refresh_hz=hz;refresh=hz;}
 q.hdr_output=ro==0&&(o.range==2||(o.flags&1));
 char note[256];std::snprintf(note,sizeof(note),"GPU output handle=%d resolutionRC=%x %ux%u refreshCode=%llu outputRC=%x range=%u flags=%llu",handle,rr,r.width,r.height,(unsigned long long)r.rate,ro,o.range,(unsigned long long)o.flags);opennow_media_note(note);
 std::snprintf(label,sizeof(label),"OUTPUT %ux%u / %u HZ / %s / STEREO",q.output_width,q.output_height,hz,q.hdr_output?"HDR":"SDR");
}
void qualifyPipeline(const video::NativeMode& mode,const std::vector<std::uint8_t>& data,
                     std::size_t bytes,const video::SurfaceFingerprint& baseline){
 if(baseline.luma_span<8){opennow_media_note("NATIVE pipeline qualification lacks a contrast baseline; depth=1");return;}
 for(unsigned depth:{3u,2u}){
  video::HardwareDecoder decoder;
  bool ok=decoder.openForQualification(mode,depth),flipped=false;
  std::uint64_t submitted=0,completed=0;
  auto accept=[&](video::HardwareDecoder::Picture& p){
   const bool drawn=drawVideo(p.surface,p.mode);
   const auto fingerprint=drawn?video::fingerprintSurface(p.surface,p.mode):std::nullopt;
   bool same=fingerprint&&fingerprint->hash==baseline.hash&&p.pts==completed;
   if(same&&p.pts==31)flipped=swap();
   ++completed;const bool released=decoder.release(p);return same&&released;
  };
  // Check initial output, complete drain/resume and a second reset cycle.
  // Every output must match the classic fixture, including the first.
  for(unsigned cycle=0;ok&&cycle<2;++cycle){
   ok=decoder.reset();
   for(unsigned burst=0;ok&&burst<2;++burst){
    for(unsigned n=0;ok&&n<8;++n){
     video::HardwareDecoder::Picture p;
     const auto result=decoder.decode(data.data(),bytes,submitted++,p);
     ok=result==video::HardwareDecoder::Result::no_picture||
        (result==video::HardwareDecoder::Result::picture&&accept(p));
    }
    while(ok&&decoder.timing().in_flight){
     video::HardwareDecoder::Picture p;
     ok=decoder.drain(p)==video::HardwareDecoder::Result::picture&&accept(p);
    }
   }
  }
  const int result=decoder.error();const bool closed=decoder.close();
  ok=ok&&submitted==32&&completed==32&&flipped&&closed;
  char note[240];std::snprintf(note,sizeof(note),"NATIVE pipeline qualify depth=%u submitted=%llu completed=%llu pixels=%d flip=%d closed=%d rc=%x",depth,static_cast<unsigned long long>(submitted),static_cast<unsigned long long>(completed),ok,flipped,closed,result);opennow_media_note(note);
  if(ok){video::HardwareDecoder::setQualifiedMain10Depth(depth);return;}
  if(!closed)return;
 }
 opennow_media_note("NATIVE pipeline qualification retained depth=1");
}
bool preflight(StreamProfile profile,const char* name){
 const auto settings=settingsFor(profile);
 const auto mode=video::nativeMode(settings.codec==VideoCodec::hevc?video::NativeCodec::hevc_main10:video::NativeCodec::h264,settings.width,settings.height,settings.fps);
 if(!mode)return false;
 char path[192];std::snprintf(path,sizeof(path),"/app0/assets/%s",name);auto* f=std::fopen(path,"rb");
 if(!f){std::snprintf(path,sizeof(path),"assets/%s",name);f=std::fopen(path,"rb");}if(!f){opennow_media_note("GPU qualification fixture missing");return false;}
 std::vector<std::uint8_t> data(1024*1024);const auto bytes=std::fread(data.data(),1,data.size(),f);std::fclose(f);if(!bytes||bytes==data.size())return false;
 if(settings.hdr){video::HevcHeaders h;if(!video::updateHevcHeaders(data.data(),bytes,h)||!h.hdr10()||h.width!=settings.width||h.height!=settings.height){opennow_media_note("GPU qualification invalid HDR SPS");return false;}}
 video::HardwareDecoder decoder;video::NativeQualification q;
 q.decoder_created=decoder.openForQualification(*mode,1);
 video::HardwareDecoder::Picture p;
 q.decode_output_validated=q.decoder_created&&decoder.decode(data.data(),bytes,0,p)==video::HardwareDecoder::Result::picture;
 std::optional<video::SurfaceFingerprint> baseline;
 if(q.decode_output_validated){q.gpu_surface_import=drawVideo(p.surface,*mode);q.completed_flip=q.gpu_surface_import&&swap();if(profile==StreamProfile::native_hdr120)baseline=video::fingerprintSurface(p.surface,*mode);decoder.release(p);}
 const bool closed=decoder.close();queryOutput(q);
 const bool ok=closed&&video::canNegotiateNativeMode(*mode,q);
 char note[256];std::snprintf(note,sizeof(note),"GPU qualification profile=%s created=%d decoded=%d import=%d flip=%d qualified=%d error=%x",profileLabel(profile),q.decoder_created,q.decode_output_validated,q.gpu_surface_import,q.completed_flip,ok,decoder.error());opennow_media_note(note);
 qualified[static_cast<unsigned>(profile)]=ok;
 if(ok&&baseline&&profile==StreamProfile::native_hdr120)qualifyPipeline(*mode,data,bytes,*baseline);
 return ok;
}
}
bool initializeRuntime() noexcept {
 opennow_media_note("GPU START 00.002.026");video::HardwareDecoder::setQualifiedMain10Depth(1);
 const int load=video::HardwareDecoder::loadModule();char note[100];std::snprintf(note,sizeof(note),"Videodec2 load=%x",load);opennow_media_note(note);
 display=eglGetDisplay(EGL_DEFAULT_DISPLAY);if(display==EGL_NO_DISPLAY)return false;
 if(!eglSetDisplayModePS5(display,3840,2160))opennow_media_note("GPU 4K request refused");
 if(!eglSetDisplayRefreshPS5(display,120))opennow_media_note("GPU 120 Hz request refused");
 EGLint major=0,minor=0;if(!eglInitialize(display,&major,&minor))return false;
 eglBindAPI(EGL_OPENGL_API);
 const EGLint attrs[]={EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
 EGLConfig config{};EGLint count=0;if(!eglChooseConfig(display,attrs,&config,1,&count)||count!=1)return false;
 const EGLint ctx[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
 context=eglCreateContext(display,config,EGL_NO_CONTEXT,ctx);if(context==EGL_NO_CONTEXT)return false;
 window=eglCreateWindowSurface(display,config,0,nullptr);if(window==EGL_NO_SURFACE||!eglMakeCurrent(display,window,window,context))return false;
 EGLint w=0,h=0;eglQuerySurface(display,window,EGL_WIDTH,&w);eglQuerySurface(display,window,EGL_HEIGHT,&h);if(w<=0||h<=0)return false;width=w;height=h;
 eglSwapInterval(display,1);glDisable(GL_DITHER);glDisable(GL_FRAMEBUFFER_SRGB);
 const auto vs=shader(GL_VERTEX_SHADER,vertex),fs=shader(GL_FRAGMENT_SHADER,fragment);if(!vs||!fs)return false;
 program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);glDeleteShader(vs);glDeleteShader(fs);GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);if(!ok)return false;
 glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenTextures(1,&uiTexture);
 imageTarget=reinterpret_cast<ImageTarget>(eglGetProcAddress("glEGLImageTargetTexture2DOES"));if(!imageTarget)return false;
 glViewport(0,0,width,height);ready=true;glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);swap();
 if(load==0){preflight(StreamProfile::native_hdr120,"hdr-check.hevc");preflight(StreamProfile::native_hdr90,"hdr-check.hevc");if(!profileAvailable(StreamProfile::native_hdr120))preflight(StreamProfile::native_hdr60,"hdr-check.hevc");preflight(StreamProfile::native_4k120,"4k-check.h264");preflight(StreamProfile::native_4k90,"4k-check.h264");preflight(StreamProfile::native_1080,"1080-check.h264");}
 setHdr(false);glClear(GL_COLOR_BUFFER_BIT);swap();video::NativeQualification q;queryOutput(q);
 videoDrawn=false;return true;
}
bool initialize() noexcept {
 if(initializeRuntime())return true;
 char note[128];std::snprintf(note,sizeof(note),"GPU initialization failed: EGL error=%x GL error=%x",eglGetError(),context!=EGL_NO_CONTEXT?glGetError():0);opennow_media_note(note);
 ready=false;
 if(display!=EGL_NO_DISPLAY){
  eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
  if(window!=EGL_NO_SURFACE)eglDestroySurface(display,window);
  if(context!=EGL_NO_CONTEXT)eglDestroyContext(display,context);
  eglTerminate(display);
 }
 window=EGL_NO_SURFACE;context=EGL_NO_CONTEXT;display=EGL_NO_DISPLAY;return false;
}
void shutdown() noexcept {
 if(display==EGL_NO_DISPLAY)return;
 if(context!=EGL_NO_CONTEXT){
  glFinish();glDeleteTextures(1,&uiTexture);glDeleteVertexArrays(1,&vao);glDeleteProgram(program);
 }
 ready=false;
 eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
 if(window!=EGL_NO_SURFACE)eglDestroySurface(display,window);
 if(context!=EGL_NO_CONTEXT)eglDestroyContext(display,context);
 eglTerminate(display);
 window=EGL_NO_SURFACE;context=EGL_NO_CONTEXT;display=EGL_NO_DISPLAY;
}
bool available() noexcept{return ready;}
bool profileAvailable(StreamProfile p) noexcept{const unsigned index=static_cast<unsigned>(p);return index<static_cast<unsigned>(StreamProfile::count)&&qualified[index];}
StreamProfile bestProfile() noexcept{for(auto p:{StreamProfile::native_hdr120,StreamProfile::native_hdr90,StreamProfile::native_hdr60,StreamProfile::native_4k120,StreamProfile::native_4k90,StreamProfile::native_1080})if(profileAvailable(p))return p;return StreamProfile::quality;}
bool drawVideo(const video::NativeSurface& s,const video::NativeMode& mode) noexcept {
 if(!ready||!s.buffer||!setHdr(mode.hdr))return false;
 const unsigned bytes=mode.storage==video::SampleStorage::low_aligned_10bit?2:1,rows=s.height;
 void* images[2]={ps5_opengl_memory_image_create(const_cast<void*>(s.buffer),s.pitch_bytes/bytes,rows,s.pitch_bytes,1,bytes),
 ps5_opengl_memory_image_create(static_cast<std::uint8_t*>(const_cast<void*>(s.buffer))+std::size_t(s.pitch_bytes)*rows,s.pitch_bytes/(2*bytes),rows/2,s.pitch_bytes,2,bytes)};
 GLuint textures[2]{};bool ok=images[0]&&images[1];
 if(ok){glGenTextures(2,textures);glUseProgram(program);glBindVertexArray(vao);glDisable(GL_FRAMEBUFFER_SRGB);glDisable(GL_DITHER);
  for(unsigned i=0;i<2;++i){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,textures[i]);imageTarget(GL_TEXTURE_2D,images[i]);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);}
  glUniform1i(glGetUniformLocation(program,"yTex"),0);glUniform1i(glGetUniformLocation(program,"uvTex"),1);glUniform1i(glGetUniformLocation(program,"mode"),mode.hdr?2:bytes==2?3:1);
  glUniform1i(glGetUniformLocation(program,"fullRange"),mode.full_range?1:0);
  glUniform2f(glGetUniformLocation(program,"crop"),float(mode.visible_width)/s.pitch_components,float(mode.visible_height)/rows);glDrawArrays(GL_TRIANGLES,0,3);glFinish();ok=glGetError()==GL_NO_ERROR;
 }
 glDeleteTextures(2,textures);for(auto* image:images)if(image)ps5_opengl_memory_image_destroy(image);
 if(!ok)opennow_media_note("GPU surface import/draw failed");videoDrawn=ok;return ok;
}
void drawInterface(const std::uint32_t* pixels) noexcept {
 if(!ready||!pixels||!setHdr(false))return;
 glUseProgram(program);glBindVertexArray(vao);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,uiTexture);
 glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1920,1080,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
 glUniform1i(glGetUniformLocation(program,"yTex"),0);glUniform1i(glGetUniformLocation(program,"mode"),0);glDrawArrays(GL_TRIANGLES,0,3);
}
void drawOverlay(const std::uint32_t* pixels) noexcept {
 if(!ready||!pixels)return;
 glUseProgram(program);glBindVertexArray(vao);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,uiTexture);
 glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1920,1080,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 glUniform1i(glGetUniformLocation(program,"yTex"),0);glUniform1i(glGetUniformLocation(program,"mode"),hdrScanout?5:4);
 glDrawArrays(GL_TRIANGLES,0,3);
}
bool swap() noexcept{return ready&&eglSwapBuffers(display,window)==EGL_TRUE;}
bool takeVideoDrawn() noexcept{const bool drawn=videoDrawn;videoDrawn=false;return drawn;}
const char* outputLabel() noexcept{return label;}
}
#else
namespace opennow::gpu {
bool initialize() noexcept{return false;}bool available() noexcept{return false;}
void shutdown() noexcept{}
bool profileAvailable(StreamProfile) noexcept{return false;}StreamProfile bestProfile() noexcept{return StreamProfile::quality;}
bool drawVideo(const video::NativeSurface&,const video::NativeMode&) noexcept{return false;}
void drawOverlay(const std::uint32_t*) noexcept{}
void drawInterface(const std::uint32_t*) noexcept{}bool swap() noexcept{return false;}
bool takeVideoDrawn() noexcept{return false;}
const char* outputLabel() noexcept{return "1080P SDR / STEREO";}
}
#endif
