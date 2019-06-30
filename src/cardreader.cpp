#include "cardreader.h"
#include "common.h"

using namespace v8;
using namespace node;

Nan::Persistent<Function> CardReader::constructor;

void CardReader::init(Local<Object> target) {

     // Prepare constructor template
    Local<FunctionTemplate> tpl = Nan::New<FunctionTemplate>(New);
    tpl->SetClassName(Nan::New("CardReader").ToLocalChecked());
    tpl->InstanceTemplate()->SetInternalFieldCount(1);

    // Symbol
    name_symbol.Reset(Nan::New("name").ToLocalChecked());
    connected_symbol.Reset(Nan::New("connected").ToLocalChecked());

    // Prototype
    Nan::SetPrototypeTemplate(tpl, "get_status", Nan::New<FunctionTemplate>(GetStatus));
    Nan::SetPrototypeTemplate(tpl, "_connect", Nan::New<FunctionTemplate>(Connect));
    Nan::SetPrototypeTemplate(tpl, "_disconnect", Nan::New<FunctionTemplate>(Disconnect));
    Nan::SetPrototypeTemplate(tpl, "_transmit", Nan::New<FunctionTemplate>(Transmit));
    Nan::SetPrototypeTemplate(tpl, "_control", Nan::New<FunctionTemplate>(Control));
    Nan::SetPrototypeTemplate(tpl, "close", Nan::New<FunctionTemplate>(Close));

    // PCSCLite constants
    // Share Mode
    Nan::SetPrototypeTemplate(tpl, "SCARD_SHARE_SHARED", Nan::New(SCARD_SHARE_SHARED));
    Nan::SetPrototypeTemplate(tpl, "SCARD_SHARE_EXCLUSIVE", Nan::New(SCARD_SHARE_EXCLUSIVE));
    Nan::SetPrototypeTemplate(tpl, "SCARD_SHARE_DIRECT", Nan::New(SCARD_SHARE_DIRECT));

    // Protocol
    Nan::SetPrototypeTemplate(tpl, "SCARD_PROTOCOL_T0", Nan::New(SCARD_PROTOCOL_T0));
    Nan::SetPrototypeTemplate(tpl, "SCARD_PROTOCOL_T1", Nan::New(SCARD_PROTOCOL_T1));
    Nan::SetPrototypeTemplate(tpl, "SCARD_PROTOCOL_RAW", Nan::New(SCARD_PROTOCOL_RAW));

    //  State
    Nan::SetPrototypeTemplate(tpl, "SCARD_STATE_UNAWARE", Nan::New(SCARD_STATE_UNAWARE));
    Nan::SetPrototypeTemplate(tpl, "SCARD_STATE_IGNORE", Nan::New(SCARD_STATE_IGNORE));
    Nan::SetPrototypeTemplate(tpl, "SCARD_STATE_CHANGED", Nan::New(SCARD_STATE_CHANGED));
    Nan::SetPrototypeTemplate(tpl, "SCARD_STATE_UNKNOWN", Nan::New(SCARD_STATE_UNKNOWN));
    Nan::SetPrototypeTemplate(tpl, "SCARD_STATE_UNAVAILABLE", Nan::New(SCARD_STATE_UNAVAILABLE));
    Nan::SetPrototypeTemplate(tpl, "SCARD_STATE_EMPTY", Nan::New(SCARD_STATE_EMPTY));
    Nan::SetPrototypeTemplate(tpl, "SCARD_STATE_PRESENT", Nan::New(SCARD_STATE_PRESENT));
    Nan::SetPrototypeTemplate(tpl, "SCARD_STATE_ATRMATCH", Nan::New(SCARD_STATE_ATRMATCH));
    Nan::SetPrototypeTemplate(tpl, "SCARD_STATE_EXCLUSIVE", Nan::New(SCARD_STATE_EXCLUSIVE));
    Nan::SetPrototypeTemplate(tpl, "SCARD_STATE_INUSE", Nan::New(SCARD_STATE_INUSE));
    Nan::SetPrototypeTemplate(tpl, "SCARD_STATE_MUTE", Nan::New(SCARD_STATE_MUTE));

    // Disconnect disposition
    Nan::SetPrototypeTemplate(tpl, "SCARD_LEAVE_CARD", Nan::New(SCARD_LEAVE_CARD));
    Nan::SetPrototypeTemplate(tpl, "SCARD_RESET_CARD", Nan::New(SCARD_RESET_CARD));
    Nan::SetPrototypeTemplate(tpl, "SCARD_UNPOWER_CARD", Nan::New(SCARD_UNPOWER_CARD));
    Nan::SetPrototypeTemplate(tpl, "SCARD_EJECT_CARD", Nan::New(SCARD_EJECT_CARD));

    Local<Function> newfunc = Nan::GetFunction(tpl).ToLocalChecked();
    constructor.Reset(newfunc);
    Nan::Set(target, Nan::New("CardReader").ToLocalChecked(), newfunc);
}

CardReader::CardReader(const std::string &reader_name): m_card_context(0),
                                                        m_card_handle(0),
                                                        m_name(reader_name),
                                                        m_state(0) {
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

NAN_METHOD(CardReader::New) {

    Nan::HandleScope scope;

    Nan::Utf8String reader_name(info[0]);
    CardReader* obj = new CardReader(*reader_name);
    obj->Wrap(info.Holder());
    Nan::Set(obj->handle(),
             Nan::New(name_symbol),
             Nan::New(*reader_name).ToLocalChecked());
    Nan::Set(obj->handle(), Nan::New(connected_symbol), Nan::False());

    info.GetReturnValue().Set(info.Holder());
}

NAN_METHOD(CardReader::GetStatus) {

    Nan::HandleScope scope;

    CardReader* obj = Nan::ObjectWrap::Unwrap<CardReader>(info.This());
    Local<Function> cb = Local<Function>::Cast(info[0]);

    AsyncBaton *async_baton = new AsyncBaton();
    async_baton->async.data = async_baton;
    async_baton->callback.Reset(cb);
    async_baton->reader = obj;

    uv_async_init(uv_default_loop(), &async_baton->async, (uv_async_cb)HandleReaderStatusChange);
    int ret = uv_thread_create(&obj->m_status_thread, HandlerFunction, async_baton);
    assert(ret == 0);
}

NAN_METHOD(CardReader::Connect) {

    Nan::HandleScope scope;

    // The second argument is the length of the data to be received
    if (!info[0]->IsUint32()) {
        return Nan::ThrowError("First argument must be an integer");
    }

    if (!info[1]->IsUint32()) {
        return Nan::ThrowError("Second argument must be an integer");
    }

    if (!info[2]->IsFunction()) {
        return Nan::ThrowError("Third argument must be a callback function");
    }

    ConnectInput* ci = new ConnectInput();
    ci->share_mode = Nan::To<uint32_t>(info[0]).ToChecked();
    ci->pref_protocol = Nan::To<uint32_t>(info[1]).ToChecked();
    Local<Function> cb = Local<Function>::Cast(info[2]);

    // This creates our work request, including the libuv struct.
    Baton* baton = new Baton();
    baton->request.data = baton;
    baton->callback.Reset(cb);
    baton->reader = Nan::ObjectWrap::Unwrap<CardReader>(info.This());
    baton->input = ci;

    // Schedule our work request with libuv. Here you can specify the functions
    // that should be executed in the threadpool and back in the main thread
    // after the threadpool function completed.
    int status = uv_queue_work(uv_default_loop(),
                               &baton->request,
                               DoConnect,
                               reinterpret_cast<uv_after_work_cb>(AfterConnect));
    assert(status == 0);


}

NAN_METHOD(CardReader::Disconnect) {

    Nan::HandleScope scope;

    if (!info[0]->IsUint32()) {
        return Nan::ThrowError("First argument must be an integer");
    }

    if (!info[1]->IsFunction()) {
        return Nan::ThrowError("Second argument must be a callback function");
    }

    DWORD disposition = Nan::To<uint32_t>(info[0]).ToChecked();
    Local<Function> cb = Local<Function>::Cast(info[1]);

    // This creates our work request, including the libuv struct.
    Baton* baton = new Baton();
    baton->input = reinterpret_cast<void*>(new DWORD(disposition));
    baton->request.data = baton;
    baton->callback.Reset(cb);
    baton->reader = Nan::ObjectWrap::Unwrap<CardReader>(info.This());

    // Schedule our work request with libuv. Here you can specify the functions
    // that should be executed in the threadpool and back in the main thread
    // after the threadpool function completed.
    int status = uv_queue_work(uv_default_loop(),
                               &baton->request,
                               DoDisconnect,
                               reinterpret_cast<uv_after_work_cb>(AfterDisconnect));
    assert(status == 0);


}

NAN_METHOD(CardReader::Transmit) {

    Nan::HandleScope scope;

    // The first argument is the buffer to be transmitted.
    if (!Buffer::HasInstance(info[0])) {
        return Nan::ThrowError("First argument must be a Buffer");
    }

    // The second argument is the length of the data to be received
    if (!info[1]->IsUint32()) {
        return Nan::ThrowError("Second argument must be an integer");
    }

    // The third argument is the protocol to be used
    if (!info[2]->IsUint32()) {
        return Nan::ThrowError("Third argument must be an integer");
    }

    // The fourth argument is the callback function
    if (!info[3]->IsFunction()) {
        return Nan::ThrowError("Fourth argument must be a callback function");
    }

    Local<Object> buffer_data = Nan::To<Object>(info[0]).ToLocalChecked();
    uint32_t out_len = Nan::To<uint32_t>(info[1]).ToChecked();
    uint32_t protocol = Nan::To<uint32_t>(info[2]).ToChecked();
    Local<Function> cb = Local<Function>::Cast(info[3]);

    // This creates our work request, including the libuv struct.
    Baton* baton = new Baton();
    baton->request.data = baton;
    baton->callback.Reset(cb);
    baton->reader = Nan::ObjectWrap::Unwrap<CardReader>(info.This());
    TransmitInput *ti = new TransmitInput();
    ti->card_protocol = protocol;
    ti->in_data = new unsigned char[Buffer::Length(buffer_data)];
    ti->in_len = Buffer::Length(buffer_data);
    memcpy(ti->in_data, Buffer::Data(buffer_data), ti->in_len);

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


}

NAN_METHOD(CardReader::Control) {

    Nan::HandleScope scope;

    // The first argument is the buffer to be transmitted.
    if (!Buffer::HasInstance(info[0])) {
        return Nan::ThrowError("First argument must be a Buffer");
    }

    // The second argument is the control code to be used
    if (!info[1]->IsUint32()) {
        return Nan::ThrowError("Second argument must be an integer");
    }

    // The third argument is output buffer
    if (!Buffer::HasInstance(info[2])) {
        return Nan::ThrowError("Third argument must be a Buffer");
    }

    // The fourth argument is the callback function
    if (!info[3]->IsFunction()) {
        return Nan::ThrowError("Fourth argument must be a callback function");
    }

    Local<Object> in_buf = Nan::To<Object>(info[0]).ToLocalChecked();
    DWORD control_code = Nan::To<uint32_t>(info[1]).ToChecked();
    Local<Object> out_buf = Nan::To<Object>(info[2]).ToLocalChecked();
    Local<Function> cb = Local<Function>::Cast(info[3]);

    // This creates our work request, including the libuv struct.
    Baton* baton = new Baton();
    baton->request.data = baton;
    baton->callback.Reset(cb);
    baton->reader = Nan::ObjectWrap::Unwrap<CardReader>(info.This());
    ControlInput *ci = new ControlInput();
    ci->control_code = control_code;
    ci->in_data = Buffer::Data(in_buf);
    ci->in_len = Buffer::Length(in_buf);
    ci->out_data = Buffer::Data(out_buf);
    ci->out_len = Buffer::Length(out_buf);
    baton->input = ci;

    // Schedule our work request with libuv. Here you can specify the functions
    // that should be executed in the threadpool and back in the main thread
    // after the threadpool function completed.
    int status = uv_queue_work(uv_default_loop(),
                               &baton->request,
                               DoControl,
                               reinterpret_cast<uv_after_work_cb>(AfterControl));
    assert(status == 0);


}

NAN_METHOD(CardReader::Close) {

    Nan::HandleScope scope;

    LONG result = SCARD_S_SUCCESS;
    CardReader* obj = Nan::ObjectWrap::Unwrap<CardReader>(info.This());

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

    info.GetReturnValue().Set(Nan::New<Number>(result));
}

void CardReader::HandleReaderStatusChange(uv_async_t *handle, int status) {

    Nan::HandleScope scope;

    AsyncBaton* async_baton = static_cast<AsyncBaton*>(handle->data);
    CardReader* reader = async_baton->reader;

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
            const unsigned int argc = 3;
            Local<Value> argv[argc] = {
                Nan::Undefined(), // argument
                Nan::New<Number>(ar->status),
                Nan::CopyBuffer(reinterpret_cast<char*>(ar->atr), ar->atrlen).ToLocalChecked()
            };

            Nan::Call(Nan::Callback(Nan::New(async_baton->callback)), argc, argv);
        }
    } else {
        Local<Value> err = Nan::Error(error_msg("SCardGetStatusChange", ar->result).c_str());
        // Prepare the parameters for the callback function.
        const unsigned int argc = 1;
        Local<Value> argv[argc] = { err };
        Nan::Call(Nan::Callback(Nan::New(async_baton->callback)), argc, argv);
    }

    if (ar->do_exit) {
        uv_close(reinterpret_cast<uv_handle_t*>(&async_baton->async), CloseCallback); // necessary otherwise UV will block

        /* Emit end event */
        Local<Value> argv[1] = {
            Nan::New("_end").ToLocalChecked(), // event name
        };

        Nan::MakeCallback(async_baton->reader->handle(), "emit", 1, argv);
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

    Nan::HandleScope scope;
    Baton* baton = static_cast<Baton*>(req->data);
    ConnectInput *ci = static_cast<ConnectInput*>(baton->input);
    ConnectResult *cr = static_cast<ConnectResult*>(baton->result);

    if (cr->result) {
        Local<Value> err = Nan::Error(error_msg("SCardConnect", cr->result).c_str());
        // Prepare the parameters for the callback function.
        const unsigned argc = 1;
        Local<Value> argv[argc] = { err };
        Nan::Call(Nan::Callback(Nan::New(baton->callback)), argc, argv);
    } else {
        Nan::Set(baton->reader->handle(), Nan::New(connected_symbol), Nan::True());
        const unsigned argc = 2;
        Local<Value> argv[argc] = {
            Nan::Null(),
            Nan::New<Number>(cr->card_protocol)
        };

        Nan::Call(Nan::Callback(Nan::New(baton->callback)), argc, argv);
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

    Nan::HandleScope scope;
    Baton* baton = static_cast<Baton*>(req->data);
    LONG* result = reinterpret_cast<LONG*>(baton->result);

    if (*result) {
        Local<Value> err = Nan::Error(error_msg("SCardDisconnect", *result).c_str());

        // Prepare the parameters for the callback function.
        const unsigned argc = 1;
        Local<Value> argv[argc] = { err };
        Nan::Call(Nan::Callback(Nan::New(baton->callback)), argc, argv);
    } else {
        Nan::Set(baton->reader->handle(), Nan::New(connected_symbol), Nan::False());
        const unsigned argc = 1;
        Local<Value> argv[argc] = {
            Nan::Null()
        };

        Nan::Call(Nan::Callback(Nan::New(baton->callback)), argc, argv);
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

    Nan::HandleScope scope;
    Baton* baton = static_cast<Baton*>(req->data);
    TransmitInput *ti = static_cast<TransmitInput*>(baton->input);
    TransmitResult *tr = static_cast<TransmitResult*>(baton->result);

    if (tr->result) {
        Local<Value> err = Nan::Error(error_msg("SCardTransmit", tr->result).c_str());

        // Prepare the parameters for the callback function.
        const unsigned argc = 1;
        Local<Value> argv[argc] = { err };
        Nan::Call(Nan::Callback(Nan::New(baton->callback)), argc, argv);
    } else {
        const unsigned argc = 2;
        Local<Value> argv[argc] = {
            Nan::Null(),
            Nan::CopyBuffer(reinterpret_cast<char*>(tr->data), tr->len).ToLocalChecked()
        };

        Nan::Call(Nan::Callback(Nan::New(baton->callback)), argc, argv);
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

    Nan::HandleScope scope;
    Baton* baton = static_cast<Baton*>(req->data);
    ControlInput *ci = static_cast<ControlInput*>(baton->input);
    ControlResult *cr = static_cast<ControlResult*>(baton->result);

    if (cr->result) {
        Local<Value> err = Nan::Error(error_msg("SCardControl", cr->result).c_str());

        // Prepare the parameters for the callback function.
        const unsigned argc = 1;
        Local<Value> argv[argc] = { err };
        Nan::Call(Nan::Callback(Nan::New(baton->callback)), argc, argv);
    } else {
        const unsigned argc = 2;
        Local<Value> argv[argc] = {
            Nan::Null(),
            Nan::New<Number>(cr->len)
        };

        Nan::Call(Nan::Callback(Nan::New(baton->callback)), argc, argv);
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
