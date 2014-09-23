#include "pcsclite.h"
#include "common.h"

using namespace v8;
using namespace node;

Persistent<Function> PCSCLite::constructor;

void PCSCLite::init(Handle<Object> target) {

    // Prepare constructor template
    Local<FunctionTemplate> tpl = NanNew<FunctionTemplate>(New);
    tpl->SetClassName(NanNew("PCSCLite"));
    tpl->InstanceTemplate()->SetInternalFieldCount(1);
    // Prototype
    NanSetPrototypeTemplate(tpl, "start", NanNew<FunctionTemplate>(Start));
    NanSetPrototypeTemplate(tpl, "close", NanNew<FunctionTemplate>(Close));

    NanAssignPersistent<Function>(constructor, tpl->GetFunction());
    target->Set(NanNew("PCSCLite"), tpl->GetFunction());
}

PCSCLite::PCSCLite(): m_card_context(NULL),
                      m_status_thread(NULL) {
    pthread_mutex_init(&m_mutex, NULL);

    LONG result = SCardEstablishContext(SCARD_SCOPE_SYSTEM,
                                        NULL,
                                        NULL,
                                        &m_card_context);
    if (result != SCARD_S_SUCCESS) {
        NanThrowError(error_msg("SCardEstablishContext", result).c_str());
    }
}

PCSCLite::~PCSCLite() {
    if (m_card_context) {
        SCardReleaseContext(m_card_context);
    }

    pthread_mutex_destroy(&m_mutex);
    if (m_status_thread) {
        pthread_cancel(m_status_thread);
    }
}

NAN_METHOD(PCSCLite::New) {
    NanScope();
    PCSCLite* obj = new PCSCLite();
    obj->Wrap(args.Holder());
    NanReturnValue(args.Holder());
}

NAN_METHOD(PCSCLite::Start) {

    NanScope();

    PCSCLite* obj = ObjectWrap::Unwrap<PCSCLite>(args.This());
    Local<Function> cb = Local<Function>::Cast(args[0]);

    AsyncBaton *async_baton = new AsyncBaton();
    async_baton->async.data = async_baton;
    NanAssignPersistent(async_baton->callback, cb);
    async_baton->pcsclite = obj;

    uv_async_init(uv_default_loop(), &async_baton->async, (uv_async_cb)HandleReaderStatusChange);
    pthread_create(&obj->m_status_thread, NULL, HandlerFunction, async_baton);
    pthread_detach(obj->m_status_thread);

    NanReturnUndefined();
}

NAN_METHOD(PCSCLite::Close) {

    NanScope();

    PCSCLite* obj = ObjectWrap::Unwrap<PCSCLite>(args.This());

    LONG result = SCardCancel(obj->m_card_context);

    NanReturnValue(NanNew<Integer>(result));
}

void PCSCLite::HandleReaderStatusChange(uv_async_t *handle, int status) {

    NanScope();

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
            NanUndefined(), // argument
            NanNewBufferHandle(ar->readers_name, ar->readers_name_length)
        };

        NanCallback(NanNew(async_baton->callback)).Call(argc, argv);
    } else {
        Local<Value> err = NanError(error_msg("SCardListReaders", ar->result).c_str());
        // Prepare the parameters for the callback function.
        const unsigned argc = 1;
        Handle<Value> argv[argc] = { err };
        NanCallback(NanNew(async_baton->callback)).Call(argc, argv);
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
    NanDisposePersistent(async_baton->callback);
    delete async_baton;
}

LONG PCSCLite::get_card_readers(PCSCLite* pcsclite, AsyncResult* async_result) {

    LONG result = SCARD_S_SUCCESS;

    /* Reset the readers_name in the baton */
    async_result->readers_name = NULL;
    async_result->readers_name_length = 0;

    /* Find out ReaderNameLength */
    DWORD readers_name_length;
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
