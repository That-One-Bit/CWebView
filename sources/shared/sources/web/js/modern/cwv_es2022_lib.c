/* CWebView ES2022 library prelude.
 *
 * ES2022: Array.at, String.at, Object.hasOwn (class fields/private names live in the ES2015 core transform passes).
 *
 * BEGINNER NOTE: the string below is ONE long C string that CONTAINS a
 * JavaScript prelude. The host evaluates it once when the JS context is
 * created; every `if (!...)` guard keeps us from redefining anything the
 * runtime already provides.
 */
#include "web/js/modern/cwv_es_libs.h"

const char *cwv_es2022_lib_prelude(void)
{
    return
"if (!Array.prototype.at) Array.prototype.at = function (i) {  i = Math.trunc(i) || 0;  if (i < 0) i += this.length;  return i >= 0 && i < this.length ? this[i] : undefined;};"
"if (!String.prototype.at) String.prototype.at = function (i) {  i = Math.trunc(i) || 0;  if (i < 0) i += this.length;  return i >= 0 && i < this.length ? this.charAt(i) : undefined;};"
"if (!Object.hasOwn) Object.hasOwn = function (o, k) {  return Object.prototype.hasOwnProperty.call(o, k);};";
}
