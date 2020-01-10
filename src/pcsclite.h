#ifndef PCSCLITE_H
#define PCSCLITE_H

#include <nan.h>
#ifdef __APPLE__
#include <PCSC/winscard.h>
#include <PCSC/wintypes.h>
#else
#include <winscard.h>
#endif

class PCSCLite: public Nan::ObjectWrap {

    struct AsyncResult {
        LONG result;
        LPSTR readers_name;
        DWORD readers_name_length;
        bool do_exit;
        std::string err_msg;
    };

    struct AsyncBaton {
        uv_async_t async;
        Nan::Persistent<v8::Function> callback;
        PCSCLite *pcsclite;
        AsyncResult *async_result;
    };

    public:

        static void init(v8::Local<v8::Object> target);

    private:

        PCSCLite();

        ~PCSCLite();

        static Nan::Persistent<v8::Function> constructor;
        static NAN_METHOD(New);
        static NAN_METHOD(Start);
        static NAN_METHOD(Close);

        static void HandleReaderStatusChange(uv_async_t *handle, int status);
        static void HandlerFunction(void* arg);
        static void CloseCallback(uv_handle_t *handle);

        LONG get_card_readers(PCSCLite* pcsclite, AsyncResult* async_result);

    private:

        SCARDCONTEXT m_card_context;
        SCARD_READERSTATE m_card_reader_state;
        uv_thread_t m_status_thread;
        uv_mutex_t m_mutex;
        uv_cond_t m_cond;
        bool m_pnp;
        int m_state;
};

#endif /* PCSCLITE_H */
