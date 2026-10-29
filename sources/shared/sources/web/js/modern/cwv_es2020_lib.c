/* CWebView ES2020 library prelude.
 *
 * ES2020: globalThis and Promise.allSettled (the ?? / ?. syntax lives in the ES2015 core transform passes).
 *
 * BEGINNER NOTE: the string below is ONE long C string that CONTAINS a
 * JavaScript prelude. The host evaluates it once when the JS context is
 * created; every `if (!...)` guard keeps us from redefining anything the
 * runtime already provides.
 */
#include "web/js/modern/cwv_es_libs.h"

const char *cwv_es2020_lib_prelude(void)
{
    return
"if (typeof globalThis === \"undefined\") globalThis = this;"
"if (!Promise.allSettled) {  Promise.allSettled = function (list) {    return Promise.all(list.map(function (x) {      return Promise.resolve(x).then(        function (v) { return { status: \"fulfilled"
"\", value: v }; },        function (e) { return { status: \"rejected\", reason: e }; });    }));  };}";
}
