#' WebAssembly
#'
#' Loads a WebAssembly program into a new context and returns its exported
#' functions as R functions.
#'
#' The `wasm_features()` function uses the wasm-feature-detect JavaScript
#' library of Google Chrome Labs to test which WebAssembly features the V8
#' engine supports.
#'
#' @export
#' @rdname wasm
#' @param data either a raw vector or a file path with the binary wasm program
#' @examples # Load example wasm program
#' instance <- wasm(system.file('wasm/add.wasm', package = 'V8n'))
#' instance$exports$add(12, 30)
wasm <- function(data) {
  if (is.character(data)) {
    path <- normalizePath(data, mustWork = TRUE)
    data <- readBin(path, raw(), file.info(path)$size)
  }
  if (!is.raw(data)) {
    stop("Data must be a file path or a raw vector", call. = FALSE)
  }
  ctx <- v8()
  ctx$assign("bytes", data)
  ctx$eval("var module = new WebAssembly.Module(bytes);")
  ctx$eval("var instance = new WebAssembly.Instance(module);")
  exported <- ctx$get("Object.keys(instance.exports)")
  exports <- lapply(exported, function(name) {
    force(name)
    function(...) ctx$call(paste0("instance.exports.", name), ...)
  })
  names(exports) <- exported
  list(exports = exports)
}

#' @export
#' @rdname wasm
#' @examples wasm_features()
wasm_features <- function() {
  ctx <- v8()
  ctx$source(system.file("js/wasm-feature-detect.js", package = "V8n"))
  ctx$eval("async function test_wasm_features() {
  const out = {};
  for (const key of Object.keys(wasmFeatureDetect)) {
    out[key] = await wasmFeatureDetect[key]();
  }
  return out;
}")
  ctx$call("test_wasm_features", await = TRUE)
}
