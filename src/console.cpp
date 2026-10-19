#include "v8n.h"
#include <libplatform/libplatform.h>

/* The environment that holds the R side of the console.r callbacks,
 * set from .onLoad() so that the package name is not hard-coded here. */
static SEXP callback_env = R_NilValue;

// [[Rcpp::export]]
void set_callback_env(SEXP env){
  if(callback_env != R_NilValue)
    R_ReleaseObject(callback_env);
  R_PreserveObject(env);
  callback_env = env;
}

static std::string join_args(const v8::FunctionCallbackInfo<v8::Value>& args){
  std::string out;
  for(int i = 0; i < args.Length(); i++){
    if(i > 0)
      out += " ";
    out += js_text(args[i]);
  }
  return out;
}

static void console_log(const v8::FunctionCallbackInfo<v8::Value>& args) noexcept {
  v8::HandleScope scope(isolate);
  try {
    Rprintf("%s\n", join_args(args).c_str());
  } catch(...){
    throw_js("Failed to print the arguments.");
  }
}

static void r_warning(void* message){
  Rf_warningcall_immediate(R_NilValue, "%s", static_cast<std::string*>(message)->c_str());
}

/* An R error must not jump over the JavaScript frames. With
 * options(warn = 2) the warning is thrown as a JavaScript exception with the
 * message R would give, otherwise R_ToplevelExec() catches what a warning
 * handler might raise. */
static void console_warn(const v8::FunctionCallbackInfo<v8::Value>& args) noexcept {
  v8::HandleScope scope(isolate);
  try {
    std::string message = join_args(args);
    if(Rf_asInteger(Rf_GetOption1(Rf_install("warn"))) >= 2)
      message = "(converted from warning) " + message;
    else if(R_ToplevelExec(r_warning, &message))
      return;
    v8::Local<v8::String> str;
    if(v8::String::NewFromUtf8(isolate, message.c_str(), v8::NewStringType::kNormal).ToLocal(&str))
      isolate->ThrowException(str);
  } catch(...){
    throw_js("Failed to give the warning.");
  }
}

static void console_error(const v8::FunctionCallbackInfo<v8::Value>& args) noexcept {
  if(args.Length() > 0)
    isolate->ThrowException(args[0]);
}

/* Runs pending tasks, promise callbacks and due timers */
static void console_pump(const v8::FunctionCallbackInfo<v8::Value>&) noexcept {
  v8::HandleScope scope(isolate);
  v8::platform::PumpMessageLoop(platform, isolate, v8::platform::MessageLoopBehavior::kDoNotWait);
  isolate->PerformMicrotaskCheckpoint();
  try {
    run_timers(isolate->GetCurrentContext());
  } catch(const std::exception& e){
    throw_js(e.what());
  } catch(...){
    throw_js("Failed to run the timers.");
  }
}

/* console.r.call(fun, args), console.r.get(expr, options),
 * console.r.eval(code, options) and console.r.assign(name, value, options)
 * call the R function of the same name in callback_env with the arguments
 * converted to R. For assign, the options also control how the value is
 * converted. The R function catches all R errors and returns either
 * list(value, options) to convert back, NULL, or an error message. */
static void r_callback(const char* name, const v8::FunctionCallbackInfo<v8::Value>& args, bool assign) noexcept {
  v8::HandleScope scope(isolate);
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::Value> undefined = v8::Undefined(isolate);
  v8::Local<v8::Value> first = args.Length() > 0 ? args[0] : undefined;
  v8::Local<v8::Value> second = args.Length() > 1 ? args[1] : undefined;
  v8::Local<v8::Value> third = args.Length() > 2 ? args[2] : undefined;
  try {
    Rcpp::Function fun = Rcpp::Environment(callback_env).get(name);
    Rcpp::String str(js_text(first), CE_UTF8);
    Rcpp::RObject out;
    if(assign){
      out = fun(str, js_to_r(second, third, context), js_to_r(third, undefined, context));
    } else {
      out = fun(str, js_to_r(second, undefined, context));
    }
    if(Rf_inherits(out, "cb_error")){
      throw_js(Rf_translateCharUTF8(STRING_ELT(out, 0)));
    } else if(TYPEOF(out) == VECSXP && Rf_xlength(out) == 2){
      args.GetReturnValue().Set(r_to_js(VECTOR_ELT(out, 0), VECTOR_ELT(out, 1), context));
    } else {
      args.GetReturnValue().SetNull();
    }
  } catch(const std::exception& e){
    throw_js(e.what());
  } catch(...){
    throw_js("R callback was interrupted.");
  }
}

static void console_r_call(const v8::FunctionCallbackInfo<v8::Value>& args) noexcept {
  r_callback("r_call", args, false);
}

static void console_r_get(const v8::FunctionCallbackInfo<v8::Value>& args) noexcept {
  r_callback("r_get", args, false);
}

static void console_r_eval(const v8::FunctionCallbackInfo<v8::Value>& args) noexcept {
  r_callback("r_eval", args, false);
}

static void console_r_assign(const v8::FunctionCallbackInfo<v8::Value>& args) noexcept {
  r_callback("r_assign", args, true);
}

/* Replaces the console object of V8, which only prints in the inspector */
void install_console(v8::Local<v8::Context> context){
  v8::Local<v8::ObjectTemplate> console = v8::ObjectTemplate::New(isolate);
  console->Set(js_string("log"), v8::FunctionTemplate::New(isolate, console_log));
  console->Set(js_string("info"), v8::FunctionTemplate::New(isolate, console_log));
  console->Set(js_string("debug"), v8::FunctionTemplate::New(isolate, console_log));
  console->Set(js_string("warn"), v8::FunctionTemplate::New(isolate, console_warn));
  console->Set(js_string("error"), v8::FunctionTemplate::New(isolate, console_error));
  console->Set(js_string("pump"), v8::FunctionTemplate::New(isolate, console_pump));

  v8::Local<v8::ObjectTemplate> console_r = v8::ObjectTemplate::New(isolate);
  console_r->Set(js_string("call"), v8::FunctionTemplate::New(isolate, console_r_call));
  console_r->Set(js_string("get"), v8::FunctionTemplate::New(isolate, console_r_get));
  console_r->Set(js_string("eval"), v8::FunctionTemplate::New(isolate, console_r_eval));
  console_r->Set(js_string("assign"), v8::FunctionTemplate::New(isolate, console_r_assign));
  console->Set(js_string("r"), console_r);

  v8::Local<v8::Object> global = context->Global();
  v8::Local<v8::String> name = js_string("console");
  v8::Local<v8::Object> instance;
  if(!console->NewInstance(context).ToLocal(&instance) ||
     global->Delete(context, name).IsNothing() ||
     global->Set(context, name, instance).IsNothing())
    throw std::runtime_error("Failed to create the console object.");
}

/* print() is an alias of console.log() that some libraries expect */
v8::Local<v8::FunctionTemplate> print_template(){
  return v8::FunctionTemplate::New(isolate, console_log);
}
