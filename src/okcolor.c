/* okcolor.c - port fiel de packages/tui/src/oklab.ts (Delta) */
#include "okcolor.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

typedef double Vec3[3];
typedef const double (*Mat3)[3];

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

/* ---------------------------------------------------------------- */
/* Matrizes (valores exatos do Delta)                                    */
/* ---------------------------------------------------------------- */
static const double LINEAR_SRGB_TO_LMS[3][3] = {
    { 0.4122214694707629, 0.5363325372617349, 0.0514459932675022 },
    { 0.2119034958178251, 0.6806995506452344, 0.1073969535369405 },
    { 0.0883024591900564, 0.2817188391361215, 0.6299787016738222 },
};
static const double LMS_TO_LAB[3][3] = {
    {  0.2104542683093140,  0.7936177747023050, -0.0040720430116193 },
    {  1.9779985324311684, -2.4285922420485800,  0.4505937096174110 },
    {  0.0259040424655478,  0.7827717124575296, -0.8086757549230774 },
};
static const double LAB_TO_LMS[3][3] = {
    { 1.0,  0.3963377773761749,  0.2158037573099136 },
    { 1.0, -0.1055613458156586, -0.0638541728258133 },
    { 1.0, -0.0894841775298119, -1.2914855480194092 },
};
static const double LMS_TO_LINEAR_SRGB[3][3] = {
    { 4.0767416360759583, -3.3077115392580629,  0.2309699031821043 },
    { -1.2684379732850315, 2.6097573492876882, -0.3413193760026570 },
    { -0.0041960761386756, -0.7034186179359362, 1.7076146940746117 },
};

/* Half-plano (a,b) onde cada canal do sRGB clipa + o ajuste polinomial. */
static const double SATURATION_FIT_AB[3][2] = {
    { -1.88170310, -0.80936501 },
    {  1.81444080, -1.19445267 },
    {  0.13110758,  1.81333971 },
};
static const double SATURATION_FIT[3][5] = {
    { 1.19086277,  1.76576728,  0.59662641,  0.75515197,  0.56771245 },
    { 0.73956515, -0.45954404,  0.08285427,  0.12541073, -0.14503204 },
    { 1.35733652, -0.00915799, -1.15130210, -0.50559606,  0.00692167 },
};

#define K1 0.206
#define K2 0.03
#define K3 ((1.0 + K1) / (1.0 + K2))

static void mapply(const double m[3][3], const Vec3 v, Vec3 out)
{
    for (int i = 0; i < 3; i++)
        out[i] = m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2];
}

/* OkLCH lightness -> OkLCH (Okhsl usa a curva toe). */
static double okhsl_to_oklab_lightness(double x)
{
    return (x * x + K1 * x) / (K3 * (x + K2));
}

static double linear_to_srgb(double v)
{
    return v > 0.0031308 ? 1.055 * pow(v, 1.0 / 2.4) - 0.055 : 12.92 * v;
}

static double srgb_to_linear(double v)
{
    return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}

static void oklab_to_linear_srgb(const Vec3 lab, Vec3 out)
{
    Vec3 lms;
    mapply(LAB_TO_LMS, lab, lms);
    lms[0] = lms[0] * lms[0] * lms[0];
    lms[1] = lms[1] * lms[1] * lms[1];
    lms[2] = lms[2] * lms[2] * lms[2];
    mapply(LMS_TO_LINEAR_SRGB, lms, out);
}

static void linear_srgb_to_oklab(const Vec3 rgb, Vec3 out)
{
    Vec3 lms;
    mapply(LINEAR_SRGB_TO_LMS, rgb, lms);
    lms[0] = cbrt(lms[0]);
    lms[1] = cbrt(lms[1]);
    lms[2] = cbrt(lms[2]);
    mapply(LMS_TO_LAB, lms, out);
}

static Rgb8 linear_srgb_to_rgb8(const Vec3 linear)
{
    Rgb8 c;
    double v0 = linear[0], v1 = linear[1], v2 = linear[2];
    if (v0 > 1) v0 = 1; else if (v0 < 0) v0 = 0;
    if (v1 > 1) v1 = 1; else if (v1 < 0) v1 = 0;
    if (v2 > 1) v2 = 1; else if (v2 < 0) v2 = 0;
    c.r = (uint8_t)lround(linear_to_srgb(v0) * 255.0);
    c.g = (uint8_t)lround(linear_to_srgb(v1) * 255.0);
    c.b = (uint8_t)lround(linear_to_srgb(v2) * 255.0);
    return c;
}

/* Taxa de variacao de cada componente LMS ao longo da direcao de chroma (a,b). */
static void lms_slopes(double a, double b, Vec3 out)
{
    for (int i = 0; i < 3; i++) out[i] = LAB_TO_LMS[i][1] * a + LAB_TO_LMS[i][2] * b;
}

static double dot3(const double row[3], const Vec3 v)
{
    return row[0] * v[0] + row[1] * v[1] + row[2] * v[2];
}

static double max_saturation(double a, double b)
{
    int channel = 2;
    for (int i = 0; i < 3; i++) {
        if (i == 2) break;
        if (SATURATION_FIT_AB[i][0] * a + SATURATION_FIT_AB[i][1] * b > 1) { channel = i; break; }
    }
    const double *fit = SATURATION_FIT[channel];
    const double *weights = LMS_TO_LINEAR_SRGB[channel];
    double saturation = fit[0] + fit[1] * a + fit[2] * b + fit[3] * a * a + fit[4] * a * b;

    Vec3 slopes, base, base3, d1, d2;
    lms_slopes(a, b, slopes);
    for (int i = 0; i < 3; i++) {
        base[i] = 1.0 + saturation * slopes[i];
        base3[i] = base[i] * base[i] * base[i];
        d1[i] = 3.0 * slopes[i] * base[i] * base[i];
        d2[i] = 6.0 * slopes[i] * slopes[i] * base[i];
    }
    double f  = dot3(weights, base3);
    double f1 = dot3(weights, d1);
    double f2 = dot3(weights, d2);
    return saturation - (f * f1) / (f1 * f1 - 0.5 * f * f2);
}

static void cusp(double a, double b, double *cuspL, double *cuspC)
{
    double saturation = max_saturation(a, b);
    Vec3 lab = { 1.0, saturation * a, saturation * b };
    Vec3 lin;
    oklab_to_linear_srgb(lab, lin);
    double mx = lin[0];
    if (lin[1] > mx) mx = lin[1];
    if (lin[2] > mx) mx = lin[2];
    double lightness = cbrt(1.0 / mx);
    *cuspL = lightness;
    *cuspC = lightness * saturation;
}

static double max_chroma(double a, double b, double lightness, double cuspL, double cuspC)
{
    if (lightness <= cuspL) return (cuspC * lightness) / cuspL;

    double t = (cuspC * (lightness - 1.0)) / (cuspL - 1.0);
    Vec3 slopes, lms, cubes, first, second;
    lms_slopes(a, b, slopes);
    for (int i = 0; i < 3; i++) {
        lms[i] = lightness + t * slopes[i];
        cubes[i] = lms[i] * lms[i] * lms[i];
        first[i] = 3.0 * slopes[i] * lms[i] * lms[i];
        second[i] = 6.0 * slopes[i] * slopes[i] * lms[i];
    }
    double best = 1e300;
    for (int i = 0; i < 3; i++) {
        double f  = dot3(LMS_TO_LINEAR_SRGB[i], cubes) - 1.0;
        double f1 = dot3(LMS_TO_LINEAR_SRGB[i], first);
        double f2 = dot3(LMS_TO_LINEAR_SRGB[i], second);
        double u = f1 / (f1 * f1 - 0.5 * f * f2);
        double step = (u >= 0) ? -f * u : 1e300;
        if (step < best) best = step;
    }
    return t + best;
}

static void chroma_stops(double L, double a, double b, double *c0, double *cMid, double *cMax)
{
    double cL, cC;
    cusp(a, b, &cL, &cC);
    *cMax = max_chroma(a, b, L, cL, cC);
    double k = *cMax / fmin(L * (cC / cL), (1.0 - L) * (cC / (1.0 - cL)));

    double midS = 0.11516993 + 1.0 / (
        7.4477897 + 4.1590124 * b +
        a * (-2.19557347 + 1.75198401 * b +
            a * (-2.13704948 - 10.02301043 * b +
                a * (-4.24894561 + 5.38770819 * b + 4.69891013 * a))));

    double midT = 0.11239642 + 1.0 / (
        1.6132032 - 0.68124379 * b +
        a * (0.40370612 + 0.90148123 * b +
            a * (-0.27087943 + 0.6122399 * b +
                a * (0.00299215 - 0.45399568 * b - 0.14661872 * a))));

    double p1 = (L * midS) * (L * midS) * (L * midS) * (L * midS);
    double p2 = ((1.0 - L) * midT) * ((1.0 - L) * midT) * ((1.0 - L) * midT) * ((1.0 - L) * midT);
    *cMid = 0.9 * k * sqrt(sqrt(1.0 / (1.0 / p1 + 1.0 / p2)));
    *c0 = sqrt(1.0 / (1.0 / ((L * 0.4) * (L * 0.4)) + 1.0 / (((1.0 - L) * 0.8) * ((1.0 - L) * 0.8))));
}

Rgb8 okhsl_rgb8(double hue_deg, double saturation, double lightness)
{
    double L = okhsl_to_oklab_lightness(lightness);
    Vec3 lab = { L, 0.0, 0.0 };

    if (L > 0 && L < 1 && saturation > 0) {
        double h = fmod(hue_deg, 360.0);
        if (h < 0) h += 360.0;
        double angle = (2.0 * M_PI * h) / 360.0;
        double a = cos(angle), b = sin(angle);

        double c0, cMid, cMax;
        chroma_stops(L, a, b, &c0, &cMid, &cMax);

        double chroma;
        if (saturation < 0.8) {
            double t = 1.25 * saturation;
            double k1 = 0.8 * c0;
            chroma = (t * k1) / (1.0 - (1.0 - k1 / cMid) * t);
        } else {
            double t = 5.0 * (saturation - 0.8);
            double k1 = (0.2 * cMid * cMid * 1.25 * 1.25) / c0;
            chroma = cMid + (t * k1) / (1.0 - (1.0 - k1 / (cMax - cMid)) * t);
        }
        lab[0] = L;
        lab[1] = chroma * a;
        lab[2] = chroma * b;
    }

    Vec3 lin;
    oklab_to_linear_srgb(lab, lin);
    return linear_srgb_to_rgb8(lin);
}

/* ---------------------------------------------------------------- */
/* OkLCH com gamut mapping por bisseccao (colors.ts)                  */
/* ---------------------------------------------------------------- */
static int in_srgb_gamut(const Vec3 lin)
{
    const double eps = 1e-7;
    for (int i = 0; i < 3; i++)
        if (lin[i] < -eps || lin[i] > 1.0 + eps) return 0;
    return 1;
}

Rgb8 oklch_rgb8(double l, double c, double hue_deg)
{
    double radians = (hue_deg * M_PI) / 180.0;
    double cs = cos(radians), sn = sin(radians);

    Vec3 lin;
    {
        Vec3 lab = { l, c * cs, c * sn };
        oklab_to_linear_srgb(lab, lin);
    }
    if (in_srgb_gamut(lin)) return linear_srgb_to_rgb8(lin);

    /* reduz o chroma ate caber no gamut */
    {
        Vec3 lab0 = { l, 0.0, 0.0 };
        oklab_to_linear_srgb(lab0, lin);
    }
    double low = 0.0, high = c;
    for (int i = 0; i < 20; i++) {
        double chroma = (low + high) / 2.0;
        Vec3 lab = { l, chroma * cs, chroma * sn };
        Vec3 cand;
        oklab_to_linear_srgb(lab, cand);
        if (in_srgb_gamut(cand)) {
            low = chroma;
            lin[0] = cand[0]; lin[1] = cand[1]; lin[2] = cand[2];
        } else {
            high = chroma;
        }
    }
    return linear_srgb_to_rgb8(lin);
}

/* ---------------------------------------------------------------- */
/* Utilitarios                                                        */
/* ---------------------------------------------------------------- */
Rgb rgb_from_rgb8(Rgb8 c)
{
    Rgb r;
    r.r = c.r / 255.0; r.g = c.g / 255.0; r.b = c.b / 255.0;
    return r;
}

Rgb8 rgb_to_rgb8(Rgb c)
{
    Rgb8 o;
    double v[3] = { c.r, c.g, c.b };
    uint8_t *d[3] = { &o.r, &o.g, &o.b };
    for (int i = 0; i < 3; i++) {
        double x = v[i];
        if (x < 0) x = 0; else if (x > 1) x = 1;
        *d[i] = (uint8_t)lround(x * 255.0);
    }
    return o;
}

uint32_t rgb8_to_hex(Rgb8 c)
{
    return ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b;
}

Rgb8 hex_rgb8(const char *hex)
{
    Rgb8 c = { 0, 0, 0 };
    if (!hex) return c;
    if (*hex == '#') hex++;
    size_t n = strlen(hex);
    unsigned r = 0, g = 0, b = 0;
    if (n >= 6) {
        sscanf(hex, "%2x%2x%2x", &r, &g, &b);
    } else if (n >= 3) {
        unsigned rr = 0, gg = 0, bb = 0;
        sscanf(hex, "%1x%1x%1x", &rr, &gg, &bb);
        r = rr * 17; g = gg * 17; b = bb * 17;
    }
    c.r = (uint8_t)r; c.g = (uint8_t)g; c.b = (uint8_t)b;
    return c;
}

static void rgb_to_oklch(Rgb c, double *l, double *ch, double *h)
{
    Vec3 rgb = { srgb_to_linear(c.r), srgb_to_linear(c.g), srgb_to_linear(c.b) };
    Vec3 lab;
    linear_srgb_to_oklab(rgb, lab);
    *l = lab[0];
    *ch = sqrt(lab[1] * lab[1] + lab[2] * lab[2]);
    *h = atan2(lab[2], lab[1]);
}

static Rgb oklch_to_rgb3(double l, double ch, double h)
{
    Rgb8 c = oklch_rgb8(l, ch, h * 180.0 / M_PI);
    return rgb_from_rgb8(c);
}

Rgb mix_oklch(Rgb first, Rgb second, double amount)
{
    double l1, c1, h1, l2, c2, h2;
    rgb_to_oklch(first, &l1, &c1, &h1);
    rgb_to_oklch(second, &l2, &c2, &h2);
    double hue_delta = h2 - h1;
    while (hue_delta > M_PI) hue_delta -= 2 * M_PI;
    while (hue_delta < -M_PI) hue_delta += 2 * M_PI;
    return oklch_to_rgb3(l1 + (l2 - l1) * amount, c1 + (c2 - c1) * amount, h1 + hue_delta * amount);
}

Rgb mix_srgb(Rgb a, Rgb b, double t)
{
    Rgb o;
    o.r = a.r + (b.r - a.r) * t;
    o.g = a.g + (b.g - a.g) * t;
    o.b = a.b + (b.b - a.b) * t;
    return o;
}
