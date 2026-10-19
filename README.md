# V8n

> Embedded JavaScript and WebAssembly Engine for R

V8n runs JavaScript and WebAssembly inside R with [V8](https://v8.dev),
Google's JavaScript engine. It is based on the
[V8](https://github.com/jeroen/V8) package by Jeroen Ooms and keeps its
interface, so existing code works after `library(V8n)`.

V8n is experimental.

## Usage

```r
library(V8n)
ctx <- v8()

# Evaluate JavaScript
ctx$eval("var foo = 123")
ctx$eval("foo + 1")

# Move data between R and JavaScript
ctx$assign("cars", mtcars)
ctx$call("function(x){return x.filter(r => r.mpg > 30)}", mtcars)
ctx$get("cars")

# Call back into R from JavaScript
ctx$eval("var x = console.r.call('rnorm', {n: 5})")
```

Contexts also support promises (`await = TRUE`), ES modules, syntax
checks with `ct$validate()`, loading libraries with `ct$source()`, and
WebAssembly through `wasm()`. Besides JavaScript itself, they provide common
web APIs that libraries expect, such as `setTimeout()`, `TextEncoder`,
`TextDecoder`, `URL`, `structuredClone()` and `crypto.getRandomValues()`.

Running JavaScript can be stopped with Ctrl+C or Esc, or with a `timeout` in
seconds:

```r
ctx$eval("while (true) {}", timeout = 1)
#> Error: JavaScript did not finish within the timeout of 1 seconds.
```

When JavaScript runs out of memory, the call fails with an error and the R
session continues. See the vignettes "Introduction to V8n" and
"Using NPM packages in V8n".

## Data conversion

R vectors are copied directly into JavaScript typed arrays and back, without
an intermediate JSON text. The mapping follows the one described in
[Ooms (2014)](https://arxiv.org/abs/1403.2805) and implemented by jsonlite:
data frames become arrays of row objects, named lists become objects, and so
on. The options of `jsonlite::toJSON()` and `jsonlite::fromJSON()`, such as
`dataframe = "columns"` or `simplifyVector = FALSE`, can be passed to
`ct$assign()` and `ct$get()`.

Some details of the conversion:

- Doubles keep their full precision. `NaN`, `Inf` and `-Inf` stay numbers,
  `NA` becomes `null`.
- Whole numbers from JavaScript return as integers when they fit.
- Raw vectors become `Uint8Array`, and typed arrays and array buffers return
  as raw vectors.
- Strings are passed as UTF-8.

This makes V8n useful when large data frames, binary data or exact numbers
move between R and JavaScript. V8n has no dependencies besides Rcpp.

## Installation

```r
remotes::install_github("JanMarvin/V8n")
```

V8n requires libv8 11.3 or newer. On Windows and macOS, and on amd64 and
arm64 Linux with Ubuntu, RHEL, openSUSE, Alpine or Amazon Linux, the
installation downloads a static build of libv8. On other Linux systems, set
the environment variable `DOWNLOAD_STATIC_LIBV8` before the installation:

```r
Sys.setenv(DOWNLOAD_STATIC_LIBV8 = 1)
remotes::install_github("JanMarvin/V8n")
```

To build against the libv8 of the system instead, install it and set
`DISABLE_STATIC_LIBV8=1`:

```sh
# Debian 13 or newer
sudo apt-get install -y libnode-dev

# Fedora
sudo dnf install v8-devel

# macOS
brew install v8
```

A custom libv8 can be used by setting `V8_PKG_CFLAGS` and `V8_PKG_LIBS`.
