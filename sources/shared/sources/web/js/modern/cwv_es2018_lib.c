/* CWebView ES2018 library prelude.
 *
 * ES2018: Promise.prototype.finally and the __vw_oread/__vw_aread helpers the transformer emits for object/array rest destructuring.
 *
 * BEGINNER NOTE: the string below is ONE long C string that CONTAINS a
 * JavaScript prelude. The host evaluates it once when the JS context is
 * created; every `if (!...)` guard keeps us from redefining anything the
 * runtime already provides.
 */
#include "web/js/modern/cwv_es_libs.h"

const char *cwv_es2018_lib_prelude(void)
{
    return
"if (Promise.prototype && !Promise.prototype.finally) {  Promise.prototype.finally = function (f) {    return this.then(function (v) {      var r = f();      return r && typeof r.then === \"function\" "
"       ? r.then(function () { return v; }) : v;    }, function (e) {      var r = f();      if (r && typeof r.then === \"function\") r.then(function () { });      throw e;    });  };}"
"if (typeof __vw_oread === \"undefined\") __vw_oread = function (src, keys) {  var o = {};  for (var k in src) {    if (!src.hasOwnProperty(k) || keys.indexOf(k) >= 0) continue;    o[k] = src[k]; }  re"
"turn o; };"
"if (typeof __vw_aread === \"undefined\") __vw_aread = function (src, i) {  return __vw_spread(src).slice(i); };";
}
