#ifndef COMMON_H
#define COMMON_H

#define ERR_MSG_MAX_LEN 512

namespace {
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
