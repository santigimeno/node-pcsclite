#ifndef COMMON_H
#define COMMON_H

#define ERR_MSG_MAX_LEN 512

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#define Sleep(x) usleep((x)*1000)
#endif

#ifdef _WIN32
#define snprintf _snprintf
#endif

namespace {

#ifdef _WIN32

    const char *pcsc_stringify_error(const LONG) {
        return "";
    }
#endif

    std::string error_msg(const char* method, LONG result) {
        char msg[ERR_MSG_MAX_LEN];
        snprintf(msg,
                 ERR_MSG_MAX_LEN,
                 "%s error: %s(0x%.8lx)",
                 method,
                 pcsc_stringify_error(result), result);
        return msg;
    }
}

#endif /* COMMON_H */
