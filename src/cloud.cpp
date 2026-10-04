// SPDX-License-Identifier: GPL-3.0-or-later
// Protocol adapted from pinned OpenNOW-Switch gfn/catalog and cloud_session.
#include "cloud.hpp"
#include "vendor/cJSON.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <initializer_list>
namespace opennow {
namespace {
const char* str(const cJSON* j,const char* k) {auto* v=cJSON_GetObjectItemCaseSensitive(j,k);return cJSON_IsString(v)?v->valuestring:"";}
const cJSON* obj(const cJSON* j,const char* k) {return cJSON_GetObjectItemCaseSensitive(j,k);}
int num(const cJSON* j,const char* k,int fallback=0) {auto* v=obj(j,k);return cJSON_IsNumber(v)?v->valueint:fallback;}
bool put(char* out,std::size_t cap,const char* in) {if(!in||std::strlen(in)>=cap)return false;std::memcpy(out,in,std::strlen(in)+1);return true;}
template<std::size_t N> bool copy(char (&out)[N],const char* in){return put(out,N,in);}
void wipeJson(cJSON* v) {for(;v;v=v->next){if(v->valuestring)secureErase(v->valuestring,std::strlen(v->valuestring));wipeJson(v->child);}}
struct Json {cJSON* p;explicit Json(const Response& r):p(r.body?cJSON_ParseWithLengthOpts(r.body,r.length+1,nullptr,true):nullptr){}~Json(){wipeJson(p);cJSON_Delete(p);}};
const char* searchQuery=R"(query GetSearchFilterResults($vpcId:String!,$locale:String!,$fetchCount:Int!,$cursor:String!,$searchString:String!,$filters:AppFilterFields!){apps(vpcId:$vpcId,language:$locale,orderBy:"itemMetadata.relevance:DESC,sortName:ASC",first:$fetchCount,after:$cursor,searchQuery:$searchString,filters:$filters){pageInfo{hasNextPage endCursor}items{id title variants{id appStore gfn{status}}}}})";
const char* browseQuery=R"(query GetFilterBrowseResults($vpcId:String!,$locale:String!,$fetchCount:Int!,$cursor:String!,$filters:AppFilterFields!){apps(vpcId:$vpcId,language:$locale,orderBy:"itemMetadata.relevance:DESC,sortName:ASC",first:$fetchCount,after:$cursor,filters:$filters){pageInfo{hasNextPage endCursor}items{id title variants{id appStore gfn{status}}}}})";
bool safeId(const char* s) {if(!*s)return false;for(;*s;++s)if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||*s=='-'||*s=='_'))return false;return true;}
bool signalingAddress(char* out,std::size_t capacity,const cJSON* connection) {
    const char* path=str(connection,"resourcePath");
    if(!std::strncmp(path,"wss://",6))return put(out,capacity,path);
    if(!std::strncmp(path,"https://",8)){return std::snprintf(out,capacity,"wss://%s",path+8)>0&&std::strlen(path)-2<capacity;}
    if(!std::strncmp(path,"rtsps://",8)||!std::strncmp(path,"rtsp://",7)) {
        const char* start=std::strstr(path,"://")+3;
        const char* end=start;
        if(*start=='['){end=std::strchr(start,']');if(!end)return false;++end;}
        else while(*end&&*end!=':'&&*end!='/')++end;
        if(end==start||end-start>253)return false;
        for(const char* p=start;p<end;++p)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='.'||*p=='-'||*p==':'||*p=='['||*p==']'))return false;
        const int n=std::snprintf(out,capacity,"wss://%.*s/nvst/",static_cast<int>(end-start),start);
        return n>0&&static_cast<std::size_t>(n)<capacity;
    }
    const auto* ip=obj(connection,"ip");if(cJSON_IsArray(ip))ip=cJSON_GetArrayItem(ip,0);
    if(!cJSON_IsString(ip)||!ip->valuestring[0])return false;
    const char* host=ip->valuestring;
    for(const char* p=host;*p;++p)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='.'||*p=='-'))return false;
    int port=num(connection,"port",443);if(port<1||port>65535)return false;
    if(*path&&(*path!='/'||path[1]=='/'))return false;
    int n=std::snprintf(out,capacity,"wss://%s:%d%s",host,*path?port:443,*path?path:"/nvst/");
    return n>0&&static_cast<std::size_t>(n)<capacity;
}

}
bool trustedCloudUrl(const char* url) noexcept {
    if(std::strncmp(url,"https://",8))return false;
    auto* start=url+8;auto* end=std::strchr(start,'/');if(!end)return false;
    for(auto* p=url;*p;++p)if(static_cast<unsigned char>(*p)<=32||*p=='\\'||*p=='@')return false;
    const auto n=static_cast<std::size_t>(end-start);
    for(auto* suffix:{".geforcenow.com",".nvidiagrid.net",".geforce.com"}) {
        auto len=std::strlen(suffix);if(n>len&&!std::strncmp(end-len,suffix,len))return true;
    }
    return false;
}
bool parseCatalog(const Response& r,CloudView& out,char* cursor,std::size_t capacity) noexcept {
    Json j(r);const auto* apps=obj(obj(j.p,"data"),"apps");const auto* items=obj(apps,"items");const auto* page=obj(apps,"pageInfo");
    if(r.status!=200||!cJSON_IsArray(items)||cJSON_GetArraySize(items)>60||!cJSON_IsBool(obj(page,"hasNextPage"))||cJSON_GetArraySize(obj(j.p,"errors")))return false;
    CloudView parsed;parsed.state=CloudState::catalog;parsed.hasNext=cJSON_IsTrue(obj(page,"hasNextPage"));
    if(parsed.hasNext&&(!*str(page,"endCursor")||!std::strcmp(cursor,str(page,"endCursor"))))return false;
    if(!put(cursor,capacity,str(page,"endCursor")))return false;
    const cJSON* app; cJSON_ArrayForEach(app,items) {
        const cJSON* variant;cJSON_ArrayForEach(variant,obj(app,"variants")) {
            if(parsed.count==60)break;
            auto& game=parsed.games[parsed.count];
            if(!safeId(str(variant,"id"))||!copy(game.id,str(variant,"id"))||!copy(game.title,str(app,"title"))||!copy(game.store,str(variant,"appStore")))return false;
            if(!*game.title||!*game.store)continue;
            ++parsed.count;
        }
    }
    copy(parsed.message,parsed.count?"Choose a game and store":"No games found in this catalog page");out=parsed;return true;
}
void Cloud::fail(const char* text) noexcept {view_.state=CloudState::failed;std::snprintf(view_.message,sizeof(view_.message),"%s",text);}
void Cloud::reset() noexcept {secureErase(&session_,sizeof(session_));view_={};base_[0]=vpc_[0]=cursor_[0]=search_[0]=0;}
void Cloud::load(const char* jwt,const char* device,const char* search,bool next) noexcept {
    if(*session_.id){fail("Stop the active session before browsing");return;}
    view_.state=CloudState::loading;
    if(!next){cursor_[0]=0;if(!copy(search_,search)){fail("Search text too long");return;}}
    if(!*base_) {
        auto r=request_(context_,"GET","https://pcs.geforcenow.com/v1/serviceUrls",nullptr,nullptr,nullptr);
        if(r.error){fail(r.error);return;}Json j(r);
        const cJSON* entry;cJSON_ArrayForEach(entry,obj(obj(j.p,"gfnServiceInfo"),"gfnServiceEndpoints")) {
            if(!std::strcmp(str(entry,"idpId"),"PDiAhv2kJTFeQ7WOPqiQ2tRZ7lGhR2X11dXvM4TZSxg")) {
                if(trustedCloudUrl(str(entry,"streamingServiceUrl")))copy(base_,str(entry,"streamingServiceUrl"));break;
            }
        }
        if(!*base_){fail("NVIDIA streaming endpoint unavailable");return;}
        auto n=std::strlen(base_);if(base_[n-1]!='/'&&n+1<sizeof(base_)){base_[n]='/';base_[n+1]=0;}
        char url[768];std::snprintf(url,sizeof(url),"%sv2/serverInfo",base_);
        r=request_(context_,"GET",url,nullptr,jwt,device);if(r.error){fail(r.error);return;}
        Json info(r);if(r.status!=200){fail("Unable to read NVIDIA server information");return;}
        if(!copy(vpc_,str(obj(info.p,"requestStatus"),"serverId"))||!*vpc_)copy(vpc_,"GFN-PC");
    }
    auto* root=cJSON_CreateObject();auto* vars=cJSON_AddObjectToObject(root,"variables");
    // NVIDIA rejects searchQuery when it is empty; browsing uses its own operation.
    cJSON_AddStringToObject(root,"query",*search_?searchQuery:browseQuery);cJSON_AddStringToObject(vars,"vpcId",vpc_);cJSON_AddStringToObject(vars,"locale","en_US");cJSON_AddNumberToObject(vars,"fetchCount",20);cJSON_AddStringToObject(vars,"cursor",cursor_);if(*search_)cJSON_AddStringToObject(vars,"searchString",search_);cJSON_AddObjectToObject(vars,"filters");
    char* body=cJSON_PrintUnformatted(root);cJSON_Delete(root);if(!body){fail("Out of memory");return;}
    auto r=request_(context_,"POST","https://games.geforce.com/graphql",body,jwt,device);cJSON_free(body);
    if(r.error){fail(r.error);return;}
    if(!parseCatalog(r,view_,cursor_,sizeof(cursor_))){char msg[128];std::snprintf(msg,sizeof(msg),"Catalog request failed (HTTP %ld)",r.status);fail(msg);}
}
void Cloud::select(int delta) noexcept {if(view_.state!=CloudState::catalog||!view_.count)return;view_.selected=(view_.selected+view_.count+delta)%view_.count;}
void Cloud::launch(const char* jwt,const char* device,std::uint64_t now,int userAge,StreamProfile profile,unsigned audioChannels) noexcept {
    if(view_.state!=CloudState::catalog||view_.selected>=view_.count||*session_.id)return;
    if(userAge<0||userAge>120){fail("Set your age in the private launch configuration first");return;}
    const auto& game=view_.games[view_.selected];
    // Reject partial/non-numeric IDs instead of silently launching app zero.
    for(const char* digit=game.id;*digit;++digit)if(*digit<'0'||*digit>'9'){fail("Catalog variant has no numeric launch ID");return;}
    const auto settings=settingsFor(profile);
    session_.profile=profile;
    session_.audioChannels=audioChannels==8?8:audioChannels==6?6:2;
    char netId[128]{},netUrl[768],netBody[256];
    std::snprintf(netUrl,sizeof(netUrl),"%sv2/nettestsession",base_);
    std::snprintf(netBody,sizeof(netBody),
        R"({"netTestRequestData":{"clientPlatformName":"windows","netTestProfile":{"widthInPixels":%d,"heightInPixels":%d,"framesPerSecond":%d}}})",
        settings.width,settings.height,settings.fps);
    const auto net=request_(context_,"POST",netUrl,
        netBody,jwt,device);
    // Upstream permits an unavailable network test; never reuse its response buffer.
    {Json result(net);if(!net.error&&net.status>=200&&net.status<300&&num(obj(result.p,"requestStatus"),"statusCode")==1){
        const char* id=str(obj(result.p,"netTestSession"),"sessionId");if(safeId(id))copy(netId,id);
    }}
    auto* root=cJSON_CreateObject();auto* req=cJSON_AddObjectToObject(root,"sessionRequestData");
    cJSON_AddNumberToObject(req,"userAge",userAge);
    cJSON_AddNumberToObject(req,"appId",std::strtod(game.id,nullptr));cJSON_AddStringToObject(req,"cmsId",game.id);
    for(auto* key:{"internalTitle","parentSessionId","clientDisplayHdrCapabilities"})cJSON_AddNullToObject(req,key);
    if(*netId)cJSON_AddStringToObject(req,"networkTestSessionId",netId);else cJSON_AddNullToObject(req,"networkTestSessionId");
    cJSON_AddStringToObject(req,"clientIdentification","GFN-PC");cJSON_AddStringToObject(req,"deviceHashId",device);cJSON_AddStringToObject(req,"clientVersion","30.0");cJSON_AddStringToObject(req,"clientPlatformName","windows");cJSON_AddStringToObject(req,"sdkVersion","1.0");cJSON_AddStringToObject(req,"partnerCustomData","");
    cJSON_AddArrayToObject(req,"availableSupportedControllers");
    for(auto* key:{"useOps","accountLinked"})cJSON_AddBoolToObject(req,key,true);
    for(auto* key:{"secureRTSPSupported","enablePersistingInGameSettings"})cJSON_AddBoolToObject(req,key,false);
    cJSON_AddNumberToObject(req,"streamerVersion",1);cJSON_AddNumberToObject(req,"audioMode",2);cJSON_AddNumberToObject(req,"sdrHdrMode",settings.hdr?1:0);cJSON_AddNumberToObject(req,"surroundAudioInfo",0);cJSON_AddNumberToObject(req,"remoteControllersBitmap",1);cJSON_AddNumberToObject(req,"enhancedStreamMode",1);cJSON_AddNumberToObject(req,"appLaunchMode",2);cJSON_AddNumberToObject(req,"clientTimezoneOffset",0);
    auto* features=cJSON_AddObjectToObject(req,"requestedStreamingFeatures");
    for(auto* key:{"reflex","cloudGsync","enabledL4S","trueHdr","fallbackToLogicalResolution","vsync"})cJSON_AddBoolToObject(features,key,false);
    for(auto* key:{"mouseMovementFlags","supportedHidDevices","profile","chromaFormat","prefilterMode","prefilterSharpness","prefilterNoiseReduction","hudStreamingMode","hdrColorSpace"})cJSON_AddNumberToObject(features,key,0);
    cJSON_AddNumberToObject(features,"bitDepth",settings.hdr?1:0);cJSON_AddNullToObject(features,"hidDevices");cJSON_AddNumberToObject(features,"sdrColorSpace",2);cJSON_AddNumberToObject(features,"maxBitrateKbps",settings.bitrate_kbps);cJSON_AddNumberToObject(features,"codec",settings.codec==VideoCodec::hevc?2:1);if(!settings.hardware)cJSON_AddNumberToObject(features,"dynamicStreamingMode",3);cJSON_AddNumberToObject(features,"audioChannelCount",session_.audioChannels);
    cJSON_AddNumberToObject(req,"requestedAudioFormat",session_.audioChannels==8?3:session_.audioChannels==6?2:1);
    auto* meta=cJSON_AddArrayToObject(req,"metaData");
    const auto audioCount=std::to_string(session_.audioChannels);
    const char* keys[]={"SubSessionId","wssignaling","GSStreamerType","surroundAudioInfo"};const char* values[]={device,"1","WebRTC",audioCount.c_str()};
    for(int i=0;i<4;++i){auto* m=cJSON_CreateObject();cJSON_AddStringToObject(m,"key",keys[i]);cJSON_AddStringToObject(m,"value",values[i]);cJSON_AddItemToArray(meta,m);}
    auto* monitors=cJSON_AddArrayToObject(req,"clientRequestMonitorSettings");auto* monitor=cJSON_CreateObject();cJSON_AddItemToArray(monitors,monitor);
    for(auto* key:{"monitorId","positionX","positionY"})cJSON_AddNumberToObject(monitor,key,0);
    cJSON_AddNumberToObject(monitor,"widthInPixels",settings.width);cJSON_AddNumberToObject(monitor,"heightInPixels",settings.height);cJSON_AddNumberToObject(monitor,"framesPerSecond",settings.fps);cJSON_AddNumberToObject(monitor,"dpi",100);cJSON_AddNumberToObject(monitor,"sdrHdrMode",settings.hdr?1:0);if(settings.hdr){auto* display=cJSON_AddObjectToObject(monitor,"displayData");
     // Preferred content luminance defaults used by the native desktop client;
     // these are negotiation hints, not measured LG panel capabilities.
     cJSON_AddNumberToObject(display,"desiredContentMaxLuminance",1000);cJSON_AddNumberToObject(display,"desiredContentMinLuminance",0);cJSON_AddNumberToObject(display,"desiredContentMaxFrameAverageLuminance",400);
    }else cJSON_AddNullToObject(monitor,"displayData");cJSON_AddNullToObject(monitor,"hdr10PlusGamingData");
    char* body=cJSON_PrintUnformatted(root);cJSON_Delete(root);if(!body){fail("Out of memory");return;}
    char url[768];std::snprintf(url,sizeof(url),"%sv2/session?keyboardLayout=en-US_qwerty&languageCode=en_US",base_);
    view_.state=CloudState::starting;
    auto r=request_(context_,"POST",url,body,jwt,device);cJSON_free(body);parseSession(r);nextPoll_=now+3;
}
bool Cloud::parseSession(const Response& r) noexcept {
    if(r.error){fail(r.error);return false;}Json root(r);auto* sess=obj(root.p,"session");
    if(r.status<200||r.status>=300||num(obj(root.p,"requestStatus"),"statusCode",-1)!=1){char text[192];const auto* status=obj(root.p,"requestStatus");
        // Only bounded error fields reach the UI. Never log the response/token/session ID.
        std::snprintf(text,sizeof(text),"HTTP %ld code %d / %.64s / unified %.24s / session %d",
            r.status,num(status,"statusCode",-1),str(status,"statusDescription"),
            str(status,"unifiedErrorCode"),num(sess,"errorCode",-1));
        if(cJSON_IsNumber(obj(status,"unifiedErrorCode")))std::snprintf(text,sizeof(text),"HTTP %ld code %d / %.64s / unified %d / session %d",r.status,num(status,"statusCode",-1),str(status,"statusDescription"),num(status,"unifiedErrorCode"),num(sess,"errorCode",-1));
        fail(text);return false;}
    if(*str(sess,"sessionId")&&safeId(str(sess,"sessionId")))copy(session_.id,str(sess,"sessionId"));
    if(!*session_.id){fail("Cloud session response missing ID");return false;}
    const auto* status=obj(sess,"status");int state=cJSON_IsNumber(status)?status->valueint:-1;
    if(cJSON_IsString(status)){auto* s=status->valuestring;if(!std::strcmp(s,"queued"))state=0;else if(!std::strcmp(s,"ready")||!std::strcmp(s,"active"))state=2;else if(!std::strcmp(s,"streaming")||!std::strcmp(s,"playing"))state=3;else if(!std::strcmp(s,"provisioning")||!std::strcmp(s,"initializing")||!std::strcmp(s,"setup")||!std::strcmp(s,"launching"))state=1;}
    if(state==4||state==5){fail("Cloud session ended");return false;}
    if(*str(sess,"signalingUrl"))copy(session_.signaling,str(sess,"signalingUrl"));
    if(*str(sess,"serverIp"))copy(session_.mediaIp,str(sess,"serverIp"));
    const cJSON* conn;cJSON_ArrayForEach(conn,obj(sess,"connectionInfo")) {
        if((num(conn,"usage")==14||num(conn,"usage")==16)&&!*session_.signaling)signalingAddress(session_.signaling,sizeof(session_.signaling),conn);
        if(num(conn,"usage")==2||num(conn,"usage")==17){const auto* ip=obj(conn,"ip");if(cJSON_IsArray(ip))ip=cJSON_GetArrayItem(ip,0);if(cJSON_IsString(ip))copy(session_.mediaIp,ip->valuestring);session_.mediaPort=num(conn,"port");}
    }
    if((state==2||state==3)&&*session_.signaling){view_.state=CloudState::ready;copy(view_.message,"Cloud session ready. Connecting stream...");}
    else if(state==6){fail("Session requires an advertisement step not yet supported");return false;}
    else if(state<0||state>3){fail("Unknown cloud session state");return false;}
    else {
        view_.state=CloudState::queued;
        if(state==2||state==3)copy(view_.message,"Server ready, but no supported streaming address received");
        else if(state==1)std::snprintf(view_.message,sizeof(view_.message),"Server preparing game - setup step %d",num(obj(sess,"seatSetupInfo"),"seatSetupStep",-1));
        else {int queue=num(obj(sess,"seatSetupInfo"),"queuePosition",num(sess,"queuePosition",-1));
            if(queue<0)copy(view_.message,"Waiting for server allocation (queue position unavailable)");
            else std::snprintf(view_.message,sizeof(view_.message),"Waiting for server allocation - queue position %d",queue);
        }
    }
    return true;
}
void Cloud::tick(const char* jwt,const char* device,std::uint64_t now) noexcept {
    if(view_.state!=CloudState::queued||now<nextPoll_)return;
    nextPoll_=now+3;char url[768];std::snprintf(url,sizeof(url),"%sv2/session/%s",base_,session_.id);
    parseSession(request_(context_,"GET",url,nullptr,jwt,device));
}
bool Cloud::stop(const char* jwt,const char* device) noexcept {
    if(*session_.id){char url[768];std::snprintf(url,sizeof(url),"%sv2/session/%s",base_,session_.id);auto r=request_(context_,"DELETE",url,nullptr,jwt,device);if(r.error||(r.status!=404&&(r.status<200||r.status>=300))){fail("Unable to stop cloud session. Press CIRCLE to retry");return false;}}
    secureErase(&session_,sizeof(session_));view_.state=view_.count?CloudState::catalog:CloudState::idle;copy(view_.message,"Choose a game and store");return true;
}
}
