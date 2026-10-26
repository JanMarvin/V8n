#ifndef V8N_H
#define V8N_H

#include "V8n_types.h"
#include <cstdint>
#include <string>

#if V8_MAJOR_VERSION * 100 + V8_MINOR_VERSION < 1103
#error "V8n requires V8 11.3 or newer"
#endif

/* One isolate is shared by all contexts, created when the package is loaded. */
extern v8::Isolate* isolate;
extern v8::Platform* platform;

/* Functions that V8 calls are noexcept: V8 is compiled without exceptions
 * (and often by another compiler), so no C++ exception may unwind through
 * its frames. They catch everything and report errors as JavaScript
 * exceptions instead; anything left over ends in std::terminate(). */

/* engine.cpp */
v8::Local<v8::String> js_string(const char* str);
std::string js_text(v8::Local<v8::Value> value);
void throw_js(const char* message) noexcept;

/* watch.cpp: stops JavaScript on a user interrupt, a timeout or when it
 * runs out of memory, see watch.cpp. A scope spans one call from R; nested
 * calls (R callbacks that call JavaScript again) share the outermost one. */
class watch_scope {
public:
  explicit watch_scope(double timeout);
  ~watch_scope();
  watch_scope(const watch_scope&) = delete;
  watch_scope& operator=(const watch_scope&) = delete;
  bool is_outermost() const;
  void check() const;
private:
  bool outermost;
};
void watch_install(v8::Isolate* iso);
bool watch_stopped();

/* web.cpp */
void install_web(v8::Local<v8::Context> context);
double run_timers(v8::Local<v8::Context> context);
double web_clock();

/* pin.cpp */
void pin_library();
std::string library_path(const void* address);
uintptr_t stack_low_end();
extern bool icu_data_missing;
uint64_t physical_memory();

/* console.cpp */
void install_console(v8::Local<v8::Context> context);
v8::Local<v8::FunctionTemplate> print_template();

/* convert.cpp */
v8::Local<v8::Value> r_to_js(SEXP x, SEXP options, v8::Local<v8::Context> context);
SEXP js_to_r(v8::Local<v8::Value> value, v8::Local<v8::Value> options, v8::Local<v8::Context> context);
v8::Local<v8::Object> options_to_js(SEXP options);
SEXP serialize_value(v8::Local<v8::Value> value, v8::Local<v8::Context> context);

#endif
