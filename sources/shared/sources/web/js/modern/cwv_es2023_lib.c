/* CWebView ES2023 library prelude.
 *
 * ES2023: Array findLast/findLastIndex and the change-by-copy methods toSorted/toReversed/toSpliced/with.
 *
 * BEGINNER NOTE: the string below is ONE long C string that CONTAINS a
 * JavaScript prelude. The host evaluates it once when the JS context is
 * created; every `if (!...)` guard keeps us from redefining anything the
 * runtime already provides.
 */
#include "web/js/modern/cwv_es_libs.h"

const char *cwv_es2023_lib_prelude(void)
{
    return
"if (!Array.prototype.findLast) Array.prototype.findLast = function (f, t) {  for (var i = this.length - 1; i >= 0; i--)    if (f.call(t, this[i], i, this)) return this[i];  return undefined;};"
"if (!Array.prototype.findLastIndex) Array.prototype.findLastIndex = function (f, t) {  for (var i = this.length - 1; i >= 0; i--)    if (f.call(t, this[i], i, this)) return i;  return -1;};"
"if (!Array.prototype.toSorted) Array.prototype.toSorted = function (f) {  return this.slice().sort(f);};if (!Array.prototype.toReversed) Array.prototype.toReversed = function () {  var out = []; for ("
"var i = this.length - 1; i >= 0; i--) out.push(this[i]);  return out;};if (!Array.prototype.toSpliced) Array.prototype.toSpliced = function (s, d) {  var a = this.slice(); Array.prototype.splice.apply"
"(a, arguments);  return a;};if (!Array.prototype.with) Array.prototype.with = function (i, v) {  var a = this.slice(); var n = Math.trunc(i) || 0;  if (n < 0) n += a.length;  if (n < 0 || n >= a.lengt"
"h) throw new RangeError(\"index out of range\");  a[n] = v; return a;};";
}
