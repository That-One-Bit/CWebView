#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "core/cwv_args.h"

int main(void)
{
    char value[128];
    assert(cwv_args_parse(NULL) == 0);
    assert(cwv_args_parse("") == 0);
    assert(cwv_args_parse("beta-enable-canvas") == CWV_ARG_CANVAS);
    assert(cwv_args_parse(" beta-enable-canvas  beta-enable-webgl \t") ==
           (CWV_ARG_CANVAS | CWV_ARG_WEBGL));
    assert(cwv_args_parse("beta-enable-canvas beta-enable-canvas") == CWV_ARG_CANVAS);
    assert(cwv_args_parse("beta-enable-canvasx beta-enable-webglx") == 0);
    assert(cwv_args_parse("--enable-canvas") == 0);

    assert(cwv_args_manifest_value("{\"name\":\"demo\",\"cwv-args\":\"beta-enable-canvas beta-enable-webgl\"}", value, sizeof(value)) == 1);
    assert(strcmp(value, "beta-enable-canvas beta-enable-webgl") == 0);
    assert(cwv_args_manifest_value("{\"name\":\"demo\"}", value, sizeof(value)) == 0);
    assert(cwv_args_manifest_value("{\"cwv-args\":12}", value, sizeof(value)) == 0);
    assert(cwv_args_manifest_value("{\"cwv-args\":\"bad\\\"value\"}", value, sizeof(value)) == 0);
    assert(cwv_args_manifest_value("{\"cwv-args\":\"long\"}", value, 3) == 0);

    puts("cwv-args parser: PASS");
    return 0;
}
