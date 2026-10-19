#ifndef V8N_TYPES_H
#define V8N_TYPES_H

#include <Rcpp.h>
#include <v8.h>

typedef v8::Global<v8::Context> ctx_type;
void ctx_finalizer(ctx_type* ctx);
typedef Rcpp::XPtr<ctx_type, Rcpp::PreserveStorage, ctx_finalizer> ctxptr;

#endif
