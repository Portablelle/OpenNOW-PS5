// SPDX-License-Identifier: GPL-3.0-or-later
// NVIDIA signaling and controller wire formats adapted from OpenNOW-Switch (MIT).
#include "stream.hpp"
extern "C" {
#include "peer.h"
}
#include "../vendor/cJSON.h"
#include "../random.hpp"
#include <cstdio>
#include <cstring>
#include <algorithm>
extern "C" unsigned long long sceKernelGetProcessTime();
extern "C" void opennow_media_note(const char*);
static std::atomic_bool entropyFailed{false};
namespace opennow {
namespace {
void mediaSdp(const char* stage,const std::string& s){
 opennow_media_note(stage);
 std::size_t start=0;
 while(start<s.size()){
  auto end=s.find('\n',start);if(end==std::string::npos)end=s.size();
  auto line=s.substr(start,end-start);if(!line.empty()&&line.back()=='\r')line.pop_back();
  if(line.rfind("m=",0)==0||line.rfind("a=rtpmap:",0)==0||line=="a=recvonly"||line=="a=sendonly"||line=="a=inactive"||line=="a=sendrecv")opennow_media_note(line.c_str());
  start=end+1;
 }
}
const cJSON* get(const cJSON* j,const char* k){return cJSON_GetObjectItemCaseSensitive(j,k);}
const char* text(const cJSON* j,const char* k){auto* p=get(j,k);return cJSON_IsString(p)?p->valuestring:"";}
int number(const cJSON* j,const char* k){auto* p=get(j,k);return cJSON_IsNumber(p)?p->valueint:0;}
void le(std::vector<std::uint8_t>& b,std::uint64_t n,unsigned bytes){while(bytes--){b.push_back(n&255);n>>=8;}}
void be(std::vector<std::uint8_t>& b,std::uint64_t n,unsigned bytes){while(bytes)b.push_back((n>>(--bytes*8))&255);}
}
bool Stream::start(const Session& s,const char* device) {
 qos_={};nextQos_=0;qosRequested_=false;
 stop();opennow_media_note("START 00.002.026");entropyFailed=false;session_=s;settings_=settingsFor(s.profile);name_=std::string("opennow-")+device;peerId_=remoteId_=ack_=0;answerSent_=inputReady_=false;nextHeartbeat_=started_=lastInput_=0;inputAttempts_=0;inputOpened_=keyframeRequested_=0;candidates_.clear();lastVideoLoss_=0;
 capture_.arm("/data/opennow",settings_.codec==VideoCodec::hevc,sceKernelGetProcessTime());
 if(std::strncmp(s.signaling,"wss://",6)){std::snprintf(status_,sizeof(status_),"Invalid secure signaling endpoint");return false;}
 if(!media_.start(settings_)){std::snprintf(status_,sizeof(status_),"Could not initialize video decoder / audio output");return false;}
 if(peer_init()!=0){std::snprintf(status_,sizeof(status_),"WebRTC runtime initialization failed");media_.stop();return false;}runtimeReady_=true;
 PeerConfiguration config{};config.video_codec=settings_.codec==VideoCodec::hevc?CODEC_HEVC:CODEC_H264;config.audio_codec=CODEC_OPUS;config.datachannel=DATA_CHANNEL_STRING;config.user_data=this;config.onvideopacket=video;config.onaudiopacket=audio;
 config.ice_servers[0].urls="stun:s1.stun.gamestream.nvidia.com:19308";
 peer_connection_set_diagnostics_enabled(0);pc_=peer_connection_create(&config);
 if(!pc_||entropyFailed){std::snprintf(status_,sizeof(status_),"Unable to initialize WebRTC");media_.stop();return false;}
 PeerVideoRtpStats initial{};peer_connection_get_video_rtp_stats(pc_,&initial);
 if(!initial.assembler_ready){std::snprintf(status_,sizeof(status_),"Video assembler initialization failed");stop();return false;}
 peer_connection_onicecandidate(pc_,ice);peer_connection_oniceconnectionstatechange(pc_,state);peer_connection_ondatachannel(pc_,dataMessage,dataOpen,dataClose);
 std::string url=s.signaling;auto q=url.find('?');if(q!=std::string::npos)url.resize(q);while(url.size()&&url.back()=='/')url.pop_back();if(url.size()<7||url.substr(url.size()-7)!="sign_in")url+="/sign_in";
 url+="?peer_id="+name_+"&version=2&peer_role=1&pairing_id="+s.id;
 ws_=new WebSocketClient(url);ws_->set_custom_headers({"Origin: https://play.geforcenow.com",std::string("Sec-WebSocket-Protocol: x-nv-sessionid.")+s.id});ws_->set_on_message([this](const std::string& m){message(m);});
 if(!ws_->connect()){std::snprintf(status_,sizeof(status_),"Signaling: %s",ws_->get_last_error().c_str());stop();return false;}
 peerInfo();std::snprintf(status_,sizeof(status_),"Waiting for NVIDIA stream offer");return true;
}
void Stream::stop() {
 capture_.close();
 remoteInput_.release();
 if(pc_){PS5_PadData neutral{};input(neutral,lastInput_+20000);peer_connection_close(pc_);peer_connection_destroy(pc_);pc_=nullptr;}
 if(ws_){ws_->disconnect();delete ws_;ws_=nullptr;}
 if(runtimeReady_){peer_deinit();runtimeReady_=false;}
 remoteInput_=hid::RemoteInput{};virtualMode_=virtualKeyboard_=false;mouseX_=mouseY_=0;nextWheel_=nextCursorCapture_=0;
 media_.stop();secureErase(&session_,sizeof(session_));
}
void Stream::send(cJSON* root){char* value=cJSON_PrintUnformatted(root);if(value&&ws_)ws_->send_message(value);cJSON_free(value);}
void Stream::payload(cJSON* value){if(!ws_)return;char* data=cJSON_PrintUnformatted(value);if(!data)return;auto* root=cJSON_CreateObject();auto* msg=cJSON_AddObjectToObject(root,"peer_msg");cJSON_AddNumberToObject(msg,"from",peerId_);cJSON_AddNumberToObject(msg,"to",remoteId_);cJSON_AddStringToObject(msg,"msg",data);cJSON_AddNumberToObject(root,"ackid",++ack_);send(root);cJSON_Delete(root);cJSON_free(data);}
void Stream::peerInfo(){auto* root=cJSON_CreateObject();cJSON_AddNumberToObject(root,"ackid",++ack_);auto* info=cJSON_AddObjectToObject(root,"peer_info");cJSON_AddStringToObject(info,"browser","Chrome");cJSON_AddStringToObject(info,"browserVersion","131");cJSON_AddBoolToObject(info,"connected",true);cJSON_AddNumberToObject(info,"id",peerId_);cJSON_AddStringToObject(info,"name",name_.c_str());cJSON_AddNumberToObject(info,"peerRole",0);const auto resolution=std::to_string(settings_.width)+"x"+std::to_string(settings_.height);cJSON_AddStringToObject(info,"resolution",resolution.c_str());cJSON_AddNumberToObject(info,"version",2);send(root);cJSON_Delete(root);}
void Stream::message(const std::string& message) {
 if(message.size()>65536)return;auto* root=cJSON_ParseWithLength(message.c_str(),message.size());if(!root)return;
 auto* info=get(root,"peer_info");if(!std::strcmp(text(info,"name"),name_.c_str()))peerId_=number(info,"id");
 if(cJSON_IsNumber(get(root,"ackid"))&&(!info||number(info,"id")!=peerId_)){auto* a=cJSON_CreateObject();cJSON_AddNumberToObject(a,"ack",number(root,"ackid"));send(a);cJSON_Delete(a);}
 if(get(root,"hb")){auto* a=cJSON_CreateObject();cJSON_AddNumberToObject(a,"hb",1);send(a);cJSON_Delete(a);}
 auto* msg=get(root,"peer_msg");if(msg){remoteId_=number(msg,"from");auto* data=cJSON_Parse(text(msg,"msg"));if(data){
  if(!std::strcmp(text(data,"type"),"offer")){
   std::string offer=sdp::PrepareGfnOfferSdp(text(data,"sdp"),session_.signaling,session_.mediaIp,session_.mediaPort);
   if(offer.size()<60000&&!offer.empty()){
    mediaSdp("OFFER",offer);peer_connection_set_remote_description(pc_,offer.c_str(),SDP_TYPE_OFFER);
    const char* raw=peer_connection_create_answer(pc_);
    if(raw&&!entropyFailed){auto answer=sdp::AdaptAnswerSdpToOffer(raw,offer,settings_);if(answer.empty()){std::snprintf(status_,sizeof(status_),"Server did not offer the selected video codec");return;}mediaSdp("ANSWER",answer);auto nvst=webrtc::BuildNvstSdp(answer,settings_,sdp::ParseRiInputCapabilities(offer));auto* a=cJSON_CreateObject();cJSON_AddStringToObject(a,"type","answer");cJSON_AddStringToObject(a,"sdp",answer.c_str());cJSON_AddStringToObject(a,"nvstSdp",nvst.c_str());payload(a);cJSON_Delete(a);answerSent_=true;
     for(const auto& line:candidates_){auto* c=cJSON_CreateObject();cJSON_AddStringToObject(c,"candidate",line.c_str());cJSON_AddStringToObject(c,"sdpMid","0");cJSON_AddNumberToObject(c,"sdpMLineIndex",0);payload(c);cJSON_Delete(c);}candidates_.clear();
     if(session_.mediaPort>0&&session_.mediaPort!=443){auto manual=sdp::BuildManualMediaCandidate(session_.signaling,session_.mediaIp,session_.mediaPort,100);if(!manual.empty())peer_connection_add_ice_candidate(pc_,manual.data());}
     std::snprintf(status_,sizeof(status_),"Negotiating secure media connection");
    }
   }
  }else if(*text(data,"candidate")){std::string candidate=text(data,"candidate");if(candidate.rfind("a=",0))candidate="a="+candidate;if(candidate.size()<2048)peer_connection_add_ice_candidate(pc_,candidate.data());}
  cJSON_Delete(data);
 }}cJSON_Delete(root);
}
void Stream::ice(char* s,void* ctx){auto& self=*static_cast<Stream*>(ctx);std::string lines=s?s:"";std::size_t pos=0;while(pos<lines.size()){auto end=lines.find('\n',pos);if(end==std::string::npos)end=lines.size();auto line=lines.substr(pos,end-pos);if(line.size()&&line.back()=='\r')line.pop_back();if(line.rfind("a=candidate:",0)==0&&self.candidates_.size()<16)self.candidates_.push_back(line.substr(2));pos=end+1;}}
void Stream::state(PeerConnectionState s,void* ctx){auto& self=*static_cast<Stream*>(ctx);std::snprintf(self.status_,sizeof(self.status_),"WebRTC: %s",peer_connection_state_to_string(s));}
void Stream::recoverVideoLoss(){
 PeerVideoRtpStats stats{};peer_connection_get_video_rtp_stats(pc_,&stats);
 if(stats.access_units_dropped!=lastVideoLoss_){lastVideoLoss_=stats.access_units_dropped;media_.requireKeyframe();}
}
void Stream::video(const PeerVideoPacket* p,void* ctx){auto& self=*static_cast<Stream*>(ctx);self.recoverVideoLoss();if(p){self.qos_.received(p->size);const auto now=sceKernelGetProcessTime();self.capture_.receive(p->data,p->size,now,self.capture_.wantsIdr(now)&&video::Recovery::hasIdr(p->data,p->size,self.settings_.codec==VideoCodec::hevc));if(!self.media_.video(p->data,p->size)){if(now-self.keyframeRequested_>=250000){peer_connection_request_video_keyframe(self.pc_);self.keyframeRequested_=now;}}}}
void Stream::audio(const PeerAudioPacket* p,void* ctx){if(p)static_cast<Stream*>(ctx)->media_.audio(p->data,p->size,p->sequence,p->payload_type,p->timestamp);}
void Stream::dataMessage(char* data,std::size_t size,void* ctx,std::uint16_t sid){static_cast<Stream*>(ctx)->data(data,size,sid);}
void Stream::dataOpen(void* ctx){auto& self=*static_cast<Stream*>(ctx);if(peer_connection_create_datachannel_sid(self.pc_,DATA_CHANNEL_RELIABLE,0,0,const_cast<char*>("input_channel_v1"),const_cast<char*>(""),0)>=0&&!self.inputOpened_)self.inputOpened_=sceKernelGetProcessTime();}
void Stream::dataClose(void* ctx){static_cast<Stream*>(ctx)->inputReady_=false;}
void Stream::data(const char* data,std::size_t size,std::uint16_t sid){if(sid||size<2||size>64)return;auto* b=reinterpret_cast<const unsigned char*>(data);int word=b[0]|(b[1]<<8);if(word!=526&&b[0]!=14)return;protocol_=word==526?(size>=4?(b[2]|(b[3]<<8)):2):word;protocol_=std::max(2,protocol_);if(peer_connection_datachannel_send_binary_sid(pc_,const_cast<char*>(data),size,0)>=0)inputReady_=true;}
void Stream::tick(std::uint64_t now){if(!active())return;if(entropyFailed){std::snprintf(status_,sizeof(status_),"Secure entropy failed");stop();return;}if(!started_)started_=now;ws_->poll();
 if(!inputReady_&&inputOpened_&&now-inputOpened_>1500000){inputReady_=true;protocol_=2;}
 if(answerSent_&&!candidates_.empty()){for(const auto& line:candidates_){auto* c=cJSON_CreateObject();cJSON_AddStringToObject(c,"candidate",line.c_str());cJSON_AddStringToObject(c,"sdpMid","0");cJSON_AddNumberToObject(c,"sdpMLineIndex",0);payload(c);cJSON_Delete(c);}candidates_.clear();}
for(unsigned i=0;i<64;++i)if(!peer_connection_loop(pc_))break;
 recoverVideoLoss();
 capture_.poll("/data/opennow",settings_.codec==VideoCodec::hevc,now);
 // The existing input stream stays on SID 0. QoS uses the source-pinned,
 // unordered 300 ms NVST control stream on SID 6, after DCEP acknowledgment.
 if(inputReady_&&!qosRequested_){
  if(peer_connection_create_datachannel_sid(pc_,DATA_CHANNEL_PARTIAL_RELIABLE_TIMED_UNORDERED,0,300,const_cast<char*>("control_channel_partially_reliable"),const_cast<char*>(""),6)>=0){qosRequested_=true;opennow_media_note("NVST QoS control requested sid=6 lifetime=300");}
 }
 // Ask the server to composite the actual pointer into video; do not invent
 // a local position from relative deltas. Repeat on the timed control channel.
 if(virtualMode_&&peer_connection_datachannel_is_open(pc_,6)&&now>=nextCursorCapture_) {
  char cursor[]={8,3,1,0,1};
  peer_connection_datachannel_send_binary_sid(pc_,cursor,sizeof(cursor),6);
  nextCursorCapture_=now+1000000;
 }
 if(peer_connection_datachannel_is_open(pc_,6)&&now>=nextQos_){
  PeerVideoRtpStats stats{};peer_connection_get_video_rtp_stats(pc_,&stats);
  const auto sample=qos_.next(stats.latest_rtp_timestamp);auto bytes=qos_.packet(sample,now-started_>=1900000);
  if(peer_connection_datachannel_send_binary_sid(pc_,reinterpret_cast<char*>(bytes.data()),bytes.size(),6)>=0)qos_.queued(sample);
  nextQos_=now+55556;
 }
 if(now>=nextHeartbeat_){
  if(peer_connection_get_state(pc_)==PEER_CONNECTION_COMPLETED){
   PeerVideoRtpStats stats{};peer_connection_get_video_rtp_stats(pc_,&stats);
   char qosNote[120];std::snprintf(qosNote,sizeof(qosNote),"NVST QoS open=%d queued=%u newestTimestamp=%u",peer_connection_datachannel_is_open(pc_,6),qos_.queuedCount(),stats.latest_rtp_timestamp);opennow_media_note(qosNote);
   std::snprintf(qosNote,sizeof(qosNote),"CAPTURE bytes=%zu complete=%d",capture_.bytes(),capture_.done());if(capture_.bytes())opennow_media_note(qosNote);
   char diagnostic[384];std::snprintf(diagnostic,sizeof(diagnostic),"RTP total=%u decryptFail=%u unmatched=%u videoRouted=%u h264=%u decoded=%u error=%d",stats.transport_rtp,stats.decrypt_failures,stats.unmatched,stats.video_routed,stats.packets_received,media_.frames.load(),media_.decodeError.load());opennow_media_note(diagnostic);
   std::snprintf(diagnostic,sizeof(diagnostic),"PRESENTER frames=%u hardware=%d hdr=%d",media_.presented.load(),settings_.hardware,media_.actualHdr.load());opennow_media_note(diagnostic);
   std::snprintf(diagnostic,sizeof(diagnostic),"VIDEO quality actual=%dx%d bytes=%u idr=%u / AU lost=%u gaps=%u late=%u skips=%u nack=%u / QUEUE lost=%u corrupt=%u resets=%u",
    media_.decodedWidth.load(),media_.decodedHeight.load(),media_.videoBytes.load(),media_.idrFrames.load(),
    stats.access_units_dropped,stats.sequence_gaps,stats.late_packets_dropped,stats.forced_sequence_skips,stats.nack_requests,
    media_.queueDrops.load(),media_.corruptFrames.load(),media_.recoveryResets.load());opennow_media_note(diagnostic);
   std::snprintf(diagnostic,sizeof(diagnostic),"ASSEMBLER ready=%d lastResult=%d",stats.assembler_ready,stats.last_video_result);opennow_media_note(diagnostic);
   std::snprintf(diagnostic,sizeof(diagnostic),"AUDIO recovered=%u concealed=%u underruns=%u / TARGET %dx%d %d FPS %d kbps",
    media_.audioRecovered.load(),media_.audioConcealed.load(),media_.audioUnderruns.load(),settings_.width,settings_.height,settings_.fps,settings_.bitrate_kbps);opennow_media_note(diagnostic);
   std::string types="PT";for(unsigned pt=0;pt<128;++pt)if(stats.payload_counts[pt]){char item[32];std::snprintf(item,sizeof(item)," %u=%u",pt,stats.payload_counts[pt]);types+=item;}opennow_media_note(types.c_str());
   std::snprintf(status_,sizeof(status_),"VIDEO RTP %u AU %u LOST %u / DECODE %u ERR %d / AUDIO %u ERR %u",
    stats.packets_received,stats.access_units_completed,stats.access_units_dropped,media_.frames.load(),media_.decodeError.load(),media_.audioPackets.load(),media_.audioErrors.load());
   // Replace a small private snapshot, so late symptoms remain observable after
   // the bounded startup media log has filled. No session/network secrets.
   if(auto* live=std::fopen("/data/opennow/live-video.status","wb")){
    std::fprintf(live,"version=00.002.026 elapsed_us=%llu width=%d height=%d bytes=%u decoded=%u presented=%u error=%d hdr=%d lost=%u gaps=%u queue_lost=%u resets=%u qos_open=%d qos_queued=%u capture_bytes=%zu capture_done=%d\n",
     static_cast<unsigned long long>(now-started_),media_.decodedWidth.load(),media_.decodedHeight.load(),media_.videoBytes.load(),media_.frames.load(),media_.presented.load(),media_.decodeError.load(),media_.actualHdr.load(),stats.access_units_dropped,stats.sequence_gaps,media_.queueDrops.load(),media_.recoveryResets.load(),peer_connection_datachannel_is_open(pc_,6),qos_.queuedCount(),capture_.bytes(),capture_.done());
    std::fprintf(live,"au_received=%u queue_depth=%u queue_peak=%u queue_max_us=%llu decode_calls=%u decode_us=%llu decode_max_us=%llu gpu_calls=%u gpu_us=%llu gpu_max_us=%llu\n",
     stats.access_units_completed,media_.queueDepth.load(),media_.queuePeak.load(),static_cast<unsigned long long>(media_.queueMaxUs.load()),media_.decodeCalls.load(),static_cast<unsigned long long>(media_.decodeUs.load()),static_cast<unsigned long long>(media_.decodeMaxUs.load()),media_.gpuCalls.load(),static_cast<unsigned long long>(media_.gpuUs.load()),static_cast<unsigned long long>(media_.gpuMaxUs.load()));
    const auto t=media_.nativeTiming();
    std::fprintf(live,"target_fps=%d target_hdr=%d codec=%d native_copy_us=%llu native_publish_us=%llu native_decode_us=%llu native_flush_us=%llu native_decode_calls=%llu native_flush_calls=%llu native_blocked_attempts=%llu native_worker_mask=%llu native_pipeline_depth=%u native_inflight=%u\n",
     settings_.fps,settings_.hdr,static_cast<int>(settings_.codec),static_cast<unsigned long long>(t.copy_us),static_cast<unsigned long long>(t.publish_us),static_cast<unsigned long long>(t.decode_us),static_cast<unsigned long long>(t.flush_us),static_cast<unsigned long long>(t.decode_calls),static_cast<unsigned long long>(t.flush_calls),static_cast<unsigned long long>(t.blocked_attempts),static_cast<unsigned long long>(t.worker_mask),t.pipeline_depth,t.in_flight);
    std::fclose(live);
   }
   if(!media_.frames&&now-keyframeRequested_>=2000000){peer_connection_request_video_keyframe(pc_);keyframeRequested_=now;}
  }
  auto* hb=cJSON_CreateObject();cJSON_AddNumberToObject(hb,"hb",1);send(hb);cJSON_Delete(hb);nextHeartbeat_=now+2000000;
  if(inputReady_){char beat[4]={2,0,0,0};peer_connection_datachannel_send_binary_sid(pc_,beat,sizeof(beat),0);}
if(!answerSent_)peerInfo();
  if(!inputReady_&&peer_connection_get_state(pc_)==PEER_CONNECTION_COMPLETED&&inputAttempts_++<10)dataOpen(this);
 }
 if(!media_.frames&&now-started_>45000000){char last[192];std::snprintf(last,sizeof(last),"%s",status_);stop();std::snprintf(status_,sizeof(status_),"Video timeout: %.170s",last);}
}
bool Stream::sendInput(const std::vector<std::uint8_t>& bytes) {
 return pc_&&inputReady_&&peer_connection_datachannel_send_binary_sid(pc_,reinterpret_cast<char*>(const_cast<std::uint8_t*>(bytes.data())),bytes.size(),0)>=0;
}
void Stream::virtualMode(bool enabled,bool keyboard,unsigned speed,unsigned generation) {
 if(enabled!=virtualMode_||keyboard!=virtualKeyboard_||generation!=inputGeneration_) {
  remoteInput_.release();mouseX_=mouseY_=0;nextWheel_=0;
 }
 virtualMode_=enabled;virtualKeyboard_=keyboard;mouseSpeed_=std::min(speed,2u);inputGeneration_=generation;
}
void Stream::input(const PS5_PadData& source,std::uint64_t now){if(!inputReady_||!pc_||now-lastInput_<16000)return;
 const auto elapsed=lastInput_?std::min<std::uint64_t>(now-lastInput_,50000):16000;lastInput_=now;
 auto sendHid=[&](const auto& bytes){return sendInput(bytes);};
 if(!source.connected)remoteInput_.release();
 const bool flushed=remoteInput_.flush(protocol_,now,sendHid);
 if(virtualMode_&&source.connected&&flushed&&!virtualKeyboard_) {
  remoteInput_.mouse(1,(source.buttons&PS5_PAD_BUTTON_R2)||source.analogButtons.r2>32,protocol_,now,sendHid);
  remoteInput_.mouse(3,(source.buttons&PS5_PAD_BUTTON_L2)||source.analogButtons.l2>32,protocol_,now,sendHid);
  auto axis=[](unsigned v){const int n=int(v)-128;return n>-16&&n<16?0.f:float(n>0?n-16:n+16)/111.f;};
  const float speed=mouseSpeed_==0?250.f:mouseSpeed_==1?700.f:1400.f;
  mouseX_+=axis(source.rightStick.x)*speed*elapsed/1000000.f;mouseY_+=axis(source.rightStick.y)*speed*elapsed/1000000.f;
  const int x=int(mouseX_),y=int(mouseY_);
  if((x||y)&&sendInput(hid::motion(x,y,false,protocol_,now))){mouseX_-=x;mouseY_-=y;}
  // Bound accumulated movement if the channel is congested.
  mouseX_=std::max(-100.f,std::min(100.f,mouseX_));mouseY_=std::max(-100.f,std::min(100.f,mouseY_));
  if(now>=nextWheel_) {
   const int vertical=((source.buttons&PS5_PAD_BUTTON_UP)?120:0)-((source.buttons&PS5_PAD_BUTTON_DOWN)?120:0);
   const int horizontal=((source.buttons&PS5_PAD_BUTTON_RIGHT)?120:0)-((source.buttons&PS5_PAD_BUTTON_LEFT)?120:0);
   if((vertical||horizontal)&&sendInput(hid::motion(horizontal,vertical,true,protocol_,now)))nextWheel_=now+150000;
  }
 }
 PS5_PadData pad=source;
 if(virtualMode_||!flushed){pad={};}
 // Local stream-exit gesture is never forwarded as a gamepad action.
 pad.buttons&=~PS5_PAD_BUTTON_TOUCH_PAD;
 if(source.buttons&PS5_PAD_BUTTON_TOUCH_PAD)pad.buttons&=~PS5_PAD_BUTTON_OPTIONS;
 std::uint16_t buttons=0;const unsigned ps[]={PS5_PAD_BUTTON_UP,PS5_PAD_BUTTON_DOWN,PS5_PAD_BUTTON_LEFT,PS5_PAD_BUTTON_RIGHT,PS5_PAD_BUTTON_OPTIONS,PS5_PAD_BUTTON_TOUCH_PAD,PS5_PAD_BUTTON_L3,PS5_PAD_BUTTON_R3,PS5_PAD_BUTTON_L1,PS5_PAD_BUTTON_R1,PS5_PAD_BUTTON_CROSS,PS5_PAD_BUTTON_CIRCLE,PS5_PAD_BUTTON_SQUARE,PS5_PAD_BUTTON_TRIANGLE};const unsigned xb[]={1,2,4,8,16,32,64,128,256,512,4096,8192,16384,32768};for(unsigned i=0;i<14;++i)if(pad.connected&&(pad.buttons&ps[i]))buttons|=xb[i];
 auto axis=[&](unsigned v,bool flip){if(!pad.connected)return 0;int n=(static_cast<int>(v)-128)*256;if(n>-3000&&n<3000)n=0;return flip?-std::max(-32767,n):n;};
 std::vector<std::uint8_t> data;le(data,12,4);le(data,26,2);le(data,0,2);le(data,1,2);le(data,20,2);le(data,buttons,2);le(data,pad.connected?(pad.analogButtons.l2|(pad.analogButtons.r2<<8)):0,2);le(data,axis(pad.leftStick.x,false),2);le(data,axis(pad.leftStick.y,true),2);le(data,axis(pad.rightStick.x,false),2);le(data,axis(pad.rightStick.y,true),2);le(data,0,2);le(data,85,2);le(data,0,2);le(data,now,8);
 if(protocol_>2){std::vector<std::uint8_t> wire{0x23};be(wire,now,8);wire.push_back(0x21);be(wire,data.size(),2);wire.insert(wire.end(),data.begin(),data.end());data=std::move(wire);}
 peer_connection_datachannel_send_binary_sid(pc_,reinterpret_cast<char*>(data.data()),data.size(),0);
}
}
extern "C" int mbedtls_hardware_poll(void*,unsigned char* out,std::size_t size,std::size_t* used){*used=0;if(!opennow::randomBytes(out,size)){entropyFailed=true;return -1;}*used=size;return 0;}

extern "C" int opennow_peer_random(unsigned char* out,std::size_t size){if(opennow::randomBytes(out,size))return 0;entropyFailed=true;return -1;}
extern "C" void arc4random_buf(void* out,std::size_t size){opennow_peer_random(static_cast<unsigned char*>(out),size);}
