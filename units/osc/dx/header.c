/*
 *  dx/header.c — unit metadata and parameter descriptors.
 *
 *  The A and B knobs are ATK and REL, matching the other preset instruments
 *  here: both add on top of the patch rather than replacing it, so zero on
 *  both is exactly the preset.
 *
 *  PRST lives in the EDIT menu because that is where the mkII's display
 *  renders string values; on the A/B knobs it would show a bare number.
 *
 *  The device name is FM6 rather than anything Yamaha ships: this is a
 *  six-operator FM oscillator in that tradition, not a clone of a product,
 *  and it carries no patch data from one.
 *
 *  Parameter order must match DxEngine::ParamId in dsp.h.
 */

#include "unit_osc.h"

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_osc,
    .api = UNIT_API_VERSION,
    .dev_id = 0x494B4F4CU,  // 'IKOL' — placeholder, see docs/workflow.md
    .unit_id = 0x00000020U,
    .version = 0x00010000U,
    .name = "FM6",
    .num_params = 9,
    .params = {
        // min, max, center, default, type, frac bits, frac mode, reserved, name

        // A knob: attack stretch
        {0, 1023, 0, 0, k_unit_param_type_none, 0, 0, 0, {"ATK"}},

        // B knob: release stretch
        {0, 1023, 0, 0, k_unit_param_type_none, 0, 0, 0, {"REL"}},

        // EDIT menu — PRST first, where the display renders its name
        {0, 7, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"PRST"}},

        // Modulator depth. The single most useful control on an FM voice:
        // 512 is the patch as written, 0 collapses every operator to a sine.
        {0, 1023, 0, 512, k_unit_param_type_none, 0, 0, 0, {"TONE"}},

        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"FDBK"}},
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"LFO"}},
        {0, 100, 0, 60, k_unit_param_type_percent, 0, 0, 0, {"VSEN"}},
        {1, 6, 1, 6, k_unit_param_type_none, 0, 0, 0, {"VOIC"}},
        {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"DTUN"}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}}},
};
