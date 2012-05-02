#ifndef COMMON_H
#define COMMON_H

#include <node.h>

static void PerformCallback(v8::Handle<v8::Object> object,
                            v8::Persistent<v8::Function> &callback,
                            const unsigned argc, v8::Handle<v8::Value> *argv) {

    // Wrap the callback function call in a TryCatch so that we can call
    // node's FatalException afterwards. This makes it possible to catch
    // the exception from JavaScript land using the
    // process.on('uncaughtException') event.
    v8::TryCatch try_catch;
    callback->Call(object, argc, argv);
    if (try_catch.HasCaught()) {
        node::FatalException(try_catch);
    }
}

#endif /* COMMON_H */
