test_that("timeout stops JavaScript", {
  ctx <- v8()
  start <- Sys.time()
  expect_error(ctx$eval("while (true) {}", timeout = 0.5),
               "timeout of 0.5 seconds")
  elapsed <- as.numeric(difftime(Sys.time(), start, units = "secs"))
  expect_lt(elapsed, 5)
  expect_error(ctx$call("function() { while (true) {} }", timeout = 0.3),
               "timeout")
  expect_error(ctx$get("new Promise(() => {})", await = TRUE, timeout = 0.3),
               "timeout")
  src <- "var iv = setInterval(() => {}, 1); new Promise(() => {})"
  expect_error(ctx$eval(src, await = TRUE, timeout = 0.3), "timeout")
  ctx$eval("clearInterval(iv)")
  # the context keeps working after a timeout
  expect_identical(ctx$eval("6 * 7"), "42")
  expect_identical(ctx$get("[1, 2]"), 1:2)
})

test_that("a timeout is not hit by quick calls", {
  ctx <- v8()
  for (i in 1:20) expect_identical(ctx$eval("1 + 1", timeout = 10), "2")
})

test_that("running out of memory gives an error instead of ending R", {
  skip_on_cran()
  rscript <- file.path(R.home("bin"), "Rscript")
  script <- tempfile(fileext = ".R")
  writeLines(c(
    "options(V8n.heap_limit = 64)",
    "library(V8n)",
    "ctx <- v8()",
    "src <- 'var a = []; while (true) a.push(new Array(1e5).fill(1))'",
    "msg <- tryCatch(ctx$eval(src), error = function(e) conditionMessage(e))",
    "ctx$eval('a = null')",
    "cat(msg, ctx$eval('1 + 1'), sep = '\\n')"
  ), script)
  out <- suppressWarnings(system2(rscript, script, stdout = TRUE,
                                  stderr = FALSE))
  unlink(script)
  expect_identical(tail(out, 2), c("JavaScript ran out of memory.", "2"))
})

test_that("a string above the maximum length gives an error", {
  skip_on_cran()
  ctx <- v8()
  x <- strrep("a", 2^29)
  expect_error(ctx$assign("x", x), "longer than JavaScript allows")
  rm(x)
  expect_identical(ctx$eval("1 + 1"), "2")
})
