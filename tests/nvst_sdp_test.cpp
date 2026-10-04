#include "stream/nvst_sdp.hpp"
#include "stream/sdp.hpp"

#include <cassert>
#include <string>

namespace
{

bool HasLine(const std::string& sdp, const std::string& line)
{
    return sdp.find(line + "\n") != std::string::npos;
}

bool HasAttribute(const std::string& sdp, const std::string& attribute)
{
    return sdp.find("a=" + attribute) != std::string::npos;
}

} // namespace

int main()
{
    opennow::StreamSettings settings;
    settings.width = 1280;
    settings.height = 720;
    settings.fps = 60;
    settings.bitrate_kbps = 12000;
    settings.image_quality_mode = "Adaptive";

    const std::string answer =
        "v=0\r\n"
        "a=ice-ufrag:test-ufrag\r\n"
        "a=ice-pwd:test-password\r\n"
        "a=fingerprint:sha-256 AA:BB:CC\r\n";
    const std::string sdp = opennow::webrtc::BuildNvstSdp(
        answer, settings, opennow::webrtc::RiInputCapabilities {});

    for (const auto& line : {
        "a=video.maxFPS:60",
        "a=video.initialBitrateKbps:4000",
        "a=video.initialPeakBitrateKbps:4000",
        "a=vqos.bw.maximumBitrateKbps:12000",
        "a=vqos.bw.minimumBitrateKbps:4000",
        "a=vqos.dynamicStreamingMode:3",
        "a=vqos.drc.enable:1",
        "a=vqos.resControl.cpmRtc.featureMask:3",
    }) {
        assert(HasLine(sdp, line));
    }

    for (const auto& attribute : {
        "vqos.bw.peakBitrateKbps:",
        "vqos.bw.serverPeakBitrateKbps:",
        "vqos.bw.enableBandwidthEstimation:",
        "vqos.bw.disableBitrateLimit:",
        "vqos.grc.maximumBitrateKbps:",
        "vqos.grc.enable:",
        "vqos.dfc.enable:",
        "vqos.dfc.adjustResAndFps:",
        "vqos.resControl.cpmRtc.enable:",
        "vqos.resControl.cpmRtc.minResolutionPercent:",
        "vqos.resControl.cpmRtc.resolutionChangeHoldonMs:",
    }) {
        assert(!HasAttribute(sdp, attribute));
    }

    settings.bitrate_kbps = 20000;
    const std::string quality_sdp = opennow::webrtc::BuildNvstSdp(
        answer, settings, opennow::webrtc::RiInputCapabilities {});
    assert(HasLine(quality_sdp, "a=video.initialBitrateKbps:5000"));
    assert(HasLine(quality_sdp, "a=vqos.bw.maximumBitrateKbps:20000"));
    for(auto profile:{opennow::StreamProfile::quality,opennow::StreamProfile::smooth,
                     opennow::StreamProfile::experimental,opennow::StreamProfile::compatibility}){
        const auto target=opennow::settingsFor(profile);
        auto negotiated=opennow::webrtc::BuildNvstSdp(answer,target,{});
        assert(HasLine(negotiated,"a=video.clientViewportWd:"+std::to_string(target.width)));
        assert(HasLine(negotiated,"a=video.clientViewportHt:"+std::to_string(target.height)));
        assert(HasLine(negotiated,"a=video.maxFPS:"+std::to_string(target.fps)));
        assert(HasLine(negotiated,"a=vqos.bw.maximumBitrateKbps:"+std::to_string(target.bitrate_kbps)));
        assert(HasLine(negotiated,"a=video.dynamicRangeMode:0"));
        assert(HasLine(negotiated,"a=video.bitDepth:8"));
        assert(HasLine(negotiated,"a=video.maxNumReferenceFrames:4"));
        const std::string offer="v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 98\r\na=rtpmap:98 H264/90000\r\n"
            "m=audio 9 UDP/TLS/RTP/SAVPF 111 63\r\na=rtpmap:111 opus/48000/2\r\na=rtpmap:63 red/48000/2\r\na=fmtp:63 111/111\r\n";
        const std::string raw="v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 96\r\nc=IN IP4 0.0.0.0\r\na=rtpmap:96 H264/90000\r\n"
            "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\na=rtpmap:111 opus/48000/2\r\na=fmtp:111 stereo=0\r\n";
        const auto adapted=opennow::sdp::AdaptAnswerSdpToOffer(raw,offer,target);
        assert(HasLine(adapted,"b=AS:"+std::to_string(target.bitrate_kbps)+"\r"));
        assert(adapted.find("stereo=1;sprop-stereo=1;maxaveragebitrate=256000")!=std::string::npos);
        assert(adapted.find("stereo=0")==std::string::npos);
        assert(adapted.find("a=rtpmap:63 red/48000/2")!=std::string::npos);
    }
    const auto hdr=opennow::settingsFor(opennow::StreamProfile::native_hdr120);
    for(auto profile:{opennow::StreamProfile::native_hdr120,opennow::StreamProfile::native_hdr60,
                     opennow::StreamProfile::native_4k120,opennow::StreamProfile::native_1080,
                     opennow::StreamProfile::native_hdr90,opennow::StreamProfile::native_4k90}){
        const auto target=opennow::settingsFor(profile);
        const auto native=opennow::webrtc::BuildNvstSdp(answer,target,{});
        assert(HasLine(native,"a=vqos.bw.minimumBitrateKbps:"+std::to_string(target.bitrate_kbps*3/4)));
        assert(HasLine(native,"a=video.initialBitrateKbps:"+std::to_string(target.bitrate_kbps*3/4)));
        assert(HasLine(native,"a=video.maxFPS:"+std::to_string(target.fps)));
        assert(HasLine(native,"a=video.maxNumReferenceFrames:1"));
        assert(HasLine(native,"a=video.dynamicRangeMode:"+std::to_string(target.hdr?1:0)));
        assert(HasLine(native,"a=video.bitStreamFormat:"+std::to_string(target.codec==opennow::VideoCodec::hevc?1:0)));
        assert(HasLine(native,"a=vqos.dynamicStreamingMode:0"));
        assert(HasLine(native,"a=vqos.drc.enable:0"));
        assert(HasLine(native,"a=vqos.resControl.cpmRtc.minResolutionPercent:100"));
        assert(native.find("a=vqos.drc.enable:1")==std::string::npos);
        assert(native.find("a=vqos.bw.serverPeakBitrateKbps:")<native.find("m=audio"));
    }
    // Every real profile is reachable once through the user-visible L1 cycle.
    bool visited[static_cast<unsigned>(opennow::StreamProfile::count)]{};
    auto selected=opennow::StreamProfile::native_hdr120;
    for(unsigned i=0;i<static_cast<unsigned>(opennow::StreamProfile::count);++i){
        const auto index=static_cast<unsigned>(selected);assert(index<static_cast<unsigned>(opennow::StreamProfile::count)&&!visited[index]);visited[index]=true;
        selected=opennow::nextProfile(selected);
    }
    assert(selected==opennow::StreamProfile::native_hdr120);
    const std::string audioOffer="m=audio 9 UDP/TLS/RTP/SAVPF 111\r\na=rtpmap:111 opus/48000/2\r\n";
    const std::string hevcOffer="v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 98 100\r\na=rtpmap:98 H264/90000\r\na=rtpmap:100 H265/90000\r\na=fmtp:100 profile-id=2;level-id=156;sprop-max-don-diff=0\r\n";
    const std::string rawHevc="v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 96\r\na=rtpmap:96 H265/90000\r\na=fmtp:96 profile-id=2;level-id=156;sprop-max-don-diff=0\r\n";
    const auto hdrAnswer=opennow::sdp::AdaptAnswerSdpToOffer(rawHevc+audioOffer,hevcOffer+audioOffer,hdr);
    assert(hdrAnswer.find("a=rtpmap:100 H265/90000")!=std::string::npos);
    assert(hdrAnswer.find("profile-id=2;level-id=156")!=std::string::npos);
    const auto hdrSdp=opennow::webrtc::BuildNvstSdp(hdrAnswer,hdr,{});
    for(const auto* a:{"a=video.dynamicRangeMode:1","a=video.bitDepth:10","a=video.chromaFormat:1","a=video.bitStreamFormat:1","a=video.maxFPS:120"})assert(HasLine(hdrSdp,a));
    assert(opennow::sdp::AdaptAnswerSdpToOffer(rawHevc,"v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 98\r\na=rtpmap:98 H264/90000\r\n",hdr).empty());
    auto interleaved=hevcOffer;interleaved.replace(interleaved.find("sprop-max-don-diff=0"),20,"sprop-max-don-diff=1");
    assert(opennow::sdp::AdaptAnswerSdpToOffer(rawHevc,interleaved,hdr).empty());
    return 0;
}
