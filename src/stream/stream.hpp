#pragma once
#include "../cloud.hpp"
extern "C" {
#include "../platform/ps5-pad.h"
}
#include "media.hpp"
#include "WebSocketClient.hpp"
#include "sdp.hpp"
#include "nvst_qos.hpp"
#include "video_capture.hpp"
#include "remote_input.hpp"
extern "C" {
#include "peer_connection.h"
}
struct cJSON;
namespace opennow {
class Stream {
public:
 explicit Stream(Media& m):media_(m){}
 ~Stream(){stop();}
 bool start(const Session&,const char* device);
 void tick(std::uint64_t now);
 void input(const PS5_PadData&,std::uint64_t now);
 void stop();
 void virtualMode(bool enabled,bool keyboard,unsigned speed,unsigned generation);
 bool keyStroke(hid::Stroke s){return inputReady_&&virtualMode_&&remoteInput_.tap(s);}
 bool active() const {return pc_&&ws_&&ws_->is_connected();}
 const char* status() const {return status_;}
private:
 void message(const std::string&);void payload(cJSON*);void send(cJSON*);
 void recoverVideoLoss();
 void peerInfo();void data(const char*,std::size_t,std::uint16_t);
 static void ice(char*,void*);static void state(PeerConnectionState,void*);
 static void video(const PeerVideoPacket*,void*);static void audio(const PeerAudioPacket*,void*);
 static void dataMessage(char*,std::size_t,void*,std::uint16_t);
 static void dataOpen(void*);static void dataClose(void*);
 bool sendInput(const std::vector<std::uint8_t>&);
 hid::RemoteInput remoteInput_;
 bool virtualMode_=false,virtualKeyboard_=false;unsigned mouseSpeed_=1,inputGeneration_=0;
 std::uint64_t nextWheel_=0,nextCursorCapture_=0;float mouseX_=0,mouseY_=0;
 Media& media_;PeerConnection* pc_=nullptr;WebSocketClient* ws_=nullptr;
 StreamSettings settings_{};Session session_{};char status_[192]{};std::string name_;int peerId_=0,remoteId_=0,ack_=0,protocol_=2;
 bool runtimeReady_=false,answerSent_=false,inputReady_=false;std::vector<std::string> candidates_;
 std::uint64_t nextHeartbeat_=0,started_=0,lastInput_=0;unsigned inputAttempts_=0;
 unsigned lastVideoLoss_=0;
 std::uint64_t inputOpened_=0,keyframeRequested_=0;
 webrtc::QosFeedback qos_;
 std::uint64_t nextQos_=0;
 bool qosRequested_=false;
 video::ShortCapture capture_;
};
}
