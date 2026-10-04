// SPDX-License-Identifier: GPL-3.0-or-later
#include "stream/audio_playout.hpp"
#include "stream/sdp.hpp"
#include <cassert>
#include <vector>

using namespace opennow;
int main() {
    const std::string stereo="m=audio 9 UDP/TLS/RTP/SAVPF 111 63 112 113\r\n"
        "a=rtpmap:111 opus/48000/2\r\na=rtpmap:63 red/48000/2\r\na=fmtp:63 111/111\r\n";
    const std::string five="a=rtpmap:112 multiopus/48000/6\r\n"
        "a=fmtp:112 num_streams=4;coupled_streams=2;channel_mapping=0,4,1,2,3,5\r\n";
    const std::string seven="a=rtpmap:113 multiopus/48000/8\r\n"
        "a=fmtp:113 num_streams=5;coupled_streams=3;channel_mapping=0,6,1,2,3,4,5,7\r\n";
    const auto offer=stereo+five+seven;
    assert(audio::selectFormat(offer,8).channels==8);
    assert(audio::selectFormat(offer,6).channels==6);
    const auto fallback=audio::selectFormat(offer,2);
    assert(fallback.channels==2&&fallback.redPayload==63);
    assert(audio::selectFormat(stereo,8).channels==2);
    assert(audio::selectFormat(stereo+"m=video 9 RTP/AVP 112\r\n"+five,8).channels==2);
    for(const char* bad:{"0,4,1,2,3,3","0,4,1,2,3,6","0,4,1,2,3","0,4,1,2,3,5,","0,4,1,2,3,5junk"}) {
        const auto invalid=stereo+"a=rtpmap:112 multiopus/48000/6\r\n"
            "a=fmtp:112 num_streams=4;coupled_streams=2;channel_mapping="+bad+"\r\n";
        assert(audio::selectFormat(invalid,8).channels==2);
    }
    assert(!audio::selectFormat("m=audio 9 RTP/AVP 111\r\na=rtpmap:112 multiopus/48000/6\r\n"+five,8).channels);
    const auto incompatibleRed=audio::selectFormat(stereo+five+"a=rtpmap:64 red/48000/6\r\na=fmtp:64 112/111\r\n",6);
    assert(incompatibleRed.redPayload==-1);
    auto settings=settingsFor(StreamProfile::quality);settings.audio_channels=8;
    const std::string video="m=video 9 UDP/TLS/RTP/SAVPF 96\r\na=rtpmap:96 H264/90000\r\n";
    const auto answer=sdp::AdaptAnswerSdpToOffer(video+stereo,video+offer,settings);
    assert(answer.find("m=audio 9 UDP/TLS/RTP/SAVPF 113\r\n")!=std::string::npos);
    assert(answer.find("maxaveragebitrate=510000")!=std::string::npos);
    assert(answer.find("a=rtpmap:111")==std::string::npos);
    assert(answer.find("a=rtpmap:63")==std::string::npos);
    for(auto mode:{audio::Mode::automatic,audio::Mode::stereo,audio::Mode::surround51,audio::Mode::surround71})
        assert(audio::requestedChannels(mode,2)==2);
    assert(audio::requestedChannels(audio::Mode::automatic,8)==8);
    assert(audio::requestedChannels(audio::Mode::surround51,8)==6);
    assert(audio::requestedChannels(audio::Mode::surround71,6)==6);

    const std::int16_t input[]={100,200,300,400,500,600,700,800};
    std::int16_t mapped[8];audio::outputFrame(input,6,mapped,8);
    const std::int16_t expected51[]={100,300,200,600,400,500,0,0};
    assert(std::equal(mapped,mapped+8,expected51));
    audio::outputFrame(input,8,mapped,8);
    const std::int16_t expected71[]={100,300,200,800,600,700,400,500};
    assert(std::equal(mapped,mapped+8,expected71));
    audio::outputFrame(input,2,mapped,8);
    assert(mapped[0]==100&&mapped[1]==200&&mapped[2]==0&&mapped[7]==0);

    audio::PlayoutQueue queue;queue.open(8);
    std::vector<std::int16_t> pcm(5760*8,1234);
    std::int16_t block[256*8];
    queue.push(pcm.data(),480,8);queue.pop(block);
    assert(queue.frames()==480&&block[0]==0&&queue.underruns()==0);
    queue.push(pcm.data(),480,8);queue.pop(block);
    assert(queue.frames()==960&&block[0]==0);
    queue.push(pcm.data(),480,8);queue.pop(block);
    assert(queue.frames()==1184&&block[0]==1234&&block[2047]==1234);
    for(int i=0;i<5;++i)queue.pop(block);
    assert(queue.frames()==0&&queue.underruns()==1&&queue.target()==1440);
    assert(block[160*8]==0); // partial final block has a silent tail
    queue.clear();queue.pop(block);assert(block[0]==0);
    queue.open(2);
    // Overflow keeps the newest continuous samples and bounds queue latency.
    queue.push(pcm.data(),1920,2);
    std::fill(pcm.begin(),pcm.end(),4321);queue.push(pcm.data(),960,2);
    assert(queue.frames()==1920&&queue.dropped()==960);
    for(int i=0;i<4;++i)queue.pop(block);
    assert(block[0]==1234&&block[192*2]==4321);
    queue.push(pcm.data(),5760,2);
    assert(queue.frames()==5760&&queue.peak()==5760);
    queue.push(pcm.data(),5761,2);assert(queue.frames()==5760);
    while(queue.frames())queue.pop(block);
    queue.pop(block);
    queue.push(pcm.data(),960,2);queue.push(pcm.data(),960,2);queue.push(pcm.data(),960,2);
    queue.pop(block);assert(block[0]==4321); // a previous 120 ms packet must not prevent re-priming
    // Regular 20 ms packets and 256-frame output grains must not create periodic starvation.
    queue.open(2);queue.push(pcm.data(),960,2);queue.push(pcm.data(),960,2);
    unsigned elapsedFrames=0,nextPacket=960;
    for(int i=0;i<1000;++i){
        while(elapsedFrames>=nextPacket){queue.push(pcm.data(),960,2);nextPacket+=960;}
        queue.pop(block);assert(block[0]==4321&&block[511]==4321);
        elapsedFrames+=256;
    }
    assert(queue.underruns()==0);

}
