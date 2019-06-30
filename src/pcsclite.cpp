#include "pcsclite.h"
#include "common.h"

using namespace v8;
using namespace node;

Nan::Persistent<Function> PCSCLite::constructor;

void PCSCLite::init(Local<Object> target) {

    // Prepare constructor template
    Local<FunctionTemplate> tpl = Nan::New<FunctionTemplate>(New);
    tpl->SetClassName(Nan::New("PCSCLite").ToLocalChecked());
    tpl->InstanceTemplate()->SetInternalFieldCount(1);
    // Prototype
    Nan::SetPrototypeTemplate(tpl, "start", Nan::New<FunctionTemplate>(Start));
    Nan::SetPrototypeTemplate(tpl, "close", Nan::New<FunctionTemplate>(Close));

    Local<Function> newfunc = Nan::GetFunction(tpl).ToLocalChecked();
    constructor.Reset(newfunc);
    Nan::Set(target, Nan::New("PCSCLite").ToLocalChecked(), newfunc);
}

PCSCLite::PCSCLite(): m_card_context(0),
                      m_card_reader_state(),
                      m_status_thread(0),
                      m_state(0) {

    assert(uv_mutex_init(&m_mutex) == 0);
    assert(uv_cond_init(&m_cond) == 0);

    LONG result = SCardEstablishContext(SCARD_SCOPE_SYSTEM,
                                        NULL,
                                        NULL,
                                        &m_card_context);
    if (result != SCARD_S_SUCCESS) {
        Nan::ThrowError(error_msg("SCardEstablishContext", result).c_str());
    } else {
        m_card_reader_state.szReader = "\\\\?PnP?\\Notification";
        m_card_reader_state.dwCurrentState = SCARD_STATE_UNAWARE;
        result = SCardGetStatusChange(m_card_context,
                                      0,
                                      &m_card_reader_state,
                                      1);

        if ((result != SCARD_S_SUCCESS) && (result != (LONG)SCARD_E_TIMEOUT)) {
            Nan::ThrowError(error_msg("SCardGetStatusChange", result).c_str());
        } else {
            m_pnp = !(m_card_reader_state.dwEventState & SCARD_STATE_UNKNOWN);
        }
    }
}

PCSCLite::~PCSCLite() {

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

NAN_METHOD(PCSCLite::New) {
    Nan::HandleScope scope;
    PCSCLite* obj = new PCSCLite();
    obj->Wrap(info.Holder());
    info.GetReturnValue().Set(info.Holder());
}

NAN_METHOD(PCSCLite::Start) {

    Nan::HandleScope scope;

    PCSCLite* obj = Nan::ObjectWrap::Unwrap<PCSCLite>(info.This());
    Local<Function> cb = Local<Function>::Cast(info[0]);

    AsyncBaton *async_baton = new AsyncBaton();
    async_baton->async.data = async_baton;
    async_baton->callback.Reset(cb);
    async_baton->pcsclite = obj;

    uv_async_init(uv_default_loop(), &async_baton->async, (uv_async_cb)HandleReaderStatusChange);
    int ret = uv_thread_create(&obj->m_status_thread, HandlerFunction, async_baton);
    assert(ret == 0);


}

NAN_METHOD(PCSCLite::Close) {

    Nan::HandleScope scope;

    PCSCLite* obj = Nan::ObjectWrap::Unwrap<PCSCLite>(info.This());

    LONG result = SCARD_S_SUCCESS;
    if (obj->m_pnp) {
        if (obj->m_status_thread) {
            uv_mutex_lock(&obj->m_mutex);
            if (obj->m_state == 0) {
                int ret;
                int times = 0;
                obj->m_state = 1;
                do {
                    result = SCardCancel(obj->m_card_context);
                    ret = uv_cond_timedwait(&obj->m_cond, &obj->m_mutex, 10000000);
                } while ((ret != 0) && (++ times < 5));
            }

            uv_mutex_unlock(&obj->m_mutex);
        }
    } else {
        obj->m_state = 1;
    }

    if (obj->m_status_thread) {
        assert(uv_thread_join(&obj->m_status_thread) == 0);
        obj->m_status_thread = 0;
    }

    info.GetReturnValue().Set(Nan::New<Number>(result));
}

void PCSCLite::HandleReaderStatusChange(uv_async_t *handle, int status) {

    Nan::HandleScope scope;

    AsyncBaton* async_baton = static_cast<AsyncBaton*>(handle->data);
    AsyncResult* ar = async_baton->async_result;

    if (async_baton->pcsclite->m_state == 1) {
        // Swallow events : Listening thread was cancelled by user.
    } else if ((ar->result == SCARD_S_SUCCESS) ||
               (ar->result == (LONG)SCARD_E_NO_READERS_AVAILABLE)) {
        const unsigned argc = 2;
        Local<Value> argv[argc] = {
            Nan::Undefined(), // argument
            Nan::CopyBuffer(ar->readers_name, ar->readers_name_length).ToLocalChecked()
        };

        Nan::Call(Nan::Callback(Nan::New(async_baton->callback)), argc, argv);
    } else {
        Local<Value> argv[1] = { Nan::Error(ar->err_msg.c_str()) };
        Nan::Call(Nan::Callback(Nan::New(async_baton->callback)), 1, argv);
    }

    // Do exit, after throwing last events
    if (ar->do_exit) {
        // necessary otherwise UV will block
        uv_close(reinterpret_cast<uv_handle_t*>(&async_baton->async), CloseCallback);
        return;
    }

    /* reset AsyncResult */
#ifdef SCARD_AUTOALLOCATE
    PCSCLite* pcsclite = async_baton->pcsclite;
    SCardFreeMemory(pcsclite->m_card_context, ar->readers_name);
#else
    delete [] ar->readers_name;
#endif
    ar->readers_name = NULL;
    ar->readers_name_length = 0;
    ar->result = SCARD_S_SUCCESS;
}

void PCSCLite::HandlerFunction(void* arg) {

    LONG result = SCARD_S_SUCCESS;
    AsyncBaton* async_baton = static_cast<AsyncBaton*>(arg);
    PCSCLite* pcsclite = async_baton->pcsclite;
    async_baton->async_result = new AsyncResult();

    while (!pcsclite->m_state) {
        /* Get card readers */
        result = pcsclite->get_card_readers(pcsclite, async_baton->async_result);
        if (result == (LONG)SCARD_E_NO_READERS_AVAILABLE) {
            result = SCARD_S_SUCCESS;
        }

        /* Store the result in the baton */
        async_baton->async_result->result = result;
        if (result != SCARD_S_SUCCESS) {
            async_baton->async_result->err_msg = error_msg("SCardListReaders",
                                                           result);
        }

        /* Notify the nodejs thread */
        uv_async_send(&async_baton->async);

        if (result == SCARD_S_SUCCESS) {
            if (pcsclite->m_pnp) {
                /* Set current status */
                pcsclite->m_card_reader_state.dwCurrentState =
                    pcsclite->m_card_reader_state.dwEventState;
                /* Start checking for status change */
                result = SCardGetStatusChange(pcsclite->m_card_context,
                                              INFINITE,
                                              &pcsclite->m_card_reader_state,
                                              1);

                uv_mutex_lock(&pcsclite->m_mutex);
                async_baton->async_result->result = result;
                if (pcsclite->m_state) {
                    uv_cond_signal(&pcsclite->m_cond);
                }

                if (result != SCARD_S_SUCCESS) {
                    pcsclite->m_state = 2;
                    async_baton->async_result->err_msg =
                      error_msg("SCardGetStatusChange", result);
                }

                uv_mutex_unlock(&pcsclite->m_mutex);
            } else {
                /*  If PnP is not supported, just wait for 1 second */
                Sleep(1000);
            }
        } else {
            /* Error on last card access, stop monitoring */
            pcsclite->m_state = 2;
        }
    }

    async_baton->async_result->do_exit = true;
    uv_async_send(&async_baton->async);
}

void PCSCLite::CloseCallback(uv_handle_t *handle) {

    /* cleanup process */
    AsyncBaton* async_baton = static_cast<AsyncBaton*>(handle->data);
    AsyncResult* ar = async_baton->async_result;
#ifdef SCARD_AUTOALLOCATE
    PCSCLite* pcsclite = async_baton->pcsclite;
    SCardFreeMemory(pcsclite->m_card_context, ar->readers_name);
#else
    delete [] ar->readers_name;
#endif
    delete ar;
    async_baton->callback.Reset();
    delete async_baton;
}

LONG PCSCLite::get_card_readers(PCSCLite* pcsclite, AsyncResult* async_result) {

    DWORD readers_name_length;
    LPTSTR readers_name;

    LONG result = SCARD_S_SUCCESS;

    /* Reset the readers_name in the baton */
    async_result->readers_name = NULL;
    async_result->readers_name_length = 0;

#ifdef SCARD_AUTOALLOCATE
    readers_name_length = SCARD_AUTOALLOCATE;
    result = SCardListReaders(pcsclite->m_card_context,
                              NULL,
                              (LPTSTR)&readers_name,
                              &readers_name_length);
#else
    /* Find out ReaderNameLength */
    result = SCardListReaders(pcsclite->m_card_context,
                              NULL,
                              NULL,
                              &readers_name_length);
    if (result != SCARD_S_SUCCESS) {
        return result;
    }

    /*
     * Allocate Memory for ReaderName and retrieve all readers in the terminal
     */
    readers_name = new char[readers_name_length];
    result = SCardListReaders(pcsclite->m_card_context,
                              NULL,
                              readers_name,
                              &readers_name_length);
#endif

    if (result != SCARD_S_SUCCESS) {
#ifndef SCARD_AUTOALLOCATE
        delete [] readers_name;
#endif
        readers_name = NULL;
        readers_name_length = 0;
#ifndef SCARD_AUTOALLOCATE
        /* Retry in case of insufficient buffer error */
        if (result == (LONG)SCARD_E_INSUFFICIENT_BUFFER) {
            result = get_card_readers(pcsclite, async_result);
        }
#endif
    } else {
        /* Store the readers_name in the baton */
        async_result->readers_name = readers_name;
        async_result->readers_name_length = readers_name_length;
    }

    return result;
}
