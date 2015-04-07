#ifndef COMMON_H
#define COMMON_H

#define ERR_MSG_MAX_LEN 512

namespace {
    std::string error_msg(const char* method, LONG result) {
        char msg[ERR_MSG_MAX_LEN];
#ifdef _WIN32
        LPVOID lpMsgBuf;
        FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            result,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPTSTR) &lpMsgBuf,
            0, NULL );

        _snprintf(msg,
            ERR_MSG_MAX_LEN,
            "%s error: %s(0x%.8lx)",
            method,
            lpMsgBuf, result);

        LocalFree(lpMsgBuf);
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
