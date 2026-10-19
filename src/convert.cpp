#include "v8n.h"
#include "convert_js.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

/* R values are converted in two steps. C++ copies the vectors into typed
 * arrays inside a tree of nodes, then a JavaScript function builds the
 * values from the nodes, following the options. The way back works the
 * same. convert_js.h describes the nodes. */
enum node_tag : int32_t {
  TAG_NULL = 0,
  TAG_LOGICAL = 1,
  TAG_INTEGER = 2,
  TAG_DOUBLE = 3,
  TAG_STRING = 4,
  TAG_RAW = 5,
  TAG_LIST = 6,
  TAG_FRAME = 7,
  TAG_ARRAY = 8,
  TAG_JSON = 9
};

template <typename T>
static v8::Local<v8::ArrayBuffer> array_buffer(const T* data, size_t n){
  size_t bytes = n * sizeof(T);
  v8::Local<v8::ArrayBuffer> buffer = v8::ArrayBuffer::New(isolate, bytes);
  if(bytes > 0)
    std::memcpy(buffer->Data(), data, bytes);
  return buffer;
}

static v8::Local<v8::Value> tag(node_tag t){
  return v8::Integer::New(isolate, t);
}

static v8::Local<v8::String> js_name(SEXP str){
  return js_string(str == NA_STRING ? "NA" : Rf_translateCharUTF8(str));
}

/* Converts a function source to a function once per context and keeps it
 * in a private property of the global object. */
static v8::Local<v8::Function> cached_function(const char* key, const char* code, v8::Local<v8::Context> context){
  v8::Local<v8::Private> name = v8::Private::ForApi(isolate, js_string(key));
  v8::Local<v8::Object> global = context->Global();
  v8::Local<v8::Value> fn;
  if(global->GetPrivate(context, name).ToLocal(&fn) && fn->IsFunction())
    return fn.As<v8::Function>();
  v8::Local<v8::Script> script;
  if(!v8::Script::Compile(context, js_string(code)).ToLocal(&script) ||
     !script->Run(context).ToLocal(&fn) || !fn->IsFunction() ||
     global->SetPrivate(context, name, fn).IsNothing())
    throw std::runtime_error(std::string("Failed to compile the converter ") + key);
  return fn.As<v8::Function>();
}

v8::Local<v8::Object> options_to_js(SEXP options){
  v8::EscapableHandleScope scope(isolate);
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::Object> out = v8::Object::New(isolate);
  SEXP names = Rf_getAttrib(options, R_NamesSymbol);
  if(TYPEOF(options) != VECSXP || Rf_isNull(names))
    return scope.Escape(out);
  for(R_xlen_t i = 0; i < Rf_xlength(options); i++){
    SEXP val = VECTOR_ELT(options, i);
    if(Rf_xlength(val) < 1)
      continue;
    v8::Local<v8::Value> jsval;
    switch(TYPEOF(val)){
    case STRSXP:
      jsval = js_name(STRING_ELT(val, 0));
      break;
    case LGLSXP:
      if(LOGICAL(val)[0] == NA_LOGICAL)
        continue;
      jsval = v8::Boolean::New(isolate, LOGICAL(val)[0] != 0);
      break;
    case INTSXP:
      jsval = v8::Number::New(isolate, static_cast<double>(INTEGER(val)[0]));
      break;
    case REALSXP:
      jsval = v8::Number::New(isolate, REAL(val)[0]);
      break;
    default:
      continue;
    }
    if(out->Set(context, js_name(STRING_ELT(names, i)), jsval).IsNothing())
      throw std::runtime_error("Failed to set an option.");
  }
  return scope.Escape(out);
}

/* True for values that need no preparation in R before the conversion:
 * vectors and lists without classes other than AsIs, and factors. */
static bool plain_value(SEXP x){
  switch(TYPEOF(x)){
  case NILSXP:
    return true;
  case LGLSXP:
  case INTSXP:
  case REALSXP:
  case STRSXP:
  case RAWSXP:
  case VECSXP: {
    SEXP cls = Rf_getAttrib(x, R_ClassSymbol);
    for(R_xlen_t i = 0; i < Rf_xlength(cls); i++){
      std::string name(CHAR(STRING_ELT(cls, i)));
      bool factor = TYPEOF(x) == INTSXP && (name == "factor" || name == "ordered");
      if(name != "AsIs" && !factor)
        return false;
    }
    if(TYPEOF(x) == VECSXP){
      for(R_xlen_t i = 0; i < Rf_xlength(x); i++){
        if(!plain_value(VECTOR_ELT(x, i)))
          return false;
      }
    }
    return true;
  }
  default:
    return false;
  }
}

// [[Rcpp::export]]
bool is_plain_value(SEXP x){
  return plain_value(x);
}

/* [tag, data, extra, scalar] for logical, integer, double and character
 * vectors and factors */
static v8::Local<v8::Value> vector_node(SEXP x, bool scalar){
  v8::EscapableHandleScope scope(isolate);
  size_t n = static_cast<size_t>(Rf_xlength(x));
  v8::Local<v8::Value> node[4];
  node[2] = v8::Null(isolate);
  node[3] = v8::Boolean::New(isolate, scalar);
  if(TYPEOF(x) == REALSXP){
    /* Whole numbers go as Int32Array, which V8 stores without boxing */
    const double* v = REAL(x);
    bool whole = true;
    bool has_na = false;
    for(size_t i = 0; i < n; i++){
      double d = v[i];
      if(R_IsNA(d)){
        has_na = true;
      } else if(whole){
        whole = d >= -2147483647.0 && d <= 2147483647.0 && d == std::floor(d) && !(d == 0 && std::signbit(d));
      }
    }
    node[0] = tag(TAG_DOUBLE);
    if(whole){
      std::vector<int32_t> ints(n);
      for(size_t i = 0; i < n; i++)
        ints[i] = R_IsNA(v[i]) ? 0 : static_cast<int32_t>(v[i]);
      node[1] = v8::Int32Array::New(array_buffer(ints.data(), n), 0, n);
    } else {
      node[1] = v8::Float64Array::New(array_buffer(v, n), 0, n);
    }
    if(has_na){
      std::vector<uint8_t> mask(n);
      for(size_t i = 0; i < n; i++)
        mask[i] = R_IsNA(v[i]) ? 1 : 0;
      node[2] = v8::Uint8Array::New(array_buffer(mask.data(), n), 0, n);
    }
  } else if(TYPEOF(x) == STRSXP){
    /* Unique strings are converted once, the vector holds 1-based codes */
    std::unordered_map<SEXP, int32_t> index;
    std::vector<int32_t> codes(n);
    std::vector<v8::Local<v8::Value>> levels;
    for(size_t i = 0; i < n; i++){
      SEXP el = STRING_ELT(x, static_cast<R_xlen_t>(i));
      if(el == NA_STRING){
        codes[i] = NA_INTEGER;
        continue;
      }
      std::unordered_map<SEXP, int32_t>::iterator it = index.find(el);
      if(it != index.end()){
        codes[i] = it->second;
        continue;
      }
      levels.push_back(js_string(Rf_translateCharUTF8(el)));
      codes[i] = static_cast<int32_t>(levels.size());
      index[el] = codes[i];
    }
    node[0] = tag(TAG_STRING);
    node[1] = v8::Int32Array::New(array_buffer(codes.data(), n), 0, n);
    node[2] = v8::Array::New(isolate, levels.data(), levels.size());
  } else if(Rf_isFactor(x)){
    SEXP lev = Rf_getAttrib(x, R_LevelsSymbol);
    size_t nlev = static_cast<size_t>(Rf_xlength(lev));
    std::vector<v8::Local<v8::Value>> levels(nlev);
    for(size_t i = 0; i < nlev; i++)
      levels[i] = js_name(STRING_ELT(lev, static_cast<R_xlen_t>(i)));
    node[0] = tag(TAG_STRING);
    node[1] = v8::Int32Array::New(array_buffer(INTEGER(x), n), 0, n);
    node[2] = v8::Array::New(isolate, levels.data(), nlev);
  } else if(TYPEOF(x) == INTSXP){
    node[0] = tag(TAG_INTEGER);
    node[1] = v8::Int32Array::New(array_buffer(INTEGER(x), n), 0, n);
  } else {
    node[0] = tag(TAG_LOGICAL);
    node[1] = v8::Int32Array::New(array_buffer(LOGICAL(x), n), 0, n);
  }
  return scope.Escape(v8::Array::New(isolate, node, 4));
}

static v8::Local<v8::Value> r_to_node(SEXP x, bool auto_unbox){
  v8::EscapableHandleScope scope(isolate);
  size_t n = static_cast<size_t>(Rf_xlength(x));
  switch(TYPEOF(x)){
  case NILSXP: {
    v8::Local<v8::Value> node[1] = {tag(TAG_NULL)};
    return scope.Escape(v8::Array::New(isolate, node, 1));
  }
  case LGLSXP:
  case INTSXP:
  case REALSXP:
  case STRSXP: {
    if(TYPEOF(x) == STRSXP && n == 1 && Rf_inherits(x, "json")){
      v8::Local<v8::Value> node[2] = {tag(TAG_JSON), js_string(Rf_translateCharUTF8(STRING_ELT(x, 0)))};
      return scope.Escape(v8::Array::New(isolate, node, 2));
    }
    SEXP dims = Rf_getAttrib(x, R_DimSymbol);
    if(Rf_isNull(dims) || Rf_xlength(dims) < 2){
      bool scalar = auto_unbox && n == 1 && !Rf_inherits(x, "AsIs");
      return scope.Escape(vector_node(x, scalar));
    }
    size_t ndims = static_cast<size_t>(Rf_xlength(dims));
    std::vector<v8::Local<v8::Value>> dimvals(ndims);
    for(size_t i = 0; i < ndims; i++)
      dimvals[i] = v8::Integer::New(isolate, INTEGER(dims)[i]);
    v8::Local<v8::Value> node[3] = {
      tag(TAG_ARRAY),
      vector_node(x, false),
      v8::Array::New(isolate, dimvals.data(), ndims)
    };
    return scope.Escape(v8::Array::New(isolate, node, 3));
  }
  case RAWSXP: {
    v8::Local<v8::Value> node[2] = {tag(TAG_RAW), v8::Uint8Array::New(array_buffer(RAW(x), n), 0, n)};
    return scope.Escape(v8::Array::New(isolate, node, 2));
  }
  case VECSXP: {
    std::vector<v8::Local<v8::Value>> children(n);
    for(size_t i = 0; i < n; i++)
      children[i] = r_to_node(VECTOR_ELT(x, static_cast<R_xlen_t>(i)), auto_unbox);
    SEXP names = Rf_getAttrib(x, R_NamesSymbol);
    v8::Local<v8::Value> jsnames = v8::Null(isolate);
    if(!Rf_isNull(names)){
      std::vector<v8::Local<v8::Value>> nm(n);
      for(size_t i = 0; i < n; i++)
        nm[i] = js_name(STRING_ELT(names, static_cast<R_xlen_t>(i)));
      jsnames = v8::Array::New(isolate, nm.data(), n);
    }
    if(!Rf_inherits(x, "data.frame")){
      v8::Local<v8::Value> node[3] = {tag(TAG_LIST), v8::Array::New(isolate, children.data(), n), jsnames};
      return scope.Escape(v8::Array::New(isolate, node, 3));
    }
    if(Rf_isNull(names))
      throw std::invalid_argument("data frame columns must be named");
    R_xlen_t nrow = (n > 0) ? Rf_xlength(VECTOR_ELT(x, 0)) : Rf_xlength(Rf_getAttrib(x, R_RowNamesSymbol));
    v8::Local<v8::Value> node[4] = {
      tag(TAG_FRAME),
      v8::Array::New(isolate, children.data(), n),
      jsnames,
      v8::Number::New(isolate, static_cast<double>(nrow))
    };
    return scope.Escape(v8::Array::New(isolate, node, 4));
  }
  default:
    throw std::invalid_argument(std::string("No conversion to JavaScript for R type: ") + Rf_type2char(static_cast<SEXPTYPE>(TYPEOF(x))));
  }
}

v8::Local<v8::Value> r_to_js(SEXP x, SEXP options, v8::Local<v8::Context> context){
  v8::EscapableHandleScope scope(isolate);
  bool auto_unbox = true;
  SEXP names = Rf_getAttrib(options, R_NamesSymbol);
  for(R_xlen_t i = 0; TYPEOF(options) == VECSXP && !Rf_isNull(names) && i < Rf_xlength(options); i++){
    SEXP val = VECTOR_ELT(options, i);
    if(std::strcmp(CHAR(STRING_ELT(names, i)), "auto_unbox") == 0 && TYPEOF(val) == LGLSXP && Rf_xlength(val) == 1)
      auto_unbox = LOGICAL(val)[0] == 1;
  }
  v8::Local<v8::Function> decode = cached_function("V8n::decode", decode_js, context);
  v8::Local<v8::Value> argv[2] = {r_to_node(x, auto_unbox), options_to_js(options)};
  v8::TryCatch trycatch(isolate);
  v8::Local<v8::Value> out;
  if(!decode->Call(context, context->Global(), 2, argv).ToLocal(&out))
    throw std::runtime_error(js_text(trycatch.Exception()));
  return scope.Escape(out);
}

/* Copies a typed array into an R vector and sets NA where the mask is 1 */
template <typename T>
static void copy_masked(v8::Local<v8::ArrayBufferView> data, v8::Local<v8::Value> mask, T* out, size_t n, T na){
  data->CopyContents(out, n * sizeof(T));
  if(!mask->IsUint8Array())
    return;
  std::vector<uint8_t> m(n);
  mask.As<v8::Uint8Array>()->CopyContents(m.data(), n);
  for(size_t i = 0; i < n; i++){
    if(m[i])
      out[i] = na;
  }
}

static v8::Local<v8::Value> element(v8::Local<v8::Array> node, uint32_t i, v8::Local<v8::Context> context){
  v8::Local<v8::Value> out;
  if(!node->Get(context, i).ToLocal(&out))
    throw std::runtime_error("Invalid node in conversion to R.");
  return out;
}

static Rcpp::CharacterVector string_vector(v8::Local<v8::Array> strings, v8::Local<v8::Context> context){
  uint32_t n = strings->Length();
  Rcpp::CharacterVector out(static_cast<R_xlen_t>(n));
  for(uint32_t i = 0; i < n; i++){
    v8::String::Utf8Value str(isolate, element(strings, i, context));
    out[i] = Rf_mkCharCE(*str, CE_UTF8);
  }
  return out;
}

/* Uses the "_row" column as row names when it holds unique strings */
static Rcpp::List data_frame(Rcpp::List x, Rcpp::CharacterVector names, int nrow){
  Rcpp::RObject rownames = Rcpp::IntegerVector::create(NA_INTEGER, -nrow);
  for(R_xlen_t i = 0; i < x.size(); i++){
    if(std::strcmp(CHAR(STRING_ELT(names, i)), "_row") != 0)
      continue;
    SEXP rn = x[i];
    bool valid = TYPEOF(rn) == STRSXP && !Rf_any_duplicated(rn, FALSE);
    for(R_xlen_t k = 0; valid && k < Rf_xlength(rn); k++)
      valid = STRING_ELT(rn, k) != NA_STRING;
    if(valid){
      rownames = rn;
      x.erase(static_cast<int>(i));
      names.erase(static_cast<int>(i));
      x.attr("names") = names;
    }
    break;
  }
  x.attr("row.names") = rownames;
  x.attr("class") = "data.frame";
  return x;
}

static SEXP node_to_r(v8::Local<v8::Value> value, v8::Local<v8::Context> context){
  v8::HandleScope scope(isolate);
  if(value.IsEmpty() || !value->IsArray())
    throw std::runtime_error("Invalid node in conversion to R.");
  v8::Local<v8::Array> node = value.As<v8::Array>();
  int32_t kind = element(node, 0, context)->Int32Value(context).FromMaybe(-1);
  switch(kind){
  case TAG_NULL:
    return R_NilValue;
  case TAG_LOGICAL: {
    v8::Local<v8::ArrayBufferView> data = element(node, 1, context).As<v8::ArrayBufferView>();
    size_t n = data->ByteLength() / sizeof(int32_t);
    Rcpp::LogicalVector out(static_cast<R_xlen_t>(n));
    copy_masked<int>(data, element(node, 2, context), LOGICAL(out), n, NA_LOGICAL);
    return out;
  }
  case TAG_INTEGER: {
    v8::Local<v8::ArrayBufferView> data = element(node, 1, context).As<v8::ArrayBufferView>();
    size_t n = data->ByteLength() / sizeof(int32_t);
    Rcpp::IntegerVector out(static_cast<R_xlen_t>(n));
    copy_masked<int>(data, element(node, 2, context), INTEGER(out), n, NA_INTEGER);
    return out;
  }
  case TAG_DOUBLE: {
    v8::Local<v8::ArrayBufferView> data = element(node, 1, context).As<v8::ArrayBufferView>();
    size_t n = data->ByteLength() / sizeof(double);
    Rcpp::NumericVector out(static_cast<R_xlen_t>(n));
    copy_masked<double>(data, element(node, 2, context), REAL(out), n, NA_REAL);
    return out;
  }
  case TAG_STRING: {
    v8::Local<v8::Int32Array> codes = element(node, 1, context).As<v8::Int32Array>();
    Rcpp::CharacterVector levels = string_vector(element(node, 2, context).As<v8::Array>(), context);
    size_t n = codes->Length();
    std::vector<int32_t> code(n);
    codes->CopyContents(code.data(), n * sizeof(int32_t));
    Rcpp::CharacterVector out(static_cast<R_xlen_t>(n));
    for(size_t i = 0; i < n; i++)
      out[static_cast<R_xlen_t>(i)] = (code[i] == 0) ? NA_STRING : STRING_ELT(levels, code[i] - 1);
    return out;
  }
  case TAG_RAW: {
    v8::Local<v8::ArrayBufferView> view = element(node, 1, context).As<v8::ArrayBufferView>();
    Rcpp::RawVector out(static_cast<R_xlen_t>(view->ByteLength()));
    view->CopyContents(RAW(out), view->ByteLength());
    return out;
  }
  case TAG_LIST:
  case TAG_FRAME: {
    v8::Local<v8::Array> children = element(node, 1, context).As<v8::Array>();
    v8::Local<v8::Value> names = element(node, 2, context);
    uint32_t n = children->Length();
    Rcpp::List out(static_cast<R_xlen_t>(n));
    for(uint32_t i = 0; i < n; i++)
      out[i] = node_to_r(element(children, i, context), context);
    if(!names->IsArray())
      return out;
    Rcpp::CharacterVector nm = string_vector(names.As<v8::Array>(), context);
    out.attr("names") = nm;
    if(kind == TAG_LIST)
      return out;
    int nrow = element(node, 3, context)->Int32Value(context).FromMaybe(0);
    return data_frame(out, nm, nrow);
  }
  case TAG_ARRAY: {
    Rcpp::RObject out = node_to_r(element(node, 1, context), context);
    v8::Local<v8::Array> dims = element(node, 2, context).As<v8::Array>();
    uint32_t nd = dims->Length();
    Rcpp::IntegerVector dim(static_cast<R_xlen_t>(nd));
    for(uint32_t i = 0; i < nd; i++)
      dim[i] = element(dims, i, context)->Int32Value(context).FromMaybe(0);
    out.attr("dim") = dim;
    return out;
  }
  default:
    throw std::runtime_error("Invalid node in conversion to R.");
  }
}

SEXP js_to_r(v8::Local<v8::Value> value, v8::Local<v8::Value> options, v8::Local<v8::Context> context){
  v8::HandleScope scope(isolate);
  v8::Local<v8::Function> encode = cached_function("V8n::encode", encode_js, context);
  v8::Local<v8::Value> undefined = v8::Undefined(isolate);
  v8::Local<v8::Value> argv[2] = {
    value.IsEmpty() ? undefined : value,
    options.IsEmpty() ? undefined : options
  };
  v8::TryCatch trycatch(isolate);
  v8::Local<v8::Value> node;
  if(!encode->Call(context, context->Global(), 2, argv).ToLocal(&node))
    throw std::runtime_error(js_text(trycatch.Exception()));
  return node_to_r(node, context);
}

/* The result of ct$eval(serialize = TRUE): JSON text, or raw bytes for
 * array buffers and their views */
SEXP serialize_value(v8::Local<v8::Value> value, v8::Local<v8::Context> context){
  if(value->IsUndefined())
    return R_NilValue;
  if(value->IsArrayBufferView()){
    v8::Local<v8::ArrayBufferView> view = value.As<v8::ArrayBufferView>();
    Rcpp::RawVector out(static_cast<R_xlen_t>(view->ByteLength()));
    view->CopyContents(RAW(out), view->ByteLength());
    return out;
  }
  if(value->IsArrayBuffer()){
    v8::Local<v8::ArrayBuffer> buffer = value.As<v8::ArrayBuffer>();
    Rcpp::RawVector out(static_cast<R_xlen_t>(buffer->ByteLength()));
    if(buffer->ByteLength() > 0)
      std::memcpy(RAW(out), buffer->Data(), buffer->ByteLength());
    return out;
  }
  v8::TryCatch trycatch(isolate);
  v8::Local<v8::String> json;
  if(!v8::JSON::Stringify(context, value).ToLocal(&json))
    throw std::runtime_error(js_text(trycatch.Exception()));
  return Rcpp::CharacterVector::create(Rcpp::String(js_text(json), CE_UTF8));
}
