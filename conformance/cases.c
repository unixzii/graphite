#include "internal.h"
#define GR_CONF_CASE(tier, name) void name(ctx *c);
#include "cases.def"
#undef GR_CONF_CASE

const conf_case gr_conf_cases[] = {
#define GR_CONF_CASE(tier, name) {#name, #tier, name},
#include "cases.def"
#undef GR_CONF_CASE
};
const size_t gr_conf_case_count = sizeof(gr_conf_cases) / sizeof(gr_conf_cases[0]);
