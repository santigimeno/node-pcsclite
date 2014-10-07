#ifndef COMMON_H
#define COMMON_H

#define ERR_MSG_MAX_LEN 512

namespace {
    std::string error_msg(const char* method, LONG result) {
        char msg[ERR_MSG_MAX_LEN];
#ifdef _WIN32
        _snprintf(msg,
                 ERR_MSG_MAX_LEN,
                 "%s error: 0x%.8lx",
                 method,
                 result);
#else
        snprintf(msg,
                 ERR_MSG_MAX_LEN,
                 "%s error: %s(0x%.8lx)",
                 method,
                 pcsc_stringify_error(result), result);
#endif
        return msg;
    }
}

#endif /* COMMON_H */
