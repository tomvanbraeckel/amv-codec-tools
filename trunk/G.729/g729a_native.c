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
overflow: FAIL
parity  : PASS
pitch   : PASS
speech  : PASS
tame    : PASS
test    : PASS

Naming conventions:

Routines:
g729_*    : common for G.729 and G.729 AnnexA
g729a_*   : specific to G.729 AnnexA. Those marked
ff_       : interface to FFmpeg API
no prefix : common routines for miscelaneous tasks (e.g. fixed-point math operations)

Parameters:
[out]     : all data in array will be overwritten regardless of previous value
[in/out]  : array is filled using previously stored data
no mark   : input data only

Misc:
Q<n>      : Means "value * (1<<n)" (i.e. fixed-point value with 2^n base)

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

#define L0_BITS 1             ///< Switched MA predictor of LSP quantizer (size in bits)
#define L1_BITS 7             ///< First stage vector of quantizer (size in bits)
#define L2_BITS 5             ///< First stage lowervector of quantizer (size in bits)
#define L3_BITS 5             ///< First stage hihjer vector of quantizer (size in bits)
#define P1_BITS 8             ///< Adaptive codebook index for first subframe (size in bits)
#define P2_BITS 5             ///< Adaptive codebook index for second subframe (size in bits)
#define P0_BITS 1             ///< Parity bit for pitch delay (size in bits)
#define GA_BITS 3             ///< GA codebook index (size in bits)
#define GB_BITS 4             ///< GB codebook index (size in bits)
#define FC_PULSE_COUNT 4      ///< Number of pulses in fixed-codebook vector
#define FC_BITS(ctx) (formats[ctx->format].fc_index_bits*FC_PULSE_COUNT+1) ///< Fixed codebook index (size in bits)

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
    uint8_t input_frame_size; ///< Size (in bytes) of input frame
    uint8_t output_frame_size;///< Size (in bytes) of output frame
    uint8_t fc_index_bits;    ///< Size (in bits) of fixed codebook index entry
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
    int format;                 ///< format index from formats array
    int subframe_size;          ///< number of samples produced from one subframe
    int data_error;             ///< data error detected during decoding
    int bad_pitch;              ///< parity check failed
    /// past excitation signal buffer
    int16_t exc_base[2*MAX_SUBFRAME_SIZE+PITCH_MAX+INTERPOL_LEN];
    int16_t* exc;               ///< start of past excitation data in buffer
    int intT2_prev;             ///< int(T2) value of previous frame (4.1.3)
    int16_t lq_prev[MA_NP][10]; ///< (Q13) LSP quantizer output (3.2.4)
    int16_t lsp_prev[10];       ///< (Q15) LSP coefficients from previous frame (3.2.5)
    int16_t lsf_prev[10];       ///< (Q13) LSF coefficients from previous frame
    int16_t pred_energ_q[4];    ///< (Q10) past quantized energies
    int16_t gain_pitch;         ///< (Q14) Pitch gain of previous subframe (3.8) [SHARP_MIN ... SHARP_MAX]
    int16_t gain_code;          ///< (Q1) Gain code of previous subframe
    int16_t pitch_sharp;        ///< pitch sharpening of the previous frame
    /// Residual signal buffer (used in long-term postfilter)
    int16_t residual[MAX_SUBFRAME_SIZE+PITCH_MAX];
    int16_t syn_filter_data[10];
    int16_t res_filter_data[10];
    int16_t pos_filter_data[10];///< previous speech data for postfilter
    int16_t ht_prev_data;       ///< previous data for 4.2.3, equation 86
    int16_t g;                  ///< gain coefficient (4.2.4)
    uint16_t rand_value;        ///< random number generator value (4.4.4)
    int prev_mode;              ///< L0 from previous frame
    //High-pass filter data
    int hpf_f1;
    int hpf_f2;
    int16_t hpf_z0;
    int16_t hpf_z1;
    int subframe_idx;           ///< subframe index (for debugging)
}  G729A_Context;

//Stability constants (3.2.4)
#define LSFQ_MIN    40 //0.005 in Q13
#define LSFQ_MAX 25681 //3.135 in Q13

#define LSFQ_DIFF_MIN 321 //0.0391 in Q13

/* Gain pitch maximum and minimum (3.8) */
#define SHARP_MIN  3277 //0.2 in Q14
#define SHARP_MAX 13017 //0.8 in Q14

/* 4.2.2 */
#define GAMMA_N 18022 //0.55 in Q15
#define GAMMA_D 22938 //0.70 in Q15
#define GAMMA_T 26214 //0.80 in Q15

/* 4.2.1 */
#define GAMMA_P 16384 //0.50  in Q15

#define Q12_BASE 4096.0
#define Q13_BASE 8192.0
#define Q15_BASE 32768.0

/**
 * L1 codebook (10-dimensional, with 128 entries (3.24)
 */
static const int16_t cb_L1[1<<L1_BITS][10] =
{ /* Q13 */
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
{ /* Q13 */
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
{ /* Q15 */
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
static const int16_t cb_GA[GA_CB_SIZE][2] =
{ /* Q14      Q13 */
  { 3242 ,  9949 }, //5
  { 1551 ,  2425 }, //1
  { 2678 , 27162 }, //7
  { 1921 ,  9291 }, //4
  { 1831 ,  5022 }, //2
  {    1 ,  1516 }, //0
  {  356 , 14756 }, //6
  {   57 ,  5404 }, //3
};

/**
 * GB codebook (3.9.2)
 */
#define GB_CB_SIZE (1<<GB_BITS)
static int16_t cb_GB[GB_CB_SIZE][2] =
{ /* Q14       Q13 */
  {  5142 ,   592 }, //2
  { 17299 ,  1861 }, //14
  {  6160 ,  2395 }, //3
  { 16112 ,  3392 }, //13
  {   826 ,  2005 }, //0
  { 18973 ,  5935 }, //15
  {  1994 ,     0 }, //1
  { 15434 ,   237 }, //12
  { 10573 ,  2966 }, //6
  { 15132 ,  4914 }, //10
  { 11569 ,  1196 }, //7
  { 14194 ,  1630 }, //9
  {  8091 ,  4861 }, //4
  { 15161 , 14276 }, //11
  {  9120 ,   525 }, //5
  { 13260 ,  3256 }, //8
};

/**
 * MA predictor (3.2.4)
 */
static const int16_t ma_predictor[2][MA_NP][10] =
{ /* Q15 */
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
{ /* Q15 */
  { 7798,  8447,  8205,  8293,  8126,  8477,  8447,  8703,  9043,  8604},
  {14585, 18333, 19772, 17344, 16426, 16459, 15155, 15220, 16043, 15708}
};

static const int16_t ma_predictor_sum_inv[2][10] =
{ /* Q12 */
  {17210, 15888, 16357, 16183, 16516, 15833, 15888, 15421, 14840, 15597},
  { 9202,  7320,  6788,  7738,  8170,  8154,  8856,  8818,  8366,  8544}
};

/**
 * MA prediction coefficients (3.9.1, near Equation 69)
 * values are multiplied by 100
 */
static const uint16_t ma_prediction_coeff[4] =
{ /* Q13 */
  5571, 4751, 2785, 1556
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

/**
 * Cosine table: base_cos[i]=cos((i+1)*PI/64)
 */
static const int16_t base_cos[64] =
{ /* Q15 */
  32767,  32729,  32610,  32413,  32138,  31786,  31357,  30853,
  30274,  29622,  28899,  28106,  27246,  26320,  25330,  24279,
  23170,  22006,  20788,  19520,  18205,  16846,  15447,  14010,
  12540,  11039,   9512,   7962,   6393,   4808,   3212,   1608,
      0,  -1608,  -3212,  -4808,  -6393,  -7962,  -9512, -11039,
 -12540, -14010, -15447, -16846, -18205, -19520, -20788, -22006,
 -23170, -24279, -25330, -26320, -27246, -28106, -28899, -29622,
 -30274, -30853, -31357, -31786, -32138, -32413, -32610, -32729 
};

/**
 * Sslope used to compute y = cos(x)
 *
 * cos(ind*64+offset) = base_cos[ind]+offset*slope_cos[ind]
 */
static const int16_t slope_cos[64] =
{ /* Q19 */
   -632,  -1893,  -3150,  -4399,  -5638,  -6863,  -8072,  -9261,
 -10428, -11570, -12684, -13767, -14817, -15832, -16808, -17744,
 -18637, -19486, -20287, -21039, -21741, -22390, -22986, -23526,
 -24009, -24435, -24801, -25108, -25354, -25540, -25664, -25726,
 -25726, -25664, -25540, -25354, -25108, -24801, -24435, -24009,
 -23526, -22986, -22390, -21741, -21039, -20287, -19486, -18637,
 -17744, -16808, -15832, -14817, -13767, -12684, -11570, -10428,
  -9261,  -8072,  -6863,  -5638,  -4399,  -3150,  -1893,   -632 
};

/**
 * Table used to compute pow(2, x)
 *
 * tab_pow2[i] = pow(2, i/32) = 2^(i/32) i=0..32
 */
static const uint16_t tab_pow2[33] =
{ /* Q14 */
  16384, 16743, 17109, 17484, 17867, 18258, 18658, 19066, 19484, 19911,
  20347, 20792, 21247, 21713, 22188, 22674, 23170, 23678, 24196, 24726,
  25268, 25821, 26386, 26964, 27554, 28158, 28774, 29405, 30048, 30706,
  31379, 32066, 32767
};

/**
 * Table used to compute log2(x)
 *
 * tab_log2[i] = log2(1 + i/32), i=0..32
 */
static const uint16_t tab_log2[33] =
{ /* Q15 */
     0,  1455,  2866,  4236,  5568,  6863,  8124,  9352, 10549, 11716,
 12855, 13967, 15054, 16117, 17156, 18172, 19167, 20142, 21097, 22033,
 22951, 23852, 24735, 25603, 26455, 27291, 28113, 28922, 29716, 30497,
 31266, 32023, 32767
};

/**
 * Table used to compute 1/sqrt(x)
 *
 * tab_inv_sqrt[i] = 1/sqrt((16+i)/64)
 */
static const uint16_t tab_inv_sqrt[49] =
{ /* Q!4 */
 32767, 31790, 30894, 30070, 29309, 28602, 27945, 27330, 26755, 26214,
 25705, 25225, 24770, 24339, 23930, 23541, 23170, 22817, 22479, 22155,
 21845, 21548, 21263, 20988, 20724, 20470, 20225, 19988, 19760, 19539,
 19326, 19119, 18919, 18725, 18536, 18354, 18176, 18004, 17837, 17674,
 17515, 17361, 17211, 17064, 16921, 16782, 16646, 16514, 16384
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
 * \note this code is bit-equal to reference's Mpy_32_16
 */
static inline int mul_24_15(int var_q24, int16_t var_q15)
{
    int hi = var_q24 >> 15;
    int lo = var_q24 & 0x7fff;
    return var_q15 * hi + ((var_q15 * lo) >> 15);
}

/**
 * \brief shift to right with rounding
 * \param var1 32-bit integer to shift
 * \param var2 16-bit shift
 */
static int l_shr_r(int var1, int16_t var2)
{
    if(var1 && (var1 & (1 << (var2 - 1))))
        return (var1 >> var2) + 1;
    else
        return (var1 >> var2);
}

/**
 * \brief Calculates 2^x
 * \param arg (Q15) power (>=0)
 *
 * \return (Q0) result of pow(2, power)
 *
 * \note If integer part of power is greater than 15, function
 *       will return INT_MAX
 */
static int l_pow2(int power)
{
    uint16_t frac_x0;
    uint16_t frac_dx;
    uint16_t power_int = power >> 15;
    int result;

    assert(power>=0);

    if(power_int > 30 )
        return INT_MAX; // overflow

    /*
      power in Q15, thus
      b31-b15 - integer part
      b00-b14 - fractional part      

      When fractional part is treated as Q10,
      bits 10-14 are integer part, 00-09 - fractional
      
    */
    frac_x0 = (power & 0x7c00) >> 10; // b10-b14 and Q10 -> Q0
    frac_dx = (power & 0x03ff) << 5;  // b00-b09 and Q10 -> Q15 

    result = tab_pow2[frac_x0] << 15; // Q14 -> Q29;
    result += frac_dx * (tab_pow2[frac_x0+1] - tab_pow2[frac_x0]); // Q15*Q14;

    // multiply by 2^power_int and Q29 -> Q1
    result >>= 28 - power_int;
    // rounding
    result++;
    
    return result >> 1; // Q1 -> Q0
}

/**
 * \brief Calculates log2(x)
 * \param value function argument (>0)
 *
 * \return (Q15) result of log2(value)
 */
static int l_log2(int value)
{
    int result;
    int power_int;
    uint16_t frac_x0;
    uint16_t frac_dx;

    assert(value > 0);

    // Stripping zeros from beginning ( ?? -> Q31)
    result=value;    
    for(power_int=31; power_int>=0 && !(result & 0x80000000); power_int--)
        result <<= 1;

    /*
      After normalization :
      b31 - integer part (always nonzero)
      b00-b30 - fractional part

      When fractional part is treated as Q26,
      bits 26-31 are integer part, 16-25 - fractional
    */
    frac_x0 = (result & 0x7c000000) >> 26; // b26-b31 and [32..63] -> [0..31]  then Q26 -> Q0
    frac_dx = (result & 0x03fff800) >> 11; // b11-b25 and Q26 -> Q15, [0..1) in Q15

    result = tab_log2[frac_x0] << 15; // Q15 -> Q30
    result += frac_dx * (tab_log2[frac_x0+1] - tab_log2[frac_x0]); // Q15*Q15;

    result >>= 15; // Q30 -> Q15

    result += power_int << 15; // Q0 -> Q15

    return result;
}

/**
 * \brief Computes 1/sqrt(x)
 * \param arg (Q0) positive integer
 *
 * \return (Q29) 0 < 1/sqrt(arg) <= 1
 */
static int l_inv_sqrt(int arg)
{
    uint32_t result;
    uint16_t frac_x0;
    uint16_t frac_dx;
    int8_t power_int;

    assert(arg > 0);

    result=arg;    
    for(power_int=16; power_int>=0 && !(result & 0xc0000000); power_int--)
        result <<= 2;
    /*
      When result is treated as Q26,
      bits 26-31 are integer part, 16-25 - fractional
    */
    frac_x0 = (result >> 26) - 16; // b26-b31 and [16..63] -> [0..47] 
    frac_dx = (result >> 11) & 0x7fe0; // b16-b25 and Q26 -> Q15 [0..1) in Q15

    result = tab_inv_sqrt[frac_x0] << 15; // Q15 -> Q30
    result += frac_dx * (tab_inv_sqrt[frac_x0+1] - tab_inv_sqrt[frac_x0]); // Q15*Q15;

    return result >> power_int;
}

/**
 * \brief divide two positive fixed point numbers
 * \param num numenator
 * \param denom denumenator
 * \param base base to scale result to
 *
 * \return result of division scaled to given base
 *
 * \remark numbers should in same base, result will be scaled to given base
 *
 * \todo Better implementation requred
 */
int l_div(int num, int denom, int base)
{
    int diff =0;
    int sig=0;

    assert(denom);

    if(!num)
        return 0;

    if(num < 0)
    {
        num = -num;
        sig = !sig;
    }

    if(denom < 0)
    {
        denom = -denom;
        sig = !sig;
    }

    for(; num < 0x4000000; diff++)
        num <<= 1;

    for(; denom < 0x4000000; diff--)
        denom <<= 1;

    if(diff > base)
        num >>= diff-base;
    else
        denom >>= base-diff;

    if(sig)
        return -num/denom;
    else
        return num/denom;
}

/**
 * \brief Calculates sum of array elements multiplications
 * \param speech array with input data
 * \param cycles number elements to proceed
 * \param offset offset for calculation sum of s[i]*s[i+offset]
 * \param shift right shift by this value will be done before multiplication
 *
 * \return sum of multiplications
 *
 * \note array must be at least length+offset long!
 */
static int sum_of_squares(const int16_t* speech, int cycles, int offset, int shift)
{
    int n;
    int sum=0;

    if(offset < 0)
        return 0;

    for(n=0; n<cycles; n++)
       sum += (speech[n] >> shift) * (speech[n + offset] >> shift);

    return sum;
}

/**
 * \brief rounding to nearest
 * \param value (Q16) 32-bit fixed-point value for rounding
 *
 * \return (Q0) 16-bit integer
 */
static int16_t g729_round(int value)
{
    if(value > INT_MAX-0x8000) // Overflow
        return SHRT_MAX;

    return (value + 0x8000) >> 16;
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
   return ((0x6996966996696996ULL >> (P1 >> 2)) ^ P0) & 1;
}

/**
 * \brief Decoding of the adaptive-codebook vector (4.1.3)
 * \param pitch_delay_int pitch delay, integer part
 * \param pitch_delay_frac pitch delay, fraction part [-1, 0, 1]
 * \param ac_v [out] (Q0) buffer to store decoded vector into
 * \param subframe_size length of subframe
 */
static void g729_decode_ac_vector(int pitch_delay_int, int pitch_delay_frac, int16_t* ac_v, int subframe_size)
{
    int n, i;
    int v;

    //Make sure that pitch_delay_frac will be always positive
    pitch_delay_frac =- pitch_delay_frac;
    if(pitch_delay_frac < 0)
    {
        pitch_delay_frac += 3;
        pitch_delay_int++;
    }

    //t [0, 1, 2]
    //k [PITCH_MIN-1; PITCH_MAX]
    for(n=0; n<subframe_size; n++)
    {
        /* 3.7.1, Equation 40 */
        v=0;
        for(i=0; i<10; i++)
        {
            /*  R(x):=ac_v[-k+x] */
            v+=ac_v[n - pitch_delay_int - i    ] * interp_filter[i][    pitch_delay_frac]; //R(n-i)*b30(t+3i)
            v+=ac_v[n - pitch_delay_int + i + 1] * interp_filter[i][3 - pitch_delay_frac]; //R(n+i+1)*b30(3-t+3i)
        }
        v = FFMIN(FFMAX(v, SHRT_MIN << 15), SHRT_MAX << 15);
        ac_v[n] = g729_round(v << 1);
    }
}

/**
 * \brief Decoding fo the fixed-codebook vector (3.8)
 * \param fc_index Fixed codebook index
 * \param fc_index_its number of bits per index entry
 * \param pulses_signs Signs of the excitation pulses (0 bit value means negative sign)
 * \param fc_v [out] (Q13) decoded fixed codebook vector
 * \param subframe_size length of subframe
 *
 * \return 1 if data error occured
 *
 * bit allocations:
 *   8k mode: 3+3+3+1+3
 * 4.4k mode: 4+4+4+1+4 (non-standard)
 *
 * FIXME: error handling required
 */
static int g729_decode_fc_vector(int fc_index, int fc_index_bits, int pulses_signs, int16_t* fc_v, int subframe_size)
{
    int i;
    int index;
    int mask=(1 << fc_index_bits) - 1;

    memset(fc_v, 0, sizeof(int16_t) * subframe_size);

    /* reverted Equation 62 and Equation 45 */
    for(i=0; i<FC_PULSE_COUNT-1; i++)
    {
        index=(fc_index & mask) * 5 + i;
        //overflow can occur in 4.4k case
        if(index>=subframe_size)
            return 1;

        fc_v[ index ] = (pulses_signs & 1) ? 8191 : -8192; // +/-1 in Q13
        fc_index>>=fc_index_bits;
        pulses_signs>>=1;
    }
    index=((fc_index>>1) & mask) * 5 + i + (fc_index & 1);
    //overflow can occur in 4.4k case
    if(index>=subframe_size)
        return 1;

    fc_v[ index ] = (pulses_signs & 1) ? 8191 : -8192;
    return 0;
}

/**
 * \brief fixed codebook vector modification if delay is less than 40 (4.1.4 and 3.8)
 * \param pitch_delay integer part of pitch delay
 * \param gain_pitch (Q14) gain pitch
 * \param fc_v [in/out] (Q13) fixed codebook vector to change
 * \param length length of fc_v array
 */
static void g729_fix_fc_vector(int pitch_delay, int16_t gain_pitch, int16_t* fc_v, int length)
{
    int i;

    for(i=pitch_delay; i<length;i++)
        fc_v[i] += (fc_v[i - pitch_delay] * gain_pitch) >> 14;
}

/**
 * \brief Attenuation of the memory of the gain predictor (4.4.3)
 * \param pred_energ_q [in/out] (Q10) past quantized energies
 */
static void g729_update_gain_erasure(int16_t *pred_energ_q)
{
    int avg_gain=pred_energ_q[3]; // Q10
    int i;

    /* 4.4.3. Equation 95 */
    for(i=3; i>0; i--)
    {
        avg_gain       += pred_energ_q[i-1];
        pred_energ_q[i] = pred_energ_q[i-1];
    }
    pred_energ_q[0] = FFMAX((avg_gain >> 2) - 4096, -14336); // -14 in Q10
}

/**
 * \brief Decoding of the adaptive codebook gain (4.1.5 and 3.9.1)
 * \param ctx private data structure
 * \param ga_cb_index GA gain codebook index (stage 2)
 * \param gb_cb_index GB gain codebook (stage 2)
 * \param fc_v (Q13) fixed-codebook vector
 * \param pred_energ_q [in/out] (Q10) past quantized energies
 * \param subframe_size length of subframe
 *
 * \return (Q1) quantized adaptive-codebook gain (gain code)
 */
static int16_t g729_get_gain_code(int ga_cb_index, int gb_cb_index, const int16_t* fc_v, int16_t* pred_energ_q, int subframe_size)
{
    int i;
    int cb1_sum; // Q12
    int energy;
    int exp;

    /* 3.9.1, Equation 66 */
    energy = sum_of_squares(fc_v, subframe_size, 0, 0);

    /*
      energy=mean_energy-E
      mean_energy=30dB
      E is calculated in 3.9.1 Equation 66

      (energy is in Q26)
      energy = 30 - 10 * log10(energy / (2^26 * subframe_size))
      =30 - 10 * log2(energy / (2^26 * subframe_size)) / log2(10)
      =30 - 10*log2(energy/2^26)/log2(10) + 10*log2(subframe_size)/log2(10)
      =30 - [10/log2(10)] * log2(energy/2^26) + [10/log2(10)] * log2(subframe_size)
      = -24660 * log2(energy) + 24660 * log2(subframe_size) + 24660 * 26 + 30<<13
      
      24660 = 10/log2(10) in Q13
    */
    energy =  mul_24_15(l_log2(energy),    -24660); // Q13
    energy += mul_24_15(l_log2(subframe_size), 24660); // Q13
    energy += mul_24_15(26 << 15,              24660); // Q13
    energy += 30 << 13;

    // FIXME: Compensation. Makes result bit-equal with reference code
    energy -= 2;

    energy <<= 10; // Q14 -> Q23
    /* 3.9.1, Equation 69 */
    for(i=0; i<4; i++)
        energy += pred_energ_q[i] * ma_prediction_coeff[i];

    /* 3.9.1, Equation 71 */
    /*
      energy = 10^(energy / 20) = 2^(3.3219 * energy / 20) = 2^ (0.166 * energy)
      5439 = 0.166 in Q15
    */
    energy = (5439 * (energy >> 15)) >> 8; // Q23->Q8, Q23 -> Q15

    /* 
      Following code will calculate energy*2^14 instead of energy*2^exp
      due to recent change of energy_int's integer part.
      This is done to avoid overflow. Result fits into 16-bit.
    */
    exp = (energy >> 15);             // integer part (exponent)
    energy += (14-exp) << 15;         // replacing integer part (exponent) with 14
    energy = l_pow2(energy) & 0x7fff; // Only fraction part of Q15

    // shift prediction error vector
    for(i=3; i>0; i--)
        pred_energ_q[i]=pred_energ_q[i-1];

    cb1_sum = cb_GA[ga_cb_index][1] + cb_GB[gb_cb_index][1]; // Q13

    /* 3.9.1, Equation 72 */
    /*
      pred_energ_q[0] = 20*log10(cb1_sum) in Q12
      24660 = 10/log2(10) in Q13
    */
    pred_energ_q[0] = (24660 * ((l_log2(cb1_sum) >> 2) - (13 << 13))) >> 15;
    energy *= cb1_sum >> 1; // energy*2^14 in Q12

    // energy*2^14 in Q12 -> energy*2^exp in Q1
    if(25-exp > 0)
        energy >>= 25-exp;
    else
        energy <<= exp-25;

    return energy;
}

/**
 * \brief Memory update (3.10)
 * \param fc_v (Q13) fixed-codebook vector
 * \param gp (Q14) quantized fixed-codebook gain (gain pitch)
 * \param gc (Q1) quantized adaptive-codebook gain (gain code)
 * \param exc [in/out] (Q0) last excitation signal buffer for current subframe
 * \param subframe_size length of subframe
 */
static void g729_mem_update(const int16_t *fc_v, int16_t gp, int16_t gc, int16_t* exc, int subframe_size)
{
    int i, sum;

    for(i=0; i<subframe_size; i++)
    {
        sum = exc[i] * gp + fc_v[i] * gc;
        sum = FFMAX(FFMIN(sum, SHRT_MAX << 14), SHRT_MIN << 14);
        exc[i] = g729_round(sum << 2);
    }
}

/**
 * \brief LP synthesis filter
 * \param lp (Q12) filter coefficients
 * \param in (Q0) input signal
 * \param out [out] (Q0) output (filtered) signal
 * \param filter_data [in/out] (Q0) filter data array (previous synthesis data)
 * \param subframe_size length of subframe
 * \param exit_on_overflow 1 - If overflow occured routine updates neither out nor
 *                         filter data arrays, 0 - always update
 *
 * \return 1 if overflow occured, o - otherwise
 *
 * Routine applies 1/A(z) filter to given speech data
 */
static int g729_lp_synthesis_filter(const int16_t* lp, const int16_t *in, int16_t *out, int16_t *filter_data, int subframe_size, int exit_on_overflow)
{
    int16_t tmp_buf[MAX_SUBFRAME_SIZE+10];
    int16_t* tmp=tmp_buf+10;
    int i,n;
    int sum;

    memcpy(tmp_buf, filter_data, 10*sizeof(int16_t));

    for(n=0; n<subframe_size; n++)
    {
        sum = in[n] << 12;
        for(i=0; i<10; i++)
            sum -= (lp[i] * tmp[n-i-1]);
	sum >>= 12;
	if(sum > SHRT_MAX || sum < SHRT_MIN)
	{
            if(exit_on_overflow)
                return 1;
            sum = FFMAX(FFMIN(sum, SHRT_MAX), SHRT_MIN);
	}
        tmp[n] = sum;
    }

    memcpy(filter_data, tmp + subframe_size - 10, 10*sizeof(int16_t));
    memcpy(out, tmp, subframe_size*sizeof(int16_t));

    return 0;
}

/**
 * \brief Adaptive gain control (4.2.4)
 * \param gain_before (Q0) gain of speech before applying postfilters
 * \param gain_after  (Q0) gain of speech after applying postfilters
 * \param speech [in/out] (Q0) signal buffer
 * \param subframe_size length of subframe
 * \param gain_prev previous value of gain coefficient
 *
 * \return last value of gain coefficient
 */
static int16_t g729a_adaptive_gain_control(int gain_before, int gain_after, int16_t *speech, int subframe_size, int16_t gain_prev)
{
    int gain; // Q12
    int n;

    if(!gain_after)
        return;

    if(gain_before)
    {
        gain = l_div(gain_after,gain_before,12); // Q12
        gain = l_inv_sqrt(gain) >> 11; // Q23 -> Q12
    }
    else
        gain = 0;

    for(n=0; n<subframe_size; n++)
    {
        // 0.9 * ctx->g + 0.1 * gain
        gain_prev = (29491 * gain_prev + 3276 * gain) >> 15;
        speech[n] = (speech[n] * gain_prev) >> 12;
    }
    return gain_prev;
}

/**
 * \brief Calculates coefficients of weighted A(z/GAMMA) filter
 * \param Az (Q12) source filter
 * \param gamma (Q15) weight coefficients
 * \param Azg [out] (Q12) resulted weighted A(z/GAMMA) filter
 *
 * Azg[i]=GAMMA^i*Az[i] , i=0..subframe_size
 */
static void g729a_weighted_filter(const int16_t* Az, int16_t gamma, int16_t *Azg)
{
    int gamma_pow = gamma;
    int n;

    for(n=0; n<10; n++)
    {
        Azg[n] = (Az[n] * gamma_pow) >> 15;
        gamma_pow = (gamma_pow * gamma) >> 15;
    }
}

/**
 * \brief long-term postfilter (4.2.1)
 * \param intT1 integer part of the pitch delay T1 in the first subframe
 * \param residual (Q0) input data to filtering
 * \param residual_filt [out] (Q0) speech signal with applied A(z/GAMMA_N) filter
 * \param subframe_size size of subframe
 */
static void g729a_long_term_filter(int intT1, const int16_t* residual, int16_t *residual_filt, int subframe_size)
{
    int k, n, intT0;
    int gl;      // gain coefficient for long-term postfilter
    int corr_t0; // correlation of residual signal with delay intT0
    int corr_0;  // correlation of residual signal with delay 0
    int correlation, corr_max;
    int inv_glgp;      // Q15 1.0/(1+gl*GAMMA_P)
    int glgp_inv_glgp; // Q15 gl*GAMMA_P/(1+gl*GAMMA_P);
    int tmp;

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
        /* 4.2.1, Equation 80 */
        correlation = sum_of_squares(residual + PITCH_MAX - k, subframe_size, k, 1);
        if(correlation>corr_max)
        {
            corr_max=correlation;
            intT0=k;
        }
    }

    corr_t0 = sum_of_squares(residual + PITCH_MAX - intT0, subframe_size, 0, 1);
    corr_0  = sum_of_squares(residual + PITCH_MAX,         subframe_size, 0, 1);

    //Downscaling corellaions to fit on 16-bit
    tmp = FFMAX(corr_0, FFMAX(corr_t0, corr_max));
    for(n=0; n<32 && tmp > SHRT_MAX; n++)
    {
        corr_t0 >>=1;
        corr_0 >>=1;
        corr_max >>=1;
        tmp >>=1;
    }

    /* 4.2.1, Equation 82. checking if filter should be disabled */
    if(corr_max * corr_max < (corr_0 * corr_t0) >> 1)
        gl = 0;
    else if(!corr_t0 || corr_max > corr_t0)
        gl = 32768; // 1.0 in Q15
    else
        gl=l_div(corr_max, corr_t0, 15);

    gl = (gl * GAMMA_P) >> 15;

    if (gl < -32768) // -1.0 in Q15
        inv_glgp = 0;
    else
        inv_glgp = l_div(32768, 32768 + gl, 15); // 1/(1+gl) in Q15

    glgp_inv_glgp = 32768 - inv_glgp; // 1.0 in Q15

    /* 4.2.1, Equation 78, reconstructing delayed signal */
    for(n=0; n<subframe_size; n++)
        residual_filt[n] = (residual[n + PITCH_MAX        ] * inv_glgp +
                            residual[n + PITCH_MAX - intT0] * glgp_inv_glgp) >> 15;
}

/**
 * \brief compensates the tilt in the short-term postfilter (4.2.3)
 * \param ctx private data structure
 * \param lp_gn (Q12) coefficients of A(z/GAMMA_N) filter
 * \param lp_gd (Q12) coefficients of A(z/GAMMA_D) filter
 * \param res_pst [in/out] (Q0) residual signal (partially filtered)
*/
static void g729a_tilt_compensation(G729A_Context *ctx, const int16_t *lp_gn, const int16_t *lp_gd, int16_t* res_pst)
{
    int tmp;
    int gt;      // Q12
    int rh1,rh0; // Q12
    int16_t hf_buf[11+22]; // Q12 A(Z/GAMMA_N)/A(z/GAMMA_D) filter impulse response
    int sum;
    int i, n;
    
    memset(hf_buf, 0, 33 * sizeof(int16_t));

    hf_buf[10] = 4096; //1.0 in Q12
    for(i=0; i<10; i++)
        hf_buf[i+11] = lp_gn[i];

    /* Applying 1/A(z/GAMMA_D) to hf */
    for(n=0; n<22; n++)
    {
        sum=hf_buf[n+10];
        for(i=0; i<10; i++)
            sum -= (lp_gd[i] * hf_buf[n+10-i-1]) >> 12;
        hf_buf[n+10] = sum;
    }

    /* Now hf_buf (starting with 10) contains impulse response of A(z/GAMMA_N)/A(z/GAMMA_D) filter */

    /* A.4.2.3, Equation A.14, calcuating rh(0)  */
    rh0 = sum_of_squares(hf_buf+10, 22, 0, 0) >> 12;   // Q24 -> Q12

    /* A.4.2.3, Equation A.14, calcuating rh(1)  */
    rh1 = sum_of_squares(hf_buf+10, 22-1, 1, 0) >> 12; // Q24 -> Q12

    rh1 = rh1 * GAMMA_T >> 15; // Q12 * Q15 = Q27 -> Q12

    /* A.4.2.3, Equation A.14 */
    if(rh1>0)
        gt = -l_div(rh1, rh0, 12); // l_div accepts only positive parameters
    else
        gt = 0;

    /* A.4.2.3. Equation A.13, applying filter to signal */
    tmp=res_pst[ctx->subframe_size-1];

    for(i=ctx->subframe_size-1; i>=1; i--)
        res_pst[i] += (gt * res_pst[i-1]) >> 12;
    res_pst[0] += (gt * ctx->ht_prev_data) >> 12;

    ctx->ht_prev_data=tmp;
}

/**
 * \brief Residual signal calculation (4.2.1)
 * \param lp (Q12) A(z/GAMMA_N) filter coefficients
 * \param speech (Q0) input speech data
 * \param residual [out] (Q0) output data filtered through A(z/GAMMA_N)
 * \param subframe_size size of one subframe
 * \param pos_filter_data [in/out] (Q0) speech data of previous subframe
 */
static void g729_residual(int16_t* lp, const int16_t* speech, int16_t* residual, int subframe_size, int16_t* pos_filter_data)
{
    int i, n, sum;
    int16_t tmp_speech_buf[MAX_SUBFRAME_SIZE+10];
    int16_t *tmp_speech=tmp_speech_buf+10;

    // Copying data from previous frame
    for(i=0; i<10; i++)
        tmp_speech[-10+i] = pos_filter_data[i];

    // Copying the rest of speech data
    for(i=0; i<subframe_size; i++)
        tmp_speech[i] = speech[i];
    /*
      4.2.1, Equation 79 Residual signal calculation
      ( filtering through A(z/GAMMA_N) , one half of short-term filter)
    */
    for(n=0; n<subframe_size; n++)
    {
        sum = tmp_speech[n] << 12;
        for(i=0; i<10; i++)
            sum += lp[i] * tmp_speech[n-i-1];
        sum = FFMAX(FFMIN(sum, SHRT_MAX << 12), SHRT_MIN << 12);
        residual[n+PITCH_MAX] = g729_round(sum << 4);
    }

    // Save data for using in next subframe
    for(i=0; i<10; i++)
        pos_filter_data[i] = speech[subframe_size-10+i];
}

/**
 * \brief Signal postfiltering (4.2, with A.4.2 simplification)
 * \param ctx private data structure
 * \param lp (Q12) LP filter coefficients
 * \param pitch_delay_int integer part of the pitch delay T1 of the first subframe
 * \param speech [in/out] (Q0) signal buffer
 *
 * Filtering has following  stages:
 *   Long-term postfilter (4.2.1)
 *   Short-term postfilter (4.2.2).
 *   Tilt-compensation (4.2.3)
 *   Adaptive gain control (4.2.4)
 *
 * \note This routine is G.729 Annex A specific.
 */
static void g729a_postfilter(G729A_Context *ctx, const int16_t *lp, int pitch_delay_int, int16_t *speech)
{
    int16_t residual_filt_buf[MAX_SUBFRAME_SIZE+10];
    int16_t* residual_filt=residual_filt_buf+10;
    int16_t lp_gn[10]; // Q12
    int16_t lp_gd[10]; // Q12
    int gain_before, gain_after;

    /* Calculating coefficients of A(z/GAMMA_N) filter */
    g729a_weighted_filter(lp, GAMMA_N, lp_gn);
    /* Calculating coefficients of A(z/GAMMA_D) filter */
    g729a_weighted_filter(lp, GAMMA_D, lp_gd);

    /* Calculating gain of unfiltered signal for using in AGC */
    gain_before=sum_of_squares(speech, ctx->subframe_size, 0, 4);

    /* Residual signal calculation (one-half of short-term postfilter) */
    g729_residual(lp_gn, speech, ctx->residual, ctx->subframe_size, ctx->pos_filter_data);

    /* long-term filter (A.4.2.1) */
    g729a_long_term_filter(pitch_delay_int, ctx->residual, residual_filt, ctx->subframe_size);

    //Shift residual for using in next subframe
    memmove(ctx->residual, ctx->residual + ctx->subframe_size, PITCH_MAX*sizeof(int16_t));

    /* short-term filter tilt compensation (A.4.2.3) */
    g729a_tilt_compensation(ctx, lp_gn, lp_gd, residual_filt);

    /* Applying second half of short-term postfilter: 1/A(z/GAMMA_D)*/
    g729_lp_synthesis_filter(lp_gd, residual_filt, speech, ctx->res_filter_data, ctx->subframe_size, 0);

    /* Calculating gain of filtered signal for using in AGC */
    gain_after=sum_of_squares(speech, ctx->subframe_size, 0, 4);

    /* adaptive gain control (A.4.2.4) */
    ctx->g = g729a_adaptive_gain_control(gain_before, gain_after, speech, ctx->subframe_size, ctx->g);
}

/**
 * \brief high-pass filtering and upscaling (4.2.5)
 * \param ctx private data structure
 * \param speech [in/out] reconstructed speech signal for applying filter to
 * \param length size of input data
 *
 * Filter has cut-off frequency 100Hz
 */
static void g729_high_pass_filter(G729A_Context* ctx, int16_t* speech, int length)
{
    int16_t z_2=0;
    int f_0=0;
    int i;

    for(i=0; i<length; i++)
    {
        z_2=ctx->hpf_z1;
        ctx->hpf_z1=ctx->hpf_z0;
        ctx->hpf_z0=speech[i];

        f_0 = mul_24_15(ctx->hpf_f1, 15836)
            + mul_24_15(ctx->hpf_f2, -7667)
            +  7699 * ctx->hpf_z0
            - 15398 * ctx->hpf_z1
            +  7699 * z_2;
	f_0 <<= 2; // Q13 -> Q15

        speech[i] = FFMAX(FFMIN(f_0 >> 14, SHRT_MAX), SHRT_MIN); // 2*f_0 in 15
	
        ctx->hpf_f2=ctx->hpf_f1;
        ctx->hpf_f1=f_0;
    }
}

/**
 * \brief Convert LSF to LSP
 * \param lsf (Q13) LSF coefficients (0 <= lsf < PI)
 * \param lsp [out] (Q15) LSP coefficients (-1 <= lsp < 1)
 *
 * \remark It is safe to pass the same array in lsf and lsp parameters
 */
static void g729_lsf2lsp(const int16_t *lsf, int16_t *lsp)
{
    int i;

    /* Convert LSF to LSP, lsp=cos(lsf) */
    for(i=0;i<10; i++)
    {
        int16_t freq= (lsf[i] * 20861)>>15; //1.0/(2.0*PI) in Q17, result in Q16
        int16_t offset= freq & 0xff;
        int16_t ind = FFMIN(freq >> 8, 63);

        lsp[i] = base_cos[ind] + ((slope_cos[ind] * offset) >> 12);
    }
}

/**
 * \brief Restore LSP parameters using previous frame data
 * \param ctx private data structure
 * \param lsfq [out] (Q13) Decoded LSF coefficients
 */
static void g729_lsf_restore_from_previous(G729A_Context *ctx, int16_t* lsfq)
{
    int lq[10]; // Q13, Q28
    int i,k;

    //Restore LSF from previous frame
    for(i=0;i<10; i++)
        lsfq[i]=ctx->lsf_prev[i];

    /* 4.4.1, Equation 92 */
    for(i=0; i<10; i++)
    {
        lq[i]= (int)lsfq[i] << 15; //Q13 -> Q28
        for(k=0;k<MA_NP; k++)
            lq[i] -= ctx->lq_prev[k][i] * ma_predictor[ctx->prev_mode][k][i]; // Q28
        lq[i] >>= 15;
        lq[i] *= ma_predictor_sum_inv[ctx->prev_mode][i];                     // Q12
        lq[i] >>= 12;
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
 * \brief Decode LSP coefficients from L0-L3 (3.2.4)
 * \param ctx private data structure
 * \param L0 Switched MA predictor of LSP quantizer
 * \param L1 First stage vector of quantizer
 * \param L2 Second stage lower vector of LSP quantizer
 * \param L3 Second stage higher vector of LSP quantizer
 * \param lsfq [out] (Q13) Decoded LSP coefficients
 */
static void g729_lsf_decode(G729A_Context* ctx, int16_t L0, int16_t L1, int16_t L2, int16_t L3, int16_t* lsfq)
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
            diff = (lq[i-1] - lq[i] + J[j]) >> 1;
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
                FFSWAP(int16_t, lsfq[i], lsfq[i+1]);

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
static void get_lsp_coefficients(const int16_t* lsp, int* f)
{
    int i, j;

    f[0] = 0x1000000;          // 1.0 in Q24
    f[1] = -lsp[0] << 10;      // *2 and Q15 -> Q24

    for(i=2; i<=5; i++)
    {
        f[i] = f[i-2];

        for(j=i; j>1; j--)
            f[j] -= (mul_24_15(f[j-1], lsp[2*i-2])<<1) - f[j-2];

        f[1] -= lsp[2*i-2]  << 10;
    }
}
/**
 * \brief LSP to LP conversion (3.2.6)
 * \param lsp (Q15) LSP coefficients
 * \param lp [out] (Q12) decoded LP coefficients
 */

static void g729_lsp2lp(const int16_t* lsp, int16_t* lp)
{
    int i;
    int f1[6]; // Q24
    int f2[6]; // Q24

    get_lsp_coefficients(lsp,   f1);
    get_lsp_coefficients(lsp+1, f2);

    /* 3.2.6, Equations 25 and  26*/
    for(i=0;i<5;i++)
    {
        int ff1 = f1[i+1] + f1[i]; // Q24
        int ff2 = f2[i+1] - f2[i]; // Q24

        lp[i]   = l_shr_r(ff1 + ff2, 13);
        lp[9-i] = l_shr_r(ff1 - ff2, 13);
    }
}

/**
 * \brief Interpolate LSP for the first subframe and convert LSP -> LP for both subframes (3.2.5 and 3.2.6)
 * \param (Q15) lsp_2nd LSP coefficients of the second subframe
 * \param (Q15) lsp_prev past LSP coefficients
 * \param lp [out] (Q12) decoded LP coefficients
 */
static void g729_lp_decode(const int16_t* lsp_2nd, int16_t* lsp_prev, int16_t* lp)
{
    int16_t lsp_1st[10]; // Q15
    int i;

    /* LSP values for first subframe (3.2.5, Equation 24)*/
    for(i=0;i<10;i++)
        lsp_1st[i]=(lsp_2nd[i]>>1)+(lsp_prev[i]>>1);

    g729_lsp2lp(lsp_1st, lp);

    /* LSP values for second subframe (3.2.5)*/
    g729_lsp2lp(lsp_2nd, lp+10);

    /* saving LSP coefficients for using in next frame */
    for(i=0;i<10;i++)
        lsp_prev[i]=lsp_2nd[i];
}


/*
-------------------------------------------------------------------------------
          API
------------------------------------------------------------------------------
*/

/**
 * \brief G.729A decoder initialization
 * \param avctx private data structure
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

    if(avctx->channels != 1)
    {
        av_log(avctx, AV_LOG_ERROR, "Only mono sound is suported (requested channels:%d)\n", avctx->channels);
        return AVERROR_NOFMT;
    }
    /*
       subframe size in 2-byte samples

       1st ">>1" : bytes -> samples
       2nd ">>1" : frame -> subframe 
    */
    ctx->subframe_size=formats[ctx->format].output_frame_size>>2;

    assert(ctx->subframe_size > 0 && ctx->subframe_size <= MAX_SUBFRAME_SIZE);

    /* Decoder initialization. 4.3, Table 9 */

    /*
    Pitch gain of previous subframe.

    (EE) This does not comply with specification, but reference
         and Intel decoder uses here minimum sharpen value instead of maximum. */
    ctx->pitch_sharp=SHARP_MIN;

    /* gain coefficient */
    ctx->g = 4096; // 1.0 in Q12

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
        ctx->pred_energ_q[i] = -14336; // -14 in Q10

    avctx->frame_size=2*ctx->subframe_size;
    return 0;
}

/**
 * \brief decode one G.729 frame into PCM samples
 * \param ctx private data structure
 * \param out_frame array for output PCM samples
 * \param out_frame_size maximum number of elements in output array
 * \param parm decoded parameters of the codec
 * \param frame_erasure frame erasure flag
 *
 * \return 2 * subframe_size
 */
static int  g729a_decode_frame_internal(G729A_Context* ctx, int16_t* out_frame, int out_frame_size, G729_parameters *parm, int frame_erasure)
{
    int16_t lp[20];              // Q12
    int16_t lsp[10];             // Q15
    int16_t lsf[10];             // Q13
    int pitch_delay;             // pitch delay
    int16_t fc[MAX_SUBFRAME_SIZE]; // fixed codebooc vector
    int intT1, i, j;

    ctx->data_error = frame_erasure;
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
    g729_lsf2lsp(lsf, lsp);

    g729_lp_decode(lsp, ctx->lsp_prev, lp);

    for(i=0; i<2; i++)
    {

        if(!i)
        {
            // Decoding of the adaptive-codebook vector delay for first subframe (4.1.3)
            if(ctx->bad_pitch || ctx->data_error)
            {
                pitch_delay = 3 * ctx->intT2_prev + 1;

                intT1=FFMIN(ctx->intT2_prev + 1, PITCH_MAX);
            }
            else
            {
                if(parm->ac_index[i] >= 197)
                    pitch_delay = 3 * parm->ac_index[i] - 335;
                else
                    pitch_delay = parm->ac_index[i] + 59;

                intT1=pitch_delay / 3;    //Used in long-term postfilter    
            }
        }
        else
        {
            // Decoding of the adaptive-codebook vector delay for second subframe (4.1.3)
            if(ctx->data_error)
            {
                pitch_delay=3*intT1+1;
                ctx->intT2_prev=FFMIN(intT1+1, PITCH_MAX);
            }
            else
            {
                pitch_delay = parm->ac_index[i] +
                        3*FFMIN(FFMAX(pitch_delay/3-5, PITCH_MIN), PITCH_MAX-9) - 1;
                ctx->intT2_prev = pitch_delay / 3;
            }
        }
        g729_decode_ac_vector(pitch_delay / 3, (pitch_delay%3)-1,
                ctx->exc + i*ctx->subframe_size, ctx->subframe_size);

        if(ctx->data_error)
        {
            parm->fc_indexes[i]   = g729_random(ctx) & 0x1fff;
            parm->pulses_signs[i] = g729_random(ctx) & 0x000f;
        }

        if(g729_decode_fc_vector(parm->fc_indexes[i],
                formats[ctx->format].fc_index_bits,
                parm->pulses_signs[i],
                fc,
                ctx->subframe_size))
            ctx->data_error = 1;

        g729_fix_fc_vector(pitch_delay/3, ctx->pitch_sharp, fc, ctx->subframe_size);
        if(ctx->data_error)
        {
            /*
                Decoding of the adaptive and fixed codebook gains
                from previous subframe (4.4.2)
            */

            /* 4.4.2, Equation 94 */
            ctx->gain_pitch = (14745 * FFMIN(ctx->gain_pitch, 16384)) >> 14; // 0.9 and 1.0 in Q14
            /* 4.4.2, Equation 93 */
            ctx->gain_code  = (8028 * ctx->gain_code) >> 13; // 0.98 in Q13

            g729_update_gain_erasure(ctx->pred_energ_q);
        }
        else
        {
            // Decoding of the fixed codebook gain (4.1.5 and 3.9.1)
            ctx->gain_pitch = cb_GA[parm->ga_cb_index[i]][0] + cb_GB[parm->gb_cb_index[i]][0];

            ctx->gain_code = g729_get_gain_code(parm->ga_cb_index[i],
                    parm->gb_cb_index[i],
                    fc,
                    ctx->pred_energ_q,
                    ctx->subframe_size);
        }
printf("gain pitch:%d\n",ctx->gain_pitch);
printf("gain code:%d\n",ctx->gain_code);

        /* save pitch sharpening for next subframe */
        ctx->pitch_sharp = FFMIN(FFMAX(ctx->gain_pitch, SHARP_MIN), SHARP_MAX);

        g729_mem_update(fc, ctx->gain_pitch, ctx->gain_code, ctx->exc + i*ctx->subframe_size, ctx->subframe_size);

        /* 4.1.6, Equation 77  */
        if(g729_lp_synthesis_filter(lp+i*10, 
                ctx->exc  + i*ctx->subframe_size,
                out_frame + i*ctx->subframe_size,
                ctx->syn_filter_data,
                ctx->subframe_size,
                1))
        {
            //Overflow occured, downscaling excitation signal...
            for(j=0; j<2*MAX_SUBFRAME_SIZE+PITCH_MAX+INTERPOL_LEN; j++)
                ctx->exc_base[j] /= 4;

            //... and calling the same routine again
            g729_lp_synthesis_filter(lp+i*10, 
                    ctx->exc  + i*ctx->subframe_size,
                    out_frame + i*ctx->subframe_size,
                    ctx->syn_filter_data,
                    ctx->subframe_size,
                    0);
        }

        /* 4.2 */
        g729a_postfilter(ctx, lp+i*10, pitch_delay/3, out_frame + i*ctx->subframe_size);

        ctx->subframe_idx++;
    }

    //Save signal for using in next frame
    memmove(ctx->exc_base, ctx->exc_base+2*ctx->subframe_size, (PITCH_MAX+INTERPOL_LEN)*sizeof(int16_t));

    //Postprocessing
    g729_high_pass_filter(ctx, out_frame, 2 * ctx->subframe_size);

    return 2 * sizeof(int16_t) * ctx->subframe_size; // output size in bytes
}

/**
 * \brief decodes one G.729 frame (10 bytes long) into parameters vector
 * \param ctx private data structure
 * \param buf 10 bytes of decoder parameters
 * \param buf_size size of input buffer
 * \param parm [out] decoded parameters of the codec
 *
 * \return 1 if frame erasure detected, 0 - otherwise
 */
static int g729_bytes2parm(G729A_Context *ctx, const uint8_t *buf, int buf_size, G729_parameters *parm)
{
    GetBitContext gb;
    int i, frame_erasure;

    init_get_bits(&gb, buf, buf_size);

    frame_erasure = 1;
    for(i=0; i<buf_size; i++)
        if(get_bits(&gb,8))
	    frame_erasure=0;

    if(frame_erasure)
        return 1;

    init_get_bits(&gb, buf, buf_size);

    parm->ma_predictor     = get_bits(&gb, L0_BITS);        //L0
    parm->quantizer_1st    = get_bits(&gb, L1_BITS);        //L1
    parm->quantizer_2nd_lo = get_bits(&gb, L2_BITS);        //L2
    parm->quantizer_2nd_hi = get_bits(&gb, L3_BITS);        //L3

    parm->ac_index[0]      = get_bits(&gb, P1_BITS);        //P1
    parm->parity           = get_bits(&gb, P0_BITS);        //P0 (parity)
    parm->fc_indexes[0]    = get_bits(&gb, FC_BITS(ctx));   //C1
    parm->pulses_signs[0]  = get_bits(&gb, FC_PULSE_COUNT); //S1
    parm->ga_cb_index[0]   = get_bits(&gb, GA_BITS);        //GA1
    parm->gb_cb_index[0]   = get_bits(&gb, GB_BITS);        //GB1

    parm->ac_index[1]      = get_bits(&gb, P2_BITS);        //P2
    parm->fc_indexes[1]    = get_bits(&gb, FC_BITS(ctx));   //C2
    parm->pulses_signs[1]  = get_bits(&gb, FC_PULSE_COUNT); //S2
    parm->ga_cb_index[1]   = get_bits(&gb, GA_BITS);        //GA2
    parm->gb_cb_index[1]   = get_bits(&gb, GB_BITS);        //GB2

    return 0;
}


static int ff_g729a_decode_frame(AVCodecContext *avctx,
                             void *data, int *data_size,
                             const uint8_t *buf, int buf_size)
{
    G729_parameters parm;
    G729A_Context *ctx=avctx->priv_data;
    int in_frame_size  = formats[ctx->format].input_frame_size;
    int out_frame_size = formats[ctx->format].output_frame_size;
    int frame_erasure;

    if (buf_size<in_frame_size)
        return AVERROR(EIO);

    frame_erasure = g729_bytes2parm(ctx, buf, in_frame_size, &parm);

    *data_size = g729a_decode_frame_internal(ctx, (int16_t*)data, out_frame_size, &parm, frame_erasure);

    return in_frame_size;
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
    avctx->channels=1;
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
    int frame_erasure;

    frame_erasure = g729_bytes2parm(avctx->priv_data, (uint8_t*)serial, 82, &parm);

    return g729a_decode_frame_internal(avctx->priv_data, out_frame, out_frame_size, &parm, frame_erasure);
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
