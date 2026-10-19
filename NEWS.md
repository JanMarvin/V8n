# V8n 0.1.0

First release, a fork of the V8 package (version 8.2.0) by Jeroen Ooms.

* Values are copied directly between R vectors and JavaScript typed arrays
  instead of being converted to JSON text. The mapping stays the one of
  jsonlite, and the options of `jsonlite::toJSON()` and `jsonlite::fromJSON()`
  can still be passed to `ct$assign()` and `ct$get()`. Doubles keep their
  full precision.
* Ctrl+C and Esc stop running JavaScript, and `ct$eval()`, `ct$get()` and
  `ct$call()` take a `timeout` in seconds. When JavaScript runs out of memory,
  the call fails with an error instead of ending the R session; the heap can
  be limited with `options(V8n.heap_limit = <MB>)` before loading the package.
* Every context provides common web APIs: `setTimeout()`, `setInterval()`,
  `queueMicrotask()`, `structuredClone()`, `TextEncoder`, `TextDecoder`,
  `atob()`, `btoa()`, `URL`, `URLSearchParams`, `crypto.getRandomValues()`,
  `crypto.randomUUID()` and `performance.now()`, plus `console.info()` and
  `console.debug()`.
* Builds of libv8 that read their ICU data from a file `icudtl.dat` find it
  next to the library, or where `options(V8n.icu_data)` says. If it is
  missing, `Intl` and the locale functions of JavaScript give an error
  instead of ending the R session, and `engine_info()` reports it.
* A string longer than JavaScript allows (about 512MB) gives an error
  instead of ending the R session.
* The JavaScript heap is sized by the memory of the machine, as in Node.js.
  With the small built-in defaults of V8, large data kept the garbage
  collector busy: a graph layout of 50,000 nodes ran seven times slower. With
  pointer compression the heap is at most 3000MB, since close to the 4GB
  that pointer compression allows, V8 ends the process instead of the script.
* V8n and the V8 package can be loaded in the same R session. The package
  exports only its init function, so a static libv8 inside it is not shared
  with other packages.
* `ct$call()` passes its arguments to the function directly, without a
  temporary global variable.
* `console.log()` separates its arguments by spaces, `console.warn()` gives
  one warning for all arguments.
* `console.r.call()` and `console.r.get()` evaluate in the global environment.
* Reloading the package, for example with `pkgload::load_all()` after a
  rebuild, no longer aborts R with "Wrong initialization order" when V8n is
  linked to a shared libv8: the running V8 is used again. The library also
  stays loaded when R unloads the package, so V8 threads and context
  finalizers do not point to unloaded code.
* Requires V8 11.3 or newer and R 4.3 or newer.
* Removed: the jsonlite backend, the interactive console `ct$console()`, the
  webR build and the dependencies on jsonlite and curl.
