#' Run JavaScript in a V8 context
#'
#' The [v8()] function creates a new V8 *context*. A context provides an
#' execution environment that allows separate, unrelated, JavaScript code to
#' run in a single instance of V8, like a tab in a browser.
#'
#' A V8 context cannot be saved or duplicated, but creating a new context and
#' sourcing code is cheap. You can run as many contexts as you want. R packages
#' that use V8n can use a separate context for each object or function call.
#'
#' The name of the global object (i.e. `global` in Node and `window` in
#' browsers) can be set with the `global` argument. A context always has a
#' global scope, even when no name is set. It can always be reached as
#' `globalThis`, or by evaluating `this` in the global scope, for example:
#' `ct$eval("Object.keys(this)")`.
#'
#' @section V8 Context Methods:
#' \Sexpr[results=rd, stage=build, echo=FALSE]{V8n:::generate_rd()}
#'
#' The `ct$eval` method evaluates a string of JavaScript code in the same way
#' as `eval` in JavaScript. By default `eval()` returns the result as a
#' string; when the `serialize` parameter is set to `TRUE` it serializes the
#' result to a JSON string, or to a raw vector for array buffers.
#'
#' The `ct$get`, `ct$assign` and `ct$call` functions convert arguments and
#' return values between R and JavaScript. To pass literal JavaScript
#' arguments that should not be converted, wrap them in `JS()`, see examples.
#'
#' If a call to `ct$eval()`, `ct$get()`, or `ct$call()` returns a JavaScript
#' promise, you can set `await = TRUE` to wait for the promise to be resolved.
#' It will then return the result of the promise, or an error in case the
#' promise is rejected.
#'
#' JavaScript runs until it finishes. Press Ctrl+C or Esc to stop it, or set
#' `timeout` (in seconds) in `ct$eval()`, `ct$get()` or `ct$call()` to stop it
#' with an error after that time. When JavaScript runs out of memory, the call
#' fails with an error instead of ending the R session. The heap is sized by
#' the memory of the machine; `options(V8n.heap_limit = <MB>)`, set before the
#' package is loaded, sets it instead. With pointer compression, as in the
#' static builds of libv8, the heap is at most 3000MB.
#'
#' Some builds of libv8 read their Unicode and locale data (ICU) from a file
#' `icudtl.dat`, which V8n looks for next to the library. If it is missing,
#' `Intl` and the locale functions of JavaScript, such as `localeCompare()`,
#' give an error. `options(V8n.icu_data = <path>)`, set before the package is
#' loaded, names the file. `engine_info()` reports the state as `icu`:
#' `"none"` for builds without ICU, `"available"` or `"missing"`.
#'
#' The `ct$validate` function is used to test if a piece of code is valid
#' JavaScript syntax within the context, and always returns `TRUE` or `FALSE`.
#'
#' `ct$source` evaluates a file or URL, and `ct$reset` replaces the context by
#' a new, empty one.
#'
#' @section Data Interchange:
#' Arguments and return values are converted between R and JavaScript
#' according to the mapping described in
#' [Ooms (2014)](https://arxiv.org/abs/1403.2805), the same mapping that the
#' jsonlite package implements. Values are copied directly between R vectors
#' and JavaScript typed arrays, without going through JSON text. Vectors,
#' lists, matrices and data frames are converted in both directions, data
#' frames column by column.
#'
#' The options of `toJSON()` from jsonlite can be passed to `ct$assign()` and
#' as an options object to `console.r.get()`: `dataframe`, `matrix`, `Date`,
#' `POSIXt`, `factor`, `complex`, `raw`, `null`, `na`, `auto_unbox`, `digits`,
#' `UTC`, `rownames`, `keep_vec_names`, `json_verbatim` and `time_format`.
#' The options of `fromJSON()` can be passed to `ct$get()` and as an options
#' object to `console.r.assign()`: `simplifyVector`, `simplifyDataFrame`,
#' `simplifyMatrix` and `flatten`. `ct$call()` passes `simplify` on as
#' `simplifyVector`.
#'
#' Some defaults differ from jsonlite: doubles keep full precision (jsonlite
#' rounds to 4 decimal digits unless `digits` is set), `NA` in vectors becomes
#' `null` (jsonlite writes the string `"NA"` for numbers), `NaN` and `Inf`
#' stay numbers, raw vectors become `Uint8Array` also inside lists, and whole
#' numbers from JavaScript are returned as integers whenever they fit.
#'
#' Raw vectors are converted to `Uint8Array` typed arrays, and array buffers
#' and typed arrays to raw vectors. This makes it possible to copy large
#' chunks of binary data between R and JavaScript, which is useful for running
#' [wasm].
#'
#' @section Web APIs:
#' Besides JavaScript itself, every context provides some of the APIs of
#' browsers and Node that libraries commonly expect: `setTimeout()`,
#' `setInterval()` and their `clear` functions, `queueMicrotask()`,
#' `structuredClone()`, `TextEncoder` and `TextDecoder` (UTF-8 and UTF-16LE),
#' `atob()` and `btoa()`, `URL` and `URLSearchParams`,
#' `crypto.getRandomValues()` and `crypto.randomUUID()`, and
#' `performance.now()`. Timers run after each call from R and while waiting
#' for a promise with `await = TRUE`. There is no DOM, no network access and
#' no file access.
#'
#' @section Callbacks to R:
#' JavaScript code can call back into the R session with `console.r.call()`,
#' `console.r.get()`, `console.r.eval()` and `console.r.assign()`, see the
#' vignette.
#'
#' @references A Mapping Between JSON Data and R Objects (Ooms, 2014):
#' <https://arxiv.org/abs/1403.2805>
#' @export v8 new_context
#' @param global character vector with the name(s) of the global object. Use
#' `NULL` for no name.
#' @param console expose the `console` API (`console.log`, `console.warn`,
#' `console.error` and `console.r`).
#' @param ... ignored parameters for past or future versions.
#' @aliases V8n v8 new_context
#' @rdname V8n
#' @name V8n
#' @import Rcpp
#' @useDynLib V8n, .registration = TRUE
#' @examples # Create a new context
#' ctx <- v8()
#'
#' # Evaluate some code
#' ctx$eval("var foo = 123")
#' ctx$eval("var bar = 456")
#' ctx$eval("foo+bar")
#'
#' # Functions and closures
#' ctx$eval("JSON.stringify({x:Math.random()})")
#' ctx$eval("(function(x){return x+1;})(123)")
#'
#' # Objects
#' ctx$assign("mydata", mtcars)
#' ctx$get("mydata")
#' outlist <- ctx$get("mydata", simplifyVector = FALSE)
#' outlist[1]
#'
#' # Assign JavaScript
#' ctx$assign("foo", JS("function(x){return x*x}"))
#' ctx$assign("bar", JS("foo(9)"))
#' ctx$get("bar")
#'
#' # Validate script without evaluating
#' ctx$validate("function foo(x){2*x}") #TRUE
#' ctx$validate("foo = function(x){2*x}") #TRUE
#' ctx$validate("function(x){2*x}") #FALSE
#'
#' # Call anonymous function
#' ctx$call("function(x, y){return x * y}", 123, 3)
#'
#' \dontrun{
#' # Use a JavaScript library
#' ctx$source(paste0("https://cdnjs.cloudflare.com/ajax/libs/",
#'                   "underscore.js/1.13.6/underscore-min.js"))
#' ctx$call("_.filter", mtcars, JS("function(x){return x.mpg < 15}"))
#'
#' # Example from underscore manual
#' ctx$eval("_.templateSettings = {interpolate: /\\{\\{(.+?)\\}\\}/g}")
#' ctx$eval("var template = _.template('Hello {{ name }}!')")
#' ctx$call("template", list(name = "Mustache"))
#' }
v8 <- function(global = "global", console = TRUE, ...) {
  private <- environment()
  this <- new.env(parent = private)

  this$eval <- function(src, serialize = FALSE, await = FALSE, timeout = Inf) {
    out <- context_eval(join(src), private$context, serialize, await,
                        as.numeric(timeout))
    if (identical(out, "undefined")) invisible(out) else out
  }

  this$validate <- function(src) {
    context_validate(join(src), private$context)
  }

  this$call <- function(fun, ..., auto_unbox = TRUE, await = FALSE,
                        simplify = TRUE, timeout = Inf) {
    stopifnot(is.character(fun), this$validate(c("fun=", fun)))
    args <- list(...)
    if (!is.null(names(args))) {
      stop("Named arguments are not supported in JavaScript.", call. = FALSE)
    }
    options <- list(auto_unbox = auto_unbox)
    # JS() arguments are inserted as code, other arguments are converted
    values <- list()
    exprs <- character(length(args))
    for (i in seq_along(args)) {
      if (inherits(args[[i]], "JS_EVAL")) {
        exprs[i] <- as.character(args[[i]])
      } else {
        values[length(values) + 1L] <- list(encode_value(args[[i]], options))
        exprs[i] <- sprintf("V8n_args[%d]", length(values) - 1L)
      }
    }
    src <- sprintf("(function(V8n_args){return (%s)(%s);})",
                   join(fun), paste(exprs, collapse = ", "))
    out <- context_call(src, values, options, list(simplifyVector = simplify),
                        private$context, await, as.numeric(timeout))
    if (is.null(out)) invisible() else out
  }

  this$source <- function(file) {
    if (is.character(file) && length(file) == 1 && grepl("^https?://", file)) {
      file <- url(file, open = "r")
      on.exit(close(file))
    }
    # Always assume UTF-8, even on Windows
    this$eval(readLines(file, encoding = "UTF-8", warn = FALSE))
  }

  this$get <- function(name, ..., await = FALSE, timeout = Inf) {
    stopifnot(is.character(name))
    options <- from_json_options(list(...))
    out <- context_get(join(name), private$context, await, options,
                       as.numeric(timeout))
    if (isTRUE(options$flatten)) {
      out <- flatten_frames(out)
    }
    if (is.null(out)) invisible() else out
  }

  this$assign <- function(name, value, auto_unbox = TRUE, ...) {
    # The 'na' option partially matches 'name', so bind the arguments again
    # with 'name' matched exactly
    tags <- names(sys.call())
    if ("na" %in% tags && !("name" %in% tags)) {
      args <- lapply(as.list(sys.call())[-1], base::eval,
                     envir = parent.frame())
      positional <- args[names(args) == ""]
      named <- args[names(args) != ""]
      for (arg in c("name", "value", "auto_unbox")) {
        if (is.null(named[[arg]]) && length(positional)) {
          named[[arg]] <- positional[[1]]
          positional <- positional[-1]
        }
      }
      return(do.call(this$assign, c(named, positional)))
    }
    stopifnot(is.character(name))
    if (inherits(value, "JS_EVAL")) {
      return(invisible(this$eval(paste("var", name, "=", value))))
    }
    options <- to_json_options(c(list(auto_unbox = auto_unbox), list(...)))
    value <- encode_value(value, options)
    invisible(context_assign(name, value, options, private$context))
  }

  this$reset <- function() {
    private$context <- make_context(console)
    if (length(global)) {
      src <- paste("var", global, "= this;", collapse = "\n")
      context_eval(src, private$context, FALSE, FALSE, Inf)
    }
    invisible()
  }

  this$reset()
  lockEnvironment(this, TRUE)
  structure(this, class = c("V8n", "environment"))
}

# The name used by early versions of V8
new_context <- v8

join <- function(str) {
  paste(str, collapse = "\n")
}

#' @export
`$.V8n` <- function(x, y) {
  if (!exists(y, x, inherits = FALSE)) {
    stop("V8n object has no field '", y, "'", call. = FALSE)
  }
  get(y, x, inherits = FALSE)
}

#' @export
`[[.V8n` <- `$.V8n`

#' @export
`[.V8n` <- `$.V8n`

#' @export
`@.V8n` <- function(object, name, ...) {
  object[[name]]
}

#' @exportS3Method utils::.AtNames
.AtNames.V8n <- function(x, pattern = "") {
  asNamespace("utils")$findMatches(pattern, ls(x))
}

#' @export
names.V8n <- function(x) {
  ls(x)
}

#' @export
print.V8n <- function(x, ...) {
  if (context_null(get("context", x))) {
    cat("This context has been disposed.\n")
    return(invisible(x))
  }
  cat(sprintf("<V8 engine %s>", version()), "\n")
  for (name in ls(x)) {
    cat(format_function(x[[name]], name), sep = "\n")
  }
  invisible(x)
}

# Formats the header of a method: " $eval(src, serialize = FALSE, ...)"
format_function <- function(fun, name) {
  header <- utils::head(deparse(args(fun), 100L), -1)
  header <- sub("^[ ]*", "   ", header)
  header[1] <- sub("^[ ]*function ?", paste0(" $", name), header[1])
  header
}

generate_rd <- function() {
  out <- paste(utils::capture.output(print(v8())), collapse = "\n")
  paste("\\preformatted{", "## ctx <- v8()", out, "}\n", sep = "\n")
}

#' @rdname V8n
#' @export
engine_info <- function() {
  intl <- v8(console = FALSE)$get("typeof Intl === 'object'")
  list(
    version = version(),
    numeric_version = numeric_version(sub("^([0-9.]+).*", "\\1", version())),
    icu = if (!intl) "none" else if (icu_missing()) "missing" else "available"
  )
}
