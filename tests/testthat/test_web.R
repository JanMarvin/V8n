test_that("web APIs do not show up in Object.keys()", {
  ctx <- v8()
  expect_identical(ctx$get("Object.keys(globalThis)"),
                   c("print", "console", "global"))
  expect_identical(
    ctx$eval("typeof setTimeout + typeof URL + typeof TextEncoder"),
    "functionfunctionfunction"
  )
})

test_that("TextEncoder and TextDecoder", {
  ctx <- v8()
  src <- "Array.from(new TextEncoder().encode('h\\u00e9\\ud83d\\ude00\\ud800'))"
  expect_identical(ctx$get(src), c(104L, 195L, 169L, 240L, 159L, 152L, 128L,
                                   239L, 191L, 189L))
  expect_identical(ctx$get("new TextEncoder().encode('\\u00e9')"),
                   as.raw(c(0xc3, 0xa9)))
  src <- "new TextDecoder().decode(new Uint8Array([0xef,0xbb,0xbf,104,255,97]))"
  expect_identical(ctx$eval(src), "h�a")
  src <- "new TextDecoder('utf-8', {fatal: true}).decode(new Uint8Array([255]))"
  expect_error(ctx$eval(src), "not valid utf-8")
  src <- "{
    const d = new TextDecoder();
    d.decode(new Uint8Array([0xf0, 0x9f]), {stream: true}) +
      d.decode(new Uint8Array([0x98, 0x80]))
  }"
  expect_identical(ctx$eval(src), "\U0001F600")
  src <- "new TextDecoder('utf-16le').decode(new Uint8Array([104, 0, 105, 0]))"
  expect_identical(ctx$eval(src), "hi")
  expect_error(ctx$eval("new TextDecoder('latin2')"), "not supported")
  src <- "{
    const u = new Uint8Array(4);
    [new TextEncoder().encodeInto('a\\u00e9\\u20ac', u).written, ...u]
  }"
  expect_identical(ctx$get(src), c(3L, 97L, 195L, 169L, 0L))
})

test_that("atob and btoa", {
  ctx <- v8()
  expect_identical(ctx$eval("btoa('hello')"), "aGVsbG8=")
  expect_identical(ctx$eval("atob(' aGVs bG8= ')"), "hello")
  expect_error(ctx$eval("btoa('\\u20ac')"), "InvalidCharacterError")
  expect_error(ctx$eval("atob('Y')"), "InvalidCharacterError")
})

test_that("structuredClone", {
  ctx <- v8()
  ctx$eval("var o = {d: new Date(0), m: new Map([[1, {x: [1, 2]}]]),
                     u: new Uint8Array([1, 2])};
            var c = structuredClone(o);")
  expect_true(ctx$get("c !== o && c.u !== o.u"))
  expect_identical(ctx$get("[c.d.getTime(), c.m.get(1).x[1], c.u[1]]"),
                   c(0L, 2L, 2L))
  expect_error(ctx$eval("structuredClone(() => 1)"), "DataCloneError")
})

test_that("crypto and performance", {
  ctx <- v8()
  src <- "Array.from(crypto.getRandomValues(new Uint32Array(8)))"
  expect_length(ctx$get(src), 8)
  uuid <- paste0("^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-",
                 "[0-9a-f]{12}$")
  expect_match(ctx$eval("crypto.randomUUID()"), uuid)
  expect_error(ctx$eval("crypto.getRandomValues(new Float64Array(1))"),
               "TypeMismatchError")
  expect_error(ctx$eval("crypto.getRandomValues(new Uint8Array(65537))"),
               "QuotaExceededError")
  src <- "performance.now() >= 0 && typeof performance.timeOrigin === 'number'"
  expect_true(ctx$get(src))
})

test_that("microtasks and timers", {
  ctx <- v8()
  ctx$eval("var log = [];
            queueMicrotask(() => log.push('micro'));
            log.push('sync');")
  expect_identical(ctx$get("log"), c("sync", "micro"))
  # timers that are due run after each call from R
  ctx$eval("var t = []; setTimeout(() => t.push('timer'), 0);")
  expect_identical(ctx$get("t"), "timer")
  ctx$eval("var never = [];
            var id = setTimeout(() => never.push(1), 0);
            clearTimeout(id);")
  expect_identical(ctx$get("never.length"), 0L)
  # later timers run while waiting for a promise
  src <- "new Promise(r => setTimeout(r, 30, 'done'))"
  expect_identical(ctx$eval(src, await = TRUE), "done")
  src <- "var ord = [];
          setTimeout(() => ord.push('b'), 20);
          setTimeout(() => ord.push('a'), 5);
          var n = 0;
          var iv = setInterval(() => { if (++n === 3) clearInterval(iv); }, 1);
          new Promise(r => setTimeout(() => r(ord.join() + n), 40))"
  expect_identical(ctx$eval(src, await = TRUE), "a,b3")
  src <- "setTimeout(() => { throw new Error('in timer') }, 0)"
  expect_error(ctx$eval(src), "in timer")
})

test_that("URL and URLSearchParams", {
  ctx <- v8()
  ctx$eval("var u = new URL('../b/c?x=1 2#f',
                            'HTTP://User:pw@Example.COM:80/a/d/e');")
  expect_identical(ctx$eval("u.href"),
                   "http://User:pw@example.com/a/b/c?x=1%202#f")
  src <- "[u.origin, u.host, u.pathname, u.search, u.hash,
           u.searchParams.get('x')]"
  expect_identical(ctx$get(src), c("http://example.com", "example.com",
                                   "/a/b/c", "?x=1%202", "#f", "1 2"))
  refs <- c("g", "./g", "g/", "/g", "//g", "?y", "g?y", "#s", "g#s", "..",
            "../..", "../../../g")
  ctx$assign("refs", refs)
  expect_identical(
    ctx$get("refs.map(r => new URL(r, 'http://a/b/c/d;p?q').href)"),
    c("http://a/b/c/g", "http://a/b/c/g", "http://a/b/c/g/", "http://a/g",
      "http://g/", "http://a/b/c/d;p?y", "http://a/b/c/g?y",
      "http://a/b/c/d;p?q#s", "http://a/b/c/g#s", "http://a/b/", "http://a/",
      "http://a/g")
  )
  ctx$eval("var v = new URL('https://a.b/?q=1');
            v.searchParams.append('x', 'a b&c');
            v.searchParams.set('q', '\\u00e9');")
  expect_identical(ctx$eval("v.href"), "https://a.b/?q=%C3%A9&x=a+b%26c")
  expect_identical(ctx$eval("new URL('file:///tmp/a b.txt').href"),
                   "file:///tmp/a%20b.txt")
  expect_identical(ctx$eval("new URL('mailto:me@example.com').pathname"),
                   "me@example.com")
  expect_identical(ctx$eval("new URL('http://[::1]:3000/').host"),
                   "[::1]:3000")
  expect_error(ctx$eval("new URL('/relative')"), "Invalid URL")
  expect_error(ctx$eval("new URL('http://h:65536')"), "Invalid URL")
  expect_identical(ctx$get("[URL.canParse('https://x'), URL.canParse('no')]"),
                   c(TRUE, FALSE))
  src <- "{
    const p = new URLSearchParams({b: '2', a: '1 1'});
    p.sort();
    p.toString()
  }"
  expect_identical(ctx$eval(src), "a=1+1&b=2")
  expect_identical(ctx$get("[...new URLSearchParams('?x=%20y+z&&=e&f')]"),
                   matrix(c("x", " y z", "", "e", "f", ""), 3, byrow = TRUE))
})

test_that("console.info and console.debug", {
  ctx <- v8()
  expect_output(ctx$eval("console.info('info'); console.debug('debug')"),
                "info\ndebug")
})
