/* CWebView ES2016 library prelude.
 *
 * ES2016: Array.prototype.includes.
 *
 * BEGINNER NOTE: the string below is ONE long C string that CONTAINS a
 * JavaScript prelude. The host evaluates it once when the JS context is
 * created; every `if (!...)` guard keeps us from redefining anything the
 * runtime already provides.
 */
#include "web/js/modern/cwv_es_libs.h"

const char *cwv_es2016_lib_prelude(void)
{
    return
"if (!Array.prototype.includes) Array.prototype.includes = function (x) {  return this.indexOf(x) >= 0; };";
}
