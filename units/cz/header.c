/*
 *  cz/header.c — unit metadata and parameter descriptors.
 *
 *  A and B are ATK and REL, matching the other preset instruments here: both
 *  add on top of the patch rather than replacing it, so zero on both is
 *  exactly the preset.
 *
 *  The device name is PD8 rather than anything Casio ships: this is a
 *  phase-distortion oscillator in that tradition, not a clone of a product.
 *
 *  Parameter order must match CzEngine::ParamId in dsp.h.
 */

#include "unit_osc.h"

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_osc,
    .api = UNIT_API_VERSION,
    .dev_id = 0x494B4F4CU,  // 'IKOL' — placeholder, see docs/workflow.md
    .unit_id = 0x00000021U,
    .version = 0x00010000U,
    .name = "PD8",
    .num_params = 9,
    .params = {
        // min, max, center, default, type, frac bits, frac mode, reserved, name

        // A knob: attack stretch
        {0, 1023, 0, 0, k_unit_param_type_none, 0, 0, 0, {"ATK"}},

        // B knob: release stretch
        {0, 1023, 0, 0, k_unit_param_type_none, 0, 0, 0, {"REL"}},

        // EDIT menu — PRST first, where the display renders its name
        {0, 7, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"PRST"}},

        // Depth of the phase distortion sweep. This is the filter knob on a
        // synth with no filter: 512 is the patch as written, 0 leaves cosines.
        {0, 1023, 0, 512, k_unit_param_type_none, 0, 0, 0, {"DCW"}},

        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"DTUN"}},
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"LFO"}},
        {0, 100, 0, 60, k_unit_param_type_percent, 0, 0, 0, {"VSEN"}},
        {1, 8, 1, 8, k_unit_param_type_none, 0, 0, 0, {"VOIC"}},
        {-2, 2, 0, 0, k_unit_param_type_none, 0, 0, 0, {"OCT"}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}}},
};
