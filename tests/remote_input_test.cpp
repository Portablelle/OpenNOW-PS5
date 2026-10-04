// SPDX-License-Identifier: GPL-3.0-or-later
#include "stream/remote_input.hpp"
#include "virtual_keyboard.hpp"
#include <cassert>
#include <vector>
using namespace opennow;
int main() {
    using Bytes=std::vector<std::uint8_t>;
    // Fixed wire fixtures: mixed byte order and v3 single-event marker.
    assert(hid::key(0x41,1,true,2,99)==Bytes({3,0,0,0,0,0x41,0,1,0,0,0,0,0,0,0,0,0,99}));
    assert(hid::button(3,false,3,7)==Bytes({0x23,0,0,0,0,0,0,0,7,0x22,9,0,0,0,3,0,0,0,0,0,0,0,0,0,0,0,0,7}));
    assert(hid::motion(-12,34,false,2,100)==Bytes({7,0,0,0,0xff,0xf4,0,34,0,0,0,0,0,0,0,0,0,0,0,0,0,100}));
    assert(hid::motion(120,-120,true,2,1)==Bytes({10,0,0,0,0,120,0xff,0x88,0,0,0,0,0,0,0,0,0,0,0,0,0,1}));
    hid::RemoteInput input;
    std::vector<Bytes> sent;
    auto accept=[&](const Bytes& p){sent.push_back(p);return true;};
    assert(input.tap({0x41,3}));
    unsigned attempts=0;
    // Shift, Ctrl, A down succeed; A up fails. Closing cancels remaining text,
    // then releases all three keys, retrying without forgetting a failed up.
    assert(!input.flush(2,1,[&](const Bytes& p){if(++attempts==4)return false;return accept(p);}));
    assert(sent.size()==3);
    input.release();sent.clear();
    assert(!input.flush(2,2,[](const Bytes&){return false;}));
    assert(input.releasing());assert(!input.tap({0x42,0}));
    assert(input.flush(2,3,accept));assert(sent.size()==3);
    for(const auto& p:sent)assert(p[0]==4);
    sent.clear();assert(input.flush(2,4,accept));assert(sent.empty());
    assert(input.mouse(1,true,2,5,accept));assert(input.mouse(3,true,2,5,accept));
    input.release();sent.clear();assert(input.flush(2,6,accept));assert(sent.size()==2);
    assert(sent[0][0]==9&&sent[0][4]==1&&sent[1][4]==3);
    // No modifier remains held after an ordinary chord.
    sent.clear();assert(input.tap({0x41,7}));assert(input.flush(3,7,accept));assert(sent.size()==8);
    input.release();sent.clear();assert(input.flush(2,8,accept));assert(sent.empty());
    VirtualKeyboard keyboard;
    assert(keyboard.choose().vk=='A'&&keyboard.choose().modifiers==0);
    keyboard.modifiers=1;assert(keyboard.choose().modifiers==1);
    keyboard.symbolPage=true;keyboard.modifiers=0;
    for(unsigned i=0;i<keyboard.characters().size();++i){keyboard.selected=i;assert(keyboard.choose().vk);}
    keyboard.selected=0;assert(keyboard.choose().vk=='1'&&keyboard.choose().modifiers==1);
    for(int i=0;i<200;++i){keyboard.move(-1,-1);assert(keyboard.selected<keyboard.count());}
    keyboard.reset();assert(!keyboard.open&&!keyboard.modifiers&&!keyboard.maskedCount);
}
