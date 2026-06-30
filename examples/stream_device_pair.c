/** stream_device_pair.c — v2 双 session 占位 */
#include "rkvc/rkvc.h"
#include <stdio.h>
int main(void) {
    printf("stream_device_pair: v2 LiveCapture 模板待接 V4L2\n");
    rkvc_pipeline_desc d;
    rkvc_pipeline_from_template(RKVC_TEMPLATE_LIVE_CAPTURE, &d);
    printf("default policy: %s\n", rkvc_policy_name(d.policy));
    return 0;
}
