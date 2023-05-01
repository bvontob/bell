#include "userosc.h"

#define PARTIALS 11

#define RDUR(X) (1.0f / (X)), (1.0f - (X))
#define QUARTIC(X) ((X) * (X) * (X) * (X))

struct partial {
  float amp;
  float r_dur; /* reciprocal of relative duration (1.0f/dur) */
  float n_dur; /* one minus relative duration     (1.0f-dur) */
  float fr;
  float detune;
};

struct params {
  float shape;
  float decay;
  float hold;
  float comp;
  float attack;
};

/*
 * Risset's Bell Partial Definitions
 *
 * Interpretation of a well-known bell sound by Jean-Claude Risset
 * implemented according to the description by Miller Puckette, The
 * Theory and Technique of Electronic Music, Draft, Dec. 30, 2006,
 * p. 107ff
 */
static const struct partial const partials[PARTIALS] = {
  /* amp                 dur      fr   detune */
  { 1.00f / 2.67f, RDUR(1.000f), 0.56f, 0.00f },
  { 0.67f / 2.67f, RDUR(0.900f), 0.56f, 1.00f },
  { 1.00f / 2.67f, RDUR(0.650f), 0.92f, 0.00f },
  { 1.80f / 2.67f, RDUR(0.550f), 0.92f, 1.70f },
  { 2.67f / 2.67f, RDUR(0.325f), 1.19f, 0.00f },
  { 1.67f / 2.67f, RDUR(0.350f), 1.70f, 0.00f },
  { 1.46f / 2.67f, RDUR(0.250f), 2.00f, 0.00f },
  { 1.33f / 2.67f, RDUR(0.200f), 2.74f, 0.00f },
  { 1.33f / 2.67f, RDUR(0.150f), 3.00f, 0.00f },
  { 1.00f / 2.67f, RDUR(0.100f), 3.76f, 0.00f },
  { 1.33f / 2.67f, RDUR(0.075f), 4.07f, 0.00f },
};

static float ampsum;
static int attack_phase = 0;

static float vol = 0.0f;
static float phi[PARTIALS] = { 0.0f };
static float phi_s = 0.0f;

static struct params param = { 0.0f, 0.0f, 0.0f, 0.0f, 0.1f };

void OSC_INIT(uint32_t platform, uint32_t api) {
  ampsum = 0.0f;
  (void)platform; (void)api;
  for (int i = 0; i < PARTIALS; i++)
    ampsum += QUARTIC(partials[i].amp);
}

void OSC_CYCLE(const user_osc_param_t * const params,
               int32_t *yn,
               const uint32_t frames) {
  const uint8_t note = (params->pitch) >> 8;
  const uint8_t mod  = (params->pitch) & 0x00FF;
  const float   f0   = osc_notehzf(note);
  const float   f1   = osc_notehzf(note + 1);
  const float   fb   = clipmaxf(linintf(mod * k_note_mod_fscale, f0, f1),
				k_note_max_hz);
  float w_s;
  float w[PARTIALS];
  float amp[PARTIALS];

  for (int i = 0; i < PARTIALS; i++)
    w[i] = (partials[i].fr * fb + partials[i].detune) * k_samplerate_recipf;
  w_s = fb * k_samplerate_recipf;
  
  q31_t * __restrict y = (q31_t *)yn;
  const q31_t * y_e = y + frames;
  
  for (; y != y_e; ) {
    float comp = 0.0f;
    float sig  = 0.0f;

    if (attack_phase) {
      vol += fasterpowf(vol, 0.25f) * param.attack;
      if (vol > 1.0f) {
	attack_phase = 0;
	vol = 1.0f;
      }
    } else {
      if (vol <= param.hold)
	vol = param.hold;
      else
	vol -= param.decay;
    }
    
    for (int i = 0; i < PARTIALS; i++) {
      const float a = (clip0f(vol - partials[i].n_dur)
		       * partials[i].r_dur
		       * partials[i].amp);
      amp[i] = (QUARTIC(a) / ampsum);
      comp += amp[i];

      sig += amp[i] * osc_sinf(phi[i]);
      
      phi[i] += w[i];
      phi[i] -= (uint32_t)phi[i];
    }

    comp = clipminf(1.0f, (1.0f / clipminf(0.1f, comp)) * param.comp);
    sig *= comp;

    phi_s += w_s;
    phi_s -= (uint32_t)phi_s;
    sig = (1.0f - param.shape) * sig + param.shape * osc_parf(phi_s);
    
    *(y++) = f32_to_q31(sig);
  }
}

void OSC_NOTEON(const user_osc_param_t * const params) {
  (void)params;
  attack_phase = 1;
}

void OSC_NOTEOFF(const user_osc_param_t * const params) {
  (void)params;
}

void OSC_PARAM(uint16_t idx, uint16_t val) { 
  switch (idx) {

  case k_user_osc_param_id1:
    {
      const float h1 = (float)val / 100.0f;
      const float h2 = fasterpowf(h1, 0.5f);
      const float h3 = fasterpowf(h2, 0.5f) * 1.02f;
      param.hold = clip01f(h3);
    }
    break;
    
  case k_user_osc_param_id2:
    param.comp = (float)val / 100.0f;
    break;

  case k_user_osc_param_id3:
    param.attack = (1.0f - ((float)val / 100.0f)) * 0.001f;
    break;

  case k_user_osc_param_shape:
    param.shape = param_val_to_f32(val);
    break;

  case k_user_osc_param_shiftshape:
    param.decay = (1.0f - param_val_to_f32(val)) * 0.00004f;
    break;
  }
}
