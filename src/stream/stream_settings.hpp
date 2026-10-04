#pragma once
#include <string>
namespace opennow {
// Streaming targets. Native profiles are exposed only after console startup
// qualification; sustained frame rate and received codec are measured separately.
enum class StreamProfile { quality, smooth, experimental, compatibility,
 native_hdr120, native_hdr60, native_4k120, native_1080,
 native_hdr90, native_4k90, count };
enum class VideoCodec { h264, hevc };
struct StreamSettings {
 int width=1920,height=1080,fps=30,bitrate_kbps=25000;
 std::string image_quality_mode="Original";
 VideoCodec codec=VideoCodec::h264;
 bool hardware=false,hdr=false;
 unsigned audio_channels=2;
};
inline StreamSettings settingsFor(StreamProfile profile) {
 switch(profile) {
 case StreamProfile::native_hdr120: return {3840,2160,120,100000,"Original",VideoCodec::hevc,true,true};
 case StreamProfile::native_hdr90: return {3840,2160,90,100000,"Original",VideoCodec::hevc,true,true};
 case StreamProfile::native_hdr60: return {3840,2160,60,100000,"Original",VideoCodec::hevc,true,true};
 case StreamProfile::native_4k120: return {3840,2160,120,100000,"Original",VideoCodec::h264,true,false};
 case StreamProfile::native_4k90: return {3840,2160,90,100000,"Original",VideoCodec::h264,true,false};
 case StreamProfile::native_1080: return {1920,1080,60,75000,"Original",VideoCodec::h264,true,false};
 case StreamProfile::smooth: return {1280,720,60,20000,"Original"};
 case StreamProfile::experimental: return {1920,1080,60,75000,"Original"};
 case StreamProfile::compatibility: return {1280,720,30,10000,"Original"};
 default: return {};
 }
}
inline StreamProfile nextProfile(StreamProfile profile) {
 switch(profile) {
 case StreamProfile::native_hdr120: return StreamProfile::native_hdr90;
 case StreamProfile::native_hdr90: return StreamProfile::native_hdr60;
 case StreamProfile::native_hdr60: return StreamProfile::native_4k120;
 case StreamProfile::native_4k120: return StreamProfile::native_4k90;
 case StreamProfile::native_4k90: return StreamProfile::native_1080;
 case StreamProfile::native_1080: return StreamProfile::quality;
 case StreamProfile::quality: return StreamProfile::smooth;
 case StreamProfile::smooth: return StreamProfile::experimental;
 case StreamProfile::experimental: return StreamProfile::compatibility;
 default: return StreamProfile::native_hdr120;
 }
}
inline const char* profileLabel(StreamProfile profile) {
 switch(profile) {
 case StreamProfile::native_hdr120: return "4K120 HDR / HEVC / 100 MBPS";
 case StreamProfile::native_hdr90: return "4K90 HDR / HEVC / 100 MBPS";
 case StreamProfile::native_hdr60: return "4K60 HDR / HEVC / 100 MBPS";
 case StreamProfile::native_4k120: return "4K120 SDR / HARDWARE / 100 MBPS";
 case StreamProfile::native_4k90: return "4K90 SDR / HARDWARE / 100 MBPS";
 case StreamProfile::native_1080: return "1080P60 SDR / HARDWARE / 75 MBPS";
 case StreamProfile::smooth: return "SMOOTH 720P60 SDR / 20 MBPS";
 case StreamProfile::experimental: return "EXPERIMENTAL 1080P60 SDR / 75 MBPS";
 case StreamProfile::compatibility: return "COMPATIBILITY 720P30 SDR / 10 MBPS";
 default: return "QUALITY 1080P30 SDR / 25 MBPS";
 }
}
}
