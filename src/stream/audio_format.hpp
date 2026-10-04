// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <charconv>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

namespace opennow::audio {
enum class Mode { automatic, stereo, surround51, surround71 };
inline Mode nextMode(Mode mode) { return static_cast<Mode>((static_cast<int>(mode)+1)%4); }
inline const char* modeLabel(Mode mode) {
    switch(mode) {
    case Mode::stereo: return "STEREO";
    case Mode::surround51: return "5.1";
    case Mode::surround71: return "7.1";
    default: return "AUTO";
    }
}
constexpr unsigned requestedChannels(Mode mode,unsigned capacity) {
    const unsigned limit=mode==Mode::stereo?2:mode==Mode::surround51?6:8;
    return capacity>=8&&limit>=8?8:capacity>=6&&limit>=6?6:2;
}
struct Format {
    unsigned channels=0,streams=0,coupled=0;
    int payload=111,redPayload=-1;
    std::array<unsigned char,8> mapping{};
    std::string rtpmap,fmtp,redRtpmap,redFmtp;
    unsigned bitrate() const { return channels==8?510000:channels==6?384000:256000; }
};
inline std::string trim(std::string value) {
    const auto first=value.find_first_not_of(" \t\r\n");
    if(first==std::string::npos)return {};
    return value.substr(first,value.find_last_not_of(" \t\r\n")-first+1);
}
inline bool integer(const std::string& value,unsigned& result) {
    const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
    return !value.empty()&&parsed.ec==std::errc{}&&parsed.ptr==value.data()+value.size();
}
inline std::vector<std::string> split(const std::string& value,char separator) {
    std::vector<std::string> result;std::size_t start=0,end;
    do {
        end=value.find(separator,start);
        result.push_back(value.substr(start,end==std::string::npos?end:end-start));
        if(end==std::string::npos)break;
        start=end+1;
    } while(start<=value.size());
    return result;
}
inline std::string parameter(const std::string& fmtp,const std::string& key) {
    for(auto item:split(fmtp,';')) {
        item=trim(item);const auto equal=item.find('=');
        if(equal!=std::string::npos&&trim(item.substr(0,equal))==key)return trim(item.substr(equal+1));
    }
    return {};
}
// Only accept explicitly described WebRTC multiopus layouts. Plain opus/48000/2
// cannot represent surround, and NVST DESCRIBE parameters are a different transport.
inline Format selectFormat(const std::string& offer,unsigned maximum) {
    std::array<std::string,128> maps{},params{};
    std::array<bool,128> offered{};
    bool inAudio=false;
    for(auto line:split(offer,'\n')) {
        line=trim(line);
        if(line.rfind("m=",0)==0) {
            inAudio=line.rfind("m=audio ",0)==0;
            if(inAudio){
                std::replace(line.begin(),line.end(),'\t',' ');
                std::vector<std::string> media;
                for(const auto& token:split(line,' '))if(!token.empty())media.push_back(token);
                if(media.size()<4||media[1]=="0"){inAudio=false;continue;}
                unsigned pt;
                for(unsigned i=3;i<media.size();++i)if(integer(media[i],pt)&&pt<128)offered[pt]=true;
            }
        }
        if(!inAudio)continue;
        const bool map=line.rfind("a=rtpmap:",0)==0;
        if(!map&&line.rfind("a=fmtp:",0)!=0)continue;
        const auto start=map?9u:7u;const auto space=line.find(' ',start);unsigned pt;
        if(space==std::string::npos||!integer(line.substr(start,space-start),pt)||pt>=128)continue;
        (map?maps:params)[pt]=trim(line.substr(space+1));
    }
    Format best;
    for(unsigned pt=0;pt<128;++pt) {
        if(!offered[pt])continue;
        std::string map=maps[pt];
        std::transform(map.begin(),map.end(),map.begin(),[](unsigned char c){return std::tolower(c);});
        Format candidate;candidate.payload=static_cast<int>(pt);
        if(map=="opus/48000/2") {
            candidate.channels=2;candidate.streams=candidate.coupled=1;candidate.mapping={0,1};
            candidate.fmtp="minptime=10;stereo=1;sprop-stereo=1;maxaveragebitrate=256000;useinbandfec=1";
        } else if(map=="multiopus/48000/6"||map=="multiopus/48000/8") {
            candidate.channels=map.back()=='6'?6:8;
            if(!integer(parameter(params[pt],"num_streams"),candidate.streams)||
               !integer(parameter(params[pt],"coupled_streams"),candidate.coupled)||
               !candidate.streams||candidate.coupled>candidate.streams||
               candidate.streams+candidate.coupled!=candidate.channels)continue;
            unsigned count=0,index;bool valid=true;
            for(const auto& item:split(parameter(params[pt],"channel_mapping"),',')) {
                if(count>=candidate.channels||!integer(trim(item),index)||index>=candidate.channels){valid=false;break;}
                candidate.mapping[count++]=static_cast<unsigned char>(index);
            }
            if(!valid||count!=candidate.channels)continue;
            // Require a complete permutation; duplicated/missing speakers are not supported.
            unsigned seen=0;for(unsigned i=0;i<count;++i)seen|=1u<<candidate.mapping[i];
            if(seen!=(1u<<count)-1)continue;
            candidate.fmtp="num_streams="+std::to_string(candidate.streams)+
                ";coupled_streams="+std::to_string(candidate.coupled)+
                ";channel_mapping="+parameter(params[pt],"channel_mapping")+
                ";maxaveragebitrate="+std::to_string(candidate.bitrate())+";useinbandfec=1";
        } else continue;
        if(candidate.channels>maximum||candidate.channels<=best.channels)continue;
        candidate.rtpmap="a=rtpmap:"+std::to_string(pt)+" "+maps[pt];best=candidate;
    }
    if(!best.channels)return best;
    for(unsigned pt=0;pt<128;++pt) {
        std::string map=maps[pt];
        std::transform(map.begin(),map.end(),map.begin(),[](unsigned char c){return std::tolower(c);});
        if(!offered[pt]||map!="red/48000/"+std::to_string(best.channels))continue;
        bool valid=!params[pt].empty();unsigned count=0,value;
        for(const auto& item:split(params[pt],'/')) {
            if(!integer(trim(item),value)||value!=static_cast<unsigned>(best.payload))valid=false;
            ++count;
        }
        if(!valid||!count)continue;
        best.redPayload=static_cast<int>(pt);best.redRtpmap="a=rtpmap:"+std::to_string(pt)+" "+maps[pt];
        best.redFmtp="a=fmtp:"+std::to_string(pt)+" "+params[pt];break;
    }
    return best;
}
// Opus mapping family 1 -> AudioOut S16_8CH (format 2):
// FL FR FC LFE BL BR SL SR. Stereo occupies only FL/FR.
inline void outputFrame(const std::int16_t* input,unsigned channels,
                        std::int16_t* output,unsigned outputChannels) {
    std::fill(output,output+outputChannels,0);
    if(channels==2){output[0]=input[0];output[1]=input[1];return;}
    constexpr unsigned fiveOne[]={0,2,1,5,3,4};
    constexpr unsigned sevenOne[]={0,2,1,7,5,6,3,4};
    const auto* order=channels==6?fiveOne:sevenOne;
    for(unsigned i=0;i<channels&&i<outputChannels;++i)output[i]=input[order[i]];
}
} // namespace opennow::audio
