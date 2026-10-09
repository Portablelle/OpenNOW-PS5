// SPDX-License-Identifier: GPL-3.0-or-later
#include "gfn.hpp"
#include "account_file.hpp"
#include "launch_config.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

namespace {
struct Reply {
    const char* endpoint;
    long status;
    std::string body;
    const char* form = nullptr;
    const char* bearer = nullptr;
    const char* error = nullptr;
};
struct Fake {
    std::vector<Reply> replies;
    unsigned calls = 0;
    static opennow::Response request(void* context, const char* method, const char* url,
        const char* form, const char* bearer, const char*) {
        auto& self = *static_cast<Fake*>(context);
        assert(self.calls < self.replies.size());
        auto& reply = self.replies[self.calls++];
        assert(std::strstr(url,reply.endpoint));
        assert(!std::strcmp(method,form ? "POST" : "GET"));
        if (reply.form) assert(form && std::strstr(form,reply.form));
        if (reply.bearer) assert(bearer && !std::strcmp(bearer,reply.bearer));
        return {reply.status,reply.body.data(),reply.body.size(),reply.error};
    }
};
const char* challenge = R"({"device_code":"synthetic-device-code","user_code":"TEST-CODE","verification_uri":"https://login.nvidia.com/activate","interval":5})";
const char* identity = "00000000-0000-4000-8000-000000000000";
Fake fresh() {
    return {{{"/device/authorize",200,challenge},
        {"/token",200,R"({"access_token":"access1","id_token":"cloud1","refresh_token":"r&1","expires_in":180})"},
        {"/userinfo",200,R"({"sub":"synthetic-user"})",nullptr,"access1"},
        {"/client_token",200,R"({"client_token":"client&1"})",nullptr,"access1"}}};
}
void signIn(const char* path) {
    auto fake = fresh();
    opennow::Login login(Fake::request,&fake,path);
    login.begin(identity,0); login.tick(5);
    assert(login.view().state == opennow::State::authenticated);
    assert(login.view().sessionSaved);
    assert(!std::strcmp(login.cloudToken(),"cloud1"));
}
std::string contents(const char* path) {
    FILE* file = std::fopen(path,"rb"); assert(file);
    std::string data; char buffer[1024]; std::size_t n;
    while ((n = std::fread(buffer,1,sizeof(buffer),file))) data.append(buffer,n);
    std::fclose(file); return data;
}
}
int main() {
    char directory[] = "/tmp/opennow-auth-test-XXXXXX"; assert(mkdtemp(directory));
    const std::string path = std::string(directory) + "/account.bin";
    signIn(path.c_str());
    struct stat info{}; assert(stat(path.c_str(),&info) == 0);
    assert((info.st_mode & 0777) == 0600);
    {
        // An upgrade repairs the old private directory without changing login data.
        const auto saved = contents(path.c_str());
        assert(chmod(directory,0700) == 0);
        assert(opennow::accountFile::prepareDirectory(directory,0755));
        assert(stat(directory,&info) == 0);
        assert((info.st_mode & 07777) == 0755);
        assert(contents(path.c_str()) == saved);
        assert(stat(path.c_str(),&info) == 0);
        assert((info.st_mode & 0777) == 0600);
        assert(opennow::accountFile::prepareDirectory(directory,0755));

        const auto freshDirectory = std::string(directory) + "/fresh";
        const auto mask = umask(0077);
        const bool prepared = opennow::accountFile::prepareDirectory(freshDirectory.c_str(),01777);
        umask(mask);
        assert(prepared);
        assert(stat(freshDirectory.c_str(),&info) == 0);
        assert((info.st_mode & 07777) == 01777);
        const auto link = std::string(directory) + "/link";
        assert(symlink(freshDirectory.c_str(),link.c_str()) == 0);
        assert(!opennow::accountFile::prepareDirectory(link.c_str(),01777));
        assert(!opennow::accountFile::prepareDirectory(path.c_str(),0755));
        const auto agePath = freshDirectory + "/launch-age.txt";
        const auto legacyAgePath = std::string(directory) + "/launch-age.txt";
        const auto writeAge = [](const std::string& name,const char* value) {
            FILE* file = std::fopen(name.c_str(),"wb"); assert(file);
            assert(std::fputs(value,file) >= 0);
            assert(std::fclose(file) == 0);
        };
        const auto readAge = [&] {
            return opennow::launchConfig::readAge(agePath.c_str(),legacyAgePath.c_str());
        };
        assert(readAge() == -1);
        writeAge(legacyAgePath,"42\n");
        assert(readAge() == 42);
        writeAge(agePath,"18\n");
        assert(readAge() == 18);
        for (const char* invalid : {"", "121", "-1", "18 extra", "18.5",
                                   "999999999999999999999999999999999999"}) {
            writeAge(agePath,invalid);
            assert(readAge() == -1); // Invalid new config must not use the legacy age.
        }
        writeAge(agePath," 0 \n"); assert(readAge() == 0);
        writeAge(agePath,"120\n"); assert(readAge() == 120);
        assert(unlink(agePath.c_str()) == 0);
        assert(symlink(legacyAgePath.c_str(),agePath.c_str()) == 0);
        assert(readAge() == -1);
        assert(unlink(agePath.c_str()) == 0);
        assert(mkfifo(agePath.c_str(),0644) == 0);
        assert(readAge() == -1); // Must return without waiting for a writer.
        assert(unlink(agePath.c_str()) == 0);
        assert(unlink(legacyAgePath.c_str()) == 0);
        assert(unlink(link.c_str()) == 0);
        assert(rmdir(freshDirectory.c_str()) == 0);
    }
    {
        // A new Login instance models process restart/app replacement. Device ID and
        // cloud JWT survive renewal responses that omit id_token.
        Fake fake{{{"/token",200,R"({"access_token":"access2","refresh_token":"rotated-refresh","expires_in":180})",
            "client_token=client%261"},
            {"/userinfo",200,R"({"sub":"synthetic-user"})",nullptr,"access2"},
            {"/client_token",200,R"({"client_token":"client2"})",nullptr,"access2"}}};
        opennow::Login login(Fake::request,&fake,path.c_str()); char device[37] = "new-process";
        assert(login.restore(device,100)); assert(!std::strcmp(device,identity));
        assert(login.view().state == opennow::State::authenticated);
        assert(!std::strcmp(login.cloudToken(),"cloud1"));
        const auto saved = contents(path.c_str());
        assert(saved.find("rotated-refresh") != std::string::npos);
        assert(saved.find("client2") != std::string::npos);
        assert(saved.find("client&1") == std::string::npos);
    }
    {
        Fake fake{{{"/token",400,R"({"error":"invalid_grant"})","client_token=client2"},
            {"/token",200,R"({"access_token":"access3","id_token":"cloud3","expires_in":180})",
                "refresh_token=rotated-refresh"},
            {"/userinfo",200,R"({"sub":"synthetic-user"})",nullptr,"access3"},
            {"/client_token",404,"{}"}}};
        opennow::Login login(Fake::request,&fake,path.c_str()); char device[37]{};
        assert(login.restore(device,0)); assert(!std::strcmp(login.cloudToken(),"cloud3"));
    }
    for (const auto& reply : {Reply{"/token",503,"temporary"},
        Reply{"/token",429,"rate limited"}, Reply{"/token",0,"",nullptr,nullptr,"offline"},
        Reply{"/token",200,"bad JSON"}, Reply{"/token",400,R"({"error":"invalid_request"})"}}) {
        const auto saved = contents(path.c_str());
        Fake fake{{reply,{"/token",200,R"({"access_token":"access4","expires_in":180})"},
            {"/userinfo",200,R"({"sub":"synthetic-user"})",nullptr,"access4"},
            {"/client_token",404,"{}"}}};
        opennow::Login login(Fake::request,&fake,path.c_str()); char device[37]{};
        assert(login.restore(device,0)); assert(login.view().state == opennow::State::requesting);
        assert(contents(path.c_str()) == saved);
        login.tick(29); assert(fake.calls == 1);
        login.tick(30); assert(login.view().state == opennow::State::authenticated);
        assert(!std::strcmp(login.cloudToken(),"cloud3"));
    }
    for (const auto& response : {Reply{"/token",503,"temporary"},
        Reply{"/token",200,"malformed"}, Reply{"/token",400,R"({"error":"invalid_request"})"}}) {
        // A rejected client credential does not make a temporary OAuth fallback
        // failure grounds to delete the saved login.
        const auto saved = contents(path.c_str());
        Fake fake{{{"/token",400,R"({"error":"invalid_grant"})"},response}};
        opennow::Login login(Fake::request,&fake,path.c_str()); char device[37]{};
        assert(login.restore(device,0));
        assert(login.view().state == opennow::State::requesting);
        assert(contents(path.c_str()) == saved);
    }
    {
        // Renewal must also occur while the app remains open, and rotated tokens
        // must reach disk even if profile verification then fails temporarily.
        Fake fake{{{"/token",200,R"({"access_token":"access5","expires_in":180})"},
            {"/userinfo",200,R"({"sub":"synthetic-user"})"}, {"/client_token",404,"{}"},
            {"/token",200,R"({"access_token":"access6","refresh_token":"new-refresh","expires_in":180})"},
            {"/userinfo",503,"temporary"},
            {"/token",200,R"({"access_token":"access7","expires_in":180})"},
            {"/userinfo",200,R"({"sub":"synthetic-user"})"}, {"/client_token",404,"{}"}}};
        opennow::Login login(Fake::request,&fake,path.c_str()); char device[37]{};
        assert(login.restore(device,0)); login.tick(119); assert(fake.calls == 3);
        login.tick(120); assert(login.view().state == opennow::State::requesting);
        assert(contents(path.c_str()).find("new-refresh") != std::string::npos);
        login.tick(150); assert(login.view().state == opennow::State::authenticated);
        login.cancel(); assert(access(path.c_str(),F_OK) != 0); assert(!*login.cloudToken());
    }
    signIn(path.c_str());
    {
        Fake fake{{{"/token",400,R"({"error":"invalid_grant"})"},
            {"/token",400,R"({"error":"invalid_grant"})"}}};
        opennow::Login login(Fake::request,&fake,path.c_str()); char device[37]{};
        assert(login.restore(device,0)); assert(login.view().state == opennow::State::failed);
        assert(access(path.c_str(),F_OK) != 0);
    }
    for (const auto& bad : {std::string("truncated"),std::string(66605,'x')}) {
        FILE* file = std::fopen(path.c_str(),"wb"); assert(file);
        assert(std::fwrite(bad.data(),1,bad.size(),file) == bad.size()); std::fclose(file);
        Fake fake; opennow::Login login(Fake::request,&fake,path.c_str()); char device[37]{};
        assert(!login.restore(device,0)); assert(fake.calls == 0); login.cancel();
    }
    {
        auto fake = fresh(); const std::string unavailable = std::string(directory) + "/missing/account.bin";
        opennow::Login login(Fake::request,&fake,unavailable.c_str());
        login.begin(identity,0); login.tick(5);
        assert(login.view().state == opennow::State::authenticated && !login.view().sessionSaved);
    }
    {
        // A stale temporary file or failed replacement cannot destroy the old cache.
        signIn(path.c_str()); const auto saved = contents(path.c_str());
        const std::string temporary = path + ".tmp";
        assert(symlink(path.c_str(),temporary.c_str()) == 0);
        Fake fake{{{"/token",200,R"({"access_token":"updated-access","expires_in":180})"},
            {"/userinfo",200,R"({"sub":"synthetic-user"})"}, {"/client_token",404,"{}"}}};
        opennow::Login login(Fake::request,&fake,path.c_str()); char device[37]{};
        assert(login.restore(device,0));
        assert(login.view().state == opennow::State::authenticated && !login.view().sessionSaved);
        assert(contents(path.c_str()) == saved); login.cancel();
        assert(access(path.c_str(),F_OK) != 0 && lstat(temporary.c_str(),&info) != 0);
    }
    {
        // Providers without renewal credentials can reuse a still-valid access token.
        Fake fake{{{"/device/authorize",200,challenge},
            {"/token",200,R"({"access_token":"access-only","id_token":"cloud-only"})"},
            {"/userinfo",200,R"({"sub":"synthetic-user"})"}, {"/client_token",404,"{}"}}};
        opennow::Login login(Fake::request,&fake,path.c_str()); login.begin(identity,0); login.tick(5);
        assert(login.view().sessionSaved);
    }
    {
        Fake fake{{{"/userinfo",200,R"({"sub":"synthetic-user"})",nullptr,"access-only"},
            {"/client_token",404,"{}"}}};
        opennow::Login login(Fake::request,&fake,path.c_str()); char device[37]{};
        assert(login.restore(device,0)); assert(!std::strcmp(login.cloudToken(),"cloud-only"));
    }
    {
        Fake fake{{{"/userinfo",401,"{}"}}};
        opennow::Login login(Fake::request,&fake,path.c_str()); char device[37]{};
        assert(login.restore(device,0)); assert(login.view().state == opennow::State::failed);
        assert(access(path.c_str(),F_OK) != 0);
    }
    assert(rmdir(directory) == 0);
    std::puts("GFN persistence, restart, renewal, outage and sign-out regressions passed");
}
