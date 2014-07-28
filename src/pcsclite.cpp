#include "pcsclite.h"
#include "common.h"

#include <v8.h>
#include <pcsclite.h>
#include <node_buffer.h>
#include <string>
#include <string.h>

using namespace v8;
using namespace node;

Persistent<Function> PCSCLite::constructor;

void PCSCLite::init(Handle<Object> target) {

    // Prepare constructor template
    Local<FunctionTemplate> tpl = FunctionTemplate::New(New);
    tpl->SetClassName(String::NewSymbol("PCSCLite"));
    tpl->InstanceTemplate()->SetInternalFieldCount(1);
    // Prototype
    NODE_SET_PROTOTYPE_METHOD(tpl, "start", Start);
    NODE_SET_PROTOTYPE_METHOD(tpl, "close", Close);

    constructor = Persistent<Function>::New(tpl->GetFunction());
    target->Set(String::NewSymbol("PCSCLite"), constructor);
}

PCSCLite::PCSCLite(): m_card_context(0) {
    pthread_mutex_init(&m_mutex, NULL);
}

PCSCLite::~PCSCLite() {
    if (m_card_context) {
        SCardReleaseContext(m_card_context);
    }

    pthread_mutex_destroy(&m_mutex);
    pthread_cancel(m_status_thread);
}

Handle<Value> PCSCLite::New(const Arguments& args) {

    HandleScope scope;
    PCSCLite* obj = new PCSCLite();
    obj->Wrap(args.Holder());
    return scope.Close(args.Holder());
}

Handle<Value> PCSCLite::Start(const Arguments& args) {

    HandleScope scope;

    PCSCLite* obj = ObjectWrap::Unwrap<PCSCLite>(args.This());
    Local<Function> cb = Local<Function>::Cast(args[0]);

    AsyncBaton *async_baton = new AsyncBaton();
    async_baton->async.data = async_baton;
    async_baton->callback = Persistent<Function>::New(cb);
    async_baton->pcsclite = obj;

    uv_async_init(uv_default_loop(), &async_baton->async, HandleReaderStatusChange);
    pthread_create(&obj->m_status_thread, NULL, HandlerFunction, async_baton);
    pthread_detach(obj->m_status_thread);

    return scope.Close(Undefined());
}

Handle<Value> PCSCLite::Close(const Arguments& args) {

    HandleScope scope;

    PCSCLite* obj = ObjectWrap::Unwrap<PCSCLite>(args.This());

    LONG result = SCardCancel(obj->m_card_context);

    return scope.Close(Integer::New(result));
}

void PCSCLite::HandleReaderStatusChange(uv_async_t *handle, int status) {

    AsyncBaton* async_baton = static_cast<AsyncBaton*>(handle->data);
    PCSCLite* pcsclite = async_baton->pcsclite;
    AsyncResult* ar = async_baton->async_result;

    if (ar->do_exit) {
        uv_close(reinterpret_cast<uv_handle_t*>(&async_baton->async), CloseCallback); // necessary otherwise UV will block
        return;
    }

    if ((ar->result == SCARD_S_SUCCESS) || (ar->result == (LONG)SCARD_E_NO_READERS_AVAILABLE)) {
        const unsigned argc = 2;
        Handle<Value> argv[argc] = {
            Undefined(), // argument
            Buffer::New(ar->readers_name, ar->readers_name_length)->handle_
        };

        PerformCallback(async_baton->pcsclite->handle_, async_baton->callback, argc, argv);
    } else {
        Local<Value> err = Exception::Error(String::New(pcsc_stringify_error(ar->result)));
        // Prepare the parameters for the callback function.
        const unsigned argc = 1;
        Handle<Value> argv[argc] = { err };
        PerformCallback(async_baton->pcsclite->handle_, async_baton->callback, argc, argv);
    }

    /* reset AsyncResult */
    delete [] ar->readers_name;
    ar->readers_name = NULL;
    ar->readers_name_length = 0;
    ar->result = SCARD_S_SUCCESS;
    /* Unlock the mutex */
    pthread_mutex_unlock(&pcsclite->m_mutex);
}

void* PCSCLite::HandlerFunction(void* arg) {

    LONG result = SCARD_S_SUCCESS;
    AsyncBaton* async_baton = static_cast<AsyncBaton*>(arg);
    PCSCLite* pcsclite = async_baton->pcsclite;
    async_baton->async_result = new AsyncResult();

    SCARD_READERSTATE card_reader_state = SCARD_READERSTATE();
    card_reader_state.szReader = "\\\\?PnP?\\Notification";
    card_reader_state.dwCurrentState = SCARD_STATE_UNAWARE;

    while(result == SCARD_S_SUCCESS) {
        /* Lock mutex. It'll be unlocked after the callback has been sent */
        pthread_mutex_lock(&pcsclite->m_mutex);
        /* Get card readers */
        result = pcsclite->get_card_readers(pcsclite, async_baton->async_result);
        /* Store the result in the baton */
        async_baton->async_result->result = result;
        /* Notify the nodejs thread */
        uv_async_send(&async_baton->async);
        /* Start checking for status change */
        result = SCardGetStatusChange(pcsclite->m_card_context, INFINITE, &card_reader_state, 1);
    }

    async_baton->async_result->do_exit = true;
    uv_async_send(&async_baton->async);

    return NULL;
}

void PCSCLite::CloseCallback(uv_handle_t *handle) {

    /* cleanup process */
    AsyncBaton* async_baton = static_cast<AsyncBaton*>(handle->data);
    AsyncResult* ar = async_baton->async_result;
    delete [] ar->readers_name;
    delete ar;
    async_baton->callback.Dispose();
    delete async_baton;
}

LONG PCSCLite::get_card_readers(PCSCLite* pcsclite, AsyncResult* async_result) {

    LONG result = SCARD_S_SUCCESS;

    /* Reset the readers_name in the baton */
    async_result->readers_name = NULL;
    async_result->readers_name_length = 0;

    if (!pcsclite->m_card_context) {
        result = SCardEstablishContext(SCARD_SCOPE_SYSTEM, NULL, NULL, &pcsclite->m_card_context);
    }

    if (result != SCARD_S_SUCCESS) {
        return result;
    }

    /* Find out ReaderNameLength */
    unsigned long readers_name_length;
    result = SCardListReaders(pcsclite->m_card_context, NULL, NULL, &readers_name_length);
    if (result != SCARD_S_SUCCESS) {
        return result;
    }

    /* Allocate Memory for ReaderName  and retrieve all readers in the terminal */
    char* readers_name  = new char[readers_name_length];
    result = SCardListReaders(pcsclite->m_card_context, NULL, readers_name, &readers_name_length);
    if (result != SCARD_S_SUCCESS) {
        delete [] readers_name;
        readers_name = NULL;
        readers_name_length = 0;
    }

    /* Store the readers_name in the baton */
    async_result->readers_name = readers_name;
    async_result->readers_name_length = readers_name_length;

    return result;
}
