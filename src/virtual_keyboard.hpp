// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "stream/remote_input.hpp"
#include <string_view>
namespace opennow {
struct VirtualKeyboard {
    static constexpr std::string_view letters="abcdefghijklmnopqrstuvwxyz0123456789 ";
    static constexpr std::string_view symbols="!@#$%^&*()_+-=[]{}\\|;:'\",.<>/?`~";
    static constexpr std::string_view special[]={"ENTER","ESC","TAB","BACK","SHIFT","CTRL","ALT","SYMBOLS","LEFT","RIGHT","UP","DOWN"};
    unsigned selected=0,maskedCount=0;
    bool open=false,symbolPage=false;
    std::uint8_t modifiers=0;
    auto characters() const{return symbolPage?symbols:letters;}
    unsigned count() const{return characters().size()+12;}
    void move(int x,int y){selected=(selected+count()+x+y*10)%count();}
    std::string_view label(unsigned i) const {
        auto chars=characters();return i<chars.size()?chars.substr(i,1):special[i-chars.size()];
    }
    hid::Stroke choose() {
        auto chars=characters();
        if(selected>=chars.size()) {
            const unsigned i=selected-chars.size();
            if(i>=4&&i<=6){modifiers^=1<<(i-4);return {};}
            if(i==7){symbolPage=!symbolPage;selected=0;return {};}
            constexpr std::uint8_t vk[]={13,27,9,8,0,0,0,0,37,39,38,40};
            return {vk[i],modifiers};
        }
        const char c=chars[selected];
        if(c>='a'&&c<='z')return {static_cast<std::uint8_t>(c-'a'+'A'),modifiers};
        if((c>='0'&&c<='9')||c==' ')return {static_cast<std::uint8_t>(c),modifiers};
        constexpr std::string_view plain="-=[]\\;'`,./",shifted="_+{}|:\"~<>?",digits=")!@#$%^&*(";
        constexpr std::uint8_t keys[]={0xbd,0xbb,0xdb,0xdd,0xdc,0xba,0xde,0xc0,0xbc,0xbe,0xbf};
        auto at=plain.find(c);if(at!=plain.npos)return {keys[at],modifiers};
        at=shifted.find(c);if(at!=shifted.npos)return {keys[at],static_cast<std::uint8_t>(modifiers|1)};
        at=digits.find(c);if(at!=digits.npos)return {static_cast<std::uint8_t>('0'+at),static_cast<std::uint8_t>(modifiers|1)};
        return {};
    }
    void reset(){*this={};}
};
}
