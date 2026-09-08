/*
 *  drive/header.c — unit metadata and parameter descriptors.
 *
 *  Parameter order must match DriveEngine::ParamId in dsp.h.
 */

#include "unit_modfx.h"

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_modfx,
    .api = UNIT_API_VERSION,
    .dev_id = 0x494B4F4CU,  // 'IKOL' — placeholder, see docs/workflow.md
    .unit_id = 0x00000006U,
    .version = 0x00010000U,
    .name = "DRIVE",
    .num_params = 6,
    .params = {
        // min, max, center, default, type, frac bits, frac mode, reserved, name

        // A knob: drive
        {0, 1023, 0, 400, k_unit_param_type_none, 0, 0, 0, {"DRIV"}},

        // B knob: post tone
        {0, 1023, 0, 620, k_unit_param_type_none, 0, 0, 0, {"TONE"}},

        // EDIT menu
        {0, 3, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"MODE"}},
        {0, 100, 0, 100, k_unit_param_type_percent, 0, 0, 0, {"MIX"}},
        {0, 100, 0, 70, k_unit_param_type_percent, 0, 0, 0, {"LVL"}},
        {0, 100, 0, 20, k_unit_param_type_percent, 0, 0, 0, {"BIAS"}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}}},
};
