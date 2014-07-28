#include "cardreader.h"
#include "common.h"

#include <v8.h>
#include <node_buffer.h>
#include <pcsclite.h>
#include <string.h>

using namespace v8;
using namespace node;

Persistent<Function> CardReader::constructor;

void CardReader::init(Handle<Object> target) {

     // Prepare constructor template
    Local<FunctionTemplate> tpl = FunctionTemplate::New(New);
    tpl->SetClassName(String::NewSymbol("CardReader"));
    tpl->InstanceTemplate()->SetInternalFieldCount(1);

    // Symbol
    name_symbol = NODE_PSYMBOL("name");
    connected_symbol = NODE_PSYMBOL("connected");

    // Prototype
    NODE_SET_PROTOTYPE_METHOD(tpl, "get_status", GetStatus);
    NODE_SET_PROTOTYPE_METHOD(tpl, "_connect", Connect);
    NODE_SET_PROTOTYPE_METHOD(tpl, "_disconnect", Disconnect);
    NODE_SET_PROTOTYPE_METHOD(tpl, "_transmit", Transmit);
    NODE_SET_PROTOTYPE_METHOD(tpl, "_control", Control);
    NODE_SET_PROTOTYPE_METHOD(tpl, "close", Close);

    constructor = Persistent<Function>::New(tpl->GetFunction());
    target->Set(String::NewSymbol("CardReader"), constructor);
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
        LONG result = SCardCancel(m_status_card_context);
    }

    pthread_mutex_destroy(&m_mutex);
}

Handle<Value> CardReader::New(const Arguments& args) {

    HandleScope scope;

    v8::String::Utf8Value reader_name(args[0]->ToString());
    CardReader* obj = new CardReader(*reader_name);
    obj->Wrap(args.Holder());
    obj->handle_->Set(name_symbol, args[0]->ToString());
    obj->handle_->Set(connected_symbol, Boolean::New(false));

    return scope.Close(args.Holder());
}

Handle<Value> CardReader::GetStatus(const Arguments& args) {

    HandleScope scope;

    CardReader* obj = ObjectWrap::Unwrap<CardReader>(args.This());
    Local<Function> cb = Local<Function>::Cast(args[0]);

    AsyncBaton *async_baton = new AsyncBaton();
    async_baton->async.data = async_baton;
    async_baton->callback = Persistent<Function>::New(cb);
    async_baton->reader = obj;

    uv_async_init(uv_default_loop(), &async_baton->async, HandleReaderStatusChange);
    pthread_create(&obj->m_status_thread, NULL, HandlerFunction, async_baton);
    pthread_detach(obj->m_status_thread);

    return scope.Close(Undefined());
}

Handle<Value> CardReader::Connect(const Arguments& args) {

    HandleScope scope;

    if (!args[0]->IsFunction()) {
        return ThrowException(Exception::TypeError(
            String::New("First argument must be a callback function")));
    }

    Local<Function> cb = Local<Function>::Cast(args[0]);

    // This creates our work request, including the libuv struct.
    Baton* baton = new Baton();
    baton->request.data = baton;
    baton->callback = Persistent<Function>::New(cb);
    baton->reader = ObjectWrap::Unwrap<CardReader>(args.This());

    // Schedule our work request with libuv. Here you can specify the functions
    // that should be executed in the threadpool and back in the main thread
    // after the threadpool function completed.
    int status = uv_queue_work(uv_default_loop(), &baton->request, DoConnect, AfterConnect);
    assert(status == 0);

    return scope.Close(Undefined());
}

Handle<Value> CardReader::Disconnect(const Arguments& args) {

    HandleScope scope;

    if (!args[0]->IsFunction()) {
        return ThrowException(Exception::TypeError(
            String::New("First argument must be a callback function")));
    }

    Local<Function> cb = Local<Function>::Cast(args[0]);

    // This creates our work request, including the libuv struct.
    Baton* baton = new Baton();
    baton->request.data = baton;
    baton->callback = Persistent<Function>::New(cb);
    baton->reader = ObjectWrap::Unwrap<CardReader>(args.This());

    // Schedule our work request with libuv. Here you can specify the functions
    // that should be executed in the threadpool and back in the main thread
    // after the threadpool function completed.
    int status = uv_queue_work(uv_default_loop(), &baton->request, DoDisconnect, AfterDisconnect);
    assert(status == 0);

    return scope.Close(Undefined());
}

Handle<Value> CardReader::Transmit(const Arguments& args) {

    HandleScope scope;

    // The first argument is the buffer to be transmitted.
    if (!Buffer::HasInstance(args[0])) {
        return ThrowException(Exception::TypeError(
            String::New("First argument must be a Buffer")));
    }

    // The second argument is the length of the data to be received
    if (!args[1]->IsUint32()) {
        return ThrowException(Exception::TypeError(
            String::New("Second argument must be an integer")));
    }

    // The third argument is the protocol to be used
    if (!args[2]->IsUint32()) {
        return ThrowException(Exception::TypeError(
            String::New("Third argument must be an integer")));
    }

    // The fourth argument is the callback function
    if (!args[3]->IsFunction()) {
        return ThrowException(Exception::TypeError(
            String::New("Fourth argument must be a callback function")));
    }

    Local<Object> buffer_data = args[0]->ToObject();
    uint32_t out_len = args[1]->Uint32Value();
    uint32_t protocol = args[2]->Uint32Value();
    Local<Function> cb = Local<Function>::Cast(args[3]);

    // This creates our work request, including the libuv struct.
    Baton* baton = new Baton();
    baton->request.data = baton;
    baton->callback = Persistent<Function>::New(cb);
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
    int status = uv_queue_work(uv_default_loop(), &baton->request, DoTransmit, AfterTransmit);
    assert(status == 0);

    return scope.Close(Undefined());
}

Handle<Value> CardReader::Control(const Arguments& args) {

    HandleScope scope;

    // The first argument is the buffer to be transmitted.
    if (!Buffer::HasInstance(args[0])) {
        return ThrowException(Exception::TypeError(
            String::New("First argument must be a Buffer")));
    }

    // The second argument is the control code to be used
    if (!args[1]->IsUint32()) {
        return ThrowException(Exception::TypeError(
            String::New("Second argument must be an integer")));
    }

    // The third argument is output buffer
    if (!Buffer::HasInstance(args[2])) {
        return ThrowException(Exception::TypeError(
            String::New("First argument must be a Buffer")));
    }

    // The fourth argument is the callback function
    if (!args[3]->IsFunction()) {
        return ThrowException(Exception::TypeError(
            String::New("Fourth argument must be a callback function")));
    }

    Local<Object> in_buf = args[0]->ToObject();
    DWORD control_code = args[1]->Uint32Value();
    Local<Object> out_buf = args[2]->ToObject();
    Local<Function> cb = Local<Function>::Cast(args[3]);

    // This creates our work request, including the libuv struct.
    Baton* baton = new Baton();
    baton->request.data = baton;
    baton->callback = Persistent<Function>::New(cb);
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
    int status = uv_queue_work(uv_default_loop(), &baton->request, DoControl, AfterControl);
    assert(status == 0);

    return scope.Close(Undefined());
}

Handle<Value> CardReader::Close(const Arguments& args) {

    HandleScope scope;

    CardReader* obj = ObjectWrap::Unwrap<CardReader>(args.This());

    LONG result = SCardCancel(obj->m_status_card_context);
    obj->m_status_card_context = 0;

    return scope.Close(Integer::New(result));
}

void CardReader::HandleReaderStatusChange(uv_async_t *handle, int status) {

    AsyncBaton* async_baton = static_cast<AsyncBaton*>(handle->data);
    AsyncResult* ar = async_baton->async_result;

    if (ar->do_exit) {
        uv_close(reinterpret_cast<uv_handle_t*>(&async_baton->async), CloseCallback); // necessary otherwise UV will block

        /* Emit end event */
        Handle<Value> argv[1] = {
            String::New("end"), // event name
        };

        MakeCallback(async_baton->reader->handle_, "emit", 1, argv);
        return;
    }

    if (ar->result == SCARD_S_SUCCESS) {
        const unsigned argc = 3;
        Handle<Value> argv[argc] = {
            Undefined(), // argument
            Integer::New(ar->status),
            CreateBufferInstance(reinterpret_cast<char*>(ar->atr), ar->atrlen)
        };

        PerformCallback(async_baton->reader->handle_, async_baton->callback, argc, argv);
    } else {
        Local<Value> err = Exception::Error(String::New(pcsc_stringify_error(ar->result)));
        // Prepare the parameters for the callback function.
        const unsigned argc = 1;
        Handle<Value> argv[argc] = { err };
        PerformCallback(async_baton->reader->handle_, async_baton->callback, argc, argv);
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

    unsigned long card_protocol;
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
        result = SCardConnect(obj->m_card_context, obj->m_name.c_str(),
                              SCARD_SHARE_EXCLUSIVE, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1,
                              &obj->m_card_handle, &card_protocol);
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

#if NODE_VERSION_AT_LEAST(0, 9, 4)
void CardReader::AfterConnect(uv_work_t* req, int status) {
#else
void CardReader::AfterConnect(uv_work_t* req) {
#endif

    HandleScope scope;
    Baton* baton = static_cast<Baton*>(req->data);
    ConnectResult *cr = static_cast<ConnectResult*>(baton->result);

    if (cr->result) {
        Local<Value> err = Exception::Error(String::New(pcsc_stringify_error(cr->result)));
        // Prepare the parameters for the callback function.
        const unsigned argc = 1;
        Handle<Value> argv[argc] = { err };
        PerformCallback(baton->reader->handle_, baton->callback, argc, argv);
    } else {
        baton->reader->handle_->Set(connected_symbol, Boolean::New(true));
        const unsigned argc = 2;
        Handle<Value> argv[argc] = {
            Local<Value>::New(Null()),
            Integer::New(cr->card_protocol)
        };

        PerformCallback(baton->reader->handle_, baton->callback, argc, argv);
    }

    // The callback is a permanent handle, so we have to dispose of it manually.
    baton->callback.Dispose();
    delete cr;
    delete baton;
}

void CardReader::DoDisconnect(uv_work_t* req) {

    Baton* baton = static_cast<Baton*>(req->data);

    LONG result = SCARD_S_SUCCESS;
    CardReader* obj = baton->reader;

    /* Lock mutex */
    pthread_mutex_lock(&obj->m_mutex);
    /* Connect */
    if (obj->m_card_handle) {
        result = SCardDisconnect(obj->m_card_handle, SCARD_UNPOWER_CARD);
        if (result == SCARD_S_SUCCESS) {
            obj->m_card_handle = 0;
        }
    }

    /* Unlock the mutex */
    pthread_mutex_unlock(&obj->m_mutex);

    baton->result = reinterpret_cast<void*>(result);
}

#if NODE_VERSION_AT_LEAST(0, 9, 4)
void CardReader::AfterDisconnect(uv_work_t* req, int status) {
#else
void CardReader::AfterDisconnect(uv_work_t* req) {
#endif

    HandleScope scope;
    Baton* baton = static_cast<Baton*>(req->data);
    LONG result = reinterpret_cast<LONG>(baton->result);

    if (result) {
        Local<Value> err = Exception::Error(String::New(pcsc_stringify_error(result)));

        // Prepare the parameters for the callback function.
        const unsigned argc = 1;
        Handle<Value> argv[argc] = { err };
        PerformCallback(baton->reader->handle_, baton->callback, argc, argv);
    } else {
        baton->reader->handle_->Set(connected_symbol, Boolean::New(false));
        const unsigned argc = 1;
        Handle<Value> argv[argc] = {
            Local<Value>::New(Null())
        };

        PerformCallback(baton->reader->handle_, baton->callback, argc, argv);
    }

    // The callback is a permanent handle, so we have to dispose of it manually.
    baton->callback.Dispose();

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

#if NODE_VERSION_AT_LEAST(0, 9, 4)
void CardReader::AfterTransmit(uv_work_t* req, int status) {
#else
void CardReader::AfterTransmit(uv_work_t* req) {
#endif

    HandleScope scope;
    Baton* baton = static_cast<Baton*>(req->data);
    TransmitInput *ti = static_cast<TransmitInput*>(baton->input);
    TransmitResult *tr = static_cast<TransmitResult*>(baton->result);

    if (tr->result) {
        Local<Value> err = Exception::Error(String::New(pcsc_stringify_error(tr->result)));

        // Prepare the parameters for the callback function.
        const unsigned argc = 1;
        Handle<Value> argv[argc] = { err };
        PerformCallback(baton->reader->handle_, baton->callback, argc, argv);
    } else {
        const unsigned argc = 2;
        Handle<Value> argv[argc] = {
            Local<Value>::New(Null()),
            CreateBufferInstance(reinterpret_cast<char*>(tr->data), tr->len)
        };

        PerformCallback(baton->reader->handle_, baton->callback, argc, argv);
    }


    // The callback is a permanent handle, so we have to dispose of it manually.
    baton->callback.Dispose();
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

#if NODE_VERSION_AT_LEAST(0, 9, 4)
void CardReader::AfterControl(uv_work_t* req, int status) {
#else
void CardReader::AfterControl(uv_work_t* req) {
#endif

    HandleScope scope;
    Baton* baton = static_cast<Baton*>(req->data);
    ControlInput *ci = static_cast<ControlInput*>(baton->input);
    ControlResult *cr = static_cast<ControlResult*>(baton->result);

    if (cr->result) {
        Local<Value> err = Exception::Error(String::New(pcsc_stringify_error(cr->result)));

        // Prepare the parameters for the callback function.
        const unsigned argc = 1;
        Handle<Value> argv[argc] = { err };
        PerformCallback(baton->reader->handle_, baton->callback, argc, argv);
    } else {
        const unsigned argc = 2;
        Handle<Value> argv[argc] = {
            Local<Value>::New(Null()),
            Integer::New(cr->len)
        };

        PerformCallback(baton->reader->handle_, baton->callback, argc, argv);
    }


    // The callback is a permanent handle, so we have to dispose of it manually.
    baton->callback.Dispose();
    delete ci;
    delete cr;
    delete baton;
}

void CardReader::CloseCallback(uv_handle_t *handle) {

    /* cleanup process */
    AsyncBaton* async_baton = static_cast<AsyncBaton*>(handle->data);
    AsyncResult* ar = async_baton->async_result;
    delete ar;
    async_baton->callback.Dispose();
    SCardReleaseContext(async_baton->reader->m_status_card_context);
    delete async_baton;
}

Handle<Value> CardReader::CreateBufferInstance(char* data, unsigned long size) {
    if (size == 0) {
        return Undefined();
    }

    // get Buffer from global scope.
    Local<Object> global = Context::GetCurrent()->Global();
    Local<Value> bv = global->Get(String::NewSymbol("Buffer"));
    assert(bv->IsFunction());
    Local<Function> b = Local<Function>::Cast(bv);
    Handle<Value> argv[3] = {
        Buffer::New(data, size)->handle_,
        Integer::New(size),
        Integer::New(0)
    };

    return b->NewInstance(3, argv);
}
