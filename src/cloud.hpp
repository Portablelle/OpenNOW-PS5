// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "gfn.hpp"
#include "stream/stream_settings.hpp"
namespace opennow {
struct Game { char id[96]{}, title[160]{}, store[48]{}; };
enum class CloudState { idle, loading, catalog, starting, queued, ready, failed };
struct CloudView {
    CloudState state=CloudState::idle;
    Game games[60]{};
    unsigned count=0, selected=0;
    bool hasNext=false;
    char message[192]{};
};
struct Session {
    char id[128]{}, signaling[1024]{}, mediaIp[128]{};
    int mediaPort=0;
    StreamProfile profile=StreamProfile::quality;
    unsigned audioChannels=2;
};
class Cloud {
public:
    Cloud(Request r,void* c):request_(r),context_(c){}
    void load(const char* jwt,const char* device,const char* search="",bool next=false) noexcept;
    void select(int delta) noexcept;
    void launch(const char* jwt,const char* device,std::uint64_t now,int userAge=-1,StreamProfile profile=StreamProfile::quality,unsigned audioChannels=2) noexcept;
    void tick(const char* jwt,const char* device,std::uint64_t now) noexcept;
    bool stop(const char* jwt,const char* device) noexcept;
    void reset() noexcept;
    const CloudView& view() const {return view_;}
    const Session& session() const {return session_;}
private:
    void fail(const char*) noexcept;
    bool parseSession(const Response&) noexcept;
    Request request_;void* context_;
    CloudView view_{};Session session_{};
    char base_[512]{},vpc_[128]{},cursor_[512]{},search_[128]{};
    std::uint64_t nextPoll_=0;
};
bool trustedCloudUrl(const char*) noexcept;
bool parseCatalog(const Response&,CloudView&,char*,std::size_t) noexcept;
}
