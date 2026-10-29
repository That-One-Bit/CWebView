/* CWebView ES2021 library prelude.
 *
 * ES2021: Promise.any and String.replaceAll (the &&= ||= ??= syntax lives in the ES2015 core transform passes).
 *
 * BEGINNER NOTE: the string below is ONE long C string that CONTAINS a
 * JavaScript prelude. The host evaluates it once when the JS context is
 * created; every `if (!...)` guard keeps us from redefining anything the
 * runtime already provides.
 */
#include "web/js/modern/cwv_es_libs.h"

const char *cwv_es2021_lib_prelude(void)
{
    return
"if (!Promise.any) {  Promise.any = function (list) {    var errs = [], n = 0;    return new Promise(function (res, rej) {      for (var i = 0; i < list.length; i++) {        Promise.resolve(list[i]).t"
"hen(function (v) { res(v); },          function (e) {            errs.push(e); n++;            if (n === list.length) rej(errs[0]);          });      }    });  };}"
"if (!String.prototype.replaceAll) String.prototype.replaceAll = function (a, b) {  if (a instanceof RegExp) {    var flags = a.flags || \"\";    if (flags.indexOf(\"g\") < 0) flags += \"g\";    return"
" this.replace(new RegExp(a.source, flags), b);  }  return this.split(a).join(b);};";
}
