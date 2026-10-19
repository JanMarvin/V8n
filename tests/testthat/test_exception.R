context("Error message")

test_that("SyntaxError from V8", {
  ctx <- V8n::v8()
  expect_error(ctx$eval("var foo = }bla}"), "SyntaxError",
               class = "std::invalid_argument")
})
