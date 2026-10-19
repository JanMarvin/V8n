test_that("global names", {
  ctx <- v8()
  expect_identical(ctx$eval("global === globalThis"), "true")
  ctx <- v8(global = c("window", "self"))
  expect_identical(ctx$eval("window === globalThis && self === globalThis"),
                   "true")
  ctx <- v8(global = NULL)
  expect_identical(ctx$eval("typeof global"), "undefined")
  expect_identical(ctx$get("Object.keys(this)"), c("print", "console"))
})

test_that("console", {
  ctx <- v8()
  expect_output(ctx$eval("console.log('a', 1, true)"), "a 1 true")
  # console.warn() prints the warning at once, R handlers cannot catch it
  out <- capture.output(ctx$eval("console.warn('careful')"), type = "message")
  expect_identical(out, "Warning: careful")
  old <- options(warn = 2)
  expect_error(ctx$eval("console.warn('careful')"),
               "(converted from warning) careful", fixed = TRUE)
  options(old)
  expect_error(ctx$eval("console.error('broken')"), "broken")
  expect_output(ctx$eval("print('x')"), "x")
  ctx <- v8(console = FALSE)
  expect_identical(ctx$eval("typeof console.r"), "undefined")
})

test_that("methods", {
  ctx <- v8()
  expect_identical(sort(names(ctx)), c("assign", "call", "eval", "get",
                                       "reset", "source", "validate"))
  expect_output(print(ctx), "<V8 engine")
  expect_identical(ctx@eval, ctx$eval)
  expect_error(ctx$foo, "no field")
  expect_true(ctx$validate("function foo(x){2*x}"))
  expect_false(ctx$validate("function(x){2*x}"))
  expect_invisible(ctx$eval("undefined"))
  ctx$eval("var a = 1")
  ctx$reset()
  expect_identical(ctx$eval("typeof a"), "undefined")
  expect_identical(ctx$eval("typeof global"), "object")
})

test_that("source", {
  file <- tempfile(fileext = ".js")
  writeLines(c("var x = 1;", "var y = x + 1;"), file)
  ctx <- v8()
  ctx$source(file)
  expect_identical(ctx$get("y"), 2L)
  unlink(file)
})

test_that("disposed context", {
  ctx <- v8()
  file <- tempfile(fileext = ".rds")
  saveRDS(ctx, file)
  ctx2 <- readRDS(file)
  unlink(file)
  expect_output(print(ctx2), "disposed")
  expect_error(ctx2$eval("1"), "disposed")
})

test_that("engine info", {
  info <- engine_info()
  expect_true(info$numeric_version >= "11.3")
  expect_match(info$version, "^[0-9]+\\.[0-9]+")
})
