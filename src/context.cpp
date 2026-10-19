#include "v8n.h"
#include <libplatform/libplatform.h>
#include <chrono>
#include <thread>

void ctx_finalizer(ctx_type* ctx){
  if(ctx != nullptr)
    ctx->Reset();
  delete ctx;
}

static void check_context(const ctxptr& ctx){
  if(!ctx)
    throw std::runtime_error("This V8 context has been disposed.");
}

/* Waits for a promise and returns its value, or throws its rejection.
 * Meanwhile it runs background tasks, promise callbacks and timers, and
 * sleeps for a moment when there is nothing to do. */
static v8::Local<v8::Value> await_promise(v8::Local<v8::Value> value, v8::Local<v8::Context> context){
  if(!value->IsPromise())
    return value;
  v8::Local<v8::Promise> promise = value.As<v8::Promise>();
  while(promise->State() == v8::Promise::kPending){
    bool busy = v8::platform::PumpMessageLoop(platform, isolate, v8::platform::MessageLoopBehavior::kDoNotWait);
    isolate->PerformMicrotaskCheckpoint();
    if(promise->State() != v8::Promise::kPending)
      break;
    double next = run_timers(context);
    if(promise->State() != v8::Promise::kPending)
      break;
    if(watch_stopped())
      throw std::runtime_error("JavaScript was stopped.");
    Rcpp::checkUserInterrupt();
    if(!busy && next != 0){
      double ms = (next < 0 || next > 10) ? (next < 0 ? 1 : 10) : next;
      std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(ms));
    }
  }
  if(promise->State() == v8::Promise::kRejected)
    throw std::runtime_error(js_text(promise->Result()));
  return promise->Result();
}

/* Syntax errors are thrown as std::invalid_argument, errors at runtime as
 * std::runtime_error, which become the classes of the R condition. */
static v8::Local<v8::Value> run_source(Rcpp::String src, v8::Local<v8::Context> context, bool await){
  src.set_encoding(CE_UTF8);
  v8::TryCatch trycatch(isolate);
  v8::Local<v8::Script> script;
  if(!v8::Script::Compile(context, js_string(src.get_cstring())).ToLocal(&script)){
    if(trycatch.HasCaught())
      throw std::invalid_argument(js_text(trycatch.Exception()));
    throw std::runtime_error("Failed to compile the script (check memory or stack limits).");
  }
  v8::Local<v8::Value> result;
  if(!script->Run(context).ToLocal(&result)){
    if(trycatch.HasCaught())
      throw std::runtime_error(js_text(trycatch.Exception()));
    throw std::runtime_error("Failed to run the script (check memory or stack limits).");
  }
  return await ? await_promise(result, context) : result;
}

// [[Rcpp::export]]
ctxptr make_context(bool console){
  v8::Isolate::Scope isolate_scope(isolate);
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::ObjectTemplate> global = v8::ObjectTemplate::New(isolate);
  global->Set(js_string("print"), print_template());
  v8::Local<v8::Context> context = v8::Context::New(isolate, nullptr, global);
  if(context.IsEmpty())
    throw std::runtime_error("Failed to create a V8 context (check memory or stack limits).");
  v8::Context::Scope context_scope(context);
  install_web(context);
  if(console)
    install_console(context);
  return ctxptr(new ctx_type(isolate, context));
}

// [[Rcpp::export]]
bool context_null(ctxptr ctx){
  return !ctx;
}

/* Each call from R below runs inside a watch_scope. When the watcher stopped
 * the script, the error that V8 gives is replaced by the reason. After the
 * outermost call, timers that are due by then run, as after a script in a
 * browser. */

/* Returns the result as text, or with serialize = TRUE as JSON text or raw bytes */
// [[Rcpp::export]]
SEXP context_eval(Rcpp::String src, ctxptr ctx, bool serialize, bool await, double timeout){
  check_context(ctx);
  watch_scope watch(timeout);
  try {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = ctx->Get(isolate);
    v8::Context::Scope context_scope(context);
    v8::Local<v8::Value> result = run_source(src, context, await);
    if(watch.is_outermost())
      run_timers(context);
    if(serialize)
      return serialize_value(result, context);
    return Rcpp::CharacterVector::create(Rcpp::String(js_text(result), CE_UTF8));
  } catch(...){
    watch.check();
    throw;
  }
}

// [[Rcpp::export]]
SEXP context_get(Rcpp::String src, ctxptr ctx, bool await, SEXP options, double timeout){
  check_context(ctx);
  watch_scope watch(timeout);
  try {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = ctx->Get(isolate);
    v8::Context::Scope context_scope(context);
    v8::Local<v8::Value> result = run_source(src, context, await);
    if(watch.is_outermost())
      run_timers(context);
    return js_to_r(result, options_to_js(options), context);
  } catch(...){
    watch.check();
    throw;
  }
}

/* Sets a property of the global object; an existing property is deleted
 * first so that a getter or a non-writable value does not get in the way. */
// [[Rcpp::export]]
bool context_assign(Rcpp::String name, SEXP value, SEXP options, ctxptr ctx){
  check_context(ctx);
  watch_scope watch(R_PosInf);
  try {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = ctx->Get(isolate);
    v8::Context::Scope context_scope(context);
    name.set_encoding(CE_UTF8);
    v8::Local<v8::Value> js_value = r_to_js(value, options, context);
    v8::Local<v8::String> key = js_string(name.get_cstring());
    v8::Local<v8::Object> global = context->Global();
    if(global->Has(context, key).FromMaybe(true) && global->Delete(context, key).IsNothing())
      return false;
    return global->Set(context, key, js_value).FromMaybe(false);
  } catch(...){
    watch.check();
    throw;
  }
}

/* src evaluates to a function that takes the converted arguments as one array */
// [[Rcpp::export]]
SEXP context_call(Rcpp::String src, SEXP args, SEXP to_options, SEXP from_options, ctxptr ctx, bool await,
                  double timeout){
  check_context(ctx);
  watch_scope watch(timeout);
  try {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = ctx->Get(isolate);
    v8::Context::Scope context_scope(context);
    v8::Local<v8::Value> fn = run_source(src, context, false);
    if(!fn->IsFunction())
      throw std::invalid_argument("The call wrapper did not evaluate to a function.");
    v8::Local<v8::Value> argv[1] = {r_to_js(args, to_options, context)};
    v8::TryCatch trycatch(isolate);
    v8::Local<v8::Value> result;
    if(!fn.As<v8::Function>()->Call(context, context->Global(), 1, argv).ToLocal(&result))
      throw std::runtime_error(js_text(trycatch.Exception()));
    if(await)
      result = await_promise(result, context);
    if(watch.is_outermost())
      run_timers(context);
    return js_to_r(result, options_to_js(from_options), context);
  } catch(...){
    watch.check();
    throw;
  }
}

// [[Rcpp::export]]
bool context_validate(Rcpp::String src, ctxptr ctx){
  check_context(ctx);
  v8::Isolate::Scope isolate_scope(isolate);
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Context> context = ctx->Get(isolate);
  v8::Context::Scope context_scope(context);
  src.set_encoding(CE_UTF8);
  v8::TryCatch trycatch(isolate);
  v8::Local<v8::Script> script;
  return v8::Script::Compile(context, js_string(src.get_cstring())).ToLocal(&script);
}
