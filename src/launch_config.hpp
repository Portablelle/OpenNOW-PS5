// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "account_file.hpp"
#include <cctype>
#include <cstdlib>

namespace opennow::launchConfig {
inline int readAge(const char* path,const char* legacyPath) noexcept {
    // Configuration can be uploaded by a different user. Never follow symlinks
    // or wait for a FIFO writer, and bound both the read and accepted input.
    int fd = accountFile::open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if (fd < 0 && errno == ENOENT)
        fd = accountFile::open(legacyPath,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if (fd < 0) return -1;
    char text[32]{};
    std::size_t size = 0;
    bool ok = true;
    while (size < sizeof(text)-1) {
        const auto n = accountFile::read(fd,text+size,sizeof(text)-1-size);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { ok = false; break; }
        if (n == 0) break;
        size += static_cast<std::size_t>(n);
    }
    if (accountFile::close(fd) != 0) ok = false;
    if (!ok || size == sizeof(text)-1 || std::memchr(text,0,size)) return -1;
    char* end = nullptr;
    errno = 0;
    const long age = std::strtol(text,&end,10);
    if (errno || end == text || age < 0 || age > 120) return -1;
    while (*end && std::isspace(static_cast<unsigned char>(*end))) ++end;
    return *end ? -1 : static_cast<int>(age);
}
}
