# The R side of console.r.call, console.r.get, console.r.eval and
# console.r.assign. Each returns list(value, options) to convert back to
# JavaScript, NULL, or an error message, because an R error must not jump
# over the JavaScript frames.

no_jumps <- function(expr) {
  tryCatch(expr, error = function(e) {
    structure(conditionMessage(e), class = "cb_error")
  }, interrupt = function(e) {
    structure("User interruption during R evaluation", class = "cb_error")
  })
}

# Calls the function that the text in fun evaluates to, with the arguments
# in args
r_call <- function(fun, args = NULL) {
  no_jumps({
    f <- eval(parse(text = fun), globalenv())
    if (!is.function(f)) {
      stop("Argument is not a valid function")
    }
    options <- list(auto_unbox = FALSE)
    list(encode_value(do.call(f, as.list(args)), options), options)
  })
}

# Returns the value of an R expression, converted with the toJSON options
r_get <- function(expr, options = NULL) {
  no_jumps({
    value <- eval(parse(text = expr), globalenv())
    options <- utils::modifyList(list(auto_unbox = FALSE), as.list(options))
    options <- to_json_options(options)
    list(encode_value(value, options), options)
  })
}

# Evaluates R code like source(), printing the results by default
r_eval <- function(code, options = NULL) {
  no_jumps({
    if (is.null(options)) {
      options <- list(print.eval = TRUE)
    }
    con <- textConnection(code)
    on.exit(close(con))
    do.call(source, c(list(file = con), as.list(options)))
    NULL
  })
}

# Assigns a value in the global environment
r_assign <- function(name, value = NULL, options = NULL) {
  no_jumps({
    options <- from_json_options(as.list(options))
    if (isTRUE(options$flatten)) {
      value <- flatten_frames(value)
    }
    base::assign(name, value, envir = globalenv())
    NULL
  })
}
