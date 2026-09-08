/*
 *  piano/header.c — unit metadata and parameter descriptors.
 *
 *  The A and B knobs are ATK and REL, mirroring the hardware EG's own layout —
 *  which is dead while the amp EG type is `Open`, and `Open` is what a poly
 *  unit needs. Both add on top of the preset rather than replacing it.
 *
 *  PRST lives in the EDIT menu because that is where the mkII's display
 *  renders string values; on the A/B knobs it would show a bare number.
 *
 *  Parameter order must match PianoEngine::ParamId.
 */

#include "unit_osc.h"

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_osc,
    .api = UNIT_API_VERSION,
    .dev_id = 0x494B4F4CU,  // 'IKOL' — placeholder, see docs/workflow.md
    .unit_id = 0x00000007U,
    .version = 0x00030000U,
    .name = "PIANO",
    .num_params = 7,
    .params = {
        // min, max, center, default, type, frac bits, frac mode, reserved, name

        // A knob: attack stretch
        {0, 1023, 0, 0, k_unit_param_type_none, 0, 0, 0, {"ATK"}},

        // B knob: release stretch
        {0, 1023, 0, 0, k_unit_param_type_none, 0, 0, 0, {"REL"}},

        // EDIT menu — PRST first, where the display renders its name
        {0, 4, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"PRST"}},
        {0, 1023, 0, 520, k_unit_param_type_none, 0, 0, 0, {"TONE"}},
        {0, 100, 0, 25, k_unit_param_type_percent, 0, 0, 0, {"LOFI"}},
        {0, 100, 0, 60, k_unit_param_type_percent, 0, 0, 0, {"VSEN"}},
        {1, 6, 1, 6, k_unit_param_type_none, 0, 0, 0, {"VOIC"}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}}},
};
