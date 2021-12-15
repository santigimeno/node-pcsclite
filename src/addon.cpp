#include "pcsclite.h"
#include "cardreader.h"

using namespace v8;
using namespace node;

void init_all(Local<Object> target) {
    PCSCLite::init(target);
    CardReader::init(target);
}

#if NODE_MAJOR_VERSION >= 10
NAN_MODULE_WORKER_ENABLED(pcsclite, init_all)
#else
NODE_MODULE(pcsclite, init_all)
#endif
