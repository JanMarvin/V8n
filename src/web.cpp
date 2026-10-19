#include "v8n.h"
#include "web_js.h"
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

/* Native helpers for the web APIs in web_js.h */

/* Milliseconds on a steady clock, shared by performance.now() and timers */
double web_clock(){
  static const std::chrono::steady_clock::time_point origin = std::chrono::steady_clock::now();
  std::chrono::duration<double, std::milli> ms = std::chrono::steady_clock::now() - origin;
  return ms.count();
}

static void native_now(const v8::FunctionCallbackInfo<v8::Value>& args) noexcept {
  args.GetReturnValue().Set(v8::Number::New(isolate, web_clock()));
}

static v8::Local<v8::Uint8Array> copy_bytes(const void* data, size_t n){
  v8::Local<v8::ArrayBuffer> buffer = v8::ArrayBuffer::New(isolate, n);
  if(n > 0)
    std::memcpy(buffer->Data(), data, n);
  return v8::Uint8Array::New(buffer, 0, n);
}

/* Lone surrogates become U+FFFD, as in TextEncoder */
static void native_utf8_encode(const v8::FunctionCallbackInfo<v8::Value>& args) noexcept {
  v8::HandleScope scope(isolate);
  if(args.Length() < 1)
    return;
  try {
    v8::String::Utf8Value str(isolate, args[0]);
    if(*str == nullptr){
      throw_js("Failed to encode the string.");
      return;
    }
    args.GetReturnValue().Set(copy_bytes(*str, static_cast<size_t>(str.length())));
  } catch(...){
    throw_js("Failed to encode the string.");
  }
}

/* Well-formed UTF-8 as defined by the Encoding standard */
static bool valid_utf8(const uint8_t* s, size_t n){
  size_t i = 0;
  while(i < n){
    uint8_t c = s[i];
    size_t len;
    uint8_t lo = 0x80, hi = 0xbf;
    if(c < 0x80){
      i++;
      continue;
    } else if(c >= 0xc2 && c <= 0xdf){
      len = 2;
    } else if(c >= 0xe0 && c <= 0xef){
      len = 3;
      if(c == 0xe0) lo = 0xa0;
      if(c == 0xed) hi = 0x9f;
    } else if(c >= 0xf0 && c <= 0xf4){
      len = 4;
      if(c == 0xf0) lo = 0x90;
      if(c == 0xf4) hi = 0x8f;
    } else {
      return false;
    }
    if(i + len > n || s[i + 1] < lo || s[i + 1] > hi)
      return false;
    for(size_t k = 2; k < len; k++){
      if(s[i + k] < 0x80 || s[i + k] > 0xbf)
        return false;
    }
    i += len;
  }
  return true;
}

/* utf8Decode(view, fatal): invalid bytes become U+FFFD, or with fatal the
 * result is null */
static void native_utf8_decode(const v8::FunctionCallbackInfo<v8::Value>& args) noexcept {
  v8::HandleScope scope(isolate);
  if(args.Length() < 2 || !args[0]->IsArrayBufferView()){
    throw_js("Expected an ArrayBufferView.");
    return;
  }
  v8::Local<v8::ArrayBufferView> view = args[0].As<v8::ArrayBufferView>();
  size_t n = view->ByteLength();
  if(n > static_cast<size_t>(v8::String::kMaxLength)){
    throw_js("The data is too large for a string.");
    return;
  }
  try {
    std::vector<uint8_t> bytes(n);
    if(n > 0)
      view->CopyContents(bytes.data(), n);
    if(args[1]->IsTrue() && !valid_utf8(bytes.data(), n)){
      args.GetReturnValue().SetNull();
      return;
    }
    v8::Local<v8::String> out;
    if(!v8::String::NewFromUtf8(isolate, reinterpret_cast<const char*>(bytes.data()),
                                v8::NewStringType::kNormal, static_cast<int>(n)).ToLocal(&out)){
      throw_js("Failed to decode the data.");
      return;
    }
    args.GetReturnValue().Set(out);
  } catch(...){
    throw_js("Failed to decode the data.");
  }
}

static void native_queue_microtask(const v8::FunctionCallbackInfo<v8::Value>& args) noexcept {
  if(args.Length() < 1 || !args[0]->IsFunction()){
    throw_js("The callback must be a function.");
    return;
  }
  isolate->EnqueueMicrotask(args[0].As<v8::Function>());
}

/* structuredClone() with V8's own serializer, as browsers use it. Without a
 * delegate, V8 throws the error for values it cannot clone itself; libv8 is
 * compiled without RTTI, so its Delegate class cannot be derived from here. */

static void native_clone(const v8::FunctionCallbackInfo<v8::Value>& args) noexcept {
  v8::EscapableHandleScope scope(isolate);
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::Value> value = args.Length() > 0 ? args[0] : v8::Undefined(isolate).As<v8::Value>();
  v8::ValueSerializer serializer(isolate);
  serializer.WriteHeader();
  if(!serializer.WriteValue(context, value).FromMaybe(false))
    return;
  std::pair<uint8_t*, size_t> buffer = serializer.Release();
  v8::ValueDeserializer deserializer(isolate, buffer.first, buffer.second);
  v8::Local<v8::Value> out;
  bool ok = deserializer.ReadHeader(context).FromMaybe(false) && deserializer.ReadValue(context).ToLocal(&out);
  std::free(buffer.first);
  if(ok)
    args.GetReturnValue().Set(scope.Escape(out));
}

/* crypto.getRandomValues() */
static void native_random_fill(const v8::FunctionCallbackInfo<v8::Value>& args) noexcept {
  static std::random_device device;
  v8::Local<v8::Value> value = args.Length() > 0 ? args[0] : v8::Undefined(isolate).As<v8::Value>();
  bool integers = value->IsInt8Array() || value->IsUint8Array() || value->IsUint8ClampedArray() ||
    value->IsInt16Array() || value->IsUint16Array() || value->IsInt32Array() || value->IsUint32Array() ||
    value->IsBigInt64Array() || value->IsBigUint64Array();
  if(!integers){
    throw_js("TypeMismatchError: getRandomValues() needs an integer typed array.");
    return;
  }
  v8::Local<v8::TypedArray> array = value.As<v8::TypedArray>();
  size_t n = array->ByteLength();
  if(n > 65536){
    throw_js("QuotaExceededError: getRandomValues() fills at most 65536 bytes.");
    return;
  }
  try {
    std::vector<uint8_t> bytes(n);
    for(size_t i = 0; i < n; i += 4){
      uint32_t r = static_cast<uint32_t>(device());
      for(size_t k = 0; k < 4 && i + k < n; k++)
        bytes[i + k] = static_cast<uint8_t>(r >> (8 * k));
    }
    uint8_t* data = static_cast<uint8_t*>(array->Buffer()->Data()) + array->ByteOffset();
    if(n > 0)
      std::memcpy(data, bytes.data(), n);
  } catch(...){
    throw_js("Failed to get random values.");
    return;
  }
  args.GetReturnValue().Set(array);
}

static void set_native(v8::Local<v8::ObjectTemplate> object, const char* name, v8::FunctionCallback fn){
  object->Set(js_string(name), v8::FunctionTemplate::New(isolate, fn));
}

/* Defines the web APIs in a new context and keeps the timer function */
void install_web(v8::Local<v8::Context> context){
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> natives = v8::ObjectTemplate::New(isolate);
  set_native(natives, "now", native_now);
  set_native(natives, "utf8Encode", native_utf8_encode);
  set_native(natives, "utf8Decode", native_utf8_decode);
  set_native(natives, "queueMicrotask", native_queue_microtask);
  set_native(natives, "clone", native_clone);
  set_native(natives, "randomFill", native_random_fill);
  natives->Set(js_string("icuMissing"), v8::Boolean::New(isolate, icu_data_missing));
  v8::TryCatch trycatch(isolate);
  v8::Local<v8::Object> native;
  v8::Local<v8::Script> script;
  v8::Local<v8::Value> fn;
  v8::Local<v8::Value> runner;
  v8::Local<v8::Value> argv[1];
  if(!natives->NewInstance(context).ToLocal(&native) ||
     !v8::Script::Compile(context, js_string(web_js)).ToLocal(&script) ||
     !script->Run(context).ToLocal(&fn) || !fn->IsFunction())
    throw std::runtime_error("Failed to set up the web APIs.");
  argv[0] = native;
  if(!fn.As<v8::Function>()->Call(context, context->Global(), 1, argv).ToLocal(&runner) ||
     context->Global()->SetPrivate(context, v8::Private::ForApi(isolate, js_string("V8n::timers")), runner).IsNothing())
    throw std::runtime_error("Failed to set up the web APIs.");
}

/* Runs the timers that are due now, each followed by its microtasks.
 * Returns the milliseconds until the next timer, or -1 if there is none. */
double run_timers(v8::Local<v8::Context> context){
  v8::HandleScope scope(isolate);
  v8::Local<v8::Value> runner;
  if(!context->Global()->GetPrivate(context, v8::Private::ForApi(isolate, js_string("V8n::timers"))).ToLocal(&runner) ||
     !runner->IsFunction())
    return -1;
  v8::Local<v8::Value> argv[1] = {v8::Number::New(isolate, web_clock())};
  for(;;){
    v8::TryCatch trycatch(isolate);
    v8::Local<v8::Value> next;
    if(!runner.As<v8::Function>()->Call(context, context->Global(), 1, argv).ToLocal(&next))
      throw std::runtime_error(trycatch.HasCaught() ? js_text(trycatch.Exception()) : "Failed to run the timers.");
    isolate->PerformMicrotaskCheckpoint();
    double ms = next->NumberValue(context).FromMaybe(-1);
    if(ms != 0)
      return ms;
  }
}
