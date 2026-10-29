/* CWebView ES2015 library prelude.
 *
 * ES2015 (ES6) base library: Array/Number/Math/String extensions, Object.assign, Map/Set, the Promise + job-queue runtime the transformer's async output depends on, and the __vw_spread helper.
 *
 * BEGINNER NOTE: the string below is ONE long C string that CONTAINS a
 * JavaScript prelude. The host evaluates it once when the JS context is
 * created; every `if (!...)` guard keeps us from redefining anything the
 * runtime already provides.
 */
#include "web/js/modern/cwv_es_libs.h"

const char *cwv_es2015_lib_prelude(void)
{
    return
"if (!Array.prototype.find) Array.prototype.find = function (f, t) {  for (var i = 0; i < this.length; i++)    if (f.call(t, this[i], i, this)) return this[i];  return undefined; };"
"if (!Array.prototype.findIndex) Array.prototype.findIndex = function (f, t) {  for (var i = 0; i < this.length; i++)    if (f.call(t, this[i], i, this)) return i;  return -1; };"
"if (!Array.prototype.fill) Array.prototype.fill = function (x, s, e) {  s = s || 0; e = (e === undefined) ? this.length : e;  for (var i = s; i < e; i++) this[i] = x; return this; };"
"if (!String.prototype.startsWith) String.prototype.startsWith = function (p) {  return this.lastIndexOf(p, 0) === 0; };"
"if (!String.prototype.endsWith) String.prototype.endsWith = function (p) {  var n = this.length - p.length;  return n >= 0 && this.indexOf(p, n) === n; };"
"if (!String.prototype.includes) String.prototype.includes = function (x, s) {  return this.indexOf(x, s || 0) >= 0; };"
"if (!String.prototype.repeat) String.prototype.repeat = function (n) {  var r = \"\"; for (var i = 0; i < n; i++) r += this; return r; };"
"if (!Object.assign) Object.assign = function (t) {  for (var i = 1; i < arguments.length; i++) {    var src = arguments[i];    if (!src) continue;    for (var k in src) if (src.hasOwnProperty(k)) t[k]"
" = src[k];  } return t; };"
"if (!Number.isInteger) Number.isInteger = function (n) {  return typeof n === \"number\" && isFinite(n) && Math.floor(n) === n; };"
"if (!Number.isFinite) Number.isFinite = function (n) {  return typeof n === \"number\" && isFinite(n); };"
"if (!Number.isNaN) Number.isNaN = function (n) {  return typeof n === \"number\" && isNaN(n); };"
"if (!Math.sign) Math.sign = function (n) {  n = +n; return n > 0 ? 1 : n < 0 ? -1 : n; };"
"if (!Math.trunc) Math.trunc = function (n) {  return n < 0 ? Math.ceil(n) : Math.floor(n); };"
"if (!Math.cbrt) Math.cbrt = function (n) {  return n < 0 ? -Math.pow(-n, 1 / 3) : Math.pow(n, 1 / 3); };"
"if (!Math.log2) Math.log2 = function (n) {  return Math.log(n) / Math.LN2; };"
"if (!Math.log10) Math.log10 = function (n) {  return Math.log(n) / Math.LN10; };"
"if (!Math.hypot) Math.hypot = function (a, b) {  return Math.sqrt(a * a + b * b); };"
"if (typeof Map === \"undefined\") {  function VitaMap() { this._k = []; this._v = []; }  Object.defineProperty(VitaMap.prototype, \"size\", {    get: function () { return this._k.length; } });  VitaMa"
"p.prototype.set = function (k, v) {    var i = this._k.indexOf(k);    if (i < 0) { this._k.push(k); this._v.push(v); }    else this._v[i] = v; return this; };  VitaMap.prototype.get = function (k) {  "
"  var i = this._k.indexOf(k);    return i < 0 ? undefined : this._v[i]; };  VitaMap.prototype.has = function (k) {    return this._k.indexOf(k) >= 0; };  VitaMap.prototype.delete = function (k) {    v"
"ar i = this._k.indexOf(k);    if (i < 0) return false;    this._k.splice(i, 1); this._v.splice(i, 1); return true; };  VitaMap.prototype.clear = function () {    this._k.length = 0; this._v.length = 0"
"; };  VitaMap.prototype.forEach = function (f, t) {    for (var i = 0; i < this._k.length; i++)      f.call(t, this._v[i], this._k[i], this); };  VitaMap.prototype.keys = function () { return this._k."
"slice(); };  VitaMap.prototype.values = function () { return this._v.slice(); };  Map = VitaMap;}"
"var __vw_jobs = [];function __vw_job(f) { __vw_jobs.push(f); }function __vw_drain() {  var n = 0;  while (__vw_jobs.length && n++ < 4096) {    try { __vw_jobs.shift()(); } catch (e) { }  }}"
"if (typeof Promise !== \"function\" || !Promise.resolve) {  Promise = (function () {    function VwP(r) {      this._s = 0; this._v = undefined; this._c = [];      if (typeof r === \"function\") {    "
"    var me = this;        try {          r(function (v) { vw_settle(me, 1, v); },            function (e) { vw_settle(me, 2, e); });        } catch (err) { vw_settle(me, 2, err); }      }    }    func"
"tion vw_settle(p, s, v) {      if (p._s) return;      if (s === 1 && v !== null && (typeof v === \"object\" ||          typeof v === \"function\") && typeof v.then === \"function\") {        var done "
"= false;        try {          v.then(function (w) { if (!done) { done = true; vw_settle(p, 1, w); } },                  function (e) { if (!done) { done = true; vw_settle(p, 2, e); } });        } cat"
"ch (e) { if (!done) { done = true; vw_settle(p, 2, e); } }        return;      }      p._s = s; p._v = v; vw_flush(p);    }    function vw_flush(p) {      if (!p._s || !p._c.length) return;      var s"
" = p._s, v = p._v, cb = p._c;      p._c = [];      for (var i = 0; i < cb.length; i++) (function (pair) {        __vw_jobs.push(function () {          var h = pair[s - 1], n = pair[2];          if (ty"
"peof h !== \"function\") { vw_settle(n, s, v); return; }          var r;          try { r = h(v); }          catch (e) { vw_settle(n, 2, e); return; }          vw_settle(n, 1, r);        });      })(c"
"b[i]);    }    VwP.prototype.then = function (onF, onR) {      var next = new VwP();      this._c.push([onF, onR, next]);      vw_flush(this);      return next;    };    VwP.prototype.catch = function"
" (onR) {      return this.then(undefined, onR);    };    VwP.resolve = function (v) {      var p = new VwP(); vw_settle(p, 1, v); return p;    };    VwP.reject = function (e) {      var p = new VwP();"
" vw_settle(p, 2, e); return p;    };    VwP.all = function (list) {      var p = new VwP(), arr = [], n = 0, left = list.length, ok = true;      if (!left) return VwP.resolve(arr);      for (var i = 0"
"; i < left; i++) (function (i) {        VwP.resolve(list[i]).then(function (w) {          arr[i] = w; n++;          if (n === left && ok) { ok = false; vw_settle(p, 1, arr); }        }, function (e) {"
"          if (ok) { ok = false; vw_settle(p, 2, e); }        });      })(i);      return p;    };    VwP.race = function (list) {      var p = new VwP(), ok = true;      for (var i = 0; i < list.lengt"
"h; i++)        VwP.resolve(list[i]).then(function (w) {          if (ok) { ok = false; vw_settle(p, 1, w); }        }, function (e) {          if (ok) { ok = false; vw_settle(p, 2, e); }        });   "
"   return p;    };    return VwP;  })();}"
"if (!Array.of) Array.of = function () {  return Array.prototype.slice.call(arguments);};"
"if (!Array.from) Array.from = function (a, f) {  var out = [];  if (a && typeof a.length === \"number\")    for (var i = 0; i < a.length; i++) out.push(f ? f(a[i], i) : a[i]);  return out;};"
"if (!Array.prototype.copyWithin) Array.prototype.copyWithin = function (t, s, e) {  s = s || 0;  e = e === undefined ? this.length : e;  var tmp = this.slice(s, e);  for (var i = 0; i < tmp.length; i+"
"+) this[t + i] = tmp[i];  return this;};"
"if (!Number.isSafeInteger) Number.isSafeInteger = function (n) {  return typeof n === \"number\" && isFinite(n) &&    Math.floor(n) === n && Math.abs(n) <= 9007199254740991;};"
"if (!Math.clz32) Math.clz32 = function (n) {  n = n >>> 0;  var c = 0;  while (n) { n >>>= 1; c++; }  return 32 - c;};"
"function __vw_spread(x) {  if (x == null) return [];  if (Array.isArray(x)) return x;  if (typeof x === \"string\") return x.split(\"\");  if (typeof x !== \"function\" && typeof x.length === \"number"
"\")    return Array.prototype.slice.call(x);  return [x];}"
"if (typeof Set === \"undefined\") {  function VitaSet() { this._v = []; }  Object.defineProperty(VitaSet.prototype, \"size\", {    get: function () { return this._v.length; } });  VitaSet.prototype.ad"
"d = function (v) {    if (this._v.indexOf(v) < 0) this._v.push(v); return this; };  VitaSet.prototype.has = function (v) {    return this._v.indexOf(v) >= 0; };  VitaSet.prototype.delete = function (v"
") {    var i = this._v.indexOf(v);    if (i < 0) return false;    this._v.splice(i, 1); return true; };  VitaSet.prototype.clear = function () { this._v.length = 0; };  VitaSet.prototype.forEach = fun"
"ction (f, t) {    for (var i = 0; i < this._v.length; i++) f.call(t, this._v[i], this._v[i], this); };  VitaSet.prototype.values = VitaSet.prototype.keys =    function () { return this._v.slice(); }; "
" Set = VitaSet;}";
}
