/*
 *  poly8/header.c — unit metadata and parameter descriptors.
 *
 *  Parameter order must match PolyEngine::ParamId in dsp.h.
 *  Slots 0 and 1 are the A and B knobs; 2..9 appear in the EDIT menu.
 */

#include "unit_osc.h"

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_osc,
    .api = UNIT_API_VERSION,
    .dev_id = 0x494B4F4CU,  // 'IKOL' — placeholder, see docs/workflow.md
    .unit_id = 0x00000001U,
    .version = 0x00010002U,
    .name = "POLY8",
    .num_params = 10,
    .params = {
        // min, max, center, default, type, frac bits, frac mode, reserved, name
        //
        // center must satisfy min <= center <= max: set it to min for a
        // unipolar parameter, 0 for a bipolar one. The device refuses to load
        // a unit with an out-of-range center. `make lint` checks this.

        // A knob: waveshape morph, saw -> square -> narrow pulse
        {0, 1023, 0, 0, k_unit_param_type_none, 0, 0, 0, {"SHPE"}},

        // B knob: filter cutoff
        {0, 1023, 0, 768, k_unit_param_type_none, 0, 0, 0, {"CUTF"}},

        // EDIT menu
        {0, 100, 0, 8, k_unit_param_type_cents, 0, 0, 0, {"DTUN"}},
        {1, 8, 1, 8, k_unit_param_type_none, 0, 0, 0, {"VOIC"}},
        {0, 2000, 0, 4, k_unit_param_type_msec, 0, 0, 0, {"ATK"}},
        {1, 4000, 1, 400, k_unit_param_type_msec, 0, 0, 0, {"DEC"}},
        {0, 100, 0, 75, k_unit_param_type_percent, 0, 0, 0, {"SUS"}},
        {1, 4000, 1, 250, k_unit_param_type_msec, 0, 0, 0, {"REL"}},
        {0, 100, 0, 25, k_unit_param_type_percent, 0, 0, 0, {"RESO"}},
        {-100, 100, 0, 35, k_unit_param_type_percent, 0, 0, 0, {"EGFL"}}},
};
