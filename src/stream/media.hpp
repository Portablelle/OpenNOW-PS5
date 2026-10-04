#pragma once
#include "../demo_renderer.hpp"
#include "video_recovery.hpp"
#include "compressed_queue.hpp"
#include "stream_settings.hpp"
#include "audio_format.hpp"
#include "audio_playout.hpp"
#include "native/hardware_decoder.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <pthread.h>
struct AVCodecContext;struct AVFrame;struct AVPacket;struct SwsContext;struct OpusMSDecoder;
namespace opennow {
class Media {
public:
 bool start(const StreamSettings& = {}) noexcept;
 static unsigned availableAudioChannels() noexcept;
 bool configureAudio(const audio::Format&) noexcept;
 unsigned audioCapacity() const noexcept {return outputChannels_;}
 void stop() noexcept;
 void requireKeyframe() noexcept;
 bool video(const std::uint8_t*,std::size_t) noexcept;
 void audio(const std::uint8_t*,std::size_t,std::uint16_t,std::uint8_t,std::uint32_t) noexcept;
 bool draw(ps5::demo::Canvas&) noexcept;
 std::atomic_uint frames{0},dropped{0},videoUnits{0},audioPackets{0},audioErrors{0},audioRecovered{0},audioConcealed{0},audioUnderruns{0},queueDrops{0},corruptFrames{0},recoveryResets{0},idrFrames{0};
 std::atomic_uint presented{0};
 std::atomic_bool actualHdr{false};
 std::atomic_int decodedWidth{0},decodedHeight{0};
 std::atomic_uint videoBytes{0};
 std::atomic_uint audioChannels{0},audioBytes{0},audioSamples{0},audioQueueFrames{0},audioQueuePeak{0},audioDroppedFrames{0},audioOutputErrors{0},audioFecAttempts{0};
 std::atomic_int decodeError{0};
 std::atomic_uint queueDepth{0},queuePeak{0},decodeCalls{0},gpuCalls{0};
 std::atomic<std::uint64_t> decodeUs{0},decodeMaxUs{0},queueMaxUs{0},gpuUs{0},gpuMaxUs{0};
 video::HardwareDecoder::Timing nativeTiming() const noexcept {
#ifdef OPENNOW_GPU
  return nativeDecoder_.timing();
#else
  return {};
#endif
 }
 ~Media(){stop();}
private:
 void clearNativePending() noexcept;
 static void* decode(void*);static void* output(void*);
 static constexpr unsigned cap=8*1024*1024;
 video::CompressedQueue compressed_;
 std::uint32_t* pixels_=nullptr;bool fresh_=false;video::Recovery recovery_;
 audio::PlayoutQueue audioQueue_;unsigned outputChannels_=2;
 audio::Format audioFormat_{};
 unsigned stereoProbePackets_=0;
 pthread_mutex_t lock_=PTHREAD_MUTEX_INITIALIZER;
 pthread_cond_t wake_=PTHREAD_COND_INITIALIZER;
 std::atomic_bool running_{false};void* videoThread_=nullptr;void* audioThread_=nullptr;
 StreamSettings settings_{};
#ifdef OPENNOW_GPU
 video::NativeMode nativeMode_{};video::HardwareDecoder nativeDecoder_;
 video::HardwareDecoder::Picture pendingPicture_{};bool nativePending_=false,nativeInFlight_=false;
#endif
 AVCodecContext* codec_=nullptr;SwsContext* scaler_=nullptr;OpusMSDecoder* opus_=nullptr;
 int audioHandle_=-1;std::uint16_t lastSequence_=0;bool sequenceSeen_=false;std::uint32_t expectedAudioTimestamp_=0;
};
}
