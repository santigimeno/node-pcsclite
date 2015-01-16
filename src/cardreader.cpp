#include "cardreader.h"
#include "common.h"

using namespace v8;
using namespace node;

Persistent<Function> CardReader::constructor;

void CardReader::init(Handle<Object> target) {

     // Prepare constructor template
    Local<FunctionTemplate> tpl = NanNew<FunctionTemplate>(New);
    tpl->SetClassName(NanNew("CardReader"));
    tpl->InstanceTemplate()->SetInternalFieldCount(1);

    // Symbol
    NanAssignPersistent(name_symbol, NanNew("name"));
    NanAssignPersistent(connected_symbol, NanNew("connected"));

    // Prototype
    NanSetPrototypeTemplate(tpl, "get_status", NanNew<FunctionTemplate>(GetStatus));
    NanSetPrototypeTemplate(tpl, "_connect", NanNew<FunctionTemplate>(Connect));
    NanSetPrototypeTemplate(tpl, "_disconnect", NanNew<FunctionTemplate>(Disconnect));
    NanSetPrototypeTemplate(tpl, "_transmit", NanNew<FunctionTemplate>(Transmit));
    NanSetPrototypeTemplate(tpl, "_control", NanNew<FunctionTemplate>(Control));
    NanSetPrototypeTemplate(tpl, "close", NanNew<FunctionTemplate>(Close));

    // PCSCLite constants
    // Share Mode
    NanSetPrototypeTemplate(tpl, "SCARD_SHARE_SHARED", NanNew(SCARD_SHARE_SHARED));
    NanSetPrototypeTemplate(tpl, "SCARD_SHARE_EXCLUSIVE", NanNew(SCARD_SHARE_EXCLUSIVE));
    NanSetPrototypeTemplate(tpl, "SCARD_SHARE_DIRECT", NanNew(SCARD_SHARE_DIRECT));

    // Protocol
    NanSetPrototypeTemplate(tpl, "SCARD_PROTOCOL_T0", NanNew(SCARD_PROTOCOL_T0));
    NanSetPrototypeTemplate(tpl, "SCARD_PROTOCOL_T1", NanNew(SCARD_PROTOCOL_T1));
    NanSetPrototypeTemplate(tpl, "SCARD_PROTOCOL_RAW", NanNew(SCARD_PROTOCOL_RAW));

    //  State
    NanSetPrototypeTemplate(tpl, "SCARD_STATE_UNAWARE", NanNew(SCARD_STATE_UNAWARE));
    NanSetPrototypeTemplate(tpl, "SCARD_STATE_IGNORE", NanNew(SCARD_STATE_IGNORE));
    NanSetPrototypeTemplate(tpl, "SCARD_STATE_CHANGED", NanNew(SCARD_STATE_CHANGED));
    NanSetPrototypeTemplate(tpl, "SCARD_STATE_UNKNOWN", NanNew(SCARD_STATE_UNKNOWN));
    NanSetPrototypeTemplate(tpl, "SCARD_STATE_UNAVAILABLE", NanNew(SCARD_STATE_UNAVAILABLE));
    NanSetPrototypeTemplate(tpl, "SCARD_STATE_EMPTY", NanNew(SCARD_STATE_EMPTY));
    NanSetPrototypeTemplate(tpl, "SCARD_STATE_PRESENT", NanNew(SCARD_STATE_PRESENT));
    NanSetPrototypeTemplate(tpl, "SCARD_STATE_ATRMATCH", NanNew(SCARD_STATE_ATRMATCH));
    NanSetPrototypeTemplate(tpl, "SCARD_STATE_EXCLUSIVE", NanNew(SCARD_STATE_EXCLUSIVE));
    NanSetPrototypeTemplate(tpl, "SCARD_STATE_INUSE", NanNew(SCARD_STATE_INUSE));
    NanSetPrototypeTemplate(tpl, "SCARD_STATE_MUTE", NanNew(SCARD_STATE_MUTE));

    // Disconnect disposition
    NanSetPrototypeTemplate(tpl, "SCARD_LEAVE_CARD", NanNew(SCARD_LEAVE_CARD));
    NanSetPrototypeTemplate(tpl, "SCARD_RESET_CARD", NanNew(SCARD_RESET_CARD));
    NanSetPrototypeTemplate(tpl, "SCARD_STATE_CHANGED", NanNew(SCARD_STATE_CHANGED));
    NanSetPrototypeTemplate(tpl, "SCARD_UNPOWER_CARD", NanNew(SCARD_UNPOWER_CARD));

    NanAssignPersistent<Function>(constructor, tpl->GetFunction());
    target->Set(NanNew("CardReader"), tpl->GetFunction());
}

CardReader::CardReader(const std::string &reader_name): m_card_context(0),
                                                        m_card_handle(0),
                                                        m_name(reader_name) {
    pthread_mutex_init(&m_mutex, NULL);
}

CardReader::~CardReader() {

    if (m_card_context) {
        SCardReleaseContext(m_card_context);
    }

    if (m_status_card_context) {
        SCardCancel(m_status_card_context);
    }

    pthread_mutex_destroy(&m_mutex);
}

NAN_METHOD(CardReader::New) {

    NanScope();

    v8::String::Utf8Value reader_name(args[0]->ToString());
    CardReader* obj = new CardReader(*reader_name);
    obj->Wrap(args.Holder());
    NanObjectWrapHandle(obj)->Set(NanNew(name_symbol), args[0]->ToString());
    NanObjectWrapHandle(obj)->Set(NanNew(connected_symbol), NanFalse());

    NanReturnValue(args.Holder());
}

NAN_METHOD(CardReader::GetStatus) {

    NanScope();

    CardReader* obj = ObjectWrap::Unwrap<CardReader>(args.This());
    Local<Function> cb = Local<Function>::Cast(args[0]);

    AsyncBaton *async_baton = new AsyncBaton();
    async_baton->async.data = async_baton;
    NanAssignPersistent(async_baton->callback, cb);
    async_baton->reader = obj;

    uv_async_init(uv_default_loop(), &async_baton->async, (uv_async_cb)HandleReaderStatusChange);
    pthread_create(&obj->m_status_thread, NULL, HandlerFunction, async_baton);
    pthread_detach(obj->m_status_thread);

    NanReturnUndefined();
}

NAN_METHOD(CardReader::Connect) {

    NanScope();

    // The second argument is the length of the data to be received
    if (!args[0]->IsUint32()) {
        return NanThrowError("First argument must be an integer");
    }

    if (!args[1]->IsUint32()) {
        return NanThrowError("Second argument must be an integer");
    }

    if (!args[2]->IsFunction()) {
        return NanThrowError("Third argument must be a callback function");
    }

    ConnectInput* ci = new ConnectInput();
    ci->share_mode = args[0]->Uint32Value();
    ci->pref_protocol = args[1]->Uint32Value();
    Local<Function> cb = Local<Function>::Cast(args[2]);

    // This creates our work request, including the libuv struct.
    Baton* baton = new Baton();
    baton->request.data = baton;
    NanAssignPersistent(baton->callback, cb);
    baton->reader = ObjectWrap::Unwrap<CardReader>(args.This());
    baton->input = ci;

    // Schedule our work request with libuv. Here you can specify the functions
    // that should be executed in the threadpool and back in the main thread
    // after the threadpool function completed.
    int status = uv_queue_work(uv_default_loop(),
                               &baton->request,
                               DoConnect,
                               reinterpret_cast<uv_after_work_cb>(AfterConnect));
    assert(status == 0);

    NanReturnUndefined();
}

NAN_METHOD(CardReader::Disconnect) {

    NanScope();

    if (!args[0]->IsUint32()) {
        return NanThrowError("First argument must be an integer");
    }

    if (!args[1]->IsFunction()) {
        return NanThrowError("Second argument must be a callback function");
    }

    DWORD disposition = args[0]->Uint32Value();
    Local<Function> cb = Local<Function>::Cast(args[1]);

    // This creates our work request, including the libuv struct.
    Baton* baton = new Baton();
    baton->input = reinterpret_cast<void*>(new DWORD(disposition));
    baton->request.data = baton;
    NanAssignPersistent(baton->callback, cb);
    baton->reader = ObjectWrap::Unwrap<CardReader>(args.This());

    // Schedule our work request with libuv. Here you can specify the functions
    // that should be executed in the threadpool and back in the main thread
    // after the threadpool function completed.
    int status = uv_queue_work(uv_default_loop(),
                               &baton->request,
                               DoDisconnect,
                               reinterpret_cast<uv_after_work_cb>(AfterDisconnect));
    assert(status == 0);

    NanReturnUndefined();
}

NAN_METHOD(CardReader::Transmit) {

    NanScope();

    // The first argument is the buffer to be transmitted.
    if (!Buffer::HasInstance(args[0])) {
        return NanThrowError("First argument must be a Buffer");
    }

    // The second argument is the length of the data to be received
    if (!args[1]->IsUint32()) {
        return NanThrowError("Second argument must be an integer");
    }

    // The third argument is the protocol to be used
    if (!args[2]->IsUint32()) {
        return NanThrowError("Third argument must be an integer");
    }

    // The fourth argument is the callback function
    if (!args[3]->IsFunction()) {
        return NanThrowError("Fourth argument must be a callback function");
    }

    Local<Object> buffer_data = args[0]->ToObject();
    uint32_t out_len = args[1]->Uint32Value();
    uint32_t protocol = args[2]->Uint32Value();
    Local<Function> cb = Local<Function>::Cast(args[3]);

    // This creates our work request, including the libuv struct.
    Baton* baton = new Baton();
    baton->request.data = baton;
    NanAssignPersistent(baton->callback, cb);
    baton->reader = ObjectWrap::Unwrap<CardReader>(args.This());
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

    NanReturnUndefined();
}

NAN_METHOD(CardReader::Control) {

    NanScope();

    // The first argument is the buffer to be transmitted.
    if (!Buffer::HasInstance(args[0])) {
        return NanThrowError("First argument must be a Buffer");
    }

    // The second argument is the control code to be used
    if (!args[1]->IsUint32()) {
        return NanThrowError("Second argument must be an integer");
    }

    // The third argument is output buffer
    if (!Buffer::HasInstance(args[2])) {
        return NanThrowError("Third argument must be a Buffer");
    }

    // The fourth argument is the callback function
    if (!args[3]->IsFunction()) {
        return NanThrowError("Fourth argument must be a callback function");
    }

    Local<Object> in_buf = args[0]->ToObject();
    DWORD control_code = args[1]->Uint32Value();
    Local<Object> out_buf = args[2]->ToObject();
    Local<Function> cb = Local<Function>::Cast(args[3]);

    // This creates our work request, including the libuv struct.
    Baton* baton = new Baton();
    baton->request.data = baton;
    NanAssignPersistent(baton->callback, cb);
    baton->reader = ObjectWrap::Unwrap<CardReader>(args.This());
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

    NanReturnUndefined();
}

NAN_METHOD(CardReader::Close) {

    NanScope();

    CardReader* obj = ObjectWrap::Unwrap<CardReader>(args.This());

    LONG result = SCardCancel(obj->m_status_card_context);
    obj->m_status_card_context = 0;

    NanReturnValue(NanNew<Number>(result));
}

void CardReader::HandleReaderStatusChange(uv_async_t *handle, int status) {

    NanScope();

    AsyncBaton* async_baton = static_cast<AsyncBaton*>(handle->data);
    AsyncResult* ar = async_baton->async_result;

    if (ar->do_exit) {
        uv_close(reinterpret_cast<uv_handle_t*>(&async_baton->async), CloseCallback); // necessary otherwise UV will block

        /* Emit end event */
        Handle<Value> argv[1] = {
            NanNew("end"), // event name
        };

        NanMakeCallback(NanObjectWrapHandle(async_baton->reader), "emit", 1, argv);
        return;
    }

    if (ar->result == SCARD_S_SUCCESS) {
        const unsigned argc = 3;
        Handle<Value> argv[argc] = {
            NanUndefined(), // argument
            NanNew<Number>(ar->status),
            NanNewBufferHandle(reinterpret_cast<char*>(ar->atr), ar->atrlen)
        };

        NanCallback(NanNew(async_baton->callback)).Call(argc, argv);
    } else {
        Local<Value> err = NanError(error_msg("SCardGetStatusChange", ar->result).c_str());
        // Prepare the parameters for the callback function.
        const unsigned argc = 1;
        Handle<Value> argv[argc] = { err };
        NanCallback(NanNew(async_baton->callback)).Call(argc, argv);
    }
}

void* CardReader::HandlerFunction(void* arg) {

    AsyncBaton* async_baton = static_cast<AsyncBaton*>(arg);
    CardReader* reader = async_baton->reader;
    async_baton->async_result = new AsyncResult();
    async_baton->async_result->do_exit = false;

    /* Lock mutex */
    pthread_mutex_lock(&reader->m_mutex);
    LONG result = SCardEstablishContext(SCARD_SCOPE_SYSTEM, NULL, NULL, &reader->m_status_card_context);
    /* Unlock the mutex */
    pthread_mutex_unlock(&reader->m_mutex);

    SCARD_READERSTATE card_reader_state = SCARD_READERSTATE();
    card_reader_state.szReader = reader->m_name.c_str();
    card_reader_state.dwCurrentState = SCARD_STATE_UNAWARE;

    while(result == SCARD_S_SUCCESS &&
        !((card_reader_state.dwCurrentState & SCARD_STATE_UNKNOWN) ||
        (card_reader_state.dwCurrentState & SCARD_STATE_UNAVAILABLE))) {
        result = SCardGetStatusChange(reader->m_status_card_context, INFINITE, &card_reader_state, 1);
        async_baton->async_result->result = result;
        async_baton->async_result->status = card_reader_state.dwEventState;
        memcpy(async_baton->async_result->atr, card_reader_state.rgbAtr, card_reader_state.cbAtr);
        async_baton->async_result->atrlen = card_reader_state.cbAtr;
        uv_async_send(&async_baton->async);
        card_reader_state.dwCurrentState = card_reader_state.dwEventState;
    }

    async_baton->async_result->do_exit = true;
    uv_async_send(&async_baton->async);

    return NULL;
}

void CardReader::DoConnect(uv_work_t* req) {

    Baton* baton = static_cast<Baton*>(req->data);
    ConnectInput *ci = static_cast<ConnectInput*>(baton->input);

    DWORD card_protocol;
    LONG result = SCARD_S_SUCCESS;
    CardReader* obj = baton->reader;

    /* Lock mutex */
    pthread_mutex_lock(&obj->m_mutex);
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
    pthread_mutex_unlock(&obj->m_mutex);

    ConnectResult *cr = new ConnectResult();
    cr->result = result;
    if (!result) {
        cr->card_protocol = card_protocol;
    }

    baton->result = cr;
}

void CardReader::AfterConnect(uv_work_t* req, int status) {

    NanScope();
    Baton* baton = static_cast<Baton*>(req->data);
    ConnectInput *ci = static_cast<ConnectInput*>(baton->input);
    ConnectResult *cr = static_cast<ConnectResult*>(baton->result);

    if (cr->result) {
        Local<Value> err = NanError(error_msg("SCardConnect", cr->result).c_str());
        // Prepare the parameters for the callback function.
        const unsigned argc = 1;
        Handle<Value> argv[argc] = { err };
        NanCallback(NanNew(baton->callback)).Call(argc, argv);
    } else {
        NanObjectWrapHandle(baton->reader)->Set(NanNew(connected_symbol), NanTrue());
        const unsigned argc = 2;
        Handle<Value> argv[argc] = {
            NanNull(),
            NanNew<Number>(cr->card_protocol)
        };

        NanCallback(NanNew(baton->callback)).Call(argc, argv);
    }

    // The callback is a permanent handle, so we have to dispose of it manually.
    NanDisposePersistent(baton->callback);
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
    pthread_mutex_lock(&obj->m_mutex);
    /* Connect */
    if (obj->m_card_handle) {
        result = SCardDisconnect(obj->m_card_handle, *disposition);
        if (result == SCARD_S_SUCCESS) {
            obj->m_card_handle = 0;
        }
    }

    /* Unlock the mutex */
    pthread_mutex_unlock(&obj->m_mutex);

    baton->result = reinterpret_cast<void*>(new LONG(result));
}

void CardReader::AfterDisconnect(uv_work_t* req, int status) {

    NanScope();
    Baton* baton = static_cast<Baton*>(req->data);
    LONG* result = reinterpret_cast<LONG*>(baton->result);

    if (*result) {
        Local<Value> err = NanError(error_msg("SCardDisconnect", *result).c_str());

        // Prepare the parameters for the callback function.
        const unsigned argc = 1;
        Handle<Value> argv[argc] = { err };
        NanCallback(NanNew(baton->callback)).Call(argc, argv);
    } else {
        NanObjectWrapHandle(baton->reader)->Set(NanNew(connected_symbol), NanFalse());
        const unsigned argc = 1;
        Handle<Value> argv[argc] = {
            NanNull()
        };

        NanCallback(NanNew(baton->callback)).Call(argc, argv);
    }

    // The callback is a permanent handle, so we have to dispose of it manually.
    NanDisposePersistent(baton->callback);
    DWORD* disposition = reinterpret_cast<DWORD*>(baton->input);
    delete disposition;
    delete result;
    delete baton;
}

void CardReader::DoTransmit(uv_work_t* req) {

    Baton* baton = static_cast<Baton*>(req->data);
    TransmitInput *ti = static_cast<TransmitInput*>(baton->input);
    CardReader* obj = baton->reader;

    SCARD_IO_REQUEST io_request;
    TransmitResult *tr = new TransmitResult();
    tr->data = new unsigned char[ti->out_len];
    tr->len = ti->out_len;
    LONG result = SCARD_E_INVALID_HANDLE;

    /* Lock mutex */
    pthread_mutex_lock(&obj->m_mutex);
    /* Connected? */
    if (obj->m_card_handle) {
        SCARD_IO_REQUEST send_pci = { ti->card_protocol, sizeof(SCARD_IO_REQUEST) };
        result = SCardTransmit(obj->m_card_handle, &send_pci, ti->in_data, ti->in_len,
                               &io_request, tr->data, &tr->len);
    }

    /* Unlock the mutex */
    pthread_mutex_unlock(&obj->m_mutex);

    tr->result = result;

    baton->result = tr;
}

void CardReader::AfterTransmit(uv_work_t* req, int status) {

    NanScope();
    Baton* baton = static_cast<Baton*>(req->data);
    TransmitInput *ti = static_cast<TransmitInput*>(baton->input);
    TransmitResult *tr = static_cast<TransmitResult*>(baton->result);

    if (tr->result) {
        Local<Value> err = NanError(error_msg("SCardTransmit", tr->result).c_str());

        // Prepare the parameters for the callback function.
        const unsigned argc = 1;
        Handle<Value> argv[argc] = { err };
        NanCallback(NanNew(baton->callback)).Call(argc, argv);
    } else {
        const unsigned argc = 2;
        Handle<Value> argv[argc] = {
            NanNull(),
            NanNewBufferHandle(reinterpret_cast<char*>(tr->data), tr->len)
        };

        NanCallback(NanNew(baton->callback)).Call(argc, argv);
    }


    // The callback is a permanent handle, so we have to dispose of it manually.
    NanDisposePersistent(baton->callback);
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
    pthread_mutex_lock(&obj->m_mutex);
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
    pthread_mutex_unlock(&obj->m_mutex);

    cr->result = result;

    baton->result = cr;
}

void CardReader::AfterControl(uv_work_t* req, int status) {

    NanScope();
    Baton* baton = static_cast<Baton*>(req->data);
    ControlInput *ci = static_cast<ControlInput*>(baton->input);
    ControlResult *cr = static_cast<ControlResult*>(baton->result);

    if (cr->result) {
        Local<Value> err = NanError(error_msg("SCardControl", cr->result).c_str());

        // Prepare the parameters for the callback function.
        const unsigned argc = 1;
        Handle<Value> argv[argc] = { err };
        NanCallback(NanNew(baton->callback)).Call(argc, argv);
    } else {
        const unsigned argc = 2;
        Handle<Value> argv[argc] = {
            NanNull(),
            NanNew<Number>(cr->len)
        };

        NanCallback(NanNew(baton->callback)).Call(argc, argv);
    }


    // The callback is a permanent handle, so we have to dispose of it manually.
    NanDisposePersistent(baton->callback);
    delete ci;
    delete cr;
    delete baton;
}

void CardReader::CloseCallback(uv_handle_t *handle) {

    /* cleanup process */
    AsyncBaton* async_baton = static_cast<AsyncBaton*>(handle->data);
    AsyncResult* ar = async_baton->async_result;
    delete ar;
    NanDisposePersistent(async_baton->callback);
    SCardReleaseContext(async_baton->reader->m_status_card_context);
    delete async_baton;
}
