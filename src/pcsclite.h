#ifndef PCSCLITE_H
#define PCSCLITE_H

#include <nan.h>

#include <winscard.h>

class PCSCLite: public node::ObjectWrap {

    struct AsyncResult {
        LONG result;
        LPSTR readers_name;
        DWORD readers_name_length;
        bool do_exit;
    };

    struct AsyncBaton {
        uv_async_t async;
        v8::Persistent<v8::Function> callback;
        PCSCLite *pcsclite;
        AsyncResult *async_result;
    };

    public:

        static void init(v8::Handle<v8::Object> target);

    private:

        PCSCLite();

        ~PCSCLite();

        static v8::Persistent<v8::Function> constructor;
        static NAN_METHOD(New);
        static NAN_METHOD(Start);
        static NAN_METHOD(Close);

        static void HandleReaderStatusChange(uv_async_t *handle, int status);
        static void* HandlerFunction(void* arg);
        static void CloseCallback(uv_handle_t *handle);

        LONG get_card_readers(PCSCLite* pcsclite, AsyncResult* async_result);

    private:

        SCARDCONTEXT m_card_context;
        pthread_t m_status_thread;
        pthread_mutex_t m_mutex;
};

#endif /* PCSCLITE_H */
