// SPDX-License-Identifier: GPL-3.0-or-later
#include "media.hpp"
#include "AudioRtpUtils.hpp"
#include "native/gpu_presenter.hpp"
#include "native/hevc_headers.hpp"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <algorithm>
#include <cerrno>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
#include <opus/opus.h>
#include <opus/opus_multistream.h>
// FFmpeg 7.0 FFCodec starts with its public AVCodec, pinned by PacBrew.
extern const AVCodec ff_h264_decoder;
int scePthreadCreate(void**,const void*,void*(*)(void*),void*,const char*);
int scePthreadJoin(void*,void**);
int sceAudioOutInit();int sceAudioOutOpen(int,int,int,unsigned,unsigned,unsigned);
int sceAudioOutOutput(int,const void*);int sceAudioOutClose(int);
int sceKernelUsleep(unsigned);
unsigned long long sceKernelGetProcessTime();
}
extern "C" void opennow_media_note(const char*);
extern "C" void opennow_hevc_sps_note(const std::uint8_t*,std::size_t);
namespace opennow {
namespace {
void decoderLog(void*,int level,const char* format,va_list args){
 if(level>AV_LOG_ERROR)return;
 char line[384];std::vsnprintf(line,sizeof(line),format,args);opennow_media_note(line);
}
}

unsigned Media::availableAudioChannels() noexcept {
 const int init=sceAudioOutInit();
 if(init!=0&&static_cast<unsigned>(init)!=0x8026000e)return 2;
 const int handle=sceAudioOutOpen(0xff,0,0,256,48000,2);
 if(handle<0)return 2;
 return sceAudioOutClose(handle)>=0?8:2;
}
bool Media::configureAudio(const audio::Format& format) noexcept {
 // Called on the receive thread before sending the SDP answer / accepting RTP.
 if(!format.channels||format.channels>outputChannels_)return false;
 int error=0;
 auto* decoder=opus_multistream_decoder_create(48000,format.channels,format.streams,
     format.coupled,format.mapping.data(),&error);
 if(!decoder||error){if(decoder)opus_multistream_decoder_destroy(decoder);return false;}
 if(opus_)opus_multistream_decoder_destroy(opus_);
 opus_=decoder;audioFormat_=format;audioChannels=format.channels;sequenceSeen_=false;stereoProbePackets_=0;
 pthread_mutex_lock(&lock_);audioQueue_.clear();pthread_mutex_unlock(&lock_);
 char note[192];std::snprintf(note,sizeof(note),"AUDIO negotiated=%uch requested=%uch output=%uch streams=%u coupled=%u PT=%d RED=%d ceiling=%u",
     format.channels,settings_.audio_channels,outputChannels_,format.streams,format.coupled,
     format.payload,format.redPayload,format.bitrate());opennow_media_note(note);
 return true;
}

bool Media::start(const StreamSettings& settings) noexcept {
 stop();settings_=settings;presented=0;actualHdr=false;frames=0;dropped=0;videoUnits=0;audioPackets=0;audioErrors=0;audioRecovered=0;audioConcealed=0;audioUnderruns=0;expectedAudioTimestamp_=0;decodeError=0;audioQueue_.clear();fresh_=sequenceSeen_=false;recovery_.reset();queueDrops=0;corruptFrames=0;recoveryResets=0;idrFrames=0;decodedWidth=0;decodedHeight=0;videoBytes=0;
 queueDepth=0;queuePeak=0;decodeCalls=0;gpuCalls=0;decodeUs=0;decodeMaxUs=0;queueMaxUs=0;gpuUs=0;gpuMaxUs=0;
 if(settings_.hardware){
#ifdef OPENNOW_GPU
  if(!gpu::available())return false;
  const auto mode=video::nativeMode(settings_.codec==VideoCodec::hevc?video::NativeCodec::hevc_main10:video::NativeCodec::h264,settings_.width,settings_.height,settings_.fps);
  if(!mode)return false;nativeMode_=*mode;if(!nativeDecoder_.open(nativeMode_)){decodeError=nativeDecoder_.error();return false;}
 #else
  return false;
 #endif
 } else {
 av_log_set_callback(decoderLog);
 auto* decoder=&ff_h264_decoder;
 if(LIBAVCODEC_VERSION_INT!=AV_VERSION_INT(61,3,100)||decoder->id!=AV_CODEC_ID_H264)return false;
 codec_=avcodec_alloc_context3(decoder);if(!codec_)return false;
 codec_->thread_count=3;codec_->thread_type=FF_THREAD_SLICE;codec_->flags|=AV_CODEC_FLAG_LOW_DELAY;
 if(avcodec_open2(codec_,decoder,nullptr)<0){stop();return false;}
 }
 audioChannels=0;audioBytes=0;audioSamples=0;audioQueueFrames=0;audioQueuePeak=0;audioDroppedFrames=0;audioOutputErrors=0;audioFecAttempts=0;
 if(!settings_.hardware)pixels_=static_cast<std::uint32_t*>(std::calloc(1920*1080,4));
 const bool queueReady=compressed_.open(settings_.hardware?8:2,cap,AV_INPUT_BUFFER_PADDING_SIZE);
 if((!settings_.hardware&&!pixels_)||!queueReady){stop();return false;}
 const int init=sceAudioOutInit();
 if(init!=0&&static_cast<unsigned>(init)!=0x8026000e){stop();return false;}
 outputChannels_=settings_.audio_channels>2?8:2;
 audioHandle_=sceAudioOutOpen(0xff,0,0,256,48000,outputChannels_==8?2:1);
 if(audioHandle_<0&&outputChannels_==8){outputChannels_=2;audioHandle_=sceAudioOutOpen(0xff,0,0,256,48000,1);}
 audioQueue_.open(outputChannels_);
 if(audioHandle_<0){stop();return false;}
 running_=true;
 if(scePthreadCreate(&videoThread_,nullptr,decode,this,"opennow-video")||scePthreadCreate(&audioThread_,nullptr,output,this,"opennow-audio")){stop();return false;}
 return true;
}
void Media::stop() noexcept {
 running_=false;pthread_cond_broadcast(&wake_);
 if(videoThread_){scePthreadJoin(videoThread_,nullptr);videoThread_=nullptr;}
 if(audioThread_){scePthreadJoin(audioThread_,nullptr);audioThread_=nullptr;}
 if(audioHandle_>=0){sceAudioOutOutput(audioHandle_,nullptr);sceAudioOutClose(audioHandle_);audioHandle_=-1;}
 #ifdef OPENNOW_GPU
 pthread_mutex_lock(&lock_);clearNativePending();while(nativeInFlight_)pthread_cond_wait(&wake_,&lock_);pthread_mutex_unlock(&lock_);
 nativeDecoder_.close();
 #endif
 avcodec_free_context(&codec_);if(scaler_){sws_freeContext(scaler_);scaler_=nullptr;}
 if(opus_){opus_multistream_decoder_destroy(opus_);opus_=nullptr;}
 compressed_.close();
 pthread_mutex_lock(&lock_);std::free(pixels_);pixels_=nullptr;fresh_=false;pthread_mutex_unlock(&lock_);
}
void Media::clearNativePending() noexcept {
 #ifdef OPENNOW_GPU
 if(nativePending_){nativeDecoder_.release(pendingPicture_);nativePending_=false;}
 #endif
}
void Media::requireKeyframe() noexcept {
 pthread_mutex_lock(&lock_);recovery_.invalidate();compressed_.clear();queueDepth=0;fresh_=false;clearNativePending();
 ++recoveryResets;pthread_mutex_unlock(&lock_);
}
bool Media::video(const std::uint8_t* data,std::size_t size) noexcept {
 videoBytes+=static_cast<unsigned>(size);
 const auto units=++videoUnits;
 if(units<=3){char note[256];unsigned mask=0;
  for(std::size_t i=0;data&&i+4<size;++i)if(data[i]==0&&data[i+1]==0){if(data[i+2]==1)mask|=1u<<(data[i+3]&31);else if(data[i+2]==0&&data[i+3]==1)mask|=1u<<(data[i+4]&31);}
  std::snprintf(note,sizeof(note),"VIDEO codec=%s unit=%u bytes=%zu nalTypesMask=%u",settings_.codec==VideoCodec::hevc?"HEVC":"H264",units,size,mask);opennow_media_note(note);
 }
 if(!running_||!data||!size||size>cap)return false;
 pthread_mutex_lock(&lock_);
 if(compressed_.full()){
  recovery_.invalidate();compressed_.clear();queueDepth=0;fresh_=false;clearNativePending();++queueDrops;++recoveryResets;++dropped;
  // An incoming IDR can immediately restore references. Rejecting it here
  // needlessly requested another large IDR and prolonged the reset storm.
 }
 if(!recovery_.accept(data,size,settings_.codec==VideoCodec::hevc)){pthread_mutex_unlock(&lock_);return false;}
 const bool queued=compressed_.push(data,size,sceKernelGetProcessTime());queueDepth=compressed_.size();if(queueDepth.load()>queuePeak.load())queuePeak=queueDepth.load();
 if(queued)pthread_cond_signal(&wake_);pthread_mutex_unlock(&lock_);return queued;
}
void* Media::decode(void* context) {
 auto& self=*static_cast<Media*>(context);auto* packet=av_packet_alloc();auto* frame=av_frame_alloc();
 if(!packet||!frame){av_packet_free(&packet);av_frame_free(&frame);return nullptr;}
 std::uint64_t decoderEpoch=UINT64_MAX;
#ifdef OPENNOW_GPU
 video::HevcHeaders headers;unsigned headerLogs=0;
 std::uint64_t lastNativeInput=0;
 auto publishNative=[&](video::HardwareDecoder::Picture& picture,std::uint64_t epoch){
  pthread_mutex_lock(&self.lock_);
  if(!self.running_||!self.recovery_.current(epoch))self.nativeDecoder_.release(picture);
  else{self.clearNativePending();self.pendingPicture_=picture;self.nativePending_=true;++self.frames;self.decodedWidth=picture.surface.width;self.decodedHeight=picture.mode.visible_height;self.actualHdr=picture.mode.hdr;self.decodeError=0;}
  pthread_mutex_unlock(&self.lock_);
 };
 auto recoverNative=[&](){
  self.decodeError=self.nativeDecoder_.error();++self.dropped;
  const bool deeper=self.nativeDecoder_.timing().pipeline_depth>1;
  self.requireKeyframe();
  if(deeper){
   pthread_mutex_lock(&self.lock_);self.clearNativePending();while(self.nativeInFlight_)pthread_cond_wait(&self.wake_,&self.lock_);pthread_mutex_unlock(&self.lock_);
   if(!self.nativeDecoder_.fallbackToClassic())return false;
   decoderEpoch=UINT64_MAX;
  }
  return true;
 };
#endif
 while(self.running_) {
  bool drainNow=false;
  pthread_mutex_lock(&self.lock_);while(!self.compressed_.size()&&self.running_){
#ifdef OPENNOW_GPU
   const auto timing=self.nativeDecoder_.timing();
   if(self.settings_.hardware&&timing.pipeline_depth>1&&timing.in_flight&&self.recovery_.current(decoderEpoch)){
    const auto grace=std::max(20000u,1500000u/unsigned(self.settings_.fps));
    if(sceKernelGetProcessTime()-lastNativeInput>=grace){drainNow=true;break;}
    timespec until{};
    if(clock_gettime(CLOCK_REALTIME,&until)){pthread_mutex_unlock(&self.lock_);sceKernelUsleep(5000);pthread_mutex_lock(&self.lock_);continue;}
    until.tv_nsec+=5000000;
    if(until.tv_nsec>=1000000000){++until.tv_sec;until.tv_nsec-=1000000000;}
    const int waited=pthread_cond_timedwait(&self.wake_,&self.lock_,&until);
    if(waited&&waited!=ETIMEDOUT){pthread_mutex_unlock(&self.lock_);sceKernelUsleep(5000);pthread_mutex_lock(&self.lock_);}
    continue;
   }
#endif
   pthread_cond_wait(&self.wake_,&self.lock_);
  }
  if(!self.running_){pthread_mutex_unlock(&self.lock_);break;}
  const auto packetEpoch=self.recovery_.epoch();
#ifdef OPENNOW_GPU
  if(drainNow){
   pthread_mutex_unlock(&self.lock_);bool recovered=true;
   // Once flushing starts, fully drain before taking any newly queued AU.
   while(self.running_&&self.nativeDecoder_.timing().in_flight){
    video::HardwareDecoder::Picture picture;const auto result=self.nativeDecoder_.drain(picture);
    if(result==video::HardwareDecoder::Result::blocked){sceKernelUsleep(1000);continue;}
    if(result!=video::HardwareDecoder::Result::picture){recovered=recoverNative();break;}
    publishNative(picture,packetEpoch);
   }
   if(!recovered)break;
   continue;
  }
#else
  (void)drainNow;
#endif
  const auto unit=self.compressed_.take();self.queueDepth=self.compressed_.size();pthread_mutex_unlock(&self.lock_);
  const auto waited=sceKernelGetProcessTime()-unit.received;if(waited>self.queueMaxUs.load())self.queueMaxUs=waited;
  // Native decode consumes the owned handoff buffer directly. Software FFmpeg
  // retains its refcounted packet allocation for delayed frame references.
  const std::uint8_t* data=unit.data;const auto size=unit.size;
 #ifdef OPENNOW_GPU
  if(self.settings_.hardware){
   if(decoderEpoch!=packetEpoch){
    pthread_mutex_lock(&self.lock_);self.clearNativePending();while(self.nativeInFlight_)pthread_cond_wait(&self.wake_,&self.lock_);pthread_mutex_unlock(&self.lock_);
    if(!self.nativeDecoder_.reset()){self.decodeError=self.nativeDecoder_.error();av_packet_unref(packet);break;}decoderEpoch=packetEpoch;
   }
   if(self.settings_.codec==VideoCodec::hevc){
    const auto previous=headers;const bool valid=video::updateHevcHeaders(data,size,headers);
    if(headerLogs<3&&(!headerLogs||(previous.width!=headers.width||previous.height!=headers.height||previous.bit_depth!=headers.bit_depth||previous.primaries!=headers.primaries||previous.transfer!=headers.transfer||previous.matrix!=headers.matrix||previous.full_range!=headers.full_range||previous.profile!=headers.profile||previous.valid!=headers.valid))){
     const std::uint8_t* nal=nullptr;std::size_t bytes=0;video::hevcSpsView(data,size,nal,bytes);
     if(nal)opennow_hevc_sps_note(nal,bytes);
     char note[256];std::snprintf(note,sizeof(note),"HEVC SPS valid=%d %ux%u profile=%u level=%u depth=%u colors=%u/%u/%u full=%d dpb=%u/%u bytes=%zu",valid,headers.width,headers.height,headers.profile,headers.level,headers.bit_depth,headers.primaries,headers.transfer,headers.matrix,headers.full_range,headers.max_dpb_frames,video::nativeDpbFrames(self.nativeMode_),bytes);opennow_media_note(note);++headerLogs;
    }
    const bool sdr=valid&&headers.primaries==1&&headers.matrix==1&&(headers.transfer==1||headers.transfer==13);
    const auto actual=video::nativeMode(headers.hdr10()?video::NativeCodec::hevc_main10:headers.bit_depth==10?video::NativeCodec::hevc_main10_sdr:video::NativeCodec::hevc,headers.width,headers.height,self.settings_.fps);
    if(!valid||(!headers.hdr10()&&!sdr)||!actual||headers.profile!=actual->profile||headers.level>self.nativeMode_.level||headers.max_dpb_frames>video::nativeDpbFrames(self.nativeMode_)){
     self.decodeError=-1001;if(headerLogs==1){opennow_media_note("HEVC color metadata rejected; retaining the last picture");++headerLogs;}av_packet_unref(packet);self.requireKeyframe();continue;
    }
    auto output=*actual;output.full_range=headers.full_range;
    if(!self.nativeDecoder_.expectOutput(output)){self.decodeError=self.nativeDecoder_.error();av_packet_unref(packet);self.requireKeyframe();continue;}
   }
   if(video::Recovery::hasIdr(data,size,self.settings_.codec==VideoCodec::hevc))++self.idrFrames;
   const auto decodeStart=sceKernelGetProcessTime();
   video::HardwareDecoder::Picture picture;auto result=self.nativeDecoder_.decode(data,size,unit.received,picture);
   while(result==video::HardwareDecoder::Result::blocked&&self.running_){sceKernelUsleep(1000);result=self.nativeDecoder_.decode(data,size,unit.received,picture);}
   const auto decodeTime=sceKernelGetProcessTime()-decodeStart;++self.decodeCalls;self.decodeUs+=decodeTime;if(decodeTime>self.decodeMaxUs.load())self.decodeMaxUs=decodeTime;
   lastNativeInput=sceKernelGetProcessTime();
   av_packet_unref(packet);
   if(result==video::HardwareDecoder::Result::error){if(!recoverNative())break;continue;}
   if(result==video::HardwareDecoder::Result::picture)publishNative(picture,packetEpoch);
   continue;
  }
 #endif
  if(av_new_packet(packet,size)<0)break;
  std::memcpy(packet->data,data,size);
  // Flush only on the decoder thread; recovered IDRs must not share old
  // reference pictures, and stale in-flight output must never be published.
  if(decoderEpoch!=packetEpoch){avcodec_flush_buffers(self.codec_);decoderEpoch=packetEpoch;}
  if(video::Recovery::hasIdr(packet->data,packet->size))++self.idrFrames;
  int result=avcodec_send_packet(self.codec_,packet);av_packet_unref(packet);
  if(result<0){self.decodeError=result;++self.dropped;self.requireKeyframe();continue;}
  while(avcodec_receive_frame(self.codec_,frame)==0){
   if((frame->flags&AV_FRAME_FLAG_CORRUPT)||frame->decode_error_flags){++self.corruptFrames;++self.dropped;self.requireKeyframe();av_frame_unref(frame);continue;}
   if(self.decodedWidth!=frame->width||self.decodedHeight!=frame->height){
    char note[128];std::snprintf(note,sizeof(note),"VIDEO dimensions changed: %dx%d",frame->width,frame->height);opennow_media_note(note);
   }
   self.decodedWidth=frame->width;self.decodedHeight=frame->height;
   if(frame->width<=0||frame->width>1920||frame->height<=0||frame->height>1080){++self.dropped;av_frame_unref(frame);continue;}
   self.scaler_=sws_getCachedContext(self.scaler_,frame->width,frame->height,static_cast<AVPixelFormat>(frame->format),1920,1080,AV_PIX_FMT_RGBA,SWS_BILINEAR,nullptr,nullptr,nullptr);
   if(self.scaler_){
    // Honor the stream's YUV range/matrix instead of swscale's SD defaults.
    int matrix=SWS_CS_ITU709;
    if(frame->colorspace==AVCOL_SPC_BT470BG||frame->colorspace==AVCOL_SPC_SMPTE170M)matrix=SWS_CS_ITU601;
    const auto* coefficients=sws_getCoefficients(matrix);
    sws_setColorspaceDetails(self.scaler_,coefficients,frame->color_range==AVCOL_RANGE_JPEG,
                            coefficients,1,0,1<<16,1<<16);
    pthread_mutex_lock(&self.lock_);
    if(!self.recovery_.current(packetEpoch)){pthread_mutex_unlock(&self.lock_);av_frame_unref(frame);continue;}
    std::uint8_t* dst[]={reinterpret_cast<std::uint8_t*>(self.pixels_)};int stride[]={1920*4};sws_scale(self.scaler_,frame->data,frame->linesize,0,frame->height,dst,stride);self.fresh_=true;++self.frames;pthread_mutex_unlock(&self.lock_);}
   av_frame_unref(frame);
  }
 }
 av_packet_free(&packet);av_frame_free(&frame);return nullptr;
}
void Media::audio(const std::uint8_t* data,std::size_t size,std::uint16_t sequence,std::uint8_t payloadType,std::uint32_t timestamp) noexcept {
 ++audioPackets;
 if(payloadType!=audioFormat_.payload&&payloadType!=audioFormat_.redPayload){++audioErrors;return;}
 auto payload=audio::ParseRedPrimary(data,size,payloadType,audioFormat_.payload,audioFormat_.redPayload);
 if(!payload.data||!payload.size){++audioErrors;return;}
 if(!running_||!opus_||payload.size>16384)return;
 if(sequenceSeen_&&static_cast<std::int16_t>(sequence-lastSequence_)<=0)return;
 const int packetSamples=opus_packet_get_nb_samples(payload.data,payload.size,48000);
 if(packetSamples<=0||packetSamples>5760){++audioErrors;return;}
 audioBytes+=static_cast<unsigned>(payload.size);
 auto enqueue=[this](const std::int16_t* pcm,unsigned count){
  pthread_mutex_lock(&lock_);
  audioQueue_.push(pcm,count,audioFormat_.channels);
  audioQueueFrames=audioQueue_.frames();audioQueuePeak=audioQueue_.peak();audioDroppedFrames=audioQueue_.dropped();
  pthread_mutex_unlock(&lock_);
 };
 std::int16_t decoded[5760*8];
 if(sequenceSeen_&&timestamp!=expectedAudioTimestamp_){
  const int gap=audio::RecoverySamples(timestamp,expectedAudioTimestamp_);
  if(gap){
   auto redundant=payloadType==audioFormat_.redPayload?audio::ParseLatestRedundant(data,size,audioFormat_.payload):audio::RedundantPayload{};
   int recovered=0;
   if(redundant.data&&redundant.timestamp_offset==gap&&
      opus_packet_get_nb_samples(redundant.data,redundant.size,48000)==gap){
    recovered=opus_multistream_decode(opus_,redundant.data,redundant.size,decoded,gap,0);
    if(recovered>0)++audioRecovered;
   }
   if(recovered<=0){
    // A single missing frame can use the next packet's in-band FEC. Opus
    // supplies PLC itself when FEC is absent, so count attempts separately.
    if(gap==packetSamples){++audioFecAttempts;recovered=opus_multistream_decode(opus_,payload.data,payload.size,decoded,gap,1);}
    if(recovered<=0)recovered=opus_multistream_decode(opus_,nullptr,0,decoded,gap,0);
    if(recovered>0)++audioConcealed;
   }
   if(recovered>0)enqueue(decoded,recovered);
  }else{
   opus_multistream_decoder_ctl(opus_,OPUS_RESET_STATE);
   pthread_mutex_lock(&lock_);audioQueue_.clear();pthread_mutex_unlock(&lock_);
  }
 }
 int n=opus_multistream_decode(opus_,payload.data,payload.size,decoded,5760,0);
 if(n<=0&&audioFormat_.channels>2&&audioSamples.load()==0){
  // Some seats describe surround but send elementary stereo from the outset.
  // Require eight consecutive decodable stereo packets before changing format;
  // never downgrade an established surround stream on a damaged packet.
  int error=0;auto* probe=opus_decoder_create(48000,2,&error);
  const int stereoSamples=probe&&!error?opus_decode(probe,payload.data,payload.size,decoded,5760,0):0;
  if(probe)opus_decoder_destroy(probe);
  stereoProbePackets_=stereoSamples==packetSamples?stereoProbePackets_+1:0;
  if(stereoProbePackets_>=8){
   auto stereo=audioFormat_;stereo.channels=2;stereo.streams=stereo.coupled=1;stereo.mapping={0,1};
   if(configureAudio(stereo)){
    opennow_media_note("AUDIO startup fallback: server sent stereo instead of described surround");
    n=opus_multistream_decode(opus_,payload.data,payload.size,decoded,5760,0);
   }
  }
 }
 if(n<=0){++audioErrors;return;}
 sequenceSeen_=true;lastSequence_=sequence;expectedAudioTimestamp_=timestamp+static_cast<unsigned>(n);
 audioSamples+=static_cast<unsigned>(n);enqueue(decoded,n);
}
void* Media::output(void* context) {
 auto& self=*static_cast<Media*>(context);alignas(64) std::int16_t block[256*8];
 while(self.running_){
  pthread_mutex_lock(&self.lock_);self.audioQueue_.pop(block);
  self.audioQueueFrames=self.audioQueue_.frames();self.audioUnderruns=self.audioQueue_.underruns();
  pthread_mutex_unlock(&self.lock_);
  if(sceAudioOutOutput(self.audioHandle_,block)<0){++self.audioOutputErrors;sceKernelUsleep(5000);}
 }
 return nullptr;
}
bool Media::draw(ps5::demo::Canvas& c) noexcept {
 #ifdef OPENNOW_GPU
 if(settings_.hardware){
  pthread_mutex_lock(&lock_);if(!nativePending_){pthread_mutex_unlock(&lock_);return false;}
  const auto picture=pendingPicture_;nativePending_=false;nativeInFlight_=true;pthread_mutex_unlock(&lock_);
  const auto drawStart=sceKernelGetProcessTime();const bool drawn=gpu::drawVideo(picture.surface,picture.mode);nativeDecoder_.release(picture);
  const auto drawTime=sceKernelGetProcessTime()-drawStart;++gpuCalls;gpuUs+=drawTime;if(drawTime>gpuMaxUs.load())gpuMaxUs=drawTime;
  pthread_mutex_lock(&lock_);nativeInFlight_=false;pthread_cond_broadcast(&wake_);pthread_mutex_unlock(&lock_);
  if(drawn)++presented;else{decodeError=-1002;requireKeyframe();}return drawn;
 }
 #endif
 pthread_mutex_lock(&lock_);bool changed=fresh_&&pixels_;if(changed){c.image(0,0,1920,1080,pixels_);fresh_=false;++presented;}pthread_mutex_unlock(&lock_);return changed;
}
}
