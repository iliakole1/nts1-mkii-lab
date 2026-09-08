/*
 *  shimmer/unit.cc — logue-sdk callback surface for the REVERB slot.
 */

#include "revfx.h"
#include "unit_revfx.h"
#include "utils/int_math.h"

static RevFx s_fx;
static int32_t s_cached[UNIT_REVFX_MAX_PARAM_COUNT];

__unit_callback int8_t unit_init(const unit_runtime_desc_t *desc) {
  if (!desc) return k_unit_err_undef;
  if (desc->target != unit_header.target) return k_unit_err_target;
  if (!UNIT_API_IS_COMPAT(desc->api)) return k_unit_err_api_version;
  if (desc->samplerate != s_fx.getSampleRate()) return k_unit_err_samplerate;
  if (desc->input_channels != 2 || desc->output_channels != 2) return k_unit_err_geometry;

  if (!desc->hooks.sdram_alloc) return k_unit_err_memory;

  float *buffer = reinterpret_cast<float *>(
      desc->hooks.sdram_alloc(s_fx.getBufferSize() * sizeof(float)));
  if (!buffer) return k_unit_err_memory;

  s_fx.init(buffer);

  for (uint8_t id = 0; id < UNIT_REVFX_MAX_PARAM_COUNT; ++id) {
    s_cached[id] = static_cast<int32_t>(unit_header.params[id].init);
    s_fx.setParameter(id, s_cached[id]);
  }

  return k_unit_err_none;
}

__unit_callback void unit_teardown() { s_fx.teardown(); }
__unit_callback void unit_reset() { s_fx.reset(); }
__unit_callback void unit_resume() { s_fx.resume(); }
__unit_callback void unit_suspend() { s_fx.suspend(); }

__unit_callback void unit_render(const float *in, float *out, uint32_t frames) {
  s_fx.process(in, out, frames);
}

__unit_callback void unit_set_param_value(uint8_t id, int32_t value) {
  value = clipminmaxi32(unit_header.params[id].min, value, unit_header.params[id].max);
  s_cached[id] = value;
  s_fx.setParameter(id, value);
}

__unit_callback int32_t unit_get_param_value(uint8_t id) { return s_cached[id]; }

__unit_callback const char *unit_get_param_str_value(uint8_t id, int32_t value) {
  value = clipminmaxi32(unit_header.params[id].min, value, unit_header.params[id].max);
  return s_fx.getParameterStrValue(id, value);
}

__unit_callback void unit_set_tempo(uint32_t tempo) {
  s_fx.setTempo((tempo >> 16) + (tempo & 0xFFFF) / static_cast<float>(0x10000));
}

__unit_callback void unit_tempo_4ppqn_tick(uint32_t counter) { s_fx.tempo4ppqnTick(counter); }
