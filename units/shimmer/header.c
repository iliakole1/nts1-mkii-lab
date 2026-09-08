/*
 *  shimmer/header.c — unit metadata and parameter descriptors.
 *
 *  Parameter order must match ShimmerEngine::ParamId in dsp.h. On the hardware
 *  the reverb slot's first three parameters are the fixed TIME, DEPTH and MIX
 *  controls; the rest live in the EDIT menu.
 */

#include "unit_revfx.h"

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_revfx,
    .api = UNIT_API_VERSION,
    .dev_id = 0x494B4F4CU,  // 'IKOL' — placeholder, see docs/workflow.md
    .unit_id = 0x00000005U,
    .version = 0x00010000U,
    .name = "SHIMMER",
    .num_params = 7,
    .params = {
        // min, max, center, default, type, frac bits, frac mode, reserved, name

        // Fixed hardware controls
        {0, 1023, 0, 700, k_unit_param_type_none, 0, 0, 0, {"TIME"}},
        {0, 1023, 0, 460, k_unit_param_type_none, 0, 0, 0, {"SHMR"}},
        {0, 1023, 0, 410, k_unit_param_type_none, 0, 0, 0, {"MIX"}},

        // EDIT menu
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"DAMP"}},
        {0, 200, 0, 20, k_unit_param_type_msec, 0, 0, 0, {"PDLY"}},
        {0, 3, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"PTCH"}},
        {0, 100, 0, 20, k_unit_param_type_percent, 0, 0, 0, {"LOCT"}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}}},
};
