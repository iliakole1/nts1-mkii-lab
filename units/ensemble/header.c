/*
 *  ensemble/header.c — unit metadata and parameter descriptors.
 *
 *  Parameter order must match EnsembleEngine::ParamId in dsp.h.
 *  On the hardware the A knob is labelled TIME and the B knob DEPTH for mod fx.
 */

#include "unit_modfx.h"

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_modfx,
    .api = UNIT_API_VERSION,
    .dev_id = 0x494B4F4CU,  // 'IKOL' — placeholder, see docs/workflow.md
    .unit_id = 0x00000002U,
    .version = 0x00010000U,
    .name = "ENSEMBLE",
    .num_params = 6,
    .params = {
        // min, max, center, default, type, frac bits, frac mode, reserved, name

        // A knob: LFO rate
        {0, 1023, 0, 400, k_unit_param_type_none, 0, 0, 0, {"RATE"}},

        // B knob: sweep depth
        {0, 1023, 0, 512, k_unit_param_type_none, 0, 0, 0, {"DPTH"}},

        // EDIT menu
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"MIX"}},
        {0, 2, 0, 1, k_unit_param_type_strings, 0, 0, 0, {"MODE"}},
        {0, 100, 0, 70, k_unit_param_type_percent, 0, 0, 0, {"SPRD"}},
        {0, 100, 0, 70, k_unit_param_type_percent, 0, 0, 0, {"TONE"}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}}},
};
