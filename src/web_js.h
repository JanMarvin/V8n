#ifndef V8N_WEB_JS_H
#define V8N_WEB_JS_H

/* Web APIs that browsers and Node provide but V8 itself does not. The
 * function receives native helpers from web.cpp, defines the globals and
 * returns the function that runs due timers. */
static const char* web_js = R"JS(
(function (native) {
  "use strict";
  const define = (name, value) => Object.defineProperty(globalThis, name,
    {value: value, writable: true, configurable: true, enumerable: false});
  const clock = native.now;
  /* Lone surrogates become U+FFFD */
  const utf8 = (text) => native.utf8Encode(String(text).toWellFormed());

  class DOMException extends Error {
    constructor(message = "", name = "Error") {
      super(message);
      Object.defineProperty(this, "name", {value: String(name), configurable: true, writable: true});
    }
  }
  define("DOMException", DOMException);

  /* performance */
  const start = clock();
  const timeOrigin = Date.now();
  define("performance", {
    timeOrigin: timeOrigin,
    now: () => clock() - start,
    toJSON: () => ({timeOrigin: timeOrigin})
  });

  /* Timers run while V8n waits for a promise and after each call from R */
  const timers = new Map();
  let nextTimer = 1;
  function addTimer(callback, delay, args, repeat) {
    if (typeof callback !== "function") {
      const code = String(callback);
      callback = () => (0, eval)(code);
    }
    delay = Number(delay);
    if (!(delay >= 0)) delay = 0;
    const id = nextTimer++;
    timers.set(id, {callback: callback, args: args, delay: delay, repeat: repeat, due: clock() + delay});
    return id;
  }
  define("setTimeout", (callback, delay, ...args) => addTimer(callback, delay, args, false));
  define("setInterval", (callback, delay, ...args) => addTimer(callback, delay, args, true));
  define("clearTimeout", (id) => { timers.delete(Number(id)); });
  define("clearInterval", (id) => { timers.delete(Number(id)); });

  /* Runs the earliest timer due at or before limit. Returns 0 when another
   * timer is due, else the milliseconds until the next timer, or -1. */
  function runTimers(limit) {
    let first = null;
    for (const [id, timer] of timers) {
      if (timer.due <= limit && (first === null || timer.due < first[1].due)) first = [id, timer];
    }
    if (first !== null) {
      const timer = first[1];
      if (timer.repeat) timer.due = clock() + Math.max(timer.delay, 1);
      else timers.delete(first[0]);
      timer.callback.apply(globalThis, timer.args);
    }
    if (timers.size === 0) return -1;
    let next = Infinity;
    for (const timer of timers.values()) next = Math.min(next, timer.due);
    return next <= limit ? 0 : Math.max(next - clock(), 0.001);
  }

  define("queueMicrotask", (callback) => {
    if (typeof callback !== "function") throw new TypeError("The callback must be a function");
    native.queueMicrotask(callback);
  });

  define("structuredClone", (value) => {
    try {
      return native.clone(value);
    } catch (e) {
      throw new DOMException(e.message, "DataCloneError");
    }
  });

  /* crypto */
  const hex = (b) => (b < 16 ? "0" : "") + b.toString(16);
  define("crypto", {
    getRandomValues: (array) => {
      const integer = array instanceof Int8Array || array instanceof Uint8Array || array instanceof Uint8ClampedArray ||
        array instanceof Int16Array || array instanceof Uint16Array || array instanceof Int32Array ||
        array instanceof Uint32Array || array instanceof BigInt64Array || array instanceof BigUint64Array;
      if (!integer) throw new DOMException("getRandomValues() needs an integer typed array", "TypeMismatchError");
      if (array.byteLength > 65536) throw new DOMException("getRandomValues() fills at most 65536 bytes", "QuotaExceededError");
      return native.randomFill(array);
    },
    randomUUID: () => {
      const b = native.randomFill(new Uint8Array(16));
      b[6] = (b[6] & 0x0f) | 0x40;
      b[8] = (b[8] & 0x3f) | 0x80;
      const h = Array.from(b, hex);
      return h.slice(0, 4).join("") + "-" + h.slice(4, 6).join("") + "-" + h.slice(6, 8).join("") + "-" +
        h.slice(8, 10).join("") + "-" + h.slice(10).join("");
    }
  });

  /* base64 */
  const B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  define("btoa", (data) => {
    data = String(data);
    let out = "";
    for (let i = 0; i < data.length; i += 3) {
      const c0 = data.charCodeAt(i), c1 = data.charCodeAt(i + 1), c2 = data.charCodeAt(i + 2);
      if (c0 > 255 || c1 > 255 || c2 > 255)
        throw new DOMException("The string contains characters outside of the Latin1 range", "InvalidCharacterError");
      const n = (c0 << 16) | ((c1 || 0) << 8) | (c2 || 0);
      out += B64[n >> 18] + B64[(n >> 12) & 63] +
        (i + 1 < data.length ? B64[(n >> 6) & 63] : "=") + (i + 2 < data.length ? B64[n & 63] : "=");
    }
    return out;
  });
  define("atob", (data) => {
    data = String(data).replace(/[\t\n\f\r ]/g, "");
    if (data.length % 4 === 0) data = data.replace(/==?$/, "");
    if (data.length % 4 === 1 || /[^A-Za-z0-9+/]/.test(data))
      throw new DOMException("The string to be decoded is not correctly encoded", "InvalidCharacterError");
    let out = "", bits = 0, value = 0;
    for (let i = 0; i < data.length; i++) {
      value = (value << 6) | B64.indexOf(data[i]);
      bits += 6;
      if (bits >= 8) {
        bits -= 8;
        out += String.fromCharCode((value >> bits) & 255);
      }
    }
    return out;
  });

  /* TextEncoder and TextDecoder */
  const bytesOf = (input) => {
    if (input === undefined) return new Uint8Array(0);
    if (input instanceof ArrayBuffer || (typeof SharedArrayBuffer !== "undefined" && input instanceof SharedArrayBuffer))
      return new Uint8Array(input);
    if (ArrayBuffer.isView(input)) return new Uint8Array(input.buffer, input.byteOffset, input.byteLength);
    throw new TypeError("The input must be an ArrayBuffer or an ArrayBufferView");
  };

  class TextEncoder {
    get encoding() { return "utf-8"; }
    encode(input = "") { return utf8(input); }
    encodeInto(source, destination) {
      source = String(source);
      let read = 0, written = 0;
      for (const ch of source) {
        const bytes = utf8(ch);
        if (written + bytes.length > destination.length) break;
        destination.set(bytes, written);
        written += bytes.length;
        read += ch.length;
      }
      return {read: read, written: written};
    }
  }
  define("TextEncoder", TextEncoder);

  const LABELS = {"utf-8": "utf-8", "utf8": "utf-8", "unicode-1-1-utf-8": "utf-8",
                  "utf-16le": "utf-16le", "utf-16": "utf-16le"};
  /* Number of bytes at the end that start an incomplete UTF-8 sequence */
  const incompleteUtf8 = (bytes) => {
    for (let k = 1; k <= Math.min(3, bytes.length); k++) {
      const b = bytes[bytes.length - k];
      if ((b & 0xc0) === 0x80) continue;
      const need = b >= 0xf0 ? 4 : b >= 0xe0 ? 3 : b >= 0xc0 ? 2 : 1;
      return need > k ? k : 0;
    }
    return 0;
  };

  class TextDecoder {
    #encoding; #fatal; #ignoreBOM; #pending = new Uint8Array(0); #started = false;
    constructor(label = "utf-8", options = {}) {
      const encoding = LABELS[String(label).trim().toLowerCase()];
      if (encoding === undefined) throw new RangeError("The encoding '" + label + "' is not supported");
      this.#encoding = encoding;
      this.#fatal = Boolean(options && options.fatal);
      this.#ignoreBOM = Boolean(options && options.ignoreBOM);
    }
    get encoding() { return this.#encoding; }
    get fatal() { return this.#fatal; }
    get ignoreBOM() { return this.#ignoreBOM; }
    decode(input, options = {}) {
      const stream = Boolean(options && options.stream);
      let bytes = bytesOf(input);
      if (this.#pending.length) {
        const joined = new Uint8Array(this.#pending.length + bytes.length);
        joined.set(this.#pending);
        joined.set(bytes, this.#pending.length);
        bytes = joined;
      }
      let keep = 0;
      if (stream) keep = this.#encoding === "utf-8" ? incompleteUtf8(bytes) : bytes.length % 2;
      this.#pending = bytes.slice(bytes.length - keep);
      bytes = bytes.subarray(0, bytes.length - keep);
      let out;
      if (this.#encoding === "utf-8") {
        out = native.utf8Decode(bytes, this.#fatal);
        if (out === null) throw new TypeError("The encoded data was not valid utf-8");
      } else {
        if (bytes.length % 2 === 1 && this.#fatal) throw new TypeError("The encoded data was not valid utf-16le");
        const n = bytes.length - (bytes.length % 2);
        const chunks = [];
        for (let i = 0; i < n; i += 16384) {
          const end = Math.min(n, i + 16384);
          const units = new Array((end - i) / 2);
          for (let j = i, k = 0; j < end; j += 2, k++) units[k] = bytes[j] | (bytes[j + 1] << 8);
          chunks.push(String.fromCharCode.apply(null, units));
        }
        out = chunks.join("");
        if (bytes.length % 2 === 1) out += "�";
      }
      if (!this.#started && !this.#ignoreBOM && out.charCodeAt(0) === 0xfeff) out = out.slice(1);
      this.#started = stream;
      return out;
    }
  }
  define("TextDecoder", TextDecoder);

  /* URLSearchParams */
  const internals = new WeakMap();
  const percentDecode = (text) => {
    if (!text.includes("%")) return text;
    const bytes = [];
    for (let i = 0; i < text.length; i++) {
      if (text[i] === "%" && /^[0-9a-fA-F]{2}$/.test(text.slice(i + 1, i + 3))) {
        bytes.push(parseInt(text.slice(i + 1, i + 3), 16));
        i += 2;
      } else {
        for (const b of utf8(text[i] + ((text.charCodeAt(i) & 0xfc00) === 0xd800 ? text[++i] || "" : ""))) bytes.push(b);
      }
    }
    return native.utf8Decode(new Uint8Array(bytes), false);
  };
  const formEncode = (text) => {
    let out = "";
    for (const b of utf8(text)) {
      if ((b >= 0x30 && b <= 0x39) || (b >= 0x41 && b <= 0x5a) || (b >= 0x61 && b <= 0x7a) ||
          b === 0x2a || b === 0x2d || b === 0x2e || b === 0x5f) out += String.fromCharCode(b);
      else if (b === 0x20) out += "+";
      else out += "%" + hex(b).toUpperCase();
    }
    return out;
  };
  const parseQuery = (query) => {
    const list = [];
    for (const part of query.split("&")) {
      if (part === "") continue;
      const eq = part.indexOf("=");
      const name = eq < 0 ? part : part.slice(0, eq);
      const value = eq < 0 ? "" : part.slice(eq + 1);
      list.push([percentDecode(name.replace(/\+/g, " ")), percentDecode(value.replace(/\+/g, " "))]);
    }
    return list;
  };

  class URLSearchParams {
    #list = [];
    constructor(init = "") {
      if (init instanceof URLSearchParams) {
        this.#list = init.#list.map((p) => [p[0], p[1]]);
      } else if (typeof init === "object" && init !== null) {
        if (typeof init[Symbol.iterator] === "function") {
          for (const pair of init) {
            const p = Array.from(pair);
            if (p.length !== 2) throw new TypeError("Each query pair must have two elements");
            this.#list.push([String(p[0]), String(p[1])]);
          }
        } else {
          for (const key of Object.keys(init)) this.#list.push([key, String(init[key])]);
        }
      } else {
        let text = String(init);
        if (text.startsWith("?")) text = text.slice(1);
        this.#list = parseQuery(text);
      }
    }
    #update() {
      const url = internals.get(this);
      if (url !== undefined) {
        const query = this.toString();
        internals.get(url).query = query === "" ? null : query;
      }
    }
    _replace(query) { this.#list = query === null ? [] : parseQuery(query); }
    get size() { return this.#list.length; }
    append(name, value) { this.#list.push([String(name), String(value)]); this.#update(); }
    delete(name, value) {
      name = String(name);
      this.#list = this.#list.filter((p) => !(p[0] === name && (value === undefined || p[1] === String(value))));
      this.#update();
    }
    get(name) { const p = this.#list.find((q) => q[0] === String(name)); return p === undefined ? null : p[1]; }
    getAll(name) { return this.#list.filter((q) => q[0] === String(name)).map((q) => q[1]); }
    has(name, value) {
      return this.#list.some((p) => p[0] === String(name) && (value === undefined || p[1] === String(value)));
    }
    set(name, value) {
      name = String(name);
      const i = this.#list.findIndex((p) => p[0] === name);
      if (i < 0) this.#list.push([name, String(value)]);
      else this.#list = this.#list.filter((p, j) => j === i || p[0] !== name).map((p) => p[0] === name ? [name, String(value)] : p);
      this.#update();
    }
    sort() {
      this.#list = this.#list.map((p, i) => [p, i]).sort((a, b) => (a[0][0] < b[0][0] ? -1 : a[0][0] > b[0][0] ? 1 : a[1] - b[1])).map((x) => x[0]);
      this.#update();
    }
    forEach(callback, thisArg) { for (const p of this.#list) callback.call(thisArg, p[1], p[0], this); }
    *keys() { for (const p of this.#list) yield p[0]; }
    *values() { for (const p of this.#list) yield p[1]; }
    *entries() { for (const p of this.#list) yield [p[0], p[1]]; }
    [Symbol.iterator]() { return this.entries(); }
    toString() { return this.#list.map((p) => formEncode(p[0]) + "=" + formEncode(p[1])).join("&"); }
    get [Symbol.toStringTag]() { return "URLSearchParams"; }
  }
  define("URLSearchParams", URLSearchParams);

  /* URL, following the WHATWG URL standard for the common cases. Host names
   * are not converted to punycode and IPv4 addresses are not normalized. */
  const SPECIAL = {"ftp:": "21", "file:": "", "http:": "80", "https:": "443", "ws:": "80", "wss:": "443"};
  const encodeWith = (pattern) => (text) => text.replace(pattern, (ch) =>
    Array.from(utf8(ch), (b) => "%" + hex(b).toUpperCase()).join(""));
  const encodePath = encodeWith(/[\x00-\x20"#<>?`{}\x7f]|[^\x00-\x7f]/gu);
  const encodeOpaque = encodeWith(/[\x00-\x1f\x7f]|[^\x00-\x7f]/gu);
  const encodeQuery = encodeWith(/[\x00-\x20"#<>\x7f]|[^\x00-\x7f]/gu);
  const encodeSpecialQuery = encodeWith(/[\x00-\x20"#'<>\x7f]|[^\x00-\x7f]/gu);
  const encodeFragment = encodeWith(/[\x00-\x20"<>`\x7f]|[^\x00-\x7f]/gu);
  const encodeUserinfo = encodeWith(/[\x00-\x20"#<>?`{}/:;=@[\\\]^|\x7f]|[^\x00-\x7f]/gu);
  const invalid = (input) => new TypeError("Invalid URL: " + input);

  const removeDots = (path, special) => {
    const out = [];
    const segments = path.split("/");
    for (let i = 0; i < segments.length; i++) {
      const s = segments[i].toLowerCase();
      const last = i === segments.length - 1;
      if (s === ".." || s === ".%2e" || s === "%2e." || s === "%2e%2e") {
        if (out.length > 1) out.pop();
        if (last) out.push("");
      } else if (s === "." || s === "%2e") {
        if (last) out.push("");
      } else {
        out.push(segments[i]);
      }
    }
    let result = out.join("/");
    if (!result.startsWith("/")) result = "/" + result;
    return special && result === "" ? "/" : result;
  };

  const splitRest = (rest) => {
    let fragment = null, query = null;
    const h = rest.indexOf("#");
    if (h >= 0) { fragment = rest.slice(h + 1); rest = rest.slice(0, h); }
    const q = rest.indexOf("?");
    if (q >= 0) { query = rest.slice(q + 1); rest = rest.slice(0, q); }
    return [rest, query, fragment];
  };

  const parseHost = (host, special, input) => {
    if (host.startsWith("[")) {
      if (!host.endsWith("]") || !/^\[[0-9a-fA-F:.]+\]$/.test(host)) throw invalid(input);
      return host.toLowerCase();
    }
    if (!special) {
      if (/[\x00\t\n\r #/:<>?@[\\\]^|]/.test(host)) throw invalid(input);
      return encodeOpaque(host);
    }
    host = percentDecode(host).toLowerCase();
    if (/[\x00-\x20#%/:<>?@[\\\]^|\x7f]/.test(host)) throw invalid(input);
    return host;
  };

  const parseAuthority = (record, authority, input) => {
    const at = authority.lastIndexOf("@");
    if (at >= 0) {
      const userinfo = authority.slice(0, at);
      const colon = userinfo.indexOf(":");
      record.username = encodeUserinfo(colon < 0 ? userinfo : userinfo.slice(0, colon));
      record.password = colon < 0 ? "" : encodeUserinfo(userinfo.slice(colon + 1));
      authority = authority.slice(at + 1);
    }
    const portAt = authority.lastIndexOf(":");
    let host = authority, port = "";
    if (portAt >= 0 && authority.indexOf("]", portAt) < 0) {
      host = authority.slice(0, portAt);
      port = authority.slice(portAt + 1);
      if (!/^[0-9]*$/.test(port) || (port !== "" && Number(port) > 65535)) throw invalid(input);
      if (port !== "") port = String(Number(port));
      if (port === SPECIAL[record.protocol]) port = "";
    }
    const special = record.protocol in SPECIAL;
    record.hostname = parseHost(host, special, input);
    if (special && record.protocol !== "file:" && record.hostname === "") throw invalid(input);
    record.port = port;
  };

  const finish = (record, path, query, fragment) => {
    const special = record.protocol in SPECIAL;
    record.pathname = record.opaque ? encodeOpaque(path) : removeDots(encodePath(path), special);
    if (!special && !record.opaque && record.hostname === null && path === "") record.pathname = "";
    record.query = query === null ? null : (special ? encodeSpecialQuery(query) : encodeQuery(query));
    record.fragment = fragment === null ? null : encodeFragment(fragment);
    return record;
  };

  const parseAbsolute = (scheme, rest, input) => {
    const record = {protocol: scheme, username: "", password: "", hostname: null, port: "",
                    pathname: "", query: null, fragment: null, opaque: false};
    const special = scheme in SPECIAL;
    let [path, query, fragment] = splitRest(rest);
    if (special) path = path.replace(/\\/g, "/");
    if (scheme === "file:") {
      if (path.startsWith("//")) {
        const end = path.indexOf("/", 2);
        parseAuthority(record, end < 0 ? path.slice(2) : path.slice(2, end), input);
        path = end < 0 ? "" : path.slice(end);
      } else {
        record.hostname = "";
      }
    } else if (special) {
      path = path.replace(/^\/*/, "");
      const end = path.indexOf("/");
      parseAuthority(record, end < 0 ? path : path.slice(0, end), input);
      path = end < 0 ? "" : path.slice(end);
    } else if (path.startsWith("//")) {
      const end = path.indexOf("/", 2);
      parseAuthority(record, end < 0 ? path.slice(2) : path.slice(2, end), input);
      path = end < 0 ? "" : path.slice(end);
    } else if (!path.startsWith("/")) {
      record.opaque = true;
    }
    return finish(record, path, query, fragment);
  };

  const parseRecord = (input, base) => {
    input = String(input).replace(/^[\x00-\x20]+|[\x00-\x20]+$/g, "").replace(/[\t\n\r]/g, "");
    const m = /^([a-zA-Z][a-zA-Z0-9+.\-]*):([\s\S]*)$/.exec(input);
    if (m !== null) {
      const scheme = m[1].toLowerCase() + ":";
      const rest = m[2];
      if (base && base.protocol === scheme && scheme in SPECIAL && scheme !== "file:" &&
          !/^[/\\]/.test(rest)) return resolve(base, rest, input);
      return parseAbsolute(scheme, rest, input);
    }
    if (!base) throw invalid(input);
    return resolve(base, input, input);
  };

  const resolve = (base, ref, input) => {
    const special = base.protocol in SPECIAL;
    if (base.opaque) {
      if (!ref.startsWith("#")) throw invalid(input);
      return Object.assign({}, base, {fragment: encodeFragment(ref.slice(1))});
    }
    let [path, query, fragment] = splitRest(ref);
    if (special) path = path.replace(/\\/g, "/");
    if (path.startsWith("//")) return parseAbsolute(base.protocol, ref, input);
    const record = Object.assign({}, base, {query: base.query, fragment: null});
    if (path === "") {
      if (query !== null) record.query = special ? encodeSpecialQuery(query) : encodeQuery(query);
      record.fragment = fragment === null ? null : encodeFragment(fragment);
      return record;
    }
    if (!path.startsWith("/")) path = base.pathname.slice(0, base.pathname.lastIndexOf("/") + 1) + path;
    return finish(record, path, query, fragment);
  };

  const serialize = (r) => {
    let out = r.protocol;
    if (r.hostname !== null) {
      out += "//";
      if (r.username !== "" || r.password !== "") out += r.username + (r.password !== "" ? ":" + r.password : "") + "@";
      out += r.hostname + (r.port !== "" ? ":" + r.port : "");
    } else if (!r.opaque && r.pathname.startsWith("//")) {
      out += "/.";
    }
    return out + r.pathname + (r.query !== null ? "?" + r.query : "") + (r.fragment !== null ? "#" + r.fragment : "");
  };

  class URL {
    #params;
    constructor(url, base) {
      const baseRecord = base === undefined ? null : internals.get(base instanceof URL ? base : new URL(base));
      internals.set(this, parseRecord(url, baseRecord));
      this.#params = new URLSearchParams(internals.get(this).query || "");
      internals.set(this.#params, this);
    }
    static canParse(url, base) { try { new URL(url, base); return true; } catch (e) { return false; } }
    static parse(url, base) { try { return new URL(url, base); } catch (e) { return null; } }
    get href() { return serialize(internals.get(this)); }
    set href(value) {
      internals.set(this, parseRecord(value, null));
      this.#params._replace(internals.get(this).query);
    }
    get origin() {
      const r = internals.get(this);
      if (r.protocol === "blob:") {
        const inner = URL.parse(r.pathname);
        return inner !== null && (inner.protocol === "http:" || inner.protocol === "https:") ? inner.origin : "null";
      }
      return (r.protocol in SPECIAL && r.protocol !== "file:") ? r.protocol + "//" + this.host : "null";
    }
    get protocol() { return internals.get(this).protocol; }
    set protocol(value) {
      const r = internals.get(this);
      const scheme = String(value).replace(/:.*$/, "").toLowerCase() + ":";
      if (!/^[a-z][a-z0-9+.\-]*:$/.test(scheme) || (scheme in SPECIAL) !== (r.protocol in SPECIAL)) return;
      r.protocol = scheme;
      if (r.port === SPECIAL[scheme]) r.port = "";
    }
    get username() { return internals.get(this).username; }
    set username(value) { const r = internals.get(this); if (r.hostname) r.username = encodeUserinfo(String(value)); }
    get password() { return internals.get(this).password; }
    set password(value) { const r = internals.get(this); if (r.hostname) r.password = encodeUserinfo(String(value)); }
    get host() { const r = internals.get(this); return r.hostname === null ? "" : r.hostname + (r.port !== "" ? ":" + r.port : ""); }
    set host(value) {
      const r = internals.get(this);
      if (r.opaque) return;
      const copy = Object.assign({}, r);
      try { parseAuthority(copy, String(value), value); } catch (e) { return; }
      r.hostname = copy.hostname;
      r.port = copy.port;
    }
    get hostname() { const r = internals.get(this); return r.hostname === null ? "" : r.hostname; }
    set hostname(value) {
      const r = internals.get(this);
      if (r.opaque) return;
      try { r.hostname = parseHost(String(value), r.protocol in SPECIAL, value); } catch (e) { return; }
    }
    get port() { return internals.get(this).port; }
    set port(value) {
      const r = internals.get(this);
      const port = String(value).replace(/[^0-9].*$/, "");
      if (r.hostname === null || r.hostname === "" || r.protocol === "file:" || Number(port) > 65535) return;
      r.port = port === "" || port === SPECIAL[r.protocol] ? "" : String(Number(port));
    }
    get pathname() { return internals.get(this).pathname; }
    set pathname(value) {
      const r = internals.get(this);
      if (r.opaque) return;
      let path = String(value);
      if (r.protocol in SPECIAL) path = path.replace(/\\/g, "/");
      r.pathname = removeDots(encodePath(path.startsWith("/") ? path : "/" + path), r.protocol in SPECIAL);
    }
    get search() { const q = internals.get(this).query; return q === null || q === "" ? "" : "?" + q; }
    set search(value) {
      const r = internals.get(this);
      let query = String(value);
      if (query.startsWith("?")) query = query.slice(1);
      r.query = query === "" ? null : (r.protocol in SPECIAL ? encodeSpecialQuery(query) : encodeQuery(query));
      this.#params._replace(r.query);
    }
    get searchParams() { return this.#params; }
    get hash() { const f = internals.get(this).fragment; return f === null || f === "" ? "" : "#" + f; }
    set hash(value) {
      let fragment = String(value);
      if (fragment.startsWith("#")) fragment = fragment.slice(1);
      internals.get(this).fragment = fragment === "" ? null : encodeFragment(fragment);
    }
    toString() { return this.href; }
    toJSON() { return this.href; }
    get [Symbol.toStringTag]() { return "URL"; }
  }
  define("URL", URL);

  /* Without its ICU data, V8 ends the process in Intl and the locale
   * functions, so they throw an error instead */
  if (native.icuMissing) {
    const fail = function () {
      throw new Error("ICU data is not available: set options(V8n.icu_data) to the file icudtl.dat of libv8 before loading V8n.");
    };
    const replace = (object, names) => {
      for (const name of names) {
        if (object && typeof object[name] === "function")
          Object.defineProperty(object, name, {value: fail, writable: true, configurable: true});
      }
    };
    replace(String.prototype, ["localeCompare", "normalize", "toLocaleLowerCase", "toLocaleUpperCase"]);
    replace(Number.prototype, ["toLocaleString"]);
    replace(BigInt.prototype, ["toLocaleString"]);
    replace(Array.prototype, ["toLocaleString"]);
    replace(Object.getPrototypeOf(Int8Array.prototype), ["toLocaleString"]);
    replace(Date.prototype, ["toLocaleString", "toLocaleDateString", "toLocaleTimeString"]);
    if (typeof Intl === "object")
      replace(Intl, Object.getOwnPropertyNames(Intl));
  }

  return runTimers;
})
)JS";

#endif
