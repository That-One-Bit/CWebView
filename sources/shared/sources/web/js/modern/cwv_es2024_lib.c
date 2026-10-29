/* CWebView ES2024 library prelude.
 *
 * ES2024: Object.groupBy, Map.groupBy, Promise.withResolvers.
 *
 * BEGINNER NOTE: the string below is ONE long C string that CONTAINS a
 * JavaScript prelude. The host evaluates it once when the JS context is
 * created; every `if (!...)` guard keeps us from redefining anything the
 * runtime already provides.
 */
#include "web/js/modern/cwv_es_libs.h"

const char *cwv_es2024_lib_prelude(void)
{
    return
"if (!Object.groupBy) Object.groupBy = function (list, f) {  var o = {};  for (var i = 0; i < list.length; i++) {    var k = f(list[i]);    if (!o[k]) o[k] = [];    o[k].push(list[i]);  }  return o;};i"
"f (!Map.groupBy) Map.groupBy = function (list, f) {  var m = new Map();  for (var i = 0; i < list.length; i++) {    var k = f(list[i]);    if (!m.has(k)) m.set(k, []);    m.get(k).push(list[i]);  }  r"
"eturn m;};if (!Promise.withResolvers) Promise.withResolvers = function () {  var o = {};  o.promise = new Promise(function (res, rej) { o.resolve = res; o.reject = rej; });  return o;};";
}
