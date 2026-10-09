// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef OPENNOW_PS5
extern "C" {
int sceKernelOpen(const char*,int,unsigned);
std::int64_t sceKernelRead(int,void*,std::size_t);
std::int64_t sceKernelWrite(int,const void*,std::size_t);
int sceKernelClose(int);
int sceKernelFsync(int);
int sceKernelFchmod(int,unsigned);
int sceKernelRename(const char*,const char*);
int sceKernelUnlink(const char*);
int sceKernelMkdir(const char*,unsigned);
}
#endif

namespace opennow::accountFile {
// File operations must bind to libkernel on PS5; the libc shim exports stubs.
#ifdef OPENNOW_PS5
inline std::int64_t checked(std::int64_t result) noexcept {
    if (result < 0) { errno = static_cast<unsigned>(result) & 0xffff; return -1; }
    return result;
}
inline int open(const char* path,int flags,unsigned mode = 0) noexcept {
    return static_cast<int>(checked(sceKernelOpen(path,flags,mode)));
}
inline std::int64_t read(int fd,void* bytes,std::size_t size) noexcept {
    return checked(sceKernelRead(fd,bytes,size));
}
inline std::int64_t write(int fd,const void* bytes,std::size_t size) noexcept {
    return checked(sceKernelWrite(fd,bytes,size));
}
inline int close(int fd) noexcept { return static_cast<int>(checked(sceKernelClose(fd))); }
inline int sync(int fd) noexcept { return static_cast<int>(checked(sceKernelFsync(fd))); }
inline int permissions(int fd,unsigned mode) noexcept {
    return static_cast<int>(checked(sceKernelFchmod(fd,mode)));
}
inline int rename(const char* from,const char* to) noexcept {
    return static_cast<int>(checked(sceKernelRename(from,to)));
}
inline int remove(const char* path) noexcept { return static_cast<int>(checked(sceKernelUnlink(path))); }
inline int makeDirectory(const char* path,unsigned mode) noexcept {
    return static_cast<int>(checked(sceKernelMkdir(path,mode)));
}
#else
inline int open(const char* path,int flags,unsigned mode = 0) noexcept { return ::open(path,flags,mode); }
inline std::int64_t read(int fd,void* bytes,std::size_t size) noexcept { return ::read(fd,bytes,size); }
inline std::int64_t write(int fd,const void* bytes,std::size_t size) noexcept { return ::write(fd,bytes,size); }
inline int close(int fd) noexcept { return ::close(fd); }
inline int sync(int fd) noexcept { return ::fsync(fd); }
inline int permissions(int fd,unsigned mode) noexcept { return ::fchmod(fd,mode); }
inline int rename(const char* from,const char* to) noexcept { return std::rename(from,to); }
inline int remove(const char* path) noexcept { return ::unlink(path); }
inline int makeDirectory(const char* path,unsigned mode) noexcept { return ::mkdir(path,mode); }
#endif
inline bool syncParent(const char* path) noexcept {
    char parent[1024];
    if (std::strlen(path) >= sizeof(parent)) return false;
    std::snprintf(parent,sizeof(parent),"%s",path);
    auto* slash = std::strrchr(parent,'/');
    if (!slash) std::snprintf(parent,sizeof(parent),".");
    else if (slash == parent) slash[1] = 0;
    else *slash = 0;
    const int fd = accountFile::open(parent,O_RDONLY|O_DIRECTORY);
    if (fd < 0) return false;
    const bool ok = accountFile::sync(fd) == 0;
    return accountFile::close(fd) == 0 && ok;
}
inline bool prepareDirectory(const char* path,unsigned mode) noexcept {
    const bool created = accountFile::makeDirectory(path,mode) == 0;
    if (!created && errno != EEXIST) return false;
    const int fd = accountFile::open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
    if (fd < 0) return false;
    // Repair directories created with 0700 and override a restrictive umask.
    const bool ok = accountFile::permissions(fd,mode) == 0;
    const bool closed = accountFile::close(fd) == 0;
    if (created) accountFile::syncParent(path);
    return ok && closed;
}
}
