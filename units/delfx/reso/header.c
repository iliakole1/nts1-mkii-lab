/*
 *  reso/header.c — unit metadata and parameter descriptors.
 *
 *  FINE is bipolar, so its center is 0. Everything else is unipolar and
 *  centers on its own minimum; `make lint` checks that.
 *
 *  Parameter order must match ResoEngine::ParamId in dsp.h.
 */

#include "unit_delfx.h"

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_delfx,
    .api = UNIT_API_VERSION,
    .dev_id = 0x494B4F4CU,  // 'IKOL' — placeholder, see docs/workflow.md
    .unit_id = 0x00000031U,
    .version = 0x00010000U,
    .name = "RESONATOR",
    .num_params = 9,
    .params = {
        // min, max, center, default, type, frac bits, frac mode, reserved, name

        // A knob: root pitch of the string set, two octaves below middle C up
        {0, 1023, 0, 500, k_unit_param_type_none, 0, 0, 0, {"NOTE"}},

        // B knob: how long the strings ring
        {0, 100, 0, 70, k_unit_param_type_percent, 0, 0, 0, {"DECY"}},

        // EDIT menu
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"MIX"}},
        {0, 7, 0, 6, k_unit_param_type_strings, 0, 0, 0, {"CHRD"}},
        {0, 100, 0, 60, k_unit_param_type_percent, 0, 0, 0, {"DAMP"}},
        {1, 4, 1, 4, k_unit_param_type_none, 0, 0, 0, {"STRG"}},
        {0, 100, 0, 70, k_unit_param_type_percent, 0, 0, 0, {"SPRD"}},
        {0, 100, 0, 60, k_unit_param_type_percent, 0, 0, 0, {"TONE"}},
        {-50, 50, 0, 0, k_unit_param_type_cents, 0, 0, 0, {"FINE"}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}}},
};
