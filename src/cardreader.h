#ifndef CARDREADER_H
#define CARDREADER_H

#include <nan.h>
#include <node_version.h>
#include <string>
#include <pthread.h>
#ifdef __APPLE__
#include <PCSC/winscard.h>
#include <PCSC/wintypes.h>
#else
#include <winscard.h>
#endif

static v8::Persistent<v8::String> name_symbol;
static v8::Persistent<v8::String> connected_symbol;

class CardReader: public node::ObjectWrap {

    // We use a struct to store information about the asynchronous "work request".
    struct Baton {
        uv_work_t request;
        v8::Persistent<v8::Function> callback;
        CardReader *reader;
        void *input;
        void *result;
    };

    struct ConnectInput {
        DWORD share_mode;
        DWORD pref_protocol;
    };

    struct ConnectResult {
        LONG result;
        DWORD card_protocol;
    };

    struct TransmitInput {
        uint32_t card_protocol;
        unsigned char *in_data;
        unsigned long in_len;
        unsigned long out_len;
    };

    struct TransmitResult {
        LONG result;
        unsigned char *data;
        unsigned long len;
    };

    struct ControlInput {
        DWORD control_code;
        LPCVOID in_data;
        DWORD in_len;
        LPVOID out_data;
        DWORD out_len;
    };

    struct ControlResult {
        LONG result;
        unsigned long len;
    };

    struct AsyncResult {
        LONG result;
        unsigned long status;
        unsigned char atr[MAX_ATR_SIZE];
        unsigned long atrlen;
        bool do_exit;
    };

    struct AsyncBaton {
        uv_async_t async;
        v8::Persistent<v8::Function> callback;
        CardReader *reader;
        AsyncResult *async_result;
    };

    public:

        static void init(v8::Handle<v8::Object> target);


    private:

        CardReader(const std::string &reader_name);

        ~CardReader();

        static v8::Persistent<v8::Function> constructor;

        static NAN_METHOD(New);
        static NAN_METHOD(GetStatus);
        static NAN_METHOD(Connect);
        static NAN_METHOD(Disconnect);
        static NAN_METHOD(Transmit);
        static NAN_METHOD(Control);
        static NAN_METHOD(Close);

        static void HandleReaderStatusChange(uv_async_t *handle, int status);
        static void* HandlerFunction(void* arg);
        static void DoConnect(uv_work_t* req);
        static void DoDisconnect(uv_work_t* req);
        static void DoTransmit(uv_work_t* req);
        static void DoControl(uv_work_t* req);
        static void CloseCallback(uv_handle_t *handle);

        static void AfterConnect(uv_work_t* req, int status);
        static void AfterDisconnect(uv_work_t* req, int status);
        static void AfterTransmit(uv_work_t* req, int status);
        static void AfterControl(uv_work_t* req, int status);

        static v8::Handle<v8::Value> CreateBufferInstance(char* data, unsigned long size);

    private:

        SCARDCONTEXT m_card_context;
        SCARDCONTEXT m_status_card_context;
        SCARDHANDLE m_card_handle;
        std::string m_name;
        pthread_t m_status_thread;
        pthread_mutex_t m_mutex;
};

#endif /* CARDREADER_H */
