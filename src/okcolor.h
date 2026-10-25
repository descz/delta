/* okcolor.h - conversao de cores Okhsl/OkLCH -> sRGB.
 * Port fiel de packages/tui/src/oklab.ts e colors.ts do Delta
 * (Okhsl e o espaco de cor de Bjorn Ottosson, licenca MIT). */
#ifndef DELTA_OKCOLOR_H
#define DELTA_OKCOLOR_H

#include <stdint.h>

typedef struct { double r, g, b; } Rgb;   /* 0..1, quantizado como o Delta faz */
typedef struct { uint8_t r, g, b; } Rgb8; /* 0..255 */

/* Okhsl: hue em graus, sat/light 0..1. */
Rgb8 okhsl_rgb8(double hue_deg, double saturation, double lightness);
/* OkLCH: light 0..1, chroma, hue em graus. Aplica gamut mapping por bisseccao. */
Rgb8 oklch_rgb8(double l, double c, double hue_deg);

Rgb   rgb_from_rgb8(Rgb8 c);
Rgb8  rgb_to_rgb8(Rgb c);
Rgb8  hex_rgb8(const char *hex);
uint32_t rgb8_to_hex(Rgb8 c);

/* Mistura em espaco OkLCH (padrao do Delta) ou sRGB. */
Rgb   mix_oklch(Rgb a, Rgb b, double t);
Rgb   mix_srgb(Rgb a, Rgb b, double t);

#endif /* DELTA_OKCOLOR_H */
