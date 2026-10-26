#include "v8n.h"
#include <libplatform/libplatform.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>

/* __has_feature is a clang-ism, while __SANITIZE_ADDRESS__ is a gcc-ism */
#if defined(__clang__) && !defined(__SANITIZE_ADDRESS__)
#if defined(__has_feature) && __has_feature(address_sanitizer)
#define __SANITIZE_ADDRESS__ 1
#endif
#endif

v8::Isolate* isolate = nullptr;
v8::Platform* platform = nullptr;
bool icu_data_missing = false;

/* V8 aborts the process for a string above its maximum length, about 512MB
 * (2^29 - 24 characters on 64-bit). */
v8::Local<v8::String> js_string(const char* str){
  size_t length = std::strlen(str);
  if(length > static_cast<size_t>(v8::String::kMaxLength))
    throw std::runtime_error("A string of " + std::to_string(length) +
                             " bytes is longer than JavaScript allows (" +
                             std::to_string(v8::String::kMaxLength) + " characters).");
  v8::Local<v8::String> out;
  if(!v8::String::NewFromUtf8(isolate, str, v8::NewStringType::kNormal, static_cast<int>(length)).ToLocal(&out))
    throw std::runtime_error("Failed to create a JavaScript string (check memory limits).");
  return out;
}

std::string js_text(v8::Local<v8::Value> value){
  v8::String::Utf8Value str(isolate, value);
  if(*str == nullptr)
    return "<string conversion failed>";
  return std::string(*str, static_cast<size_t>(str.length()));
}

/* Used inside callbacks from JavaScript, where C++ exceptions must not escape. */
void throw_js(const char* message) noexcept {
  v8::Local<v8::String> str;
  if(!v8::String::NewFromUtf8(isolate, message, v8::NewStringType::kNormal).ToLocal(&str))
    str = v8::String::Empty(isolate);
  isolate->ThrowException(v8::Exception::Error(str));
}

static void message_callback(v8::Local<v8::Message> message, v8::Local<v8::Value>) noexcept {
  try {
    REprintf("V8 MESSAGE (level %d): %s\n", message->ErrorLevel(), js_text(message->Get()).c_str());
  } catch(...){
    REprintf("V8 MESSAGE (level %d)\n", message->ErrorLevel());
  }
}

static void fatal_callback(const char* location, const char* message) noexcept {
  REprintf("V8 FATAL ERROR in %s: %s\n", location, message);
}

/* ES modules are loaded from files relative to the working directory */
static std::string read_module_file(const std::string& path){
  if(path.empty() || (path[0] != '.' && path[0] != '/'))
    throw std::runtime_error("Invalid module: " + path + " (paths should begin with . or /)");
  std::ifstream file(path);
  if(file.fail())
    throw std::runtime_error("Failed to open file: " + path);
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

static v8::MaybeLocal<v8::Module> resolve_module(v8::Local<v8::Context> context, v8::Local<v8::String> specifier,
                                                 v8::Local<v8::FixedArray>, v8::Local<v8::Module>) noexcept;

static v8::Local<v8::Module> load_module(const std::string& path, v8::Local<v8::Context> context){
  v8::Local<v8::String> source_text = js_string(read_module_file(path).c_str());
  v8::Local<v8::String> name = js_string(path.c_str());
#if V8_MAJOR_VERSION * 100 + V8_MINOR_VERSION < 1201
  v8::ScriptOrigin origin(isolate, name, 0, 0, false, -1, v8::Local<v8::Value>(), false, false, true);
#else
  v8::ScriptOrigin origin(name, 0, 0, false, -1, v8::Local<v8::Value>(), false, false, true);
#endif
  v8::ScriptCompiler::Source source(source_text, origin);
  v8::TryCatch trycatch(isolate);
  v8::Local<v8::Module> module;
  v8::Local<v8::Value> result;
  if(!v8::ScriptCompiler::CompileModule(isolate, &source).ToLocal(&module) ||
     !module->InstantiateModule(context, resolve_module).FromMaybe(false) ||
     !module->Evaluate(context).ToLocal(&result)){
    std::string reason = trycatch.HasCaught() ? js_text(trycatch.Exception()) : "unknown error";
    throw std::runtime_error("Failed to import ES module '" + path + "': " + reason);
  }
  return module;
}

static v8::MaybeLocal<v8::Module> resolve_module(v8::Local<v8::Context> context, v8::Local<v8::String> specifier,
                                                 v8::Local<v8::FixedArray>, v8::Local<v8::Module>) noexcept {
  try {
    return load_module(js_text(specifier), context);
  } catch(const std::exception& e){
    throw_js(e.what());
  } catch(...){
    throw_js("Failed to load the module.");
  }
  return v8::MaybeLocal<v8::Module>();
}

static v8::MaybeLocal<v8::Promise> import_module(v8::Local<v8::Context> context, v8::Local<v8::Data>,
                                                 v8::Local<v8::Value>, v8::Local<v8::String> specifier,
                                                 v8::Local<v8::FixedArray>) noexcept {
  v8::Local<v8::Promise::Resolver> resolver;
  if(!v8::Promise::Resolver::New(context).ToLocal(&resolver))
    return v8::MaybeLocal<v8::Promise>();
  try {
    v8::Local<v8::Module> module = load_module(js_text(specifier), context);
    resolver->Resolve(context, module->GetModuleNamespace()).FromMaybe(false);
  } catch(const std::exception& e){
    v8::Local<v8::String> message;
    if(v8::String::NewFromUtf8(isolate, e.what(), v8::NewStringType::kNormal).ToLocal(&message))
      resolver->Reject(context, message).FromMaybe(false);
  } catch(...){
    resolver->Reject(context, v8::String::Empty(isolate)).FromMaybe(false);
  }
  return resolver->GetPromise();
}

/* Depending on its build, libv8 has no ICU data (and no Intl), has it built
 * in, uses the ICU of the system, or reads it from a file icudtl.dat. Only
 * the last needs a file, which is looked for next to libv8, unless
 * options(V8n.icu_data) names it. InitializeICU() is false only if the file
 * is needed and cannot be loaded; Intl would then end the process. */
static bool load_icu_data(){
  std::string file;
  SEXP option = Rf_GetOption1(Rf_install("V8n.icu_data"));
  if(Rf_isString(option) && Rf_length(option) == 1 && STRING_ELT(option, 0) != NA_STRING)
    file = R_ExpandFileName(Rf_translateChar(STRING_ELT(option, 0)));
  if(file.empty()){
    const char* (*get_version)() = &v8::V8::GetVersion;
    const void* address = nullptr;
    static_assert(sizeof(address) == sizeof(get_version), "function pointers must fit into void*");
    std::memcpy(&address, &get_version, sizeof(address));
    std::string library = library_path(address);
    size_t end = library.find_last_of("/\\");
    if(end != std::string::npos)
      file = library.substr(0, end + 1) + "icudtl.dat";
  }
  return v8::V8::InitializeICU(file.empty() ? nullptr : file.c_str());
}

static v8::Isolate* create_isolate(){
  icu_data_missing = !load_icu_data();
  /* The platform lives as long as the process */
  std::unique_ptr<v8::Platform> owned = v8::platform::NewDefaultPlatform();
  platform = owned.release();
  v8::V8::InitializePlatform(platform);
#if V8_MAJOR_VERSION * 100 + V8_MINOR_VERSION >= 1401 && V8_MAJOR_VERSION * 100 + V8_MINOR_VERSION < 1404
  v8::V8::SetFlagsFromString("--harmony-temporal");
#endif
  v8::V8::Initialize();
  v8::Isolate::CreateParams params;
  params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
  /* Size the heap by the memory of the machine, as Node.js does. The built-in
   * defaults of V8 are small, so that large data keeps the garbage collector
   * busy close to the limit. */
  uint64_t memory = physical_memory();
  if(memory > 0)
    params.constraints.ConfigureDefaults(memory, 0);
  /* options(V8n.heap_limit = <MB>) set before loading limits the heap */
  SEXP limit = Rf_GetOption1(Rf_install("V8n.heap_limit"));
  if(Rf_isNumeric(limit) && Rf_length(limit) == 1){
    double mb = Rf_asReal(limit);
    if(std::isfinite(mb) && mb > 0)
      params.constraints.set_max_old_generation_size_in_bytes(static_cast<size_t>(mb * 1024 * 1024));
  }
#ifdef V8_COMPRESS_POINTERS
  /* With pointer compression all JavaScript objects share one region of 4GB.
   * Close to its end, V8 aborts the process before a script can be stopped
   * (at about 3.4GB with V8 12.4), so the limit stays below that. */
  const size_t max_heap = static_cast<size_t>(3000) * 1024 * 1024;
  if(params.constraints.max_old_generation_size_in_bytes() > max_heap)
    params.constraints.set_max_old_generation_size_in_bytes(max_heap);
#endif
  v8::Isolate* out = v8::Isolate::New(params);
  if(out == nullptr)
    return nullptr;
  /* The isolate stays entered, so that a later copy of this library finds
   * it, see start_v8_isolate(). Recent versions of V8 also expect it to be
   * entered for the calls below. */
  out->Enter();
  out->SetData(0, platform);
  v8::HandleScope handle_scope(out);
  watch_install(out);
  out->AddMessageListener(message_callback);
  out->SetFatalErrorHandler(fatal_callback);
  /* JavaScript may use the stack below this point down to a reserve at its
   * end, which is left to R and C++; V8 enforces that with a RangeError. The
   * end of the stack is asked from the system, as V8 does it. Sanitizers use
   * larger stack frames, so they keep a larger reserve. With an unlimited
   * stack the limit is 64MB below this point, and if the system does not
   * tell, 2MB. A limit of 1, to disable it, does not work with newer V8,
   * which derives the size of the stack from it (see jeroen/V8#207). */
  const uintptr_t position = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
  const uintptr_t low = stack_low_end();
  const uintptr_t max_size = static_cast<uintptr_t>(64) * 1024 * 1024;
  uintptr_t stack_limit = position - static_cast<uintptr_t>(2000) * 1024;
  if(low != 0 && low < position){
    const uintptr_t room = position - low;
#ifdef __SANITIZE_ADDRESS__
    stack_limit = low + room / 4;
#else
    stack_limit = low + room / 8;
#endif
    if(position - stack_limit > max_size)
      stack_limit = position - max_size;
  }
  out->SetStackLimit(stack_limit);
  out->SetHostImportModuleDynamicallyCallback(import_module);
  return out;
}

/* V8 can only be initialized once per process, or once per copy of this
 * library when libv8 is linked statically. R runs this function again when
 * it loads the same library again, and pkgload::load_all() loads a rebuilt
 * package as a new copy. With a shared libv8, V8 then already runs in this
 * process, started by the earlier copy, and its isolate and platform are
 * used again. */
// [[Rcpp::init]]
void start_v8_isolate(DllInfo* /*dll*/){
  if(isolate != nullptr)
    return;
  pin_library();
  v8::Isolate* running = v8::Isolate::TryGetCurrent();
  if(running != nullptr){
    isolate = running;
    platform = static_cast<v8::Platform*>(running->GetData(0));
    icu_data_missing = !load_icu_data();
    watch_install(running);
    return;
  }
  isolate = create_isolate();
  if(isolate == nullptr)
    Rf_error("Failed to create the V8 isolate.");
}

// [[Rcpp::export]]
std::string version(){
  return v8::V8::GetVersion();
}

// [[Rcpp::export]]
bool icu_missing(){
  return icu_data_missing;
}
