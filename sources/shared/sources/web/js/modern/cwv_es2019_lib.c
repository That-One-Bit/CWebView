/* CWebView ES2019 library prelude.
 *
 * ES2019: Array flat/flatMap, Object.fromEntries, String trimStart/trimEnd (+trimLeft/Right aliases).
 *
 * BEGINNER NOTE: the string below is ONE long C string that CONTAINS a
 * JavaScript prelude. The host evaluates it once when the JS context is
 * created; every `if (!...)` guard keeps us from redefining anything the
 * runtime already provides.
 */
#include "web/js/modern/cwv_es_libs.h"

const char *cwv_es2019_lib_prelude(void)
{
    return
"if (!Array.prototype.flat) Array.prototype.flat = function (d) {  d = d === undefined ? 1 : d;  var out = [];  function walk(a, k) {    for (var i = 0; i < a.length; i++) {      if (k > 0 && Array.isA"
"rray(a[i])) walk(a[i], k - 1);      else out.push(a[i]);    }  }  walk(this, d);  return out;};"
"if (!Array.prototype.flatMap) Array.prototype.flatMap = function (f, t) {  return this.map(f, t).flat(1);};"
"if (!Object.fromEntries) Object.fromEntries = function (list) {  var o = {};  for (var i = 0; i < list.length; i++) o[list[i][0]] = list[i][1];  return o;};"
"if (!String.prototype.trimStart) String.prototype.trimStart = function () {  return this.replace(/^\\s+/, \"\");};"
"if (!String.prototype.trimEnd) String.prototype.trimEnd = function () {  return this.replace(/\\s+$/, \"\");};"
"if (!String.prototype.trimLeft) String.prototype.trimLeft = String.prototype.trimStart;"
"if (!String.prototype.trimRight) String.prototype.trimRight = String.prototype.trimEnd;";
}
