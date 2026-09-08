/*
 *  wt/header.c — unit metadata and parameter descriptors.
 *
 *  Not a preset instrument: this one is a synth, so A and B are the two
 *  controls you actually play with — the position in the wave bank and the
 *  filter — the way units/poly8 lays them out rather than ATK/REL.
 *
 *  SWEP is bipolar, so its center is 0. Everything else is unipolar and
 *  centers on its own minimum; `make lint` checks that.
 *
 *  Parameter order must match WtEngine::ParamId in dsp.h.
 */

#include "unit_osc.h"

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_osc,
    .api = UNIT_API_VERSION,
    .dev_id = 0x494B4F4CU,  // 'IKOL' — placeholder, see docs/workflow.md
    .unit_id = 0x00000022U,
    .version = 0x00010000U,
    .name = "WAVE",
    .num_params = 10,
    .params = {
        // min, max, center, default, type, frac bits, frac mode, reserved, name

        // A knob: where in the 32-wave bank to read
        {0, 1023, 0, 0, k_unit_param_type_none, 0, 0, 0, {"WAVE"}},

        // B knob: filter cutoff
        {0, 1023, 0, 820, k_unit_param_type_none, 0, 0, 0, {"CUTF"}},

        // EDIT menu. SWEP is the instrument: the envelope walking the pointer
        // through the bank while the note sounds.
        {-100, 100, 0, 40, k_unit_param_type_percent, 0, 0, 0, {"SWEP"}},
        {0, 2000, 0, 4, k_unit_param_type_msec, 0, 0, 0, {"ATK"}},
        {1, 4000, 1, 500, k_unit_param_type_msec, 0, 0, 0, {"DEC"}},
        {0, 100, 0, 70, k_unit_param_type_percent, 0, 0, 0, {"SUS"}},
        {1, 4000, 1, 300, k_unit_param_type_msec, 0, 0, 0, {"REL"}},
        {-100, 100, 0, 25, k_unit_param_type_percent, 0, 0, 0, {"EGFL"}},
        {0, 100, 0, 8, k_unit_param_type_cents, 0, 0, 0, {"DTUN"}},
        {1, 6, 1, 6, k_unit_param_type_none, 0, 0, 0, {"VOIC"}}},
};
