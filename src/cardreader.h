#ifndef CARDREADER_H
#define CARDREADER_H

#include <napi.h>
#include <uv.h>
#include <node_version.h>
#include <string>
#ifdef __APPLE__
#include <PCSC/winscard.h>
#include <PCSC/wintypes.h>
#else
#include <winscard.h>
#endif

#ifdef _WIN32
#define MAX_ATR_SIZE 33
#endif

class CardReader : public Napi::ObjectWrap<CardReader> {

    // We use a struct to store information about the asynchronous "work request".
    struct Baton {
        Napi::Env env;
        uv_work_t request;
        Napi::FunctionReference callback;
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
        DWORD card_protocol;
        LPBYTE in_data;
        DWORD in_len;
        DWORD out_len;
    };

    struct TransmitResult {
        LONG result;
        LPBYTE data;
        DWORD len;
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
        DWORD len;
    };

    struct AsyncResult {
        LONG result;
        DWORD status;
        BYTE atr[MAX_ATR_SIZE];
        DWORD atrlen;
        bool do_exit;
    };

    struct AsyncBaton {
        Napi::Env env;
        uv_async_t async;
        Napi::FunctionReference callback;
        CardReader *reader;
        AsyncResult *async_result;
    };

    public:

        static void init(Napi::Env env, Napi::Object exports);
        CardReader(const Napi::CallbackInfo& info);
        ~CardReader();

        const SCARDHANDLE& GetHandler() const { return m_card_handle; };

    private:

        Napi::Value New(const Napi::CallbackInfo& info);
        Napi::Value GetStatus(const Napi::CallbackInfo& info);
        Napi::Value Connect(const Napi::CallbackInfo& info);
        Napi::Value Disconnect(const Napi::CallbackInfo& info);
        Napi::Value Transmit(const Napi::CallbackInfo& info);
        Napi::Value Control(const Napi::CallbackInfo& info);
        Napi::Value Close(const Napi::CallbackInfo& info);
        Napi::Value Connected(const Napi::CallbackInfo &info);
        Napi::Value GetName(const Napi::CallbackInfo &info);

        static void HandleReaderStatusChange(uv_async_t *handle);
        static void HandlerFunction(void* arg);
        static void DoConnect(uv_work_t* req);
        static void DoDisconnect(uv_work_t* req);
        static void DoTransmit(uv_work_t* req);
        static void DoControl(uv_work_t* req);
        static void CloseCallback(uv_handle_t *handle);

        static void AfterConnect(uv_work_t* req, int status);
        static void AfterDisconnect(uv_work_t* req, int status);
        static void AfterTransmit(uv_work_t* req, int status);
        static void AfterControl(uv_work_t* req, int status);

    private:

        SCARDCONTEXT m_card_context;
        SCARDCONTEXT m_status_card_context;
        SCARDHANDLE m_card_handle;
        std::string m_name;
        uv_thread_t m_status_thread;
        uv_mutex_t m_mutex;
        uv_cond_t m_cond;
        int m_state;
        bool m_connected;
};

#endif /* CARDREADER_H */
