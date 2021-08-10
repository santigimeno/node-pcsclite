#include "pcsclite.h"
#include "cardreader.h"

Napi::Object init_all(Napi::Env env, Napi::Object target) {
  PCSCLite::init(env, target);
  CardReader::init(env, target);
  return target;
}

NODE_API_MODULE(pcsclite, init_all)
