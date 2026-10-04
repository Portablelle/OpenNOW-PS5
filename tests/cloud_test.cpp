#include "cloud.hpp"
#include "vendor/cJSON.h"
#include <cassert>
#include <cstring>
#include <string>
#include <cstdio>
using namespace opennow;
static Response response(const char* s){return {200,const_cast<char*>(s),std::strlen(s),nullptr};}
struct Mock {unsigned posts=0,deletes=0,nettests=0;bool reject=false;const char* poll=nullptr;std::string body,netBody,catalogBody;const char* catalog=nullptr;};
static Response request(void* p,const char* method,const char* url,const char* body,const char*,const char*) {
 auto& m=*static_cast<Mock*>(p);
 if(std::strstr(url,"serviceUrls"))return response(R"({"gfnServiceInfo":{"gfnServiceEndpoints":[{"idpId":"PDiAhv2kJTFeQ7WOPqiQ2tRZ7lGhR2X11dXvM4TZSxg","streamingServiceUrl":"https://test.geforcenow.com/"}]}})");
 if(std::strstr(url,"serverInfo"))return response(R"({"requestStatus":{"serverId":"GFN-PC"}})");
 if(std::strstr(url,"graphql")){m.catalogBody=body;if(m.catalog)return response(m.catalog);return response(R"({"data":{"apps":{"pageInfo":{"hasNextPage":false,"endCursor":""},"items":[{"title":"Fixture Game","variants":[{"id":"42","appStore":"XBOX"},{"id":"43","appStore":"STEAM"}]}]}}})");}
 if(std::strstr(url,"nettestsession")){++m.nettests;m.netBody=body;return response(R"({"requestStatus":{"statusCode":1},"netTestSession":{"sessionId":"net-fixture"}})");}
 if(!std::strcmp(method,"POST")){if(m.reject){auto r=response(R"({"requestStatus":{"statusCode":4,"statusDescription":"INTERNAL_ERROR_STATUS","unifiedErrorCode":123}})");r.status=500;return r;}++m.posts;m.body=body;return response(R"({"requestStatus":{"statusCode":1},"session":{"sessionId":"fixture-id","status":0,"queuePosition":5}})");}
 if(!std::strcmp(method,"DELETE")){++m.deletes;return response("{}");}
 if(m.poll)return response(m.poll);
 return response(R"({"requestStatus":{"statusCode":1},"session":{"sessionId":"fixture-id","status":2,"signalingUrl":"wss://test.geforcenow.com/nvst/"}})");
}
int main(){
 assert(trustedCloudUrl("https://games.geforce.com/graphql"));
 for(const char* bad:{"http://games.geforce.com/graphql","https://games.geforce.com.evil.test/graphql","https://evil.test/@games.geforce.com/","https://geforce.com@evil.test/","https://games.geforce.com:443/"})assert(!trustedCloudUrl(bad));
 CloudView view;char cursor[128]{};
 assert(!parseCatalog(response(R"({"data":{"apps":{"items":[]}}})"),view,cursor,sizeof(cursor)));
 assert(!parseCatalog(response(R"({"data":{"apps":{"pageInfo":{"hasNextPage":true,"endCursor":""},"items":[]}}})"),view,cursor,sizeof(cursor)));
 Mock m;Cloud c(request,&m);c.load("fixture-jwt","fixture-device","Fixture");assert(c.view().count==2);assert(!std::strcmp(c.view().games[0].store,"XBOX"));
 // Square browses without sending an empty searchQuery, including subsequent pages.
 auto checkCatalog=[&](bool search,const char* expectedCursor){
  auto* root=cJSON_Parse(m.catalogBody.c_str());assert(root);
  const char* query=cJSON_GetObjectItemCaseSensitive(root,"query")->valuestring;
  assert((std::strstr(query,"searchQuery:")!=nullptr)==search);
  auto* vars=cJSON_GetObjectItemCaseSensitive(root,"variables");
  auto* text=cJSON_GetObjectItemCaseSensitive(vars,"searchString");
  if(search){assert(cJSON_IsString(text));assert(!std::strcmp(text->valuestring,"Fixture"));}
  else assert(!text);
  assert(!std::strcmp(cJSON_GetObjectItemCaseSensitive(vars,"cursor")->valuestring,expectedCursor));
  cJSON_Delete(root);
 };
 checkCatalog(true,"");
 m.catalog=R"({"data":{"apps":{"pageInfo":{"hasNextPage":true,"endCursor":"page-2"},"items":[]}}})";
 c.load("fixture-jwt","fixture-device","");assert(c.view().state==CloudState::catalog&&c.view().hasNext);checkCatalog(false,"");
 m.catalog=nullptr;
 c.load("fixture-jwt","fixture-device","",true);assert(c.view().count==2);checkCatalog(false,"page-2");
 m.catalog=R"({"data":{"apps":{"pageInfo":{"hasNextPage":true,"endCursor":"search-page-2"},"items":[]}}})";
 c.load("fixture-jwt","fixture-device","Fixture");checkCatalog(true,"");
 m.catalog=nullptr;
 c.load("fixture-jwt","fixture-device","",true);assert(c.view().count==2);checkCatalog(true,"search-page-2");
 c.launch("fixture-jwt","fixture-device",9,-1);assert(m.posts==0&&m.nettests==0);c.stop("fixture-jwt","fixture-device");
 c.select(1);c.launch("fixture-jwt","fixture-device",10,27);assert(m.posts==1&&c.view().state==CloudState::queued);assert(m.body.find("\"cmsId\":\"43\"")!=std::string::npos);assert(m.body.find("\"bitDepth\":0")!=std::string::npos);
 assert(m.body.find("\"userAge\":27")!=std::string::npos);assert(m.nettests==1);assert(m.body.find("\"networkTestSessionId\":\"net-fixture\"")!=std::string::npos);
 c.launch("fixture-jwt","fixture-device",11,27);assert(m.posts==1);
 c.tick("fixture-jwt","fixture-device",12);assert(c.view().state==CloudState::queued);c.tick("fixture-jwt","fixture-device",13);assert(c.view().state==CloudState::ready);
 assert(c.stop("fixture-jwt","fixture-device")&&m.deletes==1);assert(!*c.session().id);assert(c.view().state==CloudState::catalog);
 for(const char* address:{
 R"({"requestStatus":{"statusCode":1},"session":{"status":2,"connectionInfo":[{"usage":14,"resourcePath":"rtsps://stream.geforcenow.com:48322","port":48322}]}})",
 R"({"requestStatus":{"statusCode":1},"session":{"status":2,"connectionInfo":[{"usage":14,"resourcePath":"rtsp://stream.geforcenow.com:322"}]}})"}){
 m.poll=address;c.launch("fixture-jwt","fixture-device",20,27);c.tick("fixture-jwt","fixture-device",23);assert(c.view().state==CloudState::ready);assert(!std::strcmp(c.session().signaling,"wss://stream.geforcenow.com/nvst/"));assert(c.stop("fixture-jwt","fixture-device"));
 }
 m.poll=R"({"requestStatus":{"statusCode":1},"session":{"status":2}})";c.launch("fixture-jwt","fixture-device",30,27);c.tick("fixture-jwt","fixture-device",33);assert(std::strstr(c.view().message,"no supported streaming address"));c.stop("fixture-jwt","fixture-device");
 m.poll=R"({"requestStatus":{"statusCode":1},"session":{"status":1,"seatSetupInfo":{"seatSetupStep":3}}})";c.launch("fixture-jwt","fixture-device",40,27);c.tick("fixture-jwt","fixture-device",43);assert(std::strstr(c.view().message,"setup step 3"));c.stop("fixture-jwt","fixture-device");
 // The allocation request and net-test must match every negotiated profile.
 for(auto profile:{StreamProfile::quality,StreamProfile::smooth,StreamProfile::experimental,StreamProfile::compatibility,StreamProfile::native_hdr120,StreamProfile::native_hdr60,StreamProfile::native_4k120,StreamProfile::native_1080,StreamProfile::native_hdr90,StreamProfile::native_4k90}){
  c.launch("fixture-jwt","fixture-device",50,27,profile);
  const auto settings=settingsFor(profile);
  auto* root=cJSON_Parse(m.body.c_str());assert(root);
  auto* request=cJSON_GetObjectItemCaseSensitive(root,"sessionRequestData");
  auto* features=cJSON_GetObjectItemCaseSensitive(request,"requestedStreamingFeatures");
  auto* monitor=cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(request,"clientRequestMonitorSettings"),0);
  assert(cJSON_GetObjectItemCaseSensitive(features,"maxBitrateKbps")->valueint==settings.bitrate_kbps);
  assert(cJSON_GetObjectItemCaseSensitive(features,"codec")->valueint==(settings.codec==VideoCodec::hevc?2:1));
  assert(cJSON_GetObjectItemCaseSensitive(features,"bitDepth")->valueint==(settings.hdr?1:0));
  assert(cJSON_GetObjectItemCaseSensitive(features,"audioChannelCount")->valueint==2);
  assert((cJSON_GetObjectItemCaseSensitive(features,"dynamicStreamingMode")==nullptr)==settings.hardware);
  assert(cJSON_GetObjectItemCaseSensitive(request,"sdrHdrMode")->valueint==(settings.hdr?1:0));
  assert(cJSON_GetObjectItemCaseSensitive(monitor,"widthInPixels")->valueint==settings.width);
  assert(cJSON_GetObjectItemCaseSensitive(monitor,"heightInPixels")->valueint==settings.height);
  assert(cJSON_GetObjectItemCaseSensitive(monitor,"framesPerSecond")->valueint==settings.fps);
  const auto* display=cJSON_GetObjectItemCaseSensitive(monitor,"displayData");
  if(settings.hdr){
   assert(cJSON_IsObject(display));
   assert(cJSON_GetObjectItemCaseSensitive(display,"desiredContentMaxLuminance")->valueint==1000);
   assert(cJSON_GetObjectItemCaseSensitive(display,"desiredContentMinLuminance")->valueint==0);
   assert(cJSON_GetObjectItemCaseSensitive(display,"desiredContentMaxFrameAverageLuminance")->valueint==400);
  }else assert(cJSON_IsNull(display));
  cJSON_Delete(root);
  root=cJSON_Parse(m.netBody.c_str());assert(root);
  auto* net=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root,"netTestRequestData"),"netTestProfile");
  assert(cJSON_GetObjectItemCaseSensitive(net,"widthInPixels")->valueint==settings.width);
  assert(cJSON_GetObjectItemCaseSensitive(net,"heightInPixels")->valueint==settings.height);
  assert(cJSON_GetObjectItemCaseSensitive(net,"framesPerSecond")->valueint==settings.fps);
  cJSON_Delete(root);
  assert(c.session().profile==profile);c.stop("fixture-jwt","fixture-device");
 }
 for(unsigned channels:{6u,8u,99u}) {
  c.launch("fixture-jwt","fixture-device",60,27,StreamProfile::quality,channels);
  const unsigned expected=channels==99?2:channels;
  auto* root=cJSON_Parse(m.body.c_str());assert(root);
  auto* request=cJSON_GetObjectItemCaseSensitive(root,"sessionRequestData");
  auto* features=cJSON_GetObjectItemCaseSensitive(request,"requestedStreamingFeatures");
  assert(cJSON_GetObjectItemCaseSensitive(features,"audioChannelCount")->valueint==int(expected));
  assert(cJSON_GetObjectItemCaseSensitive(request,"requestedAudioFormat")->valueint==(expected==8?3:expected==6?2:1));
  assert(c.session().audioChannels==expected);
  auto* metadata=cJSON_GetObjectItemCaseSensitive(request,"metaData");
  auto* surround=cJSON_GetArrayItem(metadata,3);
  assert(!std::strcmp(cJSON_GetObjectItemCaseSensitive(surround,"key")->valuestring,"surroundAudioInfo"));
  assert(std::atoi(cJSON_GetObjectItemCaseSensitive(surround,"value")->valuestring)==int(expected));
  cJSON_Delete(root);assert(c.stop("fixture-jwt","fixture-device"));
 }
 m.reject=true;c.launch("fixture-jwt","fixture-device",20,27);assert(c.view().state==CloudState::failed);assert(std::strstr(c.view().message,"INTERNAL_ERROR_STATUS"));assert(std::strstr(c.view().message,"unified 123"));
 puts("Catalog and cloud lifecycle regressions passed");
}
