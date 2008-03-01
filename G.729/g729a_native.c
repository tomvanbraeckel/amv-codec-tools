/*
 * G.729 Annex A decoder
 * Copyright (c) 2007 Vladimir Voroshilov
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include <stdlib.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>

/*

Validation results on ITU test vectors for Fixed-point G729.A:

   All vectors are not bit-exactly equal (but reference C code for
   floating point implementation fails to provide bit-exact equal files
   while fixed-point code passes all tests). Thus test was based
   on presence of hearable artefacts/differences

Per-vector results (PASS means no hearable differences, FAILED means hearable artefacts):
algthm  : PASS
erasure : PASS
fixed   : PASS
lsp     : PASS
overflow: PASS
parity  : PASS
pitch   : PASS
speech  : PASS
tame    : PASS
test    : PASS

*/

//Defined in Makefile while building test executable
#ifdef G729A_NATIVE
//stubs for porting to FFmpeg
#include "g729a_native.h"
#else
#include "avcodec.h"
#include "avutil.h"
#include "bitstream.h"
#endif


/*
-------------------------------------------------------------------------------
    Formats description
-------------------------------------------------------------------------------
*/

/**
 * Maximum size of one subframe over supported formats
 */
#define MAX_SUBFRAME_SIZE 44

#define PITCH_MIN 20
#define PITCH_MAX 143
#define INTERPOL_LEN 11




///Switched MA predictor of LSP quantizer (size in bits)
#define L0_BITS 1
///First stage vector of quantizer (size in bits)
#define L1_BITS 7
///First stage lowervector of quantizer (size in bits)
#define L2_BITS 5
///First stage hihjer vector of quantizer (size in bits)
#define L3_BITS 5
///Adaptive codebook index for first subframe (size in bits)
#define P1_BITS 8
///Adaptive codebook index for second subframe (size in bits)
#define P2_BITS 5
///Parity bit for pitch delay (size in bits)
#define P0_BITS 1
/// GA codebook index (size in bits)
#define GA_BITS 3
/// GB codebook index (size in bits)
#define GB_BITS 4
/// Number of pulses in fixed-codebook vector
#define FC_PULSE_COUNT 4

typedef struct
{
    uint8_t ma_predictor;     ///< Switched MA predictor of LSP quantizer
    uint8_t quantizer_1st;    ///< First stage vector of quantizer
    uint8_t quantizer_2nd_lo; ///< First stage lowervector of quantizer (size in bits)
    uint8_t quantizer_2nd_hi; ///< First stage hihjer vector of quantizer (size in bits)
    uint8_t parity;           ///< Parity bit for pitch delay (size in bits)
    uint8_t ac_index[2];      ///< Adaptive codebook index
    uint8_t pulses_signs[2];  ///< Fixed-codebook vectors pulses' signs
    int fc_indexes[2];        ///< Fixed-codebook indexes
    uint8_t ga_cb_index[2];   ///< GA codebook index
    uint8_t gb_cb_index[2];   ///< GB codebook index
} G729_parameters;

///Size of parameters vector
#define VECTOR_SIZE 15

static const struct
{
    int sample_rate;
    ///Size (in bytes) of input frame
    uint8_t input_frame_size;
    ///Size (in bytes) of output frame
    uint8_t output_frame_size;
    ///Size (in bits) of fixed codebook index
    uint8_t fc_index_bits;
} formats[] =
{
  {8000, 10, 160, 3},
#ifdef G729_SUPPORT_4400
// Note: may not work
  {4400, 11, 176, 4},
#endif //G729_SUPPORT_4400
};

/// MA prediction order
#define MA_NP 4

typedef struct
{
    int format;             ///< format index from formats array
    int subframe_size;      ///< number of samples produced from one subframe
    int data_error;         ///< data error detected during decoding
    int bad_pitch;          ///< parity check failed
    /// past excitation signal buffer
    float exc_base[2*MAX_SUBFRAME_SIZE+PITCH_MAX+INTERPOL_LEN];
    float* exc;             ///< start of past excitation data in buffer
    int intT2_prev;         ///< int(T2) value of previous frame (4.1.3)
    int16_t lq_prev[MA_NP][10]; ///< (Q13) LSP quantizer output (3.2.4)
    int16_t lsp_prev[10];   ///< (Q15) LSP coefficients from previous frame (3.2.5)
    int16_t lsf_prev[10];   ///< (Q13) LSF coefficients from previous frame
    float pred_energ_q[4];  ///< (Q13) past quantized energies
    float gain_pitch;       ///< Pitch gain of previous subframe (3.8) [GAIN_PITCH_MIN ... GAIN_PITCH_MAX]
    float gain_code;        ///< Gain code of previous subframe
    /// Residual signal buffer (used in long-term postfilter)
    float residual[MAX_SUBFRAME_SIZE+PITCH_MAX];
    float syn_filter_data[10];
    float res_filter_data[10];
    float ht_prev_data;     ///< previous data for 4.2.3, equation 86
    float g;                ///< gain coefficient (4.2.4)
    uint16_t rand_value;    ///< random number generator value (4.4.4)
    int prev_mode;          ///< L0 from previous frame
    //High-pass filter data
    float hpf_f1;
    float hpf_f2;
    float hpf_z0;
    float hpf_z1;
    int subframe_idx;      ///< subframe index (for debugging)
}  G729A_Context;

//Stability constants (3.2.4)
#define LSFQ_MIN    40 //0.005 in Q13
#define LSFQ_MAX 25681 //3.135 in Q13

#define LSFQ_DIFF_MIN 321 //0.0391 in Q13

/* Gain pitch maximum and minimum (3.8) */
#define GAIN_PITCH_MIN 0.2
#define GAIN_PITCH_MAX 0.8

/* 4.2.2 */
#define GAMMA_N 0.55
#define GAMMA_D 0.70
#define GAMMA_T 0.80

/* 4.2.1 */
#define GAMMA_P 0.50

#define Q13_BASE 8192.0
#define Q15_BASE 32768.0
/**
 * L1 codebook (10-dimensional, with 128 entries (3.24)
 */
static const int16_t cb_L1[1<<L1_BITS][10] =
{        /* Q13 */
  { 1486,  2168,  3751,  9074, 12134, 13944, 17983, 19173, 21190, 21820},
  { 1730,  2640,  3450,  4870,  6126,  7876, 15644, 17817, 20294, 21902},
  { 1568,  2256,  3088,  4874, 11063, 13393, 18307, 19293, 21109, 21741},
  { 1733,  2512,  3357,  4708,  6977, 10296, 17024, 17956, 19145, 20350},
  { 1744,  2436,  3308,  8731, 10432, 12007, 15614, 16639, 21359, 21913},
  { 1786,  2369,  3372,  4521,  6795, 12963, 17674, 18988, 20855, 21640},
  { 1631,  2433,  3361,  6328, 10709, 12013, 13277, 13904, 19441, 21088},
  { 1489,  2364,  3291,  6250,  9227, 10403, 13843, 15278, 17721, 21451},
  { 1869,  2533,  3475,  4365,  9152, 14513, 15908, 17022, 20611, 21411},
  { 2070,  3025,  4333,  5854,  7805,  9231, 10597, 16047, 20109, 21834},
  { 1910,  2673,  3419,  4261, 11168, 15111, 16577, 17591, 19310, 20265},
  { 1141,  1815,  2624,  4623,  6495,  9588, 13968, 16428, 19351, 21286},
  { 2192,  3171,  4707,  5808, 10904, 12500, 14162, 15664, 21124, 21789},
  { 1286,  1907,  2548,  3453,  9574, 11964, 15978, 17344, 19691, 22495},
  { 1921,  2720,  4604,  6684, 11503, 12992, 14350, 15262, 16997, 20791},
  { 2052,  2759,  3897,  5246,  6638, 10267, 15834, 16814, 18149, 21675},
  { 1798,  2497,  5617, 11449, 13189, 14711, 17050, 18195, 20307, 21182},
  { 1009,  1647,  2889,  5709,  9541, 12354, 15231, 18494, 20966, 22033},
  { 3016,  3794,  5406,  7469, 12488, 13984, 15328, 16334, 19952, 20791},
  { 2203,  3040,  3796,  5442, 11987, 13512, 14931, 16370, 17856, 18803},
  { 2912,  4292,  7988,  9572, 11562, 13244, 14556, 16529, 20004, 21073},
  { 2861,  3607,  5923,  7034,  9234, 12054, 13729, 18056, 20262, 20974},
  { 3069,  4311,  5967,  7367, 11482, 12699, 14309, 16233, 18333, 19172},
  { 2434,  3661,  4866,  5798, 10383, 11722, 13049, 15668, 18862, 19831},
  { 2020,  2605,  3860,  9241, 13275, 14644, 16010, 17099, 19268, 20251},
  { 1877,  2809,  3590,  4707, 11056, 12441, 15622, 17168, 18761, 19907},
  { 2107,  2873,  3673,  5799, 13579, 14687, 15938, 17077, 18890, 19831},
  { 1612,  2284,  2944,  3572,  8219, 13959, 15924, 17239, 18592, 20117},
  { 2420,  3156,  6542, 10215, 12061, 13534, 15305, 16452, 18717, 19880},
  { 1667,  2612,  3534,  5237, 10513, 11696, 12940, 16798, 18058, 19378},
  { 2388,  3017,  4839,  9333, 11413, 12730, 15024, 16248, 17449, 18677},
  { 1875,  2786,  4231,  6320,  8694, 10149, 11785, 17013, 18608, 19960},
  {  679,  1411,  4654,  8006, 11446, 13249, 15763, 18127, 20361, 21567},
  { 1838,  2596,  3578,  4608,  5650, 11274, 14355, 15886, 20579, 21754},
  { 1303,  1955,  2395,  3322, 12023, 13764, 15883, 18077, 20180, 21232},
  { 1438,  2102,  2663,  3462,  8328, 10362, 13763, 17248, 19732, 22344},
  {  860,  1904,  6098,  7775,  9815, 12007, 14821, 16709, 19787, 21132},
  { 1673,  2723,  3704,  6125,  7668,  9447, 13683, 14443, 20538, 21731},
  { 1246,  1849,  2902,  4508,  7221, 12710, 14835, 16314, 19335, 22720},
  { 1525,  2260,  3862,  5659,  7342, 11748, 13370, 14442, 18044, 21334},
  { 1196,  1846,  3104,  7063, 10972, 12905, 14814, 17037, 19922, 22636},
  { 2147,  3106,  4475,  6511,  8227,  9765, 10984, 12161, 18971, 21300},
  { 1585,  2405,  2994,  4036, 11481, 13177, 14519, 15431, 19967, 21275},
  { 1778,  2688,  3614,  4680,  9465, 11064, 12473, 16320, 19742, 20800},
  { 1862,  2586,  3492,  6719, 11708, 13012, 14364, 16128, 19610, 20425},
  { 1395,  2156,  2669,  3386, 10607, 12125, 13614, 16705, 18976, 21367},
  { 1444,  2117,  3286,  6233,  9423, 12981, 14998, 15853, 17188, 21857},
  { 2004,  2895,  3783,  4897,  6168,  7297, 12609, 16445, 19297, 21465},
  { 1495,  2863,  6360,  8100, 11399, 14271, 15902, 17711, 20479, 22061},
  { 2484,  3114,  5718,  7097,  8400, 12616, 14073, 14847, 20535, 21396},
  { 2424,  3277,  5296,  6284, 11290, 12903, 16022, 17508, 19333, 20283},
  { 2565,  3778,  5360,  6989,  8782, 10428, 14390, 15742, 17770, 21734},
  { 2727,  3384,  6613,  9254, 10542, 12236, 14651, 15687, 20074, 21102},
  { 1916,  2953,  6274,  8088,  9710, 10925, 12392, 16434, 20010, 21183},
  { 3384,  4366,  5349,  7667, 11180, 12605, 13921, 15324, 19901, 20754},
  { 3075,  4283,  5951,  7619,  9604, 11010, 12384, 14006, 20658, 21497},
  { 1751,  2455,  5147,  9966, 11621, 13176, 14739, 16470, 20788, 21756},
  { 1442,  2188,  3330,  6813,  8929, 12135, 14476, 15306, 19635, 20544},
  { 2294,  2895,  4070,  8035, 12233, 13416, 14762, 17367, 18952, 19688},
  { 1937,  2659,  4602,  6697,  9071, 12863, 14197, 15230, 16047, 18877},
  { 2071,  2663,  4216,  9445, 10887, 12292, 13949, 14909, 19236, 20341},
  { 1740,  2491,  3488,  8138,  9656, 11153, 13206, 14688, 20896, 21907},
  { 2199,  2881,  4675,  8527, 10051, 11408, 14435, 15463, 17190, 20597},
  { 1943,  2988,  4177,  6039,  7478,  8536, 14181, 15551, 17622, 21579},
  { 1825,  3175,  7062,  9818, 12824, 15450, 18330, 19856, 21830, 22412},
  { 2464,  3046,  4822,  5977,  7696, 15398, 16730, 17646, 20588, 21320},
  { 2550,  3393,  5305,  6920, 10235, 14083, 18143, 19195, 20681, 21336},
  { 3003,  3799,  5321,  6437,  7919, 11643, 15810, 16846, 18119, 18980},
  { 3455,  4157,  6838,  8199,  9877, 12314, 15905, 16826, 19949, 20892},
  { 3052,  3769,  4891,  5810,  6977, 10126, 14788, 15990, 19773, 20904},
  { 3671,  4356,  5827,  6997,  8460, 12084, 14154, 14939, 19247, 20423},
  { 2716,  3684,  5246,  6686,  8463, 10001, 12394, 14131, 16150, 19776},
  { 1945,  2638,  4130,  7995, 14338, 15576, 17057, 18206, 20225, 20997},
  { 2304,  2928,  4122,  4824,  5640, 13139, 15825, 16938, 20108, 21054},
  { 1800,  2516,  3350,  5219, 13406, 15948, 17618, 18540, 20531, 21252},
  { 1436,  2224,  2753,  4546,  9657, 11245, 15177, 16317, 17489, 19135},
  { 2319,  2899,  4980,  6936,  8404, 13489, 15554, 16281, 20270, 20911},
  { 2187,  2919,  4610,  5875,  7390, 12556, 14033, 16794, 20998, 21769},
  { 2235,  2923,  5121,  6259,  8099, 13589, 15340, 16340, 17927, 20159},
  { 1765,  2638,  3751,  5730,  7883, 10108, 13633, 15419, 16808, 18574},
  { 3460,  5741,  9596, 11742, 14413, 16080, 18173, 19090, 20845, 21601},
  { 3735,  4426,  6199,  7363,  9250, 14489, 16035, 17026, 19873, 20876},
  { 3521,  4778,  6887,  8680, 12717, 14322, 15950, 18050, 20166, 21145},
  { 2141,  2968,  6865,  8051, 10010, 13159, 14813, 15861, 17528, 18655},
  { 4148,  6128,  9028, 10871, 12686, 14005, 15976, 17208, 19587, 20595},
  { 4403,  5367,  6634,  8371, 10163, 11599, 14963, 16331, 17982, 18768},
  { 4091,  5386,  6852,  8770, 11563, 13290, 15728, 16930, 19056, 20102},
  { 2746,  3625,  5299,  7504, 10262, 11432, 13172, 15490, 16875, 17514},
  { 2248,  3556,  8539, 10590, 12665, 14696, 16515, 17824, 20268, 21247},
  { 1279,  1960,  3920,  7793, 10153, 14753, 16646, 18139, 20679, 21466},
  { 2440,  3475,  6737,  8654, 12190, 14588, 17119, 17925, 19110, 19979},
  { 1879,  2514,  4497,  7572, 10017, 14948, 16141, 16897, 18397, 19376},
  { 2804,  3688,  7490, 10086, 11218, 12711, 16307, 17470, 20077, 21126},
  { 2023,  2682,  3873,  8268, 10255, 11645, 15187, 17102, 18965, 19788},
  { 2823,  3605,  5815,  8595, 10085, 11469, 16568, 17462, 18754, 19876},
  { 2851,  3681,  5280,  7648,  9173, 10338, 14961, 16148, 17559, 18474},
  { 1348,  2645,  5826,  8785, 10620, 12831, 16255, 18319, 21133, 22586},
  { 2141,  3036,  4293,  6082,  7593, 10629, 17158, 18033, 21466, 22084},
  { 1608,  2375,  3384,  6878,  9970, 11227, 16928, 17650, 20185, 21120},
  { 2774,  3616,  5014,  6557,  7788,  8959, 17068, 18302, 19537, 20542},
  { 1934,  4813,  6204,  7212,  8979, 11665, 15989, 17811, 20426, 21703},
  { 2288,  3507,  5037,  6841,  8278,  9638, 15066, 16481, 21653, 22214},
  { 2951,  3771,  4878,  7578,  9016, 10298, 14490, 15242, 20223, 20990},
  { 3256,  4791,  6601,  7521,  8644,  9707, 13398, 16078, 19102, 20249},
  { 1827,  2614,  3486,  6039, 12149, 13823, 16191, 17282, 21423, 22041},
  { 1000,  1704,  3002,  6335,  8471, 10500, 14878, 16979, 20026, 22427},
  { 1646,  2286,  3109,  7245, 11493, 12791, 16824, 17667, 18981, 20222},
  { 1708,  2501,  3315,  6737,  8729,  9924, 16089, 17097, 18374, 19917},
  { 2623,  3510,  4478,  5645,  9862, 11115, 15219, 18067, 19583, 20382},
  { 2518,  3434,  4728,  6388,  8082,  9285, 13162, 18383, 19819, 20552},
  { 1726,  2383,  4090,  6303,  7805, 12845, 14612, 17608, 19269, 20181},
  { 2860,  3735,  4838,  6044,  7254,  8402, 14031, 16381, 18037, 19410},
  { 4247,  5993,  7952,  9792, 12342, 14653, 17527, 18774, 20831, 21699},
  { 3502,  4051,  5680,  6805,  8146, 11945, 16649, 17444, 20390, 21564},
  { 3151,  4893,  5899,  7198, 11418, 13073, 15124, 17673, 20520, 21861},
  { 3960,  4848,  5926,  7259,  8811, 10529, 15661, 16560, 18196, 20183},
  { 4499,  6604,  8036,  9251, 10804, 12627, 15880, 17512, 20020, 21046},
  { 4251,  5541,  6654,  8318,  9900, 11686, 15100, 17093, 20572, 21687},
  { 3769,  5327,  7865,  9360, 10684, 11818, 13660, 15366, 18733, 19882},
  { 3083,  3969,  6248,  8121,  9798, 10994, 12393, 13686, 17888, 19105},
  { 2731,  4670,  7063,  9201, 11346, 13735, 16875, 18797, 20787, 22360},
  { 1187,  2227,  4737,  7214,  9622, 12633, 15404, 17968, 20262, 23533},
  { 1911,  2477,  3915, 10098, 11616, 12955, 16223, 17138, 19270, 20729},
  { 1764,  2519,  3887,  6944,  9150, 12590, 16258, 16984, 17924, 18435},
  { 1400,  3674,  7131,  8718, 10688, 12508, 15708, 17711, 19720, 21068},
  { 2322,  3073,  4287,  8108,  9407, 10628, 15862, 16693, 19714, 21474},
  { 2630,  3339,  4758,  8360, 10274, 11333, 12880, 17374, 19221, 19936},
  { 1721,  2577,  5553,  7195,  8651, 10686, 15069, 16953, 18703, 19929}
};

/**
 * L2 and L3 codebooks (both 5-dimensional, with 32 entries (3.2.4)
 */
static const int16_t cb_L2_L3[1<<L2_BITS][10] =
{        /* Q13 */
  { -435,  -815,  -742,  1033,  -518,   582, -1201,   829,    86,   385},
  { -833,  -891,   463,    -8, -1251,  1450,    72,  -231,   864,   661},
  {-1021,   231,  -306,   321,  -220,  -163,  -526,  -754, -1633,   267},
  {   57,  -198,  -339,   -33, -1468,   573,   796,  -169,  -631,   816},
  {  171,  -350,   294,  1660,   453,   519,   291,   159,  -640, -1296},
  { -701,  -842,   -58,   950,   892,  1549,   715,   527,  -714,  -193},
  {  584,    31,  -289,   356,  -333,  -457,   612,  -283, -1381,  -741},
  { -109,  -808,   231,    77,   -87,  -344,  1341,  1087,  -654,  -569},
  { -859,  1236,   550,   854,   714,  -543, -1752,  -195,   -98,  -276},
  { -877,  -954, -1248,  -299,   212,  -235,  -728,   949,  1517,   895},
  {  -77,   344,  -620,   763,   413,   502,  -362,  -960,  -483,  1386},
  { -314,  -307,  -256, -1260,  -429,   450,  -466,  -108,  1010,  2223},
  {  711,   693,   521,   650,  1305,   -28,  -378,   744, -1005,   240},
  { -112,  -271,  -500,   946,  1733,   271,   -15,   909,  -259,  1688},
  {  575,   -10,  -468,  -199,  1101, -1011,   581,   -53,  -747,   878},
  {  145,  -285, -1280,  -398,    36,  -498, -1377,    18,  -444,  1483},
  {-1133,  -835,  1350,  1284,   -95,  1015,  -222,   443,   372,  -354},
  {-1459, -1237,   416,  -213,   466,   669,   659,  1640,   932,   534},
  {  -15,    66,   468,  1019,  -748,  1385,  -182,  -907,  -721,  -262},
  { -338,   148,  1445,    75,  -760,   569,  1247,   337,   416,  -121},
  {  389,   239,  1568,   981,   113,   369, -1003,  -507,  -587,  -904},
  { -312,   -98,   949,    31,  1104,    72,  -141,  1465,    63,  -785},
  { 1127,   584,   835,   277, -1159,   208,   301,  -882,   117,  -404},
  {  539,  -114,   856,  -493,   223,  -912,   623,   -76,   276,  -440},
  { 2197,  2337,  1268,   670,   304,  -267,  -525,   140,   882,  -139},
  {-1596,   550,   801,  -456,   -56,  -697,   865,  1060,   413,   446},
  { 1154,   593,   -77,  1237,   -31,   581, -1037,  -895,   669,   297},
  {  397,   558,   203,  -797,  -919,     3,   692,  -292,  1050,   782},
  {  334,  1475,   632,   -80,    48, -1061,  -484,   362,  -597,  -852},
  { -545,  -330,  -429,  -680,  1133, -1182,  -744,  1340,   262,    63},
  { 1320,   827,  -398,  -576,   341,  -774,  -483, -1247,   -70,    98},
  { -163,   674,   -11,  -886,   531, -1125,  -265,  -242,   724,   934}
};

/**
 * interpolation filter b30 (3.7.1)
 *
 *   Specification does not provide formula for calculating table below.
 *
 *   It just says:
 *     b30 is based on Hamming windowed sinc functions, truncated at +/-29 and
 *     padded with zeros at +/-30 b30[30]=0
 *     The filter has a cut-off frequency (-3 dB) at 3600 Hz in the oversampled domain.
 *
 *   After some analisys i found this aproximation:
 *
 *                                    PI * x
 *   Hamm(x,N) = 0.53836-0.46164*cos(--------)
 *                                      N-1
 *                                      ---
 *                                       2
 *
 *                         N-1                               PI * x
 *   Hamm'(x,N) = Hamm(x - ---, N) =  0.53836 + 0.46164*cos(--------)
 *                          2                                  N-1
 *                                                             ---
 *             sin(PI * x)                                      2
 *   Sinc(x) = -----------
 *               PI * x
 *
 *   b30[n]:= 3*Hamm'(n,61)*[ 0.3 * Sinc(0.3 * n) ], n=0..30
 *
 * FIXME: what means 0.3 and 3 here?
 *
 * Comment from reference code:
 *   1/3 resolution interpolation filter  (-3 dB at 3600 Hz)
 *
 */
static const int16_t interp_filter[10][3] =
{       /* Q15 */
  { 29443,   25207,   14701},
  {  3143,   -4402,   -5850},
  { -2783,    1211,    3130},
  {  2259,       0,   -1652},
  { -1666,    -464,     756},
  {  1099,     550,    -245},
  {  -634,    -451,       0},
  {   308,     296,      78},
  {  -120,    -165,     -79},
  {    34,      91,      70},
};

/**
 * GA codebook (3.9.2)
 */
#define GA_CB_SIZE (1<<GA_BITS)
static const float cb_GA[GA_CB_SIZE][2] =
{
  { 0.197901,  1.214512}, //5
  { 0.094719,  0.296035}, //1
  { 0.163457,  3.315700}, //7
  { 0.117258,  1.134277}, //4
  { 0.111779,  0.613122}, //2
  { 0.000010,  0.185084}, //0
  { 0.021772,  1.801288}, //6
  { 0.003516,  0.659780}, //3
};

/**
 * GB codebook (3.9.2)
 */
#define GB_CB_SIZE (1<<GB_BITS)
static const float cb_GB[GB_CB_SIZE][2] =
{
  { 0.313871,  0.072357}, //2
  { 1.055892,  0.227186}, //14
  { 0.375977,  0.292399}, //3
  { 0.983459,  0.414166}, //13
  { 0.050466,  0.244769}, //0
  { 1.158039,  0.724592}, //15
  { 0.121711,  0.000010}, //1
  { 0.942028,  0.029027}, //12
  { 0.645363,  0.362118}, //6
  { 0.923602,  0.599938}, //10
  { 0.706138,  0.146110}, //7
  { 0.866379,  0.199087}, //9
  { 0.493870,  0.593410}, //4
  { 0.925376,  1.742757}, //11
  { 0.556641,  0.064087}, //5
  { 0.809357,  0.397579}, //8
};

/**
 * MA predictor (3.2.4)
 */
static const int16_t ma_predictor[2][MA_NP][10] =
{       /* Q15 */
  {
    { 8421,  9109,  9175,  8965,  9034,  9057,  8765,  8775,  9106,  8673},
    { 7018,  7189,  7638,  7307,  7444,  7379,  7038,  6956,  6930,  6868},
    { 5472,  4990,  5134,  5177,  5246,  5141,  5206,  5095,  4830,  5147},
    { 4056,  3031,  2614,  3024,  2916,  2713,  3309,  3237,  2857,  3473}
  },
  {
    { 7733,  7880,  8188,  8175,  8247,  8490,  8637,  8601,  8359,  7569},
    { 4210,  3031,  2552,  3473,  3876,  3853,  4184,  4154,  3909,  3968},
    { 3214,  1930,  1313,  2143,  2493,  2385,  2755,  2706,  2542,  2919},
    { 3024,  1592,   940,  1631,  1723,  1579,  2034,  2084,  1913,  2601}
  }
};

/**
 * ma_predicot_sum[i] := 1-sum{1}{4}{ma_predictor[k][i]}
 */
static const int16_t ma_predictor_sum[2][10] =
{      /* Q15 */
  { 7798,  8447,  8205,  8293,  8126,  8477,  8447,  8703,  9043,  8604},
  {14585, 18333, 19772, 17344, 16426, 16459, 15155, 15220, 16043, 15708}
};

/**
 * MA prediction coefficients (3.9.1, near Equation 69)
 * values are multiplied by 100
 */
static const uint8_t ma_prediction_coeff[4] =
{
  68, 58, 34, 19
};

/**
 * Initial lq values
 */
static const int16_t lq_init[10] =
{ /* Q13 */
  2339, 4679, 7018, 9358, 11698, 14037, 16377, 18717, 21056, 23396
};     /* PI*(float)(j+1)/11.0 */

/**
 * Initial LSP values
 */
static const int16_t lsp_init[10]=
{ /* Q15 */
   30000, 26000, 21000, 15000, 8000, 0, -8000,-15000,-21000,-26000
};

/*
-------------------------------------------------------------------------------
          Internal routines
------------------------------------------------------------------------------
*/
/**
 * \brief multiplies 32-bit integer by abother 16-bit and divides result by 2^15
 * \param var_q24 (Q24) 32-bit integer
 * \param var_15 (Q15) 16-bit integer
 * \return (Q24) result of mupliplication
 *
 * FIXME: if var_q15 declared as int16_t routine gives wrong result
 */
static inline int mul_24_15(int var_q24, int var_q15)
{
    int hi = var_q24 >> 15;
    int lo = var_q24 & 0x00007fff;
    return var_q15 * hi + ((var_q15 * lo) >> 15);
}

/**
 * \brief Calculates gain value of speech signal
 * \param speech signal buffer
 * \param length signal buffer length
 *
 * \return squared gain value
 */
static float sum_of_squares(float *speech, int length)
{
    int n;
    float sum=0;

    for(n=0; n<length; n++)
       sum+=speech[n]*speech[n];

    return sum;
}

/**
 * \brief pseudo random number generator
 */
static inline uint16_t g729_random(G729A_Context* ctx)
{
    return ctx->rand_value = 31821 * ctx->rand_value + 13849;
}


/**
 * \brief Check parity bit (3.7.2)
 * \param P1 Adaptive codebook index for first subframe
 * \param P0 Parity bit for P1
 *
 * \return 1 if parity check is ok, 0 - otherwise
 */
int g729_parity_check(uint8_t P1, int P0)
{
    //Parity is calculated on six most significant bits of P1
   return ((0x6996966996696996ULL >> (P1>>2)) ^ P0) & 1;
}

/**
 * \brief Decoding of the adaptive-codebook vector delay for first subframe (4.1.3)
 * \param ctx private data structure
 * \param ac_index Adaptive codebook index for first subframe
 * \param intT2 integer part of the pitch delay of the last seoond subframe
 *
 * \return 3*intT+frac+1, where
 *   intT integer part of delay
 *   frac fractional part of delay [-1, 0, 1]
 */
static int g729_decode_ac_delay_subframe1(G729A_Context* ctx, int ac_index, int intT2)
{
    /* if parity error */
    if(ctx->bad_pitch)
        return 3*intT2+1;

    if(ac_index>=197)
        return 3*ac_index-335;
	
    return ac_index+59;
}

/**
 * \brief Decoding of the adaptive-codebook vector delay for second subframe (4.1.3)
 * \param ctx private data structure
 * \param ac_index Adaptive codebook index for second subframe
 * \param intT1 first subframe's pitch delay integer part
 *
 * \return 3*intT+frac+1, where
 *   intT integer part of delay
 *   frac fractional part of delay [-1, 0, 1]
 */
static int g729_decode_ac_delay_subframe2(G729A_Context* ctx, int ac_index, int intT1)
{
    if(ctx->data_error)
        return 3*intT1+1;

    return ac_index + 3*FFMIN(FFMAX(intT1-5, PITCH_MIN), PITCH_MAX-9) - 1;
}

/**
 * \brief Decoding of the adaptive-codebook vector (4.1.3)
 * \param ctx private data structure
 * \param pitch_delay_int pitch delay, integer part
 * \param pitch_delay_frac pitch delay, fraction part [-1, 0, 1]
 * \param ac_v buffer to store decoded vector into
 */
static void g729_decode_ac_vector(G729A_Context* ctx, int pitch_delay_int, int pitch_delay_frac, float* ac_v)
{
    int n, i;
    float v;

    //Make sure that pitch_delay_frac will be always positive
    pitch_delay_frac=-pitch_delay_frac;
    if(pitch_delay_frac<0)
    {
        pitch_delay_frac+=3;
        pitch_delay_int++;
    }

    //t [0, 1, 2]
    //k [PITCH_MIN-1; PITCH_MAX]
    for(n=0; n<ctx->subframe_size; n++)
    {
        /* 3.7.1, Equation 40 */
        v=0;
        for(i=0; i<10; i++)
        {
            /*  R(x):=ac_v[-k+x] */
            v+=ac_v[n-pitch_delay_int-i  ] * (interp_filter[i][  pitch_delay_frac] / Q15_BASE); //R(n-i)*b30(t+3i)
            v+=ac_v[n-pitch_delay_int+i+1] * (interp_filter[i][3-pitch_delay_frac] / Q15_BASE); //R(n+i+1)*b30(3-t+3i)
        }
        ac_v[n]=v;
    }
}

/**
 * \brief Decoding fo the fixed-codebook vector (3.8)
 * \param ctx private data structure
 * \param fc_index Fixed codebook index
 * \param pulses_signs Signs of the excitation pulses (0 bit value means negative sign)
 * \param fc_v [out] decoded fixed codebook vector
 *
 * bit allocations:
 *   8k mode: 3+3+3+1+3
 * 4.4k mode: 4+4+4+1+4 (non-standard)
 *
 * FIXME: error handling required
 */
static void g729_decode_fc_vector(G729A_Context* ctx, int fc_index, int pulses_signs, float* fc_v)
{
    int i;
    int index;
    int bits=formats[ctx->format].fc_index_bits;
    int mask=(1 << bits) - 1;

    memset(fc_v, 0, sizeof(float)*ctx->subframe_size);

    /* reverted Equation 62 and Equation 45 */
    for(i=0; i<FC_PULSE_COUNT-1; i++)
    {
        index=(fc_index & mask) * 5 + i;
        //overflow can occur in 4.4k case
        if(index>=ctx->subframe_size)
        {
            ctx->data_error=1;
            return;
        }
        fc_v[ index ] = (pulses_signs & 1) ? 1 : -1;
        fc_index>>=bits;
        pulses_signs>>=1;
    }
    index=((fc_index>>1) & mask) * 5 + i + (fc_index & 1);
    //overflow can occur in 4.4k case
    if(index>=ctx->subframe_size)
    {
        ctx->data_error=1;
        return;
    }
    fc_v[ index ] = (pulses_signs & 1) ? 1 : -1;
}

/**
 * \brief fixed codebook vector modification if delay is less than 40 (4.1.4 and 3.8)
 * \param pitch_delay integer part of pitch delay
 * \param gain_pitch gain pitch
 * \param fc_v [in/out] fixed codebook vector to change
 * \param length length of fc_v array
 */
static void g729_fix_fc_vector(int pitch_delay, float gain_pitch, float* fc_v, int length)
{
    int i;

    for(i=pitch_delay; i<length;i++)
        fc_v[i] += fc_v[i-pitch_delay]*gain_pitch;
}

/**
 * \brief Attenuation of the memory of the gain predictor (4.4.3)
 * \param pred_energ_q past quantized energies
 */
static void g729_update_gain_erasure(float *pred_energ_q)
{
    float avg_gain=pred_energ_q[3];
    int i;

    /* 4.4.3. Equation 95 */
    for(i=3; i>0; i--)
    {
        avg_gain      +=pred_energ_q[i-1];
        pred_energ_q[i]=pred_energ_q[i-1];
    }
    pred_energ_q[0] = FFMAX(avg_gain * 0.25 - 4.0, -14);
}

/**
 * \brief Decoding of the adaptive and fixed codebook gains (4.1.5 and 3.9.1)
 * \param ctx private data structure
 * \param GA Gain codebook (stage 2)
 * \param GB Gain codebook (stage 2)
 * \param fc_v fixed-codebook vector
 * \param gp pointer to variable receiving quantized fixed-codebook gain (gain pitch)
 * \param gc pointer to variable receiving quantized adaptive-codebook gain (gain code)
 */
static void g729_get_gain(G729A_Context *ctx, int nGA, int nGB, float* fc_v, float* gp, float* gc)
{
    float energy;
    int i;
    float cb1_sum;

    /* 3.9.1, Equation 66 */
    energy=sum_of_squares(fc_v, ctx->subframe_size);

    /*
      energy=mean_energy-E
      mean_energy=30dB
      E is calculated in 3.9.1 Equation 66
    */
    energy = 30 - 10.0*log(energy/ctx->subframe_size)/M_LN10;

    /* 3.9.1, Equation 69 */
    for(i=0; i<4; i++)
        energy+= 0.01 * ctx->pred_energ_q[i] * ma_prediction_coeff[i];

    /* 3.9.1, Equation 71 */
    energy = exp(M_LN10*energy/20); //FIXME: should there be subframe_size/2 ?

    // shift prediction error vector
    for(i=3; i>0; i--)
        ctx->pred_energ_q[i]=ctx->pred_energ_q[i-1];

    cb1_sum = cb_GA[nGA][1]+cb_GB[nGB][1];
    /* 3.9.1, Equation 72 */
    ctx->pred_energ_q[0] = 20 * log(cb1_sum) / M_LN10; //FIXME: should there be subframe_size/2 ?

    /* 3.9.1, Equation 73 */
    *gp = cb_GA[nGA][0]+cb_GB[nGB][0];           // quantized adaptive-codebook gain (gain code)

    /* 3.9.1, Equation 74 */
    *gc = energy*(cb1_sum);  //quantized fixed-codebook gain (gain pitch)
}

/**
 * \brief Memory update (3.10)
 * \param ctx private data structure
 * \param fc_v fixed-codebook vector
 * \param gp quantized fixed-codebook gain (gain pitch)
 * \param gc quantized adaptive-codebook gain (gain code)
 * \param exc last excitation signal buffer for current subframe
 */
static void g729_mem_update(G729A_Context *ctx, float *fc_v, float gp, float gc, float* exc)
{
    int i;

    for(i=0; i<ctx->subframe_size; i++)
        exc[i]=exc[i]*gp+fc_v[i]*gc;
}

/**
 * \brief LP synthesis filter
 * \param ctx private data structure
 * \param lp filter coefficients
 * \param in input signal
 * \param out output (filtered) signal
 * \param filter_data filter data array (previous synthesis data)
 *
 * Routine applies 1/A(z) filter to given speech data
 *
 */
static void g729_lp_synthesis_filter(G729A_Context *ctx, float* lp, float *in, float *out, float *filter_data)
{
    float tmp_buf[MAX_SUBFRAME_SIZE+10];
    float* tmp=tmp_buf+10;
    int i,n;

    memcpy(tmp_buf, filter_data, 10*sizeof(float));

    for(n=0; n<ctx->subframe_size; n++)
    {
        tmp[n]=in[n];
        for(i=0; i<10; i++)
            tmp[n] -= lp[i]*tmp[n-i-1];
    }
    memcpy(filter_data, tmp+ctx->subframe_size-10, 10*sizeof(float));
    memcpy(out, tmp, ctx->subframe_size*sizeof(float));
}

/**
 * \brief Adaptive gain control (4.2.4)
 * \param ctx private data structure
 * \param gain_before gain of speech before applying postfilters
 * \param gain_after  gain of speech after applying postfilters
 * \param speech signal buffer
 */
static void g729a_adaptive_gain_control(G729A_Context *ctx, float gain_before, float gain_after, float *speech)
{
    float gain;
    int n;

    if(!gain_after)
        return;

    gain=sqrt(gain_before/gain_after);

    for(n=0; n<ctx->subframe_size; n++)
    {
        ctx->g = 0.9*ctx->g + 0.1*gain;
        speech[n] *= ctx->g;
    }
}

/**
 * \brief Calculates coefficients of weighted A(z/GAMMA) filter
 * \param Az source filter
 * \param gamma weight coefficients
 * \param Azg resulted weighted A(z/GAMMA) filter
 *
 * Azg[i]=GAMMA^i*Az[i] , i=0..subframe_size
 *
 */
static void g729a_weighted_filter(float* Az, float gamma, float *Azg)
{
    float gamma_pow;
    int n;

    gamma_pow=gamma;
    for(n=0; n<10; n++)
    {
        Azg[n]=Az[n]*gamma_pow;
        gamma_pow*=gamma;
    }
}

/**
 * \brief long-term postfilter (4.2.1)
 * \param ctx private data structure
 * \param intT1 integer part of the pitch delay T1 in the first subframe
 * \param residual_filt speech signal with applied A(z/GAMMA_N) filter
 */
static void g729a_long_term_filter(G729A_Context *ctx, int intT1, float *residual_filt)
{
    int k, n, intT0;
    float gl;      ///< gain coefficient for long-term postfilter
    float corr_t0; ///< correlation of residual signal with delay intT0
    float corr_0;  ///< correlation of residual signal with delay 0
    float correlation, corr_max;
    float inv_glgp;///< 1.0/(1+gl*GAMMA_P)
    float glgp_inv_glgp; ///< gl*GAMMA_P/(1+gl*GAMMA_P);

    /* A.4.2.1 */
    int minT0=FFMIN(intT1, PITCH_MAX-3)-3;
    int maxT0=FFMIN(intT1, PITCH_MAX-3)+3;
    /* Long-term postfilter start */

    /*
       First pass: searching the best T0 (pitch delay)
       Second pass is not used in G.729A: fractional part is always zero
    */
    intT0=minT0;
    corr_max=INT_MIN;
    for(k=minT0; k<=maxT0; k++)
    {
        correlation=0;
        /* 4.2.1, Equation 80 */
        for(n=0; n<ctx->subframe_size; n++)
            correlation+=ctx->residual[n+PITCH_MAX]*ctx->residual[n+PITCH_MAX-k];
        if(correlation>corr_max)
        {
            corr_max=correlation;
            intT0=k;
        }
    }

    corr_t0=sum_of_squares(ctx->residual+PITCH_MAX-intT0, ctx->subframe_size);
    corr_0=sum_of_squares(ctx->residual+PITCH_MAX, ctx->subframe_size);

    /* 4.2.1, Equation 82. checking if filter should be disabled */
    if(corr_max*corr_max < 0.5*corr_0*corr_t0)
        gl=0;
    else if(!corr_t0)
        gl=1;
    else
        gl=FFMIN(corr_max/corr_t0, 1);

    inv_glgp = 1.0 / (1 + gl*GAMMA_P);
    glgp_inv_glgp = gl * GAMMA_P * inv_glgp;

    /* 4.2.1, Equation 78, reconstructing delayed signal */
    for(n=0; n<ctx->subframe_size; n++)
        residual_filt[n]=ctx->residual[n+PITCH_MAX]*inv_glgp+ctx->residual[n+PITCH_MAX-intT0]*glgp_inv_glgp;

    //Shift residual for using in next subframe
    memmove(ctx->residual, ctx->residual+ctx->subframe_size, PITCH_MAX*sizeof(float));
}

/**
 * \brief compensates the tilt in the short-term postfilter (4.2.3)
 * \param ctx private data structure
 * \param lp_gn coefficients of A(z/GAMMA_N) filter
 * \param lp_gd coefficients of A(z/GAMMA_D) filter
 * \param res_pst residual signal (partially filtered)
*/
static void g729a_tilt_compensation(G729A_Context *ctx,float *lp_gn, float *lp_gd, float* res_pst)
{
    float tmp;
    float gt,k,rh1,rh0;
    float hf[22]; // A(Z/GAMMA_N)/A(z/GAMMA_D) filter impulse response
    float tmp_buf[11+22];
    float sum;
    int i, n;

    hf[0]=1;
    for(i=0; i<10; i++)
        hf[i+1]=lp_gn[i];

    for(i=11; i<22;i++)
        hf[i]=0;

    /* Applying 1/A(z/GAMMA_D) to hf */
    for(i=0; i<10; i++)
        tmp_buf[i]=hf[i+11];

    for(n=0; n<22; n++)
    {
        sum=hf[n];
        for(i=0; i<10; i++)
            sum -= lp_gd[i]*tmp_buf[n-i-1+10];
        tmp_buf[n+10]=sum;
        hf[n]=sum;
    }

    /* Now hf contains impulse response of A(z/GAMMA_N)/A(z/GAMMA_D) filter */

    /* A.4.2.3, Equation A.14, calcuating rh(0)  */
    rh0=0;
    for(i=0; i<22; i++)
        rh0+=hf[i]*hf[i];

    /* A.4.2.3, Equation A.14, calcuating rh(1)  */
    rh1=0;
    for(i=0; i<22-1; i++)
        rh1+=hf[i]*hf[i+1];

    /* A.4.2.3, Equation A.14 */
    k=-rh1/rh0;

    if(k>=0)
        gt=0;
    else
        gt=GAMMA_T*k;

    /* A.4.2.3. Equation A.13, applying filter to signal */
    tmp=res_pst[ctx->subframe_size-1];

    for(i=ctx->subframe_size-1; i>=1; i--)
        res_pst[i] += gt*res_pst[i-1];
    res_pst[0] += gt*ctx->ht_prev_data;

    ctx->ht_prev_data=tmp;
}

/**
 * \brief Signal postfiltering (4.2, with A.4.2 simplification)
 * \param ctx private data structure
 * \param lp LP filter coefficients
 * \param intT1 integer part of the pitch delay T1 of the first subframe
 * \param speech_buf signal buffer, containing at the top 10 samples from previous subframe
 *
 * Filtering has following  stages:
 *   Long-term postfilter (4.2.1)
 *   Short-term postfilter (4.2.2).
 *   Tilt-compensation (4.2.3)
 *   Adaptive gain control (4.2.4)
 *
 * \note This routine is G.729 Annex A specific.
 */
static void g729a_postfilter(G729A_Context *ctx, float *lp, int intT1, float *speech_buf)
{
    int i, n;
    float *speech=speech_buf+10;
    float residual_filt_buf[MAX_SUBFRAME_SIZE+10];
    float* residual_filt=residual_filt_buf+10;
    float lp_gn[10];
    float lp_gd[10];
    float gain_before, gain_after;

    /* Calculating coefficients of A(z/GAMMA_N) filter */
    g729a_weighted_filter(lp, GAMMA_N, lp_gn);
    /* Calculating coefficients of A(z/GAMMA_D) filter */
    g729a_weighted_filter(lp, GAMMA_D, lp_gd);

    /*
      4.2.1, Equation 79 Residual signal calculation
      ( filtering through A(z/GAMMA_N) , one half of short-term filter)
    */
    for(n=0; n<ctx->subframe_size; n++)
    {
        ctx->residual[n+PITCH_MAX]=speech[n];
        for(i=0; i<10; i++)
            ctx->residual[n+PITCH_MAX] += lp_gn[i]*speech[n-i-1];
    }

    /* Calculating gain of unfiltered signal for using in AGC */
    gain_before=sum_of_squares(speech, ctx->subframe_size);

    /* long-term filter (A.4.2.1) */
    g729a_long_term_filter(ctx, intT1, residual_filt);

    /* short-term filter tilt compensation (A.4.2.3) */
    g729a_tilt_compensation(ctx, lp_gn, lp_gd, residual_filt);

    /* Applying second half of short-term postfilter: 1/A(z/GAMMA_D)*/
    g729_lp_synthesis_filter(ctx, lp_gd, residual_filt, speech, ctx->res_filter_data);

    /* Calculating gain of filtered signal for using in AGC */
    gain_after=sum_of_squares(speech, ctx->subframe_size);

    /* adaptive gain control (A.4.2.4) */
    g729a_adaptive_gain_control(ctx, gain_before, gain_after, speech);
}

/**
 * \brief high-pass filtering and upscaling (4.2.5)
 * \param ctx private data structure
 * \param speech reconstructed speech signal for applying filter to
 *
 * Filter has cut-off frequency 100Hz
 */
static void g729_high_pass_filter(G729A_Context* ctx, float* speech)
{
    float z_2=0;
    float f_0=0;
    int i;

    for(i=0; i<ctx->subframe_size; i++)
    {
        z_2=ctx->hpf_z1;
        ctx->hpf_z1=ctx->hpf_z0;
        ctx->hpf_z0=speech[i];

        f_0 = 1.9330735 * ctx->hpf_f1 - 0.93589199 * ctx->hpf_f2 + 
              0.93980581 * ctx->hpf_z0 - 1.8795834 * ctx->hpf_z1 + 0.93980581 * z_2;
        speech[i] = f_0*2.0;

        ctx->hpf_f2=ctx->hpf_f1;
        ctx->hpf_f1=f_0;
    }
}

/**
 * \brief Computing the reconstructed speech (4.1.6)
 * \param ctx private data structure
 * \param lp LP filter coefficients
 * \param intT1 integer part of the pitch delay T1 of the first subframe
 * \param exc excitation
 * \param speech reconstructed speech buffer (ctx->subframe_size items)
 */
static void g729_reconstruct_speech(G729A_Context *ctx, float *lp, int intT1, float* exc, int16_t* speech)
{
    float tmp_speech_buf[MAX_SUBFRAME_SIZE+10];
    float* tmp_speech=tmp_speech_buf+10;
    int i;

    memcpy(tmp_speech_buf, ctx->syn_filter_data, 10 * sizeof(float));

    /* 4.1.6, Equation 77  */
    g729_lp_synthesis_filter(ctx, lp, exc, tmp_speech, ctx->syn_filter_data);

    /* 4.2 */
    g729a_postfilter(ctx, lp, intT1, tmp_speech_buf);

    //Postprocessing
    g729_high_pass_filter(ctx,tmp_speech);

    for(i=0; i<ctx->subframe_size; i++)
    {
        tmp_speech[i] = FFMIN(tmp_speech[i],  32767.0);
        tmp_speech[i] = FFMAX(tmp_speech[i], -32768.0);
        speech[i]=lrintf(tmp_speech[i]);
    }
}

/**
 * \brief Convert LSF to LSP
 * \param ctx private data structure
 * \param lsf (Q13) LSF coefficients
 * \param lsp LSP coefficients
 *
 * \remark It is safe to pass the same array in lsf and lsp parameters
 */
static void g729_lsf2lsp(G729A_Context *ctx, int *lsf, int *lsp)
{
    int i;

    /* Convert LSF to LSP */
    for(i=0;i<10; i++)
        lsp[i]=cos(lsf[i] / Q13_BASE) * Q15_BASE;
}

/**
 * \brief Restore LSP parameters using previous frame data
 * \param ctx private data structure
 * \param lsfq (Q13) Decoded LSF coefficients
 */
static void g729_lsf_restore_from_previous(G729A_Context *ctx, int* lsfq)
{
    int lq[10]; // Q13, Q28
    int i,k;

    //Restore LSF from previous frame
    for(i=0;i<10; i++)
        lsfq[i]=ctx->lsf_prev[i];

    /* 4.4.1, Equation 92 */
    for(i=0; i<10; i++)
    {
        lq[i]= lsfq[i] << 15; //Q13 -> Q28
        for(k=0;k<MA_NP; k++)
            lq[i] -= ctx->lq_prev[k][i] * ma_predictor[ctx->prev_mode][k][i]; // Q28
        lq[i] /= ma_predictor_sum[ctx->prev_mode][i];                         // Q13
    }

    /* Rotate lq_prev */
    for(i=0; i<10; i++)
    {
        for(k=MA_NP-1; k>0; k--)
            ctx->lq_prev[k][i]=ctx->lq_prev[k-1][i];
        ctx->lq_prev[0][i]=lq[i];
    }
}

/**
 * \brief Decode LP coefficients from L0-L3 (3.2.4)
 * \param ctx private data structure
 * \param L0 Switched MA predictor of LSP quantizer
 * \param L1 First stage vector of quantizer
 * \param L2 Second stage lower vector of LSP quantizer
 * \param L3 Second stage higher vector of LSP quantizer
 * \param lsfq (Q13) Decoded LSP coefficients
 */
static void g729_lsf_decode(G729A_Context* ctx, int16_t L0, int16_t L1, int16_t L2, int16_t L3, int* lsfq)
{
    int i,j,k;
    int16_t J[2]={10, 5}; //Q13
    int16_t lq[10];       //Q13
    int16_t diff;         //Q13
    int sum;              //Q28

    /* 3.2.4 Equation 19 */
    for(i=0;i<5; i++)
    {
        lq[i]   = cb_L1[L1][i  ] + cb_L2_L3[L2][i  ]; //Q13
        lq[i+5] = cb_L1[L1][i+5] + cb_L2_L3[L3][i+5]; //Q13
    }

    /* 3.2.4 rearrangement routine */
    for(j=0; j<2; j++)
    {
        for(i=1; i<10; i++)
        {
            diff = (lq[i-1] - lq[i] + J[j])>>1;
            if(diff>0)
            {
                lq[i-1]-= diff;
                lq[i]  += diff;
            }
        }
    }

    /* 3.2.4, Equation 20 */
    for(i=0; i<10; i++)
    {
        sum = lq[i] * ma_predictor_sum[L0][i]; //Q28
        for(k=0; k<MA_NP; k++)
            sum += ctx->lq_prev[k][i] * ma_predictor[L0][k][i];
        //Saving LSF for using when error occured in next frames
        ctx->lsf_prev[i] = lsfq[i] = sum >> 15; //Q28 -> Q13
    }

    /* Rotate lq_prev */
    for(i=0; i<10; i++)
    {
        for(k=MA_NP-1; k>0; k--)
            ctx->lq_prev[k][i] = ctx->lq_prev[k-1][i];
        ctx->lq_prev[0][i] = lq[i];
    }
    ctx->prev_mode=L0;

    /* sorting lsfq in ascending order. float bubble agorithm*/
    for(j=9; j>0; j--)
        for(i=0; i<j; i++)
            if(lsfq[i] > lsfq[i+1])
                FFSWAP(float, lsfq[i], lsfq[i+1]);

    /* checking for stability */
    lsfq[0] = FFMAX(lsfq[0],LSFQ_MIN); //Is warning required ?

    for(i=0;i<9; i++)
        lsfq[i+1]=FFMAX(lsfq[i+1], lsfq[i] + LSFQ_DIFF_MIN);
    lsfq[9] = FFMIN(lsfq[9], LSFQ_MAX);//Is warning required ?
}


/**
 * \brief decodes polinomial coefficients from LSP
 * \param lsp (Q15) LSP coefficients
 * \param f [out] (Q24) decoded polinomial coefficients
 */
static void get_lsp_coefficients(int* lsp, int* f)
{
    int i, j;
    int qidx=2;
    int b;

    f[0] = 0x1000000;   // 1.0 in Q24
    f[1] = -lsp[0] << 10; // *2 and Q15 -> Q24

    for(i=2; i<=5; i++)
    {
        b=-lsp[qidx]<<1;   // Q15        
        f[i] = mul_24_15(f[i-1], b) + 2*f[i-2];

        for(j=i-1; j>1; j--)
            f[j] += mul_24_15(f[j-1], b) + f[j-2];

        f[1]+=b << 9;
        qidx+=2;
    }
}
/**
 * \brief LSP to LP conversion (3.2.6)
 * \param ctx private data structure
 * \param lsp (Q15) LSP coefficients
 * \param lp (Q13) decoded LP coefficients
 */
static void g729_lsp2lp(G729A_Context* ctx, int* lsp, int* lp)
{
    int i;
    int f1[6];
    int f2[6];

    get_lsp_coefficients(lsp,   f1);
    get_lsp_coefficients(lsp+1, f2);

    /* 3.2.6, Equations 25 and  26*/
    for(i=0;i<5;i++)
    {
        int ff1 = f1[i+1] + f1[i];
        int ff2 = f2[i+1] - f2[i];
        lp[i]   = (ff1 + ff2)>>12; // *0.5 and Q24 -> Q13
        lp[9-i] = (ff1 - ff2)>>12; // *0.5 and Q24 -> Q13
    }
}

/**
 * \brief interpolate LSP end decode LP for both first and second subframes (3.2.5 and 3.2.6)
 * \param ctx private data structure
 * \param (Q15) lsp_curr current LSP coefficients
 * \param lp [out] decoded LP coefficients
 */
static void g729_lp_decode(G729A_Context* ctx, int* lsp_curr, float* lp)
{
    int lsp[10];
    int lp_tmp[20];
    int i;

    /* LSP values for first subframe (3.2.5, Equation 24)*/
    for(i=0;i<10;i++)
        lsp[i]=(lsp_curr[i]+ctx->lsp_prev[i])/2;

    g729_lsp2lp(ctx, lsp, lp_tmp);

    /* LSP values for second subframe (3.2.5)*/
    g729_lsp2lp(ctx, lsp_curr, lp_tmp+10);

    /* saving LSP coefficients for using in next frame */
    for(i=0;i<10;i++)
        ctx->lsp_prev[i]=lsp_curr[i];

    for(i=0;i<20;i++)
        lp[i]=lp_tmp[i] / Q13_BASE;
}


/*
-------------------------------------------------------------------------------
          API
------------------------------------------------------------------------------
*/

/**
 * \brief G.729A decoder initialization
 * \param ctx private data structure
 * \return 0 if success, non-zero otherwise
 */
static int ff_g729a_decoder_init(AVCodecContext * avctx)
{
    G729A_Context* ctx=avctx->priv_data;
    int i,k;

    if(avctx->sample_rate==8000)
        ctx->format=0;
#ifdef G729_SUPPORT_4400
    else if (avctx->sample_rate==4400)
        ctx->format=1;
#endif
    else
    {
        av_log(avctx, AV_LOG_ERROR, "Sample rate %d is not supported\n", avctx->sample_rate);
        return AVERROR_NOFMT;
    }
    ctx->subframe_size=formats[ctx->format].output_frame_size>>2; // cnumber of 2-byte long samples in one subframe

    assert(ctx->subframe_size>0 && ctx->subframe_size<=MAX_SUBFRAME_SIZE);

    /* Decoder initialization. 4.3, Table 9 */

    /*
    Pitch gain of previous subframe.

    (EE) This does not comply with specification, but reference
         and Intel decoder uses here minimum sharpen value instead of maximum. */
    ctx->gain_pitch=GAIN_PITCH_MIN;

    /* gain coefficient */
    ctx->g=1.0;

    /* LSP coefficients */
    for(i=0; i<10; i++)
        ctx->lq_prev[0][i]=lq_init[i];

    for(i=0; i<10; i++)
        ctx->lsp_prev[i]=lsp_init[i];

    for(k=1; k<MA_NP; k++)
        for(i=0; i<10; i++)
            ctx->lq_prev[k][i]=ctx->lq_prev[0][i];

    ctx->exc=&ctx->exc_base[PITCH_MAX+INTERPOL_LEN];

    /* random seed initialization (4.4.4) */
    ctx->rand_value=21845;

    //quantized prediction error
    for(i=0; i<4; i++)
        ctx->pred_energ_q[i] = -14;

    memset(ctx->syn_filter_data, 0, 10*sizeof(float));
    memset(ctx->res_filter_data, 0, 10*sizeof(float));

    //High-pass filter data
    ctx->hpf_f1=0.0;
    ctx->hpf_f2=0.0;
    ctx->hpf_z0=0;
    ctx->hpf_z1=0;

    avctx->frame_size=2*ctx->subframe_size;
    return 0;
}

/**
 * \brief decode one G.729 frame into PCM samples
 * \param serial array if bits (0x81 - 1, 0x7F -0)
 * \param serial_size number of items in array
 * \param out_frame array for output PCM samples
 * \param out_frame_size maximum number of elements in output array
 */
static int  g729a_decode_frame_internal(void* context, int16_t* out_frame, int out_frame_size, G729_parameters *parm)
{
    G729A_Context* ctx=context;
    float lp[20];
    int lsp[10];                 // Q15
    int lsf[10];                 // Q13
    int pitch_delay;             // pitch delay
    float fc[MAX_SUBFRAME_SIZE]; // fixed codebooc vector
    float gp, gc;
    int intT1, i;

    ctx->data_error=0;
    ctx->bad_pitch=0;

    ctx->bad_pitch = !g729_parity_check(parm->ac_index[0], parm->parity);

    if(ctx->data_error)
        g729_lsf_restore_from_previous(ctx, lsf);
    else
        g729_lsf_decode(ctx, 
	         parm->ma_predictor,
                 parm->quantizer_1st,
                 parm->quantizer_2nd_lo,
                 parm->quantizer_2nd_hi,
                 lsf);

    //Convert LSF to LSP
    g729_lsf2lsp(ctx, lsf, lsp);

    g729_lp_decode(ctx, lsp, lp);

    for(i=0; i<2; i++)
    {

        if(!i)
        {
            pitch_delay=g729_decode_ac_delay_subframe1(ctx, parm->ac_index[i], ctx->intT2_prev);
            intT1=pitch_delay/3;    //Used in long-term postfilter    
        }
        else
        {
            pitch_delay=g729_decode_ac_delay_subframe2(ctx, parm->ac_index[i], pitch_delay/3);
            ctx->intT2_prev=pitch_delay/3;
            if(ctx->data_error)
                ctx->intT2_prev=FFMIN(ctx->intT2_prev+1, PITCH_MAX);
        }

        g729_decode_ac_vector(ctx, pitch_delay/3, (pitch_delay%3)-1, ctx->exc+i*ctx->subframe_size);

        if(ctx->data_error)
        {
            parm->fc_indexes[i]   = g729_random(ctx) & 0x1fff;
            parm->pulses_signs[i] = g729_random(ctx) & 0x000f;
        }

        g729_decode_fc_vector(ctx, parm->fc_indexes[i], parm->pulses_signs[i], fc);
        g729_fix_fc_vector(pitch_delay/3, ctx->gain_pitch, fc, ctx->subframe_size);

        if(ctx->data_error)
        {
            /*
                Decoding of the adaptive and fixed codebook gains
                from previous subframe (4.4.2)
            */

            /* 4.4.2, Equation 94 */
            ctx->gain_pitch = gp = FFMIN(0.9 * ctx->gain_pitch, 0.9);
            /* 4.4.2, Equation 93 */
            ctx->gain_code  = 0.98 * ctx->gain_code;

            g729_update_gain_erasure(ctx->pred_energ_q);
        }
        else
        {
            g729_get_gain(ctx, parm->ga_cb_index[i], parm->gb_cb_index[i], fc, &gp, &gc);

            /* save gain code value for next subframe */
            ctx->gain_code=gc;
            /* save pitch gain value for next subframe */
            ctx->gain_pitch=FFMIN(FFMAX(gp, GAIN_PITCH_MIN), GAIN_PITCH_MAX);
        }

        g729_mem_update(ctx, fc, gp, gc, ctx->exc+i*ctx->subframe_size);
        g729_reconstruct_speech(ctx, lp+i*10, intT1, ctx->exc+i*ctx->subframe_size, out_frame+i*ctx->subframe_size);
        ctx->subframe_idx++;
    }

    //Save signal for using in next frame
    memmove(ctx->exc_base, ctx->exc_base+2*ctx->subframe_size, (PITCH_MAX+INTERPOL_LEN)*sizeof(float));

    return 2*ctx->subframe_size;
}

/**
 * \brief decodes one G.729 frame (10 bytes long) into parameters vector
 * \param ctx private data structure
 * \param buf 10 bytes of decoder parameters
 * \param buf_size size of input buffer
 * \param int parm output vector of decoded parameters
 *
 * \return 0 if success, nonzero - otherwise
 */
static int g729_bytes2parm(G729A_Context *ctx, const uint8_t *buf, int buf_size, G729_parameters *parm)
{
    GetBitContext gb;

    init_get_bits(&gb, buf, buf_size);

    parm->ma_predictor     = get_bits(&gb, L0_BITS); //L0
    parm->quantizer_1st    = get_bits(&gb, L1_BITS); //L1
    parm->quantizer_2nd_lo = get_bits(&gb, L2_BITS); //L2
    parm->quantizer_2nd_hi = get_bits(&gb, L3_BITS); //L3
    parm->ac_index[0]      = get_bits(&gb, P1_BITS); //P1
    parm->parity           = get_bits(&gb, P0_BITS); //Parity
    parm->fc_indexes[0]    = get_bits(&gb, formats[ctx->format].fc_index_bits*FC_PULSE_COUNT+1); //C1
    parm->pulses_signs[0]  = get_bits(&gb, FC_PULSE_COUNT); //S1
    parm->ga_cb_index[0]   = get_bits(&gb, GA_BITS); //GA1
    parm->gb_cb_index[0]   = get_bits(&gb, GB_BITS); //GB1
    parm->ac_index[1]      = get_bits(&gb, P2_BITS); //P2
    parm->fc_indexes[1]    = get_bits(&gb, formats[ctx->format].fc_index_bits*FC_PULSE_COUNT+1); //C2
    parm->pulses_signs[1]  = get_bits(&gb, FC_PULSE_COUNT); //S2
    parm->ga_cb_index[1]   = get_bits(&gb, GA_BITS); //GA2
    parm->gb_cb_index[1]   = get_bits(&gb, GB_BITS); //GB2

    return 0;
}


static int ff_g729a_decode_frame(AVCodecContext *avctx,
                             void *data, int *data_size,
                             const uint8_t *buf, int buf_size)
{
    G729_parameters parm;
    G729A_Context *ctx=avctx->priv_data;
    int in_frame_size=formats[ctx->format].input_frame_size;
    int out_frame_size=formats[ctx->format].output_frame_size;
    int i, ret;
    uint8_t *dst=data;
    const uint8_t *src=buf;

    if (buf_size<in_frame_size)
        return AVERROR(EIO);

    *data_size=0;
    for(i=0; i<buf_size; i+=in_frame_size)
    {
        ret=g729_bytes2parm(ctx, src, in_frame_size, &parm);
        if(ret)
            return ret;
        g729a_decode_frame_internal(ctx, (int16_t*)dst, out_frame_size, &parm);
        dst+=out_frame_size;
        src+=in_frame_size;
    }
    *data_size=(dst-(uint8_t*)data);
    return (src-buf);
}

AVCodec g729a_decoder =
{
    "g729a",
    CODEC_TYPE_AUDIO,
    CODEC_ID_G729A,
    sizeof(G729A_Context),
    ff_g729a_decoder_init,
    NULL,
    NULL,
    ff_g729a_decode_frame,
};

#ifdef G729A_NATIVE
/* debugging  stubs */
void* g729a_decoder_init()
{
    AVCodecContext *avctx=av_mallocz(sizeof(AVCodecContext));
    avctx->priv_data=av_mallocz(sizeof(G729A_Context));
    avctx->sample_rate=8000;

    ff_g729a_decoder_init(avctx);
    return avctx;
}
int g729a_decoder_uninit(void* ctx)
{
  return 0;
}
int  g729a_decode_frame(AVCodecContext* avctx, int16_t* serial, int serial_size, int16_t* out_frame, int out_frame_size)
{
    G729_parameters parm;

    g729_bytes2parm(avctx->priv_data, serial, 82, &parm);

    return g729a_decode_frame_internal(avctx->priv_data, out_frame, out_frame_size, &parm);
}
/*
---------------------------------------------------------------------------
    Encoder
---------------------------------------------------------------------------
*/
void* g729a_encoder_init()
{
    return NULL;
}
int g729a_encode_frame(void * context, int16_t* data, int ibuflen, int16_t* serial, int obuflen)
{
    return 0;
}
void g729_encoder_uninit(void* context)
{
}
#endif /* G729A_NATIVE */
