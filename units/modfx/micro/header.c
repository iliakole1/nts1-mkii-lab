/*
 *  micro/header.c — unit metadata and parameter descriptors.
 *
 *  Parameter order must match MicroEngine::ParamId in dsp.h.
 */

#include "unit_modfx.h"

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_modfx,
    .api = UNIT_API_VERSION,
    .dev_id = 0x494B4F4CU,  // 'IKOL' — placeholder, see docs/workflow.md
    .unit_id = 0x00000040U,
    .version = 0x00010000U,
    .name = "MICRO",
    .num_params = 7,
    .params = {
        // min, max, center, default, type, frac bits, frac mode, reserved, name

        // A knob: how far apart the two sides are pitched, up to 25 cents
        // before the mode scales it. Five to fifteen is the useful range.
        {0, 1023, 0, 140, k_unit_param_type_none, 0, 0, 0, {"DTUN"}},

        // B knob
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"MIX"}},

        // EDIT menu
        {0, 1023, 0, 300, k_unit_param_type_none, 0, 0, 0, {"DLAY"}},
        {0, 2, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"MODE"}},
        {0, 100, 0, 85, k_unit_param_type_percent, 0, 0, 0, {"SPRD"}},
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"FDBK"}},
        {0, 100, 0, 80, k_unit_param_type_percent, 0, 0, 0, {"TONE"}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}}},
};
