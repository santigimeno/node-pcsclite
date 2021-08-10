#include <cassert>
#include "cardreader.h"
#include "common.h"

bool isInteger(const Napi::Value& val) {
  if (!val.IsNumber()) {
    return false;
  }

  double d = val.ToNumber().DoubleValue();
  return static_cast<double>(static_cast<int64_t>(d)) == d;
}

void CardReader::init(Napi::Env env, Napi::Object exports) {
  Napi::Function func =
  DefineClass(env,
              "CardReader",
              {
                InstanceAccessor<&CardReader::Connected>("connected"),
                InstanceAccessor<&CardReader::GetName>("name"),
                InstanceMethod("get_status", &CardReader::GetStatus),
                InstanceMethod("_connect", &CardReader::Connect),
                InstanceMethod("_disconnect", &CardReader::Disconnect),
                InstanceMethod("_transmit", &CardReader::Transmit),
                InstanceMethod("_control", &CardReader::Control),
                InstanceMethod("close", &CardReader::Close),
                InstanceValue("SCARD_SHARE_SHARED", Napi::Number::New(env, SCARD_SHARE_SHARED)),
                InstanceValue("SCARD_SHARE_EXCLUSIVE", Napi::Number::New(env, SCARD_SHARE_EXCLUSIVE)),
                InstanceValue("SCARD_SHARE_DIRECT", Napi::Number::New(env, SCARD_SHARE_DIRECT)),
                InstanceValue("SCARD_PROTOCOL_T0", Napi::Number::New(env, SCARD_PROTOCOL_T0)),
                InstanceValue("SCARD_PROTOCOL_T1", Napi::Number::New(env, SCARD_PROTOCOL_T1)),
                InstanceValue("SCARD_PROTOCOL_RAW", Napi::Number::New(env, SCARD_PROTOCOL_RAW)),
                InstanceValue("SCARD_STATE_UNAWARE", Napi::Number::New(env, SCARD_STATE_UNAWARE)),
                InstanceValue("SCARD_STATE_IGNORE", Napi::Number::New(env, SCARD_STATE_IGNORE)),
                InstanceValue("SCARD_STATE_CHANGED", Napi::Number::New(env, SCARD_STATE_CHANGED)),
                InstanceValue("SCARD_STATE_UNKNOWN", Napi::Number::New(env, SCARD_STATE_UNKNOWN)),
                InstanceValue("SCARD_STATE_UNAVAILABLE", Napi::Number::New(env, SCARD_STATE_UNAVAILABLE)),
                InstanceValue("SCARD_STATE_EMPTY", Napi::Number::New(env, SCARD_STATE_EMPTY)),
                InstanceValue("SCARD_STATE_PRESENT", Napi::Number::New(env, SCARD_STATE_PRESENT)),
                InstanceValue("SCARD_STATE_ATRMATCH", Napi::Number::New(env, SCARD_STATE_ATRMATCH)),
                InstanceValue("SCARD_STATE_EXCLUSIVE", Napi::Number::New(env, SCARD_STATE_EXCLUSIVE)),
                InstanceValue("SCARD_STATE_INUSE", Napi::Number::New(env, SCARD_STATE_INUSE)),
                InstanceValue("SCARD_STATE_MUTE", Napi::Number::New(env, SCARD_STATE_MUTE)),
                InstanceValue("SCARD_LEAVE_CARD", Napi::Number::New(env, SCARD_LEAVE_CARD)),
                InstanceValue("SCARD_RESET_CARD", Napi::Number::New(env, SCARD_RESET_CARD)),
                InstanceValue("SCARD_UNPOWER_CARD", Napi::Number::New(env, SCARD_UNPOWER_CARD)),
                InstanceValue("SCARD_EJECT_CARD", Napi::Number::New(env, SCARD_EJECT_CARD))
              });

  Napi::FunctionReference* constructor = new Napi::FunctionReference();
  *constructor = Napi::Persistent(func);
  env.SetInstanceData(constructor);

  exports.Set("CardReader", func);
}

CardReader::CardReader(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<CardReader>(info),
      m_card_context(0),
      m_card_handle(0),
      m_state(0),
      m_connected(false) {

    Napi::Env env = info.Env();
    int length = info.Length();
    if (length <= 0 || !info[0].IsString()) {
        Napi::TypeError::New(env, "String expected").ThrowAsJavaScriptException();
        return;
    }

    m_name = info[0].As<Napi::String>();
    assert(uv_mutex_init(&m_mutex) == 0);
    assert(uv_cond_init(&m_cond) == 0);
}

CardReader::~CardReader() {
    if (m_status_thread) {
        SCardCancel(m_card_context);
        assert(uv_thread_join(&m_status_thread) == 0);
    }

    if (m_card_context) {
        SCardReleaseContext(m_card_context);
    }

    uv_cond_destroy(&m_cond);
    uv_mutex_destroy(&m_mutex);
}

Napi::Value CardReader::GetStatus(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    CardReader* obj = this;
    Napi::Function cb = info[0].As<Napi::Function>();

    AsyncBaton *async_baton = new AsyncBaton{ env, {}, {} , nullptr, nullptr };
    async_baton->async.data = async_baton;
    async_baton->callback.Reset(cb);
    async_baton->reader = obj;

    uv_async_init(uv_default_loop(), &async_baton->async, (uv_async_cb)HandleReaderStatusChange);
    int ret = uv_thread_create(&obj->m_status_thread, HandlerFunction, async_baton);
    assert(ret == 0);
    return env.Undefined();
}

Napi::Value CardReader::Connect(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    // The second argument is the length of the data to be received
    if (!isInteger(info[0])) {
        Napi::Error::New(env, "First argument must be an integer").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!isInteger(info[1])) {
        Napi::Error::New(env, "Second argument must be an integer").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!info[2].IsFunction()) {
        Napi::Error::New(env, "Third argument must be a callback function").ThrowAsJavaScriptException();
        return env.Null();
    }

    ConnectInput* ci = new ConnectInput();
    ci->share_mode = info[0].As<Napi::Number>().Uint32Value();
    ci->pref_protocol = info[1].As<Napi::Number>().Uint32Value();
    Napi::Function cb = info[2].As<Napi::Function>();

    // This creates our work request, including the libuv struct.
    Baton* baton = new Baton{ env, {}, {}, nullptr, nullptr, nullptr };
    baton->request.data = baton;
    baton->callback.Reset(cb);
    baton->reader = this;
    baton->input = ci;

    // Schedule our work request with libuv. Here you can specify the functions
    // that should be executed in the threadpool and back in the main thread
    // after the threadpool function completed.
    int status = uv_queue_work(uv_default_loop(),
                               &baton->request,
                               DoConnect,
                               reinterpret_cast<uv_after_work_cb>(AfterConnect));
    assert(status == 0);

    return env.Undefined();
}

Napi::Value CardReader::Disconnect(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (!isInteger(info[0])) {
        Napi::Error::New(env, "First argument must be an integer").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (!isInteger(info[1])) {
        Napi::Error::New(env, "Second argument must be a callback function").ThrowAsJavaScriptException();
        return env.Null();
    }

    DWORD disposition = info[0].As<Napi::Number>().Uint32Value();
    Napi::Function cb = info[1].As<Napi::Function>();

    // This creates our work request, including the libuv struct.
    Baton* baton = new Baton{ env, {}, {}, nullptr, nullptr, nullptr };
    baton->input = reinterpret_cast<void*>(new DWORD(disposition));
    baton->request.data = baton;
    baton->callback = Napi::Persistent(cb);
    baton->reader = this;

    // Schedule our work request with libuv. Here you can specify the functions
    // that should be executed in the threadpool and back in the main thread
    // after the threadpool function completed.
    int status = uv_queue_work(uv_default_loop(),
                               &baton->request,
                               DoDisconnect,
                               reinterpret_cast<uv_after_work_cb>(AfterDisconnect));
    assert(status == 0);

    return env.Undefined();
}

Napi::Value CardReader::Transmit(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    // The first argument is the buffer to be transmitted.
    if (!info[0].IsBuffer()) {
        Napi::Error::New(env, "First argument must be a Buffer").ThrowAsJavaScriptException();
        return env.Null();
    }

    // The second argument is the length of the data to be received
    if (!isInteger(info[1])) {
        Napi::Error::New(env, "Second argument must be an integer").ThrowAsJavaScriptException();
        return env.Null();
    }

    // The third argument is the protocol to be used
    if (!isInteger(info[2])) {
        Napi::Error::New(env, "Third argument must be an integer").ThrowAsJavaScriptException();
        return env.Null();
    }

    // The fourth argument is the callback function
    if (!info[3].IsFunction()) {
        Napi::Error::New(env, "Fourth argument must be a callback function").ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Buffer<unsigned char> buffer_data = info[0].As<Napi::Buffer<unsigned char>>();
    uint32_t out_len = info[1].As<Napi::Number>().Uint32Value();
    uint32_t protocol = info[2].As<Napi::Number>().Uint32Value();
    Napi::Function cb = info[3].As<Napi::Function>();

    // This creates our work request, including the libuv struct.
    Baton* baton = new Baton{ env, {}, {}, nullptr, nullptr, nullptr };
    baton->request.data = baton;
    baton->callback.Reset(cb);
    baton->reader = this;
    TransmitInput *ti = new TransmitInput();
    ti->card_protocol = protocol;
    ti->in_data = new unsigned char[buffer_data.Length()];
    ti->in_len = buffer_data.Length();
    memcpy(ti->in_data, buffer_data.Data(), ti->in_len);

    ti->out_len = out_len;
    baton->input = ti;

    // Schedule our work request with libuv. Here you can specify the functions
    // that should be executed in the threadpool and back in the main thread
    // after the threadpool function completed.
    int status = uv_queue_work(uv_default_loop(),
                               &baton->request,
                               DoTransmit,
                               reinterpret_cast<uv_after_work_cb>(AfterTransmit));
    assert(status == 0);

    return env.Undefined();
}

Napi::Value CardReader::Control(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    // The first argument is the buffer to be transmitted.
    if (!info[0].IsBuffer()) {
        Napi::Error::New(env, "First argument must be a Buffer").ThrowAsJavaScriptException();
        return env.Null();
    }

    // The second argument is the control code to be used
    if (!isInteger(info[1])) {
        Napi::Error::New(env, "Second argument must be an integer").ThrowAsJavaScriptException();
        return env.Null();
    }

    // The third argument is output buffer
    if (!info[2].IsBuffer()) {
        Napi::Error::New(env, "Third argument must be a Buffer").ThrowAsJavaScriptException();
        return env.Null();
    }

    // The fourth argument is the callback function
    if (!info[3].IsFunction()) {
        Napi::Error::New(env, "Fourth argument must be a callback function").ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Buffer<unsigned char> in_buf = info[0].As<Napi::Buffer<unsigned char>>();
    DWORD control_code = info[1].As<Napi::Number>().Uint32Value();
    Napi::Buffer<unsigned char> out_buf = info[2].As<Napi::Buffer<unsigned char>>();
    Napi::Function cb = info[3].As<Napi::Function>();

    // This creates our work request, including the libuv struct.
    Baton* baton = new Baton{ env, {}, {}, nullptr, nullptr, nullptr };
    baton->request.data = baton;
    baton->callback = Napi::Persistent(cb);
    baton->reader = this;
    ControlInput *ci = new ControlInput();
    ci->control_code = control_code;
    ci->in_data = in_buf.Data();
    ci->in_len = in_buf.Length();
    ci->out_data = out_buf.Data();
    ci->out_len = out_buf.Length();
    baton->input = ci;

    // Schedule our work request with libuv. Here you can specify the functions
    // that should be executed in the threadpool and back in the main thread
    // after the threadpool function completed.
    int status = uv_queue_work(uv_default_loop(),
                               &baton->request,
                               DoControl,
                               reinterpret_cast<uv_after_work_cb>(AfterControl));
    assert(status == 0);

    return env.Undefined();
}

Napi::Value CardReader::Close(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    LONG result = SCARD_S_SUCCESS;
    CardReader* obj = this;

    if (obj->m_status_thread) {
        uv_mutex_lock(&obj->m_mutex);
        if (obj->m_state == 0) {
            int ret;
            int times = 0;
            obj->m_state = 1;
            do {
                result = SCardCancel(obj->m_status_card_context);
                ret = uv_cond_timedwait(&obj->m_cond, &obj->m_mutex, 10000000);
            } while ((ret != 0) && (++ times < 5));
        }

        uv_mutex_unlock(&obj->m_mutex);
        assert(uv_thread_join(&obj->m_status_thread) == 0);
        obj->m_status_thread = 0;
    }

    return Napi::Number::New(env, result);
}

Napi::Value CardReader::Connected(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    return Napi::Boolean::New(env, this->m_connected);
}

Napi::Value CardReader::GetName(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    return Napi::String::New(env, this->m_name);
}

void CardReader::HandleReaderStatusChange(uv_async_t *handle) {

    AsyncBaton* async_baton = static_cast<AsyncBaton*>(handle->data);
    CardReader* reader = async_baton->reader;
    Napi::Env env = async_baton->env;
    if (reader->m_status_thread) {
        uv_mutex_lock(&reader->m_mutex);
    }

    AsyncResult* ar = async_baton->async_result;

    if (reader->m_state == 1) {
        // Swallow events : Listening thread was cancelled by user.
    } else if ((ar->result == SCARD_S_SUCCESS) ||
               (ar->result == (LONG)SCARD_E_NO_READERS_AVAILABLE) ||
               (ar->result == (LONG)SCARD_E_UNKNOWN_READER)) { // Card reader was unplugged, it's not an error
        if (ar->status != 0) {
            Napi::HandleScope scope(env);
            async_baton->callback.Call({ env.Undefined(), Napi::Number::New(env, ar->status),Napi::Buffer<BYTE>::Copy(env, ar->atr, ar->atrlen) });
        }
    } else {
        Napi::HandleScope scope(env);
        async_baton->callback.Call({ Napi::Error::New(env, error_msg("SCardGetStatusChange", ar->result)).Value() });
    }

    if (ar->do_exit) {
        Napi::HandleScope scope(env);
        uv_close(reinterpret_cast<uv_handle_t*>(&async_baton->async), CloseCallback); // necessary otherwise UV will block

        auto res = reader->Value();
        Napi::AsyncContext context(env, "pcsclite_reader_status_change", res);
        Napi::Function emit = res.Get("emit").As<Napi::Function>();
        emit.MakeCallback(res, { Napi::String::New(env, "_end") });
    }

    if (reader->m_status_thread) {
        uv_mutex_unlock(&reader->m_mutex);
    }
}

void CardReader::HandlerFunction(void* arg) {

    AsyncBaton* async_baton = static_cast<AsyncBaton*>(arg);
    CardReader* reader = async_baton->reader;
    async_baton->async_result = new AsyncResult();
    async_baton->async_result->do_exit = false;

    LONG result = SCardEstablishContext(SCARD_SCOPE_SYSTEM, NULL, NULL, &reader->m_status_card_context);

    SCARD_READERSTATE card_reader_state = SCARD_READERSTATE();
    card_reader_state.szReader = reader->m_name.c_str();
    card_reader_state.dwCurrentState = SCARD_STATE_UNAWARE;

    while (!reader->m_state) {
        result = SCardGetStatusChange(reader->m_status_card_context, INFINITE, &card_reader_state, 1);
        uv_mutex_lock(&reader->m_mutex);
        if (reader->m_state == 1) {
            // Exit requested by user. Notify close method about SCardStatusChange was interrupted.
            uv_cond_signal(&reader->m_cond);
        } else if (result != (LONG)SCARD_S_SUCCESS) {
            // Exit this loop due to errors
            reader->m_state = 2;
        }

        async_baton->async_result->do_exit = (reader->m_state != 0);
        async_baton->async_result->result = result;
        if (card_reader_state.dwEventState == card_reader_state.dwCurrentState) {
            async_baton->async_result->status = 0;
        } else {
            async_baton->async_result->status = card_reader_state.dwEventState;
        }
        memcpy(async_baton->async_result->atr, card_reader_state.rgbAtr, card_reader_state.cbAtr);
        async_baton->async_result->atrlen = card_reader_state.cbAtr;

        uv_mutex_unlock(&reader->m_mutex);

        uv_async_send(&async_baton->async);
        card_reader_state.dwCurrentState = card_reader_state.dwEventState;
    }

    // Exit flag set in keepwatching and handled in following uv_async_send
}

void CardReader::DoConnect(uv_work_t* req) {

    Baton* baton = static_cast<Baton*>(req->data);
    ConnectInput *ci = static_cast<ConnectInput*>(baton->input);

    DWORD card_protocol;
    LONG result = SCARD_S_SUCCESS;
    CardReader* obj = baton->reader;

    /* Lock mutex */
    uv_mutex_lock(&obj->m_mutex);
    /* Is context established */
    if (!obj->m_card_context) {
        result = SCardEstablishContext(SCARD_SCOPE_SYSTEM, NULL, NULL, &obj->m_card_context);
    }

    /* Connect */
    if (result == SCARD_S_SUCCESS) {
        result = SCardConnect(obj->m_card_context,
                              obj->m_name.c_str(),
                              ci->share_mode,
                              ci->pref_protocol,
                              &obj->m_card_handle,
                              &card_protocol);
    }

    /* Unlock the mutex */
    uv_mutex_unlock(&obj->m_mutex);

    ConnectResult *cr = new ConnectResult();
    cr->result = result;
    if (!result) {
        cr->card_protocol = card_protocol;
    }

    baton->result = cr;
}

void CardReader::AfterConnect(uv_work_t* req, int status) {

    Baton* baton = static_cast<Baton*>(req->data);
    ConnectInput *ci = static_cast<ConnectInput*>(baton->input);
    ConnectResult *cr = static_cast<ConnectResult*>(baton->result);
    Napi::Env env = baton->env;
    Napi::HandleScope scope(env);
    if (cr->result) {
        baton->callback.Call({ Napi::Error::New(env, error_msg("SCardConnect", cr->result)).Value() });
    } else {
        baton->reader->m_connected = true;
        baton->callback.Call({ env.Null(), Napi::Number::New(env, cr->card_protocol) });
    }

    // The callback is a permanent handle, so we have to dispose of it manually.
    baton->callback.Reset();
    delete ci;
    delete cr;
    delete baton;
}

void CardReader::DoDisconnect(uv_work_t* req) {

    Baton* baton = static_cast<Baton*>(req->data);
    DWORD* disposition = reinterpret_cast<DWORD*>(baton->input);

    LONG result = SCARD_S_SUCCESS;
    CardReader* obj = baton->reader;

    /* Lock mutex */
    uv_mutex_lock(&obj->m_mutex);
    /* Connect */
    if (obj->m_card_handle) {
        result = SCardDisconnect(obj->m_card_handle, *disposition);
        if (result == SCARD_S_SUCCESS) {
            obj->m_card_handle = 0;
        }
    }

    /* Unlock the mutex */
    uv_mutex_unlock(&obj->m_mutex);

    baton->result = reinterpret_cast<void*>(new LONG(result));
}

void CardReader::AfterDisconnect(uv_work_t* req, int status) {
    Baton* baton = static_cast<Baton*>(req->data);
    LONG* result = reinterpret_cast<LONG*>(baton->result);
    Napi::Env env = baton->env;
    Napi::HandleScope scope(env);
    if (*result) {
        baton->callback.Call({ Napi::Error::New(env, error_msg("SCardDisconnect", *result)).Value() });
    } else {
        baton->reader->m_connected = false;
        baton->callback.Call({ env.Null() });
    }

    // The callback is a permanent handle, so we have to dispose of it manually.
    baton->callback.Reset();
    DWORD* disposition = reinterpret_cast<DWORD*>(baton->input);
    delete disposition;
    delete result;
    delete baton;
}

void CardReader::DoTransmit(uv_work_t* req) {

    Baton* baton = static_cast<Baton*>(req->data);
    TransmitInput *ti = static_cast<TransmitInput*>(baton->input);
    CardReader* obj = baton->reader;

    TransmitResult *tr = new TransmitResult();
    tr->data = new unsigned char[ti->out_len];
    tr->len = ti->out_len;
    LONG result = SCARD_E_INVALID_HANDLE;

    /* Lock mutex */
    uv_mutex_lock(&obj->m_mutex);
    /* Connected? */
    // Under windows, SCARD_IO_REQUEST param must be NULL. Else error RPC_X_BAD_STUB_DATA / 0x06F7 on each call.
    if (obj->m_card_handle) {
        SCARD_IO_REQUEST send_pci = { ti->card_protocol, sizeof(SCARD_IO_REQUEST) };
        result = SCardTransmit(obj->m_card_handle, &send_pci, ti->in_data, ti->in_len,
                               NULL, tr->data, &tr->len);
    }

    /* Unlock the mutex */
    uv_mutex_unlock(&obj->m_mutex);

    tr->result = result;

    baton->result = tr;
}

void CardReader::AfterTransmit(uv_work_t* req, int status) {
    Baton* baton = static_cast<Baton*>(req->data);
    TransmitInput *ti = static_cast<TransmitInput*>(baton->input);
    TransmitResult *tr = static_cast<TransmitResult*>(baton->result);
    Napi::Env env = baton->env;
    Napi::HandleScope scope(env);
    if (tr->result) {
        baton->callback.Call({ Napi::Error::New(env, error_msg("SCardTransmit", tr->result)).Value() });
    } else {
        baton->callback.Call({ env.Null(), Napi::Buffer<unsigned char>::Copy(env, tr->data, tr->len) });
    }


    // The callback is a permanent handle, so we have to dispose of it manually.
    baton->callback.Reset();
    delete [] ti->in_data;
    delete ti;
    delete [] tr->data;
    delete tr;
    delete baton;
}

void CardReader::DoControl(uv_work_t* req) {

    Baton* baton = static_cast<Baton*>(req->data);
    ControlInput *ci = static_cast<ControlInput*>(baton->input);
    CardReader* obj = baton->reader;

    ControlResult *cr = new ControlResult();
    LONG result = SCARD_E_INVALID_HANDLE;

    /* Lock mutex */
    uv_mutex_lock(&obj->m_mutex);
    /* Connected? */
    if (obj->m_card_handle) {
        result = SCardControl(obj->m_card_handle,
                              ci->control_code,
                              ci->in_data,
                              ci->in_len,
                              ci->out_data,
                              ci->out_len,
                              &cr->len);
    }

    /* Unlock the mutex */
    uv_mutex_unlock(&obj->m_mutex);

    cr->result = result;

    baton->result = cr;
}

void CardReader::AfterControl(uv_work_t* req, int status) {

    Baton* baton = static_cast<Baton*>(req->data);
    ControlInput *ci = static_cast<ControlInput*>(baton->input);
    ControlResult *cr = static_cast<ControlResult*>(baton->result);
    Napi::Env env = baton->env;
    Napi::HandleScope scope(env);
    if (cr->result) {
        baton->callback.Call({ Napi::Error::New(env, error_msg("SCardControl", cr->result)).Value() });
    } else {
        baton->callback.Call({ env.Null(), Napi::Number::New(env, cr->len) });
    }


    // The callback is a permanent handle, so we have to dispose of it manually.
    baton->callback.Reset();
    delete ci;
    delete cr;
    delete baton;
}

void CardReader::CloseCallback(uv_handle_t *handle) {

    /* cleanup process */
    AsyncBaton* async_baton = static_cast<AsyncBaton*>(handle->data);
    AsyncResult* ar = async_baton->async_result;
    delete ar;
    async_baton->callback.Reset();
    SCardReleaseContext(async_baton->reader->m_status_card_context);
    delete async_baton;
}
