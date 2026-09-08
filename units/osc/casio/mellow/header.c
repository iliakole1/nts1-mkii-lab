/*
 *  casio/mellow/header.c — unit metadata and parameter descriptors.
 *
 *  A and B are ATK and REL, matching the other preset instruments in this
 *  repo: both add on top of the voice rather than replacing it, so zero on
 *  every control is the factory PT-20 MELLOW.
 *
 *  Parameter order must match Pt20Engine::ParamId in ../pt20.h.
 */

#include "unit_osc.h"

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_osc,
    .api = UNIT_API_VERSION,
    .dev_id = 0x494B4F4CU,  // 'IKOL' — placeholder, see docs/workflow.md
    .unit_id = 0x00000016U,
    .version = 0x00010000U,
    .name = "CASIO MELLOW",
    .num_params = 9,
    .params = {
        // min, max, center, default, type, frac bits, frac mode, reserved, name

        // A knob: attack stretch
        {0, 1023, 0, 0, k_unit_param_type_none, 0, 0, 0, {"ATK"}},

        // B knob: release stretch
        {0, 1023, 0, 0, k_unit_param_type_none, 0, 0, 0, {"REL"}},

        // EDIT menu
        {0, 1023, 0, 512, k_unit_param_type_none, 0, 0, 0, {"TONE"}},
        {0, 100, 0, 30, k_unit_param_type_percent, 0, 0, 0, {"LOFI"}},
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"VIB"}},
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"DTUN"}},
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"VSEN"}},
        {1, 6, 1, 6, k_unit_param_type_none, 0, 0, 0, {"VOIC"}},
        {-2, 2, 0, 0, k_unit_param_type_none, 0, 0, 0, {"OCT"}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}}},
};
