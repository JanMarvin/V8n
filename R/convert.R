# Options of jsonlite::toJSON() for ct$assign() and console.r.get(), with
# the defaults filled in
to_json_options <- function(options) {
  allowed <- c("dataframe", "matrix", "Date", "POSIXt", "factor", "complex",
               "raw", "null", "na", "auto_unbox", "digits", "pretty", "force",
               "UTC", "rownames", "keep_vec_names", "json_verbatim",
               "always_decimal", "time_format")
  unnamed <- is.null(names(options)) || any(names(options) == "")
  if (length(options) && unnamed) {
    stop("Options must be named.", call. = FALSE)
  }
  unknown <- setdiff(names(options), allowed)
  if (length(unknown)) {
    stop("Unsupported option: ", paste(unknown, collapse = ", "), call. = FALSE)
  }
  options$dataframe <- match.arg(options$dataframe,
                                 c("rows", "columns", "values"))
  options$matrix <- match.arg(options$matrix, c("rowmajor", "columnmajor"))
  options$Date <- match.arg(options$Date, c("ISO8601", "epoch"))
  options$POSIXt <- match.arg(options$POSIXt,
                              c("string", "ISO8601", "epoch", "mongo"))
  options$factor <- match.arg(options$factor, c("string", "integer"))
  options$complex <- match.arg(options$complex, c("string", "list"))
  options$null <- match.arg(options$null, c("list", "null"))
  if (!is.null(options$raw)) {
    options$raw <- match.arg(options$raw,
                             c("base64", "hex", "mongo", "int", "js"))
  }
  if (!is.null(options$na)) {
    options$na <- match.arg(options$na, c("null", "string"))
  }
  options
}

# Options of jsonlite::fromJSON() for ct$get() and console.r.assign()
from_json_options <- function(options) {
  allowed <- c("simplifyVector", "simplifyDataFrame", "simplifyMatrix",
               "flatten", "bigint_as_char")
  unnamed <- is.null(names(options)) || any(names(options) == "")
  if (length(options) && unnamed) {
    stop("Options must be named.", call. = FALSE)
  }
  unknown <- setdiff(names(options), allowed)
  if (length(unknown)) {
    stop("Unsupported option: ", paste(unknown, collapse = ", "), call. = FALSE)
  }
  options
}

base64_encode <- function(x) {
  if ("base64_encode" %in% getNamespaceExports("tools")) {
    return(getExportedValue("tools", "base64_encode")(x = x))
  }
  chars <- c(LETTERS, letters, 0:9, "+", "/")
  pad <- (3 - length(x) %% 3) %% 3
  m <- matrix(c(as.integer(x), integer(pad)), nrow = 3)
  v <- m[1, ] * 65536L + m[2, ] * 256L + m[3, ]
  idx <- rbind(v %/% 262144L, (v %/% 4096L) %% 64L, (v %/% 64L) %% 64L,
               v %% 64L)
  out <- chars[as.vector(idx) + 1L]
  if (pad > 0) {
    out[seq(length(out) - pad + 1, length(out))] <- "="
  }
  paste(out, collapse = "")
}

# Turns classes that the C++ code does not know (dates, times, complex
# numbers, ...) into plain vectors, the way jsonlite::toJSON() writes them.
encode_value <- function(x, options = list()) {
  if (is.null(options$digits) && is.null(options$raw) &&
        !isTRUE(options$keep_vec_names) &&
        !identical(options$factor, "integer") && is_plain_value(x)) {
    return(x)
  }
  if (is.data.frame(x)) {
    out <- lapply(unclass(x), encode_value, options = options)
    rn <- attr(x, "row.names")
    if (isTRUE(options$rownames)) {
      out[["_row"]] <- if (is.character(rn)) rn else seq_len(nrow(x))
    } else if (!isFALSE(options$rownames) && is.character(rn)) {
      out[["_row"]] <- rn
    }
    return(structure(out, row.names = .set_row_names(nrow(x)),
                     class = "data.frame"))
  }
  if (inherits(x, "POSIXlt")) {
    x <- as.POSIXct(x)
  }
  if (inherits(x, "POSIXct")) {
    posixt <- if (is.null(options$POSIXt)) "string" else options$POSIXt
    if (posixt == "epoch" || posixt == "mongo") {
      ms <- floor(as.numeric(x) * 1000)
      if (posixt == "epoch") {
        return(ms)
      }
      return(structure(list(`$date` = ms),
                       row.names = .set_row_names(length(ms)),
                       class = "data.frame"))
    }
    format <- options$time_format
    if (is.null(format) && posixt == "string") {
      format <- ""
    } else if (is.null(format) && isTRUE(options$UTC)) {
      format <- "%Y-%m-%dT%H:%M:%SZ"
    } else if (is.null(format)) {
      format <- "%Y-%m-%dT%H:%M:%S"
    }
    u <- unique(x)
    if (isTRUE(options$UTC)) {
      text <- format(u, format = format, tz = "UTC")
    } else {
      text <- format(u, format = format)
    }
    out <- text[match(unclass(x), unclass(u))]
    return(if (inherits(x, "AsIs")) I(out) else out)
  }
  if (inherits(x, "Date")) {
    if (identical(options$Date, "epoch")) {
      return(as.numeric(x))
    }
    u <- unique(x)
    out <- format(u)[match(unclass(x), unclass(u))]
    return(if (inherits(x, "AsIs")) I(out) else out)
  }
  if (is.factor(x)) {
    return(if (identical(options$factor, "integer")) as.integer(x) else x)
  }
  if (inherits(x, "json")) {
    return(if (isTRUE(options$json_verbatim)) x else as.character(x))
  }
  other <- setdiff(oldClass(x), c("AsIs", "JS_EVAL"))
  if (length(other)) {
    stop("No conversion to JavaScript for class: ",
         paste(other, collapse = ", "), call. = FALSE)
  }
  if (is.list(x)) {
    x[] <- lapply(x, encode_value, options = options)
    return(x)
  }
  if (is.complex(x)) {
    if (identical(options$complex, "list")) {
      return(list(real = Re(x), imaginary = Im(x)))
    }
    out <- as.character(x)
    dim(out) <- dim(x)
    return(if (inherits(x, "AsIs")) I(out) else out)
  }
  if (is.raw(x) && !is.null(options$raw) && options$raw != "js") {
    return(switch(options$raw,
      int = as.integer(x),
      hex = as.character(as.hexmode(as.integer(x))),
      base64 = base64_encode(x),
      mongo = list(`$binary` = base64_encode(x), `$type` = "5")
    ))
  }
  if (is.double(x) && length(options$digits) == 1 && !is.na(options$digits)) {
    if (inherits(options$digits, "AsIs")) {
      x[] <- signif(x, options$digits)
    } else {
      rounded <- round(x, options$digits)
      small <- !is.na(x) & rounded == 0 & x != 0
      rounded[small] <- signif(x[small], options$digits)
      x[] <- rounded
    }
  }
  if (isTRUE(options$keep_vec_names) && !is.null(names(x)) &&
        is.null(dim(x))) {
    return(encode_value(as.list(x), options))
  }
  x
}

# Nested data frames become columns named "outer.inner", like the flatten
# option of jsonlite
flatten_frames <- function(x) {
  if (is.data.frame(x)) {
    out <- list()
    for (i in seq_along(x)) {
      col <- flatten_frames(x[[i]])
      if (is.data.frame(col)) {
        cols <- unclass(col)
        names(cols) <- paste(names(x)[i], names(col), sep = ".")
        out <- c(out, cols)
      } else {
        out[[names(x)[i]]] <- col
      }
    }
    return(structure(out, row.names = .set_row_names(nrow(x)),
                     class = "data.frame"))
  }
  if (is.list(x)) {
    x[] <- lapply(x, flatten_frames)
  }
  x
}
