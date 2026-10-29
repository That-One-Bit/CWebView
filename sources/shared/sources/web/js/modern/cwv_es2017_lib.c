/* CWebView ES2017 library prelude.
 *
 * ES2017: String padStart/padEnd, Object.entries/values.
 *
 * BEGINNER NOTE: the string below is ONE long C string that CONTAINS a
 * JavaScript prelude. The host evaluates it once when the JS context is
 * created; every `if (!...)` guard keeps us from redefining anything the
 * runtime already provides.
 */
#include "web/js/modern/cwv_es_libs.h"

const char *cwv_es2017_lib_prelude(void)
{
    return
"if (!String.prototype.padStart) String.prototype.padStart = function (w, c) {  c = c || \" \"; var s = this;  while (s.length < w) s = c + s;  return s; };"
"if (!String.prototype.padEnd) String.prototype.padEnd = function (w, c) {  c = c || \" \"; var s = this;  while (s.length < w) s = s + c;  return s; };"
"if (!Object.entries) Object.entries = function (o) {  var out = [];  for (var k in o) if (o.hasOwnProperty(k)) out.push([k, o[k]]);  return out;};"
"if (!Object.values) Object.values = function (o) {  var out = [];  for (var k in o) if (o.hasOwnProperty(k)) out.push(o[k]);  return out;};";
}
