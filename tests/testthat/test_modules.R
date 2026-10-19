context("ESM modules")

test_that("get modules", {
  ctx <- V8n::v8()
  ctx$source("modules/main.js")
  expect_error(ctx$eval("test_bad_path()", await = TRUE), "path")
  expect_equal(ctx$eval("run_test()", await = TRUE), "579")
  expect_error(ctx$eval("test_syntax_error1()", await = TRUE), "SyntaxError")
  expect_equal(ctx$eval("run_test()", await = TRUE), "579")
})
