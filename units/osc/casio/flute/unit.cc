/*
 *  casio/flute/unit.cc — logue-sdk callback surface.
 *
 *  Thin: every callback forwards into the Osc adapter. Keep DSP out of here.
 */

#include "osc.h"
#include "unit_osc.h"
#include "utils/int_math.h"

static Osc s_osc;
static int32_t s_cached[UNIT_OSC_MAX_PARAM_COUNT];
static const unit_runtime_osc_context_t *s_ctx;

#define q31_to_f32_c 4.65661287307739e-010f
#define q31_to_f32(q) ((float)(q) * q31_to_f32_c)

__unit_callback int8_t unit_init(const unit_runtime_desc_t *desc) {
  if (!desc) return k_unit_err_undef;
  if (desc->target != unit_header.target) return k_unit_err_target;
  if (!UNIT_API_IS_COMPAT(desc->api)) return k_unit_err_api_version;
  if (desc->samplerate != s_osc.getSampleRate()) return k_unit_err_samplerate;
  if (desc->input_channels != 2 || desc->output_channels != 1) return k_unit_err_geometry;

  s_ctx = static_cast<const unit_runtime_osc_context_t *>(desc->hooks.runtime_context);

  s_osc.init(nullptr);

  for (uint8_t id = 0; id < UNIT_OSC_MAX_PARAM_COUNT; ++id) {
    s_cached[id] = static_cast<int32_t>(unit_header.params[id].init);
    s_osc.setParameter(id, s_cached[id]);
  }

  return k_unit_err_none;
}

__unit_callback void unit_teardown() { s_osc.teardown(); }
__unit_callback void unit_reset() { s_osc.reset(); }
__unit_callback void unit_resume() { s_osc.resume(); }
__unit_callback void unit_suspend() { s_osc.suspend(); }

__unit_callback void unit_render(const float *in, float *out, uint32_t frames) {
  s_osc.setShapeLfo(q31_to_f32(s_ctx->shape_lfo));
  s_osc.setRuntimeNote(static_cast<uint8_t>(s_ctx->pitch >> 8));
  s_osc.process(in, out, frames);
}

__unit_callback void unit_set_param_value(uint8_t id, int32_t value) {
  value = clipminmaxi32(unit_header.params[id].min, value, unit_header.params[id].max);
  s_cached[id] = value;
  s_osc.setParameter(id, value);
}

__unit_callback int32_t unit_get_param_value(uint8_t id) { return s_cached[id]; }

__unit_callback const char *unit_get_param_str_value(uint8_t id, int32_t value) {
  value = clipminmaxi32(unit_header.params[id].min, value, unit_header.params[id].max);
  return s_osc.getParameterStrValue(id, value);
}

__unit_callback void unit_note_on(uint8_t note, uint8_t velo) { s_osc.noteOn(note, velo); }
__unit_callback void unit_note_off(uint8_t note) { s_osc.noteOff(note); }
__unit_callback void unit_all_note_off() { s_osc.allNoteOff(); }

__unit_callback void unit_set_tempo(uint32_t tempo) {
  s_osc.setTempo((tempo >> 16) + (tempo & 0xFFFF) / static_cast<float>(0x10000));
}

__unit_callback void unit_tempo_4ppqn_tick(uint32_t counter) { s_osc.tempo4ppqnTick(counter); }

__unit_callback void unit_pitch_bend(uint16_t bend) { s_osc.pitchBend(bend); }

__unit_callback void unit_channel_pressure(uint8_t press) { s_osc.channelPressure(press); }

__unit_callback void unit_aftertouch(uint8_t note, uint8_t press) {
  s_osc.aftertouch(note, press);
}
