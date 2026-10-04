// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "audio_format.hpp"
#include <algorithm>

namespace opennow::audio {
// 48 kHz frames, independent of channel count. Start with 20 ms of jitter
// protection; repeated starvation raises this to at most 40 ms. Bound excess
// latency to twice that target (or one complete, valid 120 ms Opus packet).
class PlayoutQueue {
public:
    static constexpr unsigned capacity=5760,grain=256;
    void open(unsigned channels) {
        outputChannels_=channels;read_=used_=peak_=dropped_=underruns_=0;
        target_=960;packetFrames_=0;primed_=false;
    }
    void clear() {read_=used_=packetFrames_=0;primed_=false;}
    void push(const std::int16_t* pcm,unsigned frames,unsigned channels) {
        if(!pcm||!frames||frames>capacity||
           (channels!=2&&channels!=6&&channels!=8)||channels>outputChannels_)return;
        packetFrames_=frames;
        const unsigned limit=std::min(capacity,std::max(target_*2,frames));
        if(used_+frames>limit) {
            const unsigned excess=used_+frames-limit;
            read_=(read_+excess)%capacity;used_-=excess;dropped_+=excess;
        }
        for(unsigned i=0;i<frames;++i)
            outputFrame(pcm+i*channels,channels,
                pcm_.data()+((read_+used_+i)%capacity)*outputChannels_,outputChannels_);
        used_+=frames;peak_=std::max(peak_,used_);
    }
    void pop(std::int16_t* block) {
        std::fill(block,block+grain*outputChannels_,0);
        // Reserve the target in addition to one packet, otherwise 20 ms packets
        // immediately exhaust a 20 ms prebuffer at a 256-frame grain boundary.
        if(!primed_&&used_>=std::min(std::min(capacity,std::max(target_*2,packetFrames_)),target_+packetFrames_))primed_=true;
        if(!primed_)return;
        const unsigned count=std::min(used_,grain);
        for(unsigned i=0;i<count;++i)
            std::copy_n(pcm_.data()+((read_+i)%capacity)*outputChannels_,outputChannels_,block+i*outputChannels_);
        read_=(read_+count)%capacity;used_-=count;
        if(count<grain) {primed_=false;++underruns_;target_=std::min(1920u,target_+480);}
    }
    unsigned frames() const {return used_;}
    unsigned peak() const {return peak_;}
    unsigned dropped() const {return dropped_;}
    unsigned underruns() const {return underruns_;}
    unsigned target() const {return target_;}
private:
    std::array<std::int16_t,capacity*8> pcm_{};
    unsigned outputChannels_=2,read_=0,used_=0,peak_=0,dropped_=0,underruns_=0,target_=960,packetFrames_=0;
    bool primed_=false;
};
} // namespace opennow::audio
