/*
 *  bbd/header.c — unit metadata and parameter descriptors.
 *
 *  Parameter order must match BbdEngine::ParamId in dsp.h.
 */

#include "unit_delfx.h"

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_delfx,
    .api = UNIT_API_VERSION,
    .dev_id = 0x494B4F4CU,  // 'IKOL' — placeholder, see docs/workflow.md
    .unit_id = 0x00000030U,
    .version = 0x00010000U,
    .name = "BBD",
    .num_params = 9,
    .params = {
        // min, max, center, default, type, frac bits, frac mode, reserved, name

        // A knob: delay time. On this kind of delay it is also the bandwidth
        // control, because time is clock rate and clock rate is bandwidth.
        {0, 1023, 0, 360, k_unit_param_type_none, 0, 0, 0, {"TIME"}},

        // B knob: feedback. Past about 95 it runs away, gently.
        {0, 100, 0, 35, k_unit_param_type_percent, 0, 0, 0, {"FDBK"}},

        // EDIT menu
        {0, 100, 0, 35, k_unit_param_type_percent, 0, 0, 0, {"MIX"}},
        {0, 100, 0, 70, k_unit_param_type_percent, 0, 0, 0, {"TONE"}},
        {0, 100, 0, 15, k_unit_param_type_percent, 0, 0, 0, {"MOD"}},
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"AGE"}},
        {0, 5, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"SYNC"}},
        {0, 100, 0, 30, k_unit_param_type_percent, 0, 0, 0, {"SPRD"}},
        {0, 100, 0, 25, k_unit_param_type_percent, 0, 0, 0, {"RATE"}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}}},
};
