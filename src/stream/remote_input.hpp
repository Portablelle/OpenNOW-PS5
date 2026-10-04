// SPDX-License-Identifier: GPL-3.0-or-later
// Native HID layouts cross-checked against OpenNOW nvst_input.rs at
// bee18c118dbc89f42319436dcdb172d5b9e15e0c. Uses the existing input_channel_v1.
#pragma once
#include <array>
#include <cstdint>
#include <vector>
namespace opennow::hid {
inline void put(std::vector<std::uint8_t>& p,std::uint64_t v,unsigned n,bool big=false) {
    for(unsigned i=0;i<n;++i)p.push_back((v>>(8*(big?n-1-i:i)))&255);
}
inline std::vector<std::uint8_t> wrap(std::vector<std::uint8_t> p,int protocol,std::uint64_t now) {
    if(protocol<=2)return p;
    std::vector<std::uint8_t> wire{0x23};put(wire,now,8,true);wire.push_back(0x22);
    wire.insert(wire.end(),p.begin(),p.end());return wire;
}
inline auto key(unsigned vk,unsigned modifiers,bool down,int protocol,std::uint64_t now) {
    std::vector<std::uint8_t> p;put(p,down?3:4,4);put(p,vk,2,true);put(p,modifiers,2,true);
    put(p,0,2);put(p,now,8,true);return wrap(std::move(p),protocol,now);
}
inline auto button(unsigned id,bool down,int protocol,std::uint64_t now) {
    std::vector<std::uint8_t> p;put(p,down?8:9,4);put(p,id,1);put(p,0,5);
    put(p,now,8,true);return wrap(std::move(p),protocol,now);
}
inline auto motion(int x,int y,bool wheel,int protocol,std::uint64_t now) {
    std::vector<std::uint8_t> p;put(p,wheel?10:7,4);put(p,std::uint16_t(x),2,true);
    put(p,std::uint16_t(y),2,true);put(p,0,6);put(p,now,8,true);
    return wrap(std::move(p),protocol,now);
}
struct Stroke {std::uint8_t vk=0,modifiers=0;};
// Only successfully queued downs count as held. Failed ups are retried, and
// cancellation discards unsent text before releasing everything already held.
class RemoteInput {
    struct Event {Stroke stroke;bool down=false;};
    std::array<Event,256> queue_{};
    std::array<bool,256> held_{};
    std::array<bool,4> mouse_{};
    unsigned head_=0,size_=0;
    bool releasing_=false;
public:
    bool tap(Stroke s) {
        if(releasing_||size_+8>queue_.size()||!s.vk)return false;
        auto add=[&](unsigned vk,unsigned mods,bool down){queue_[(head_+size_++)%queue_.size()]={{static_cast<std::uint8_t>(vk),static_cast<std::uint8_t>(mods)},down};};
        constexpr unsigned mods[]={0x10,0x11,0x12};
        for(unsigned i=0;i<3;++i)if(s.modifiers&(1<<i))add(mods[i],0,true);
        add(s.vk,s.modifiers,true);add(s.vk,s.modifiers,false);
        for(int i=2;i>=0;--i)if(s.modifiers&(1<<i))add(mods[i],0,false);
        return true;
    }
    void release(){queue_={};head_=size_=0;releasing_=true;}
    bool releasing() const{return releasing_;}
    template<class Send> bool flush(int protocol,std::uint64_t now,Send send) {
        if(releasing_) {
            for(unsigned vk=0;vk<held_.size();++vk)if(held_[vk]) {
                if(!send(key(vk,0,false,protocol,now)))return false;
                held_[vk]=false;
            }
            for(unsigned id=1;id<mouse_.size();++id)if(mouse_[id]) {
                if(!send(button(id,false,protocol,now)))return false;
                mouse_[id]=false;
            }
            releasing_=false;
        }
        while(size_) {
            const auto e=queue_[head_];
            if(!send(key(e.stroke.vk,e.stroke.modifiers,e.down,protocol,now)))return false;
            held_[e.stroke.vk]=e.down;queue_[head_]={};head_=(head_+1)%queue_.size();--size_;
        }
        return true;
    }
    template<class Send> bool mouse(unsigned id,bool down,int protocol,std::uint64_t now,Send send) {
        if(id>=mouse_.size()||releasing_)return false;
        if(mouse_[id]==down)return true;
        if(!send(button(id,down,protocol,now)))return false;
        mouse_[id]=down;return true;
    }
};
}
