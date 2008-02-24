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

///Size of parameters vector
#define VECTOR_SIZE 15

static const struct{
    int sample_rate;
    char bits_per_frame;
    char fc_index_bits;
} formats[]={
  {8000, 80, 3},
#ifdef G729_SUPPORT_4400
// Note: may not work
  {4400, 88, 4},
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
    float* exc_base;        ///< past excitation signal buffer
    float* exc;             ///< start of past excitation data in buffer
    int intT2_prev;         ///< int(T2) value of previous frame (4.1.3)
    int intT1;              ///< int(T1) value of first subframe
    float *lq_prev[MA_NP];  ///< l[i], LSP quantizer output (3.2.4)
    float lsp_prev[10];     ///< q[i], LSP coefficients from previous frame (3.2.5)
    float lsf_prev[10];     ///< lq[i], LSF coefficients from previous frame
    float pred_vect_q[4];   ///< quantized prediction error
    float gain_pitch;       ///< Pitch gain of previous subframe (3.8) [GAIN_PITCH_MIN ... GAIN_PITCH_MAX]
    float gain_code;        ///< Gain code of previous subframe
    float *residual;        ///< Residual signal buffer (used in long-term postfilter)
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
#define LSFQ_MIN 0.005
#define LSFQ_MAX 3.135
#define LSFQ_DIFF_MIN 0.0391

/* Gain pitch maximum and minimum (3.8) */
#define GAIN_PITCH_MIN 0.2
#define GAIN_PITCH_MAX 0.8

/* 4.2.2 */
#define GAMMA_N 0.55
#define GAMMA_D 0.70
#define GAMMA_T 0.80

/* 4.2.1 */
#define GAMMA_P 0.50

#define PITCH_MIN 20
#define PITCH_MAX 143
#define INTERPOL_LEN 11

/**
 * L1 codebook (10-dimensional, with 128 entries (3.24)
 * values are multiplied by 10000
 */
static const uint16_t cb_L1[1<<L1_BITS][10] = {
  { 1814,  2647,  4580, 11077, 14813, 17022, 21953, 23405, 25867, 26636},
  { 2113,  3223,  4212,  5946,  7479,  9615, 19097, 21750, 24773, 26737},
  { 1915,  2755,  3770,  5950, 13505, 16349, 22348, 23552, 25768, 26540},
  { 2116,  3067,  4099,  5748,  8518, 12569, 20782, 21920, 23371, 24842},
  { 2129,  2974,  4039, 10659, 12735, 14658, 19061, 20312, 26074, 26750},
  { 2181,  2893,  4117,  5519,  8295, 15825, 21575, 23179, 25458, 26417},
  { 1991,  2971,  4104,  7725, 13073, 14665, 16208, 16973, 23732, 25743},
  { 1818,  2886,  4018,  7630, 11264, 12699, 16899, 18650, 21633, 26186},
  { 2282,  3093,  4243,  5329, 11173, 17717, 19420, 20780, 25160, 26137},
  { 2528,  3693,  5290,  7146,  9528, 11269, 12936, 19589, 24548, 26653},
  { 2332,  3263,  4174,  5202, 13633, 18447, 20236, 21474, 23572, 24738},
  { 1393,  2216,  3204,  5644,  7929, 11705, 17051, 20054, 23623, 25985},
  { 2677,  3871,  5746,  7091, 13311, 15260, 17288, 19122, 25787, 26598},
  { 1570,  2328,  3111,  4216, 11688, 14605, 19505, 21173, 24038, 27460},
  { 2346,  3321,  5621,  8160, 14042, 15860, 17518, 18631, 20749, 25380},
  { 2505,  3368,  4758,  6405,  8104, 12533, 19329, 20526, 22155, 26459},
  { 2196,  3049,  6857, 13976, 16100, 17958, 20813, 22211, 24789, 25857},
  { 1232,  2011,  3527,  6969, 11647, 15081, 18593, 22576, 25594, 26896},
  { 3682,  4632,  6600,  9118, 15245, 17071, 18712, 19939, 24356, 25380},
  { 2690,  3711,  4635,  6644, 14633, 16495, 18227, 19983, 21797, 22954},
  { 3555,  5240,  9751, 11685, 14114, 16168, 17769, 20178, 24420, 25724},
  { 3493,  4404,  7231,  8587, 11272, 14715, 16760, 22042, 24735, 25604},
  { 3747,  5263,  7284,  8994, 14017, 15502, 17468, 19816, 22380, 23404},
  { 2972,  4470,  5941,  7078, 12675, 14310, 15930, 19126, 23026, 24208},
  { 2467,  3180,  4712, 11281, 16206, 17876, 19544, 20873, 23521, 24721},
  { 2292,  3430,  4383,  5747, 13497, 15187, 19070, 20958, 22902, 24301},
  { 2573,  3508,  4484,  7079, 16577, 17929, 19456, 20847, 23060, 24208},
  { 1968,  2789,  3594,  4361, 10034, 17040, 19439, 21044, 22696, 24558},
  { 2955,  3853,  7986, 12470, 14723, 16522, 18684, 20084, 22849, 24268},
  { 2036,  3189,  4314,  6393, 12834, 14278, 15796, 20506, 22044, 23656},
  { 2916,  3684,  5907, 11394, 13933, 15540, 18341, 19835, 21301, 22800},
  { 2289,  3402,  5166,  7716, 10614, 12389, 14386, 20769, 22715, 24366},
  {  829,  1723,  5682,  9773, 13973, 16174, 19242, 22128, 24855, 26327},
  { 2244,  3169,  4368,  5625,  6897, 13763, 17524, 19393, 25121, 26556},
  { 1591,  2387,  2924,  4056, 14677, 16802, 19389, 22067, 24635, 25919},
  { 1756,  2566,  3251,  4227, 10167, 12649, 16801, 21055, 24088, 27276},
  { 1050,  2325,  7445,  9491, 11982, 14658, 18093, 20397, 24155, 25797},
  { 2043,  3324,  4522,  7477,  9361, 11533, 16703, 17631, 25071, 26528},
  { 1522,  2258,  3543,  5504,  8815, 15516, 18110, 19915, 23603, 27735},
  { 1862,  2759,  4715,  6908,  8963, 14341, 16322, 17630, 22027, 26043},
  { 1460,  2254,  3790,  8622, 13394, 15754, 18084, 20798, 24319, 27632},
  { 2621,  3792,  5463,  7948, 10043, 11921, 13409, 14845, 23159, 26002},
  { 1935,  2937,  3656,  4927, 14015, 16086, 17724, 18837, 24374, 25971},
  { 2171,  3282,  4412,  5713, 11554, 13506, 15227, 19923, 24100, 25391},
  { 2274,  3157,  4263,  8202, 14293, 15884, 17535, 19688, 23939, 24934},
  { 1704,  2633,  3259,  4134, 12948, 14802, 16619, 20393, 23165, 26083},
  { 1763,  2585,  4012,  7609, 11503, 15847, 18309, 19352, 20982, 26681},
  { 2447,  3535,  4618,  5979,  7530,  8908, 15393, 20075, 23557, 26203},
  { 1826,  3496,  7764,  9888, 13915, 17421, 19412, 21620, 24999, 26931},
  { 3033,  3802,  6981,  8664, 10254, 15401, 17180, 18124, 25068, 26119},
  { 2960,  4001,  6465,  7672, 13782, 15751, 19559, 21373, 23601, 24760},
  { 3132,  4613,  6544,  8532, 10721, 12730, 17566, 19217, 21693, 26531},
  { 3329,  4131,  8073, 11297, 12869, 14937, 17885, 19150, 24505, 25760},
  { 2340,  3605,  7659,  9874, 11854, 13337, 15128, 20062, 24427, 25859},
  { 4131,  5330,  6530,  9360, 13648, 15388, 16994, 18707, 24294, 25335},
  { 3754,  5229,  7265,  9301, 11724, 13440, 15118, 17098, 25218, 26242},
  { 2138,  2998,  6283, 12166, 14187, 16084, 17992, 20106, 25377, 26558},
  { 1761,  2672,  4065,  8317, 10900, 14814, 17672, 18685, 23969, 25079},
  { 2801,  3535,  4969,  9809, 14934, 16378, 18021, 21200, 23135, 24034},
  { 2365,  3246,  5618,  8176, 11073, 15702, 17331, 18592, 19589, 23044},
  { 2529,  3251,  5147, 11530, 13291, 15005, 17028, 18200, 23482, 24831},
  { 2125,  3041,  4259,  9935, 11788, 13615, 16121, 17930, 25509, 26742},
  { 2685,  3518,  5707, 10410, 12270, 13927, 17622, 18876, 20985, 25144},
  { 2373,  3648,  5099,  7373,  9129, 10421, 17312, 18984, 21512, 26342},
  { 2229,  3876,  8621, 11986, 15655, 18861, 22376, 24239, 26648, 27359},
  { 3009,  3719,  5887,  7297,  9395, 18797, 20423, 21541, 25132, 26026},
  { 3114,  4142,  6476,  8448, 12495, 17192, 22148, 23432, 25246, 26046},
  { 3666,  4638,  6496,  7858,  9667, 14213, 19300, 20564, 22119, 23170},
  { 4218,  5075,  8348, 10009, 12057, 15032, 19416, 20540, 24352, 25504},
  { 3726,  4602,  5971,  7093,  8517, 12361, 18052, 19520, 24137, 25518},
  { 4482,  5318,  7114,  8542, 10328, 14751, 17278, 18237, 23496, 24931},
  { 3316,  4498,  6404,  8162, 10332, 12209, 15130, 17250, 19715, 24141},
  { 2375,  3221,  5042,  9760, 17503, 19014, 20822, 22225, 24689, 25632},
  { 2813,  3575,  5032,  5889,  6885, 16040, 19318, 20677, 24546, 25701},
  { 2198,  3072,  4090,  6371, 16365, 19468, 21507, 22633, 25063, 25943},
  { 1754,  2716,  3361,  5550, 11789, 13728, 18527, 19919, 21349, 23359},
  { 2832,  3540,  6080,  8467, 10259, 16467, 18987, 19875, 24744, 25527},
  { 2670,  3564,  5628,  7172,  9021, 15328, 17131, 20501, 25633, 26574},
  { 2729,  3569,  6252,  7641,  9887, 16589, 18726, 19947, 21884, 24609},
  { 2155,  3221,  4580,  6995,  9623, 12339, 16642, 18823, 20518, 22674},
  { 4224,  7009, 11714, 14334, 17595, 19629, 22185, 23304, 25446, 26369},
  { 4560,  5403,  7568,  8989, 11292, 17687, 19575, 20784, 24260, 25484},
  { 4299,  5833,  8408, 10596, 15524, 17484, 19471, 22034, 24617, 25812},
  { 2614,  3624,  8381,  9829, 12220, 16064, 18083, 19362, 21397, 22773},
  { 5064,  7481, 11021, 13271, 15486, 17096, 19503, 21006, 23911, 25141},
  { 5375,  6552,  8099, 10219, 12407, 14160, 18266, 19936, 21951, 22911},
  { 4994,  6575,  8365, 10706, 14116, 16224, 19200, 20667, 23262, 24539},
  { 3353,  4426,  6469,  9161, 12528, 13956, 16080, 18909, 20600, 21380},
  { 2745,  4341, 10424, 12928, 15461, 17940, 20161, 21758, 24742, 25937},
  { 1562,  2393,  4786,  9513, 12395, 18010, 20320, 22143, 25243, 26204},
  { 2979,  4242,  8224, 10564, 14881, 17808, 20898, 21882, 23328, 24389},
  { 2294,  3070,  5490,  9244, 12229, 18248, 19704, 20627, 22458, 23653},
  { 3423,  4502,  9144, 12313, 13694, 15517, 19907, 21326, 24509, 25789},
  { 2470,  3275,  4729, 10093, 12519, 14216, 18540, 20877, 23151, 24156},
  { 3447,  4401,  7099, 10493, 12312, 14001, 20225, 21317, 22894, 24263},
  { 3481,  4494,  6446,  9336, 11198, 12620, 18264, 19712, 21435, 22552},
  { 1646,  3229,  7112, 10725, 12964, 15663, 19843, 22363, 25798, 27572},
  { 2614,  3707,  5241,  7425,  9269, 12976, 20945, 22014, 26204, 26959},
  { 1963,  2900,  4131,  8397, 12171, 13705, 20665, 21546, 24640, 25782},
  { 3387,  4415,  6121,  8005,  9507, 10937, 20836, 22342, 23849, 25076},
  { 2362,  5876,  7574,  8804, 10961, 14240, 19519, 21742, 24935, 26493},
  { 2793,  4282,  6149,  8352, 10106, 11766, 18392, 20119, 26433, 27117},
  { 3603,  4604,  5955,  9251, 11006, 12572, 17688, 18607, 24687, 25623},
  { 3975,  5849,  8059,  9182, 10552, 11850, 16356, 19627, 23318, 24719},
  { 2231,  3192,  4256,  7373, 14831, 16874, 19765, 21097, 26152, 26906},
  { 1221,  2081,  3665,  7734, 10341, 12818, 18162, 20727, 24446, 27377},
  { 2010,  2791,  3796,  8845, 14030, 15615, 20538, 21567, 23171, 24686},
  { 2086,  3053,  4047,  8224, 10656, 12115, 19641, 20871, 22430, 24313},
  { 3203,  4285,  5467,  6891, 12039, 13569, 18578, 22055, 23906, 24881},
  { 3074,  4192,  5772,  7799,  9866, 11335, 16068, 22441, 24194, 25089},
  { 2108,  2910,  4993,  7695,  9528, 15681, 17838, 21495, 23522, 24636},
  { 3492,  4560,  5906,  7379,  8855, 10257, 17128, 19997, 22019, 23694},
  { 5185,  7316,  9708, 11954, 15066, 17887, 21396, 22918, 25429, 26489},
  { 4276,  4946,  6934,  8308,  9944, 14582, 20324, 21294, 24891, 26324},
  { 3847,  5973,  7202,  8787, 13938, 15959, 18463, 21574, 25050, 26687},
  { 4835,  5919,  7235,  8862, 10756, 12853, 19118, 20215, 22213, 24638},
  { 5492,  8062,  9810, 11293, 13189, 15415, 19385, 21378, 24439, 25691},
  { 5190,  6764,  8123, 10154, 12085, 14266, 18433, 20866, 25113, 26474},
  { 4602,  6503,  9602, 11427, 13043, 14427, 16676, 18758, 22868, 24271},
  { 3764,  4845,  7627,  9914, 11961, 13421, 15129, 16707, 21836, 23322},
  { 3334,  5701,  8622, 11232, 13851, 16767, 20600, 22946, 25375, 27295},
  { 1449,  2719,  5783,  8807, 11746, 15422, 18804, 21934, 24734, 28728},
  { 2333,  3024,  4780, 12327, 14180, 15815, 19804, 20921, 23524, 25304},
  { 2154,  3075,  4746,  8477, 11170, 15369, 19847, 20733, 21880, 22504},
  { 1709,  4486,  8705, 10643, 13047, 15269, 19175, 21621, 24073, 25718},
  { 2835,  3752,  5234,  9898, 11484, 12974, 19363, 20378, 24065, 26214},
  { 3211,  4077,  5809, 10206, 12542, 13835, 15723, 21209, 23464, 24336},
  { 2101,  3146,  6779,  8783, 10561, 13045, 18395, 20695, 22831, 24328},
};
/**
 * L2 codebook (5-dimensional, with 32 entries (3.2.4)
 * values are multiplied by 10000
 */
static const int16_t cb_L2[1<<L2_BITS][5] = {
  {  -532,   -995,   -906,   1261,   -633},
  { -1017,  -1088,    566,    -10,  -1528},
  { -1247,    283,   -374,    393,   -269},
  {    70,   -242,   -415,    -41,  -1793},
  {   209,   -428,    359,   2027,    554},
  {  -856,  -1028,    -71,   1160,   1089},
  {   713,     39,   -353,    435,   -407},
  {  -134,   -987,    283,     95,   -107},
  { -1049,   1510,    672,   1043,    872},
  { -1071,  -1165,  -1524,   -365,    260},
  {   -94,    420,   -758,    932,    505},
  {  -384,   -375,   -313,  -1539,   -524},
  {   869,    847,    637,    794,   1594},
  {  -137,   -332,   -611,   1156,   2116},
  {   703,    -13,   -572,   -243,   1345},
  {   178,   -349,  -1563,   -487,     44},
  { -1384,  -1020,   1649,   1568,   -116},
  { -1782,  -1511,    509,   -261,    570},
  {   -19,     81,    572,   1245,   -914},
  {  -413,    181,   1764,     92,   -928},
  {   476,    292,   1915,   1198,    139},
  {  -382,   -120,   1159,     39,   1348},
  {  1376,    713,   1020,    339,  -1415},
  {   658,   -140,   1046,   -603,    273},
  {  2683,   2853,   1549,    819,    372},
  { -1949,    672,    978,   -557,    -69},
  {  1409,    724,    -94,   1511,    -39},
  {   485,    682,    248,   -974,  -1122},
  {   408,   1801,    772,    -98,     59},
  {  -666,   -403,   -524,   -831,   1384},
  {  1612,   1010,   -486,   -704,    417},
  {  -199,    823,    -14,  -1082,    649},
};

/**
 * L3 codebook (5-dimensional, with 32 entries (3.2.4)
 * values are multiplied by 10000
 */
static const int16_t cb_L3[1<<L3_BITS][5] = {
  {   711,  -1467,   1012,    106,    470},
  {  1771,     89,   -282,   1055,    808},
  {  -200,   -643,   -921,  -1994,    327},
  {   700,    972,   -207,   -771,    997},
  {   634,    356,    195,   -782,  -1583},
  {  1892,    874,    644,   -872,   -236},
  {  -558,    748,   -346,  -1686,   -905},
  {  -420,   1638,   1328,   -799,   -695},
  {  -663,  -2139,   -239,   -120,   -338},
  {  -288,   -889,   1159,   1852,   1093},
  {   614,   -443,  -1172,   -590,   1693},
  {   550,   -569,   -133,   1233,   2714},
  {   -35,   -462,    909,  -1227,    294},
  {   332,    -19,   1110,   -317,   2061},
  { -1235,    710,    -65,   -912,   1072},
  {  -609,  -1682,     23,   -542,   1811},
  {  1240,   -271,    541,    455,   -433},
  {   817,    805,   2003,   1138,    653},
  {  1691,   -223,  -1108,   -881,   -320},
  {   695,   1523,    412,    508,   -148},
  {   451,  -1225,   -619,   -717,  -1104},
  {    88,   -173,   1789,     78,   -959},
  {   254,    368,  -1077,    143,   -494},
  { -1114,    761,    -93,    338,   -538},
  {  -327,   -642,    172,   1077,   -170},
  {  -851,   1057,   1294,    505,    545},
  {   710,  -1266,  -1093,    817,    363},
  {     4,    845,   -357,   1282,    955},
  { -1296,   -591,    443,   -729,  -1041},
  { -1443,   -909,   1636,    320,     77},
  {  -945,   -590,  -1523,    -86,    120},
  { -1374,   -324,   -296,    885,   1141},
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
 */
static const float b30[31]=
{
   0.898517,
   0.769271,   0.448635,   0.095915,
  -0.134333,  -0.178528,  -0.084919,
   0.036952,   0.095533,   0.068936,
  -0.000000,  -0.050404,  -0.050835,
  -0.014169,   0.023083,   0.033543,
   0.016774,  -0.007466,  -0.019340,
  -0.013755,   0.000000,   0.009400,
   0.009029,   0.002381,  -0.003658,
  -0.005027,  -0.002405,   0.001050,
   0.002780,   0.002145,   0.000000
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
static const float cb_GB[GB_CB_SIZE][2] = {
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
 * values are multiplied by 10000
 */
static const uint16_t ma_predictor[2][MA_NP][10] = {
  {
    { 2570,    2780,    2800,    2736,    2757,    2764,    2675,    2678,    2779,    2647},
    { 2142,    2194,    2331,    2230,    2272,    2252,    2148,    2123,    2115,    2096},
    { 1670,    1523,    1567,    1580,    1601,    1569,    1589,    1555,    1474,    1571},
    { 1238,     925,     798,     923,     890,     828,    1010,     988,     872,    1060},
  },
  {
    { 2360,    2405,    2499,    2495,    2517,    2591,    2636,    2625,    2551,    2310},
    { 1285,     925,     779,    1060,    1183,    1176,    1277,    1268,    1193,    1211},
    {  981,     589,     401,     654,     761,     728,     841,     826,     776,     891},
    {  923,     486,     287,     498,     526,     482,     621,     636,     584,     794}
  }
};

/**
 * ma_predicot_sum[i] := 1-sum{1}{4}{ma_predictor[k][i]}
 * values are multiplied by 10000
 */
static const uint16_t ma_predictor_sum[2][10] = {
  { 2380, 2578, 2504, 2531, 2480, 2587, 2578, 2656, 2760, 2626},
  { 4451, 5595, 6034, 5293, 5013, 5023, 4625, 4645, 4896, 4794}
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
static const float lq_init[10] = {
  0.285599,  0.571199,  0.856798,  1.142397,  1.427997, 1.713596,  1.999195,  2.284795,  2.570394,  2.855993,
};

/**
 * Initial LSP values
 */
static const float lsp_init[10] = {
  0.9595, 0.8413, 0.6549, 0.4154, 0.1423, -0.1423, -0.4154, -0.6549, -0.8413, -0.9595,
};

/*
-------------------------------------------------------------------------------
          Internal routines
------------------------------------------------------------------------------
*/

/**
 * \brief pseudo random number generator
 */
static inline uint16_t g729_random(G729A_Context* ctx)
{
    return ctx->rand_value = 31821 * (uint32_t)ctx->rand_value + 13849;
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
 * \param P1 Adaptive codebook index for first subframe
 * \param intT [out] integer part of delay
 * \param frac [out] fractional part of delay [-1, 0, 1]
 */
static void g729_decode_ac_delay_subframe1(G729A_Context* ctx, int P1, int* intT, int* frac)
{
    /* if no parity error */
    if(!ctx->bad_pitch)
    {
        if(P1<197)
        {
            *intT=1.0*(P1+2)/3+19;
            *frac=P1-3*(*intT)+58;
        }
        else
        {
            *intT=P1-112;
            *frac=0;
        }
    }
    else{
        *intT=ctx->intT2_prev;
        *frac=0;
    }
    ctx->intT1=*intT;
}

/**
 * \brief Decoding of the adaptive-codebook vector delay for second subframe (4.1.3)
 * \param ctx private data structure
 * \param P1 Adaptive codebook index for second subframe
 * \param T1 first subframe's vector delay integer part
 * \param intT [out] integer part of delay
 * \param frac [out] fractional part of delay [-1, 0, 1]
 */
static void g729_decode_ac_delay_subframe2(G729A_Context* ctx, int P2, int intT1, int* intT, int* frac)
{

    int tmin=FFMIN(FFMAX(intT1-5, PITCH_MIN)+9, PITCH_MAX)-9;

    if(ctx->data_error)
    {
        *intT=intT1;
        *frac=0;
        ctx->intT2_prev=FFMIN(intT1+1, PITCH_MAX);
        return;
    }

    *intT=(P2+2)/3-1;
    *frac=P2-2-3*(*intT);

    *intT+=tmin;

    ctx->intT2_prev=*intT;
}

/**
 * \brief Decoding of the adaptive-codebook vector (4.1.3)
 * \param ctx private data structure
 * \param k pitch delay, integer part
 * \param t pitch delay, fraction part [-1, 0, 1]
 * \param ac_v buffer to store decoded vector into
 */
static void g729_decode_ac_vector(G729A_Context* ctx, int k, int t, float* ac_v)
{
    int n, i;
    float v;

    //Make sure that t will be always positive
    t=-t;
    if(t<0)
    {
        t+=3;
        k++;
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
            v+=ac_v[n-k-i]*b30[t+3*i];     //R(n-i)*b30(t+3i)
            v+=ac_v[n-k+i+1]*b30[3-t+3*i]; //R(n+i+1)*b30(3-t+3i)
        }
        ac_v[n]=v;
    }
}

/**
 * \brief Decoding fo the fixed-codebook vector (3.8)
 * \param ctx private data structure
 * \param C Fixed codebook
 * \param S Signs of fixed-codebook pulses (0 bit value means negative sign)
 * \param fc_v [out] decoded fixed codebook vector
 *
 * bit allocations:
 *   8k mode: 3+3+3+1+3
 * 4.4k mode: 4+4+4+1+4 (non-standard)
 *
 * FIXME: error handling required
 */
static void g729_decode_fc_vector(G729A_Context* ctx, int C, int S, float* fc_v)
{
    int accC=C;
    int accS=S;
    int i;
    int index;
    int bits=formats[ctx->format].fc_index_bits;
    int mask=(1<<bits)-1;

    memset(fc_v, 0, sizeof(float)*ctx->subframe_size);

    /* reverted Equation 62 and Equation 45 */
    for(i=0; i<FC_PULSE_COUNT-1; i++)
    {
        index=(accC&mask) * 5 + i;
        //overflow can occur in 4.4k case
        if(index>=ctx->subframe_size)
        {
            ctx->data_error=1;
            return;
        }
        fc_v[ index ] = (accS&1) ? 1 : -1;
        accC>>=bits;
        accS>>=1;
    }
    index=((accC>>1)&mask) * 5 + i + (accC&1);
    //overflow can occur in 4.4k case
    if(index>=ctx->subframe_size)
    {
        ctx->data_error=1;
        return;
    }
    fc_v[ index ] = (accS&1) ? 1 : -1;
}

/**
 * \brief fixed codebook vector modification if delay is less than 40 (4.1.4 and 3.8)
 * \param T pitch delay to check
 * \param fc_v [in/out] fixed codebook vector to change
 *
 * \remark if T>=subframe_size no changes to vector are made
 */
static void g729_fix_fc_vector(G729A_Context *ctx, int T, float* fc_v)
{
    int i;

    if(T>=ctx->subframe_size)
        return;

    for(i=T; i<ctx->subframe_size;i++)
        fc_v[i]+=fc_v[i-T]*ctx->gain_pitch;
}

/**
 * \brief Decoding of the adaptive and fixed codebook gains from previous subframe (4.4.2)
 * \param ctx private data structure
 * \param gp pointer to variable receiving quantized fixed-codebook gain (gain pitch)
 * \param gc pointer to variable receiving quantized adaptive-codebook gain (gain code)
 */
static void g729_get_gain_from_previous(G729A_Context *ctx, float* gp, float* gc)
{
    /* 4.4.2, Equation 93 */
    *gc=0.98*ctx->gain_code;
    ctx->gain_code=*gc;

    /* 4.4.2, Equation 94 */
    *gp=FFMIN(0.9*ctx->gain_pitch, 0.9);
    ctx->gain_pitch = *gp;
}

/**
 * \brief Attenuation of the memory of the gain predictor (4.4.3)
 * \param ctx private data structure
 */
static void g729_update_gain(G729A_Context *ctx)
{
    float avg_gain=0;
    int i;

    /* 4.4.3. Equation 95 */
    for(i=0; i<4; i++)
        avg_gain+=ctx->pred_vect_q[i];

    avg_gain = FFMAX(avg_gain * 0.25 - 4.0, -14);

    for(i=3; i>0; i--)
        ctx->pred_vect_q[i]=ctx->pred_vect_q[i-1];
    ctx->pred_vect_q[0]=avg_gain;
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
    float energy=0;
    int i;
    float cb1_sum;

    /* 3.9.1, Equation 66 */
    for(i=0; i<ctx->subframe_size; i++)
        energy+=fc_v[i]*fc_v[i];

    /*
      energy=mean_energy-E
      mean_energy=30dB
      E is calculated in 3.9.1 Equation 66
    */
    energy=30-10.*log(energy/ctx->subframe_size)/M_LN10;

    /* 3.9.1, Equation 69 */
    for(i=0; i<4; i++)
        energy+= 0.01 * ctx->pred_vect_q[i] * ma_prediction_coeff[i];

    /* 3.9.1, Equation 71 */
    energy = exp(M_LN10*energy/20); //FIXME: should there be subframe_size/2 ?

    // shift prediction error vector
    for(i=3; i>0; i--)
        ctx->pred_vect_q[i]=ctx->pred_vect_q[i-1];

    cb1_sum=cb_GA[nGA][1]+cb_GB[nGB][1];
    /* 3.9.1, Equation 72 */
    ctx->pred_vect_q[0]=20*log(cb1_sum)/M_LN10; //FIXME: should there be subframe_size/2 ?

    /* 3.9.1, Equation 73 */
    *gp = cb_GA[nGA][0]+cb_GB[nGB][0];           // quantized adaptive-codebook gain (gain code)

    /* 3.9.1, Equation 74 */
    *gc = energy*(cb1_sum);  //quantized fixed-codebook gain (gain pitch)

    /* save gain code value for next subframe */
    ctx->gain_code=*gc;
    /* save pitch gain value for next subframe */
    ctx->gain_pitch=*gp;
    ctx->gain_pitch = FFMAX(ctx->gain_pitch, GAIN_PITCH_MIN);
    ctx->gain_pitch = FFMIN(ctx->gain_pitch, GAIN_PITCH_MAX);
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
            tmp[n]-= lp[i]*tmp[n-i-1];
    }
    memcpy(filter_data, tmp+ctx->subframe_size-10, 10*sizeof(float));
    memcpy(out, tmp, ctx->subframe_size*sizeof(float));
}

/**
 * \brief Calculates gain value of speech signal
 * \param speech signal buffer
 * \param length signal buffer length
 *
 * \return squared gain value
 */
static float g729_get_signal_gain(float *speech, int length)
{
    int n;
    float gain;

    gain=speech[0]*speech[0];
    for(n=1; n<length; n++)
       gain+=speech[n]*speech[n];

    return gain;
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
        ctx->g=0.9*ctx->g+0.1*gain;
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
 * \param residual_filt speech signal with applied A(z/GAMMA_N) filter
 */
static void g729a_long_term_filter(G729A_Context *ctx, float *residual_filt)
{
    int k, n, intT0;
    float gl;      ///< gain coefficient for long-term postfilter
    float corr_t0; ///< correlation of residual signal with delay intT0
    float corr_0;  ///< correlation of residual signal with delay 0
    float correlation, corr_max;
    float inv_glgp;///< 1.0/(1+gl*GAMMA_P)
    float glgp_inv_glgp; ///< gl*GAMMA_P/(1+gl*GAMMA_P);

    /* A.4.2.1 */
    int minT0=FFMIN(ctx->intT1, PITCH_MAX-3)-3;
    int maxT0=FFMIN(ctx->intT1, PITCH_MAX-3)+3;
    /* Long-term postfilter start */

    /*
       First pass: searching the best T0 (pitch delay)
       Second pass is not used in G.729A: fractional part is always zero
    */
    k=minT0;
    correlation=0;
    /* 4.2.1, Equation 80 */
    for(n=0; n<ctx->subframe_size; n++)
        correlation+=ctx->residual[n+PITCH_MAX]*ctx->residual[n+PITCH_MAX-k];

    corr_max=correlation;
    intT0=k;

    for(; k<=maxT0; k++)
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

    corr_t0=g729_get_signal_gain(ctx->residual+PITCH_MAX-intT0, ctx->subframe_size);
    corr_0=g729_get_signal_gain(ctx->residual+PITCH_MAX, ctx->subframe_size);

    /* 4.2.1, Equation 82. checking if filter should be disabled */
    if(corr_max*corr_max < 0.5*corr_0*corr_t0)
        gl=0;
    else if(!corr_t0)
        gl=1;
    else
        gl=FFMIN(corr_max/corr_t0, 1);

    inv_glgp=1.0/(1+gl*GAMMA_P);
    glgp_inv_glgp=gl*GAMMA_P/(1+gl*GAMMA_P);

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
            sum-= lp_gd[i]*tmp_buf[n-i-1+10];
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
        res_pst[i]+=gt*res_pst[i-1];
    res_pst[0]+=gt*ctx->ht_prev_data;

    ctx->ht_prev_data=tmp;
}

/**
 * \brief Signal postfiltering (4.2, with A.4.2 simplification)
 * \param ctx private data structure
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
static void g729a_postfilter(G729A_Context *ctx, float *lp, float *speech_buf)
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
    gain_before=g729_get_signal_gain(speech, ctx->subframe_size);

    /* long-term filter (A.4.2.1) */
    g729a_long_term_filter(ctx, residual_filt);

    /* short-term filter tilt compensation (A.4.2.3) */
    g729a_tilt_compensation(ctx, lp_gn, lp_gd, residual_filt);

    /* Applying second half of short-term postfilter: 1/A(z/GAMMA_D)*/
    g729_lp_synthesis_filter(ctx, lp_gd, residual_filt, speech, ctx->res_filter_data);

    /* Calculating gain of filtered signal for using in AGC */
    gain_after=g729_get_signal_gain(speech, ctx->subframe_size);

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
        speech[i]=f_0*2.0;

        ctx->hpf_f2=ctx->hpf_f1;
        ctx->hpf_f1=f_0;
    }
}

/**
 * \brief Computing the reconstructed speech (4.1.6)
 * \param ctx private data structure
 * \param lp LP filter coefficients
 * \param exc excitation
 * \param speech reconstructed speech buffer (ctx->subframe_size items)
 */
static void g729_reconstruct_speech(G729A_Context *ctx, float *lp, float* exc, short* speech)
{
    float tmp_speech_buf[MAX_SUBFRAME_SIZE+10];;
    float* tmp_speech=tmp_speech_buf+10;
    int i;

    memcpy(tmp_speech_buf, ctx->syn_filter_data, 10 * sizeof(float));

    /* 4.1.6, Equation 77  */
    g729_lp_synthesis_filter(ctx, lp, exc, tmp_speech, ctx->syn_filter_data);

    /* 4.2 */
    g729a_postfilter(ctx, lp, tmp_speech_buf);

    //Postprocessing
    g729_high_pass_filter(ctx,tmp_speech);

    for(i=0; i<ctx->subframe_size; i++)
    {
        tmp_speech[i] = FFMIN(tmp_speech[i],32767.0);
        tmp_speech[i] = FFMAX(tmp_speech[i],-32768.0);
        speech[i]=tmp_speech[i];
    }
}

/**
 * \brief Convert LSF to LSP
 * \param ctx private data structure
 * \param lsf LSF coefficients
 * \param lsp LSP coefficients
 *
 * \remark It is safe to pass the same array in lsf and lsp parameters
 */
static void g729_lsf2lsp(G729A_Context *ctx, float *lsf, float *lsp)
{
    int i;

    /* Convert LSF to LSP */
    for(i=0;i<10; i++)
        lsp[i]=cos(lsf[i]);
}

/**
 * \brief Restore LSP parameters using previous frame data
 * \param ctx private data structure
 * \param lsfq Decoded LSP coefficients
 */
static void g729_lsp_restore_from_previous(G729A_Context *ctx, float* lsfq)
{
    float lq[10];
    int i,k;
    float* tmp;

    //Restore LSF from previous frame
    for(i=0;i<10; i++)
        lsfq[i]=ctx->lsf_prev[i];

    /* 4.4.1, Equation 92 */
    for(i=0; i<10; i++)
    {
        // ma_predictor and ma_predictor_sum are multiplied by 10000
        lq[i]=10000.0 * lsfq[i];
        for(k=0;k<MA_NP; k++)
            lq[i]-=ma_predictor[ctx->prev_mode][k][i];
        lq[i]/=ma_predictor_sum[ctx->prev_mode][i];
    }

    /* Rotate lq_prev */
    tmp=ctx->lq_prev[MA_NP-1];
    for(k=MA_NP-1; k>0; k--)
        ctx->lq_prev[k]=ctx->lq_prev[k-1];
    ctx->lq_prev[0]=tmp;
    for(i=0; i<10; i++)
        ctx->lq_prev[0][i]=lq[i];

    g729_lsf2lsp(ctx, lsfq, lsfq);
}

/**
 * \brief Decode LP coefficients from L0-L3 (3.2.4)
 * \param ctx private data structure
 * \param L0 Switched MA predictor of LSP quantizer
 * \param L1 First stage vector of quantizer
 * \param L2 Second stage lower vector of LSP quantizer
 * \param L3 Second stage higher vector of LSP quantizer
 * \param lsfq Decoded LSP coefficients
 */
static void g729_lsp_decode(G729A_Context* ctx, int16_t L0, int16_t L1, int16_t L2, int16_t L3, float* lsfq)
{
    int i,j,k;
    float J[2]={0.0012, 0.0006};
    float lq[10];
    float diff;
    float* tmp;

    /* 3.2.4 Equation 19 */
    for(i=0;i<5; i++)
    {
        lq[i]   = 0.0001 * (cb_L1[L1][i  ] + cb_L2[L2][i]);
        lq[i+5] = 0.0001 * (cb_L1[L1][i+5] + cb_L3[L3][i]);
    }

    /* 3.2.4 rearrangement routine */
    for(j=0; j<2; j++)
    {
        for(i=1; i<10; i++)
        {
            diff=(lq[i-1]-lq[i]+J[j])/2;
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
        lsfq[i]=lq[i] * ma_predictor_sum[L0][i];
        for(k=0; k<MA_NP; k++)
            lsfq[i] += (ctx->lq_prev[k][i] * ma_predictor[L0][k][i]);
	//ma_predictor and ma_predictor_sum were multiplied by 10000
	lsfq[i] *= 0.0001;
        //Saving LSF for using when error occured in next frames
        ctx->lsf_prev[i]=lsfq[i];
    }

    /* Rotate lq_prev */
    tmp=ctx->lq_prev[MA_NP-1];
    for(k=MA_NP-1; k>0; k--)
        ctx->lq_prev[k]=ctx->lq_prev[k-1];
    ctx->lq_prev[0]=tmp;
    for(i=0; i<10; i++)
        ctx->lq_prev[0][i]=lq[i];
    ctx->prev_mode=L0;

    /* sorting lsfq in ascending order. float bubble agorithm*/
    for(i=0; i<5; i++)
    {
        float min=lsfq[i];
        int mini=i;
        float max=lsfq[i];
        int maxi=i;
        for(j=i; j< 10-i; j++)
        {
            if(lsfq[j]>max)
            {
                maxi=j;
                max=lsfq[j];
            }
            if(lsfq[j]<min)
            {
                mini=j;
                min=lsfq[j];
            }
        }
        FFSWAP(float, lsfq[i], lsfq[mini]);
        FFSWAP(float, lsfq[10-i-1], lsfq[maxi]);
    }

    /* checking for stability */
    lsfq[0] = FFMAX(lsfq[0],LSFQ_MIN); //Is warning required ?

    for(i=0;i<9; i++)
        lsfq[i+1]=FFMAX(lsfq[i+1], lsfq[i]+LSFQ_DIFF_MIN);
    lsfq[9] = FFMIN(lsfq[9],LSFQ_MAX);//Is warning required ?

    g729_lsf2lsp(ctx, lsfq, lsfq);
}

static void get_lsp_coefficients(float* q, float* f)
{
    int i, j;
    int qidx=2;
    float b;

    f[0]=1.0;
    f[1]=-2*q[0];

    for(i=2; i<=5; i++)
    {
        b=-2*q[qidx];
        f[i]=b*f[i-1]+2*f[i-2];

        for(j=i-1; j>1; j--)
        {
            f[j]+=b  * f[j-1] + f[j-2];
        }
        f[1]+=b;
        qidx+=2;
    }
}
/**
 * \brief LSP to LP conversion (3.2.6)
 * \param ctx private data structure
 * \param q LSP coefficients
 * \param a decoded LP coefficients
 */
static void g729_lsp2lp(G729A_Context* ctx, float* q, float* a)
{
    int i;
    float f1[6];
    float f2[6];

    float ff1[5];
    float ff2[5];

    get_lsp_coefficients(q, f1);
    get_lsp_coefficients(q+1, f2);

    /* 3.2.6, Equation 25*/
    for(i=0;i<5;i++)
    {
        ff1[i]=f1[i+1]+f1[i];
        ff2[i]=f2[i+1]-f2[i];
    }

    /* 3.2.6, Equation 26*/
    for(i=0;i<5;i++)
    {
        a[i]=(ff1[i]+ff2[i])/2;
        a[i+5]=(ff1[4-i]-ff2[4-i])/2;
    }
}

/**
 * \brief interpolate LSP end decode LP for both first and second subframes (3.2.5 and 3.2.6)
 * \param ctx private data structure
 * \param lsp_curr current LSP coefficients
 * \param lp [out] decoded LP coefficients
 */
static void g729_lp_decode(G729A_Context* ctx, float* lsp_curr, float* lp)
{
    float lsp[10];
    int i;

    /* LSP values for first subframe (3.2.5, Equation 24)*/
    for(i=0;i<10;i++)
        lsp[i]=(lsp_curr[i]+ctx->lsp_prev[i])/2;

    g729_lsp2lp(ctx, lsp, lp);

    /* LSP values for second subframe (3.2.5)*/
    g729_lsp2lp(ctx, lsp_curr, lp+10);

    /* saving LSP coefficients for using in next frame */
    for(i=0;i<10;i++)
        ctx->lsp_prev[i]=lsp_curr[i];
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
    int frame_size=10;
    int i,k;

    if(avctx->sample_rate==8000)
        ctx->format=0;
#ifdef G729_SUPPORT_4400
    else if (avctx->sample_rate==4400)
        ctx->format=1;
#endif
    else{
        av_log(avctx, AV_LOG_ERROR, "Sample rate %d is not supported\n", avctx->sample_rate);
        return AVERROR_NOFMT;
    }
    frame_size=formats[ctx->format].bits_per_frame>>3; //frame_size is in bits

    ctx->subframe_size=formats[ctx->format].bits_per_frame>>1;

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
    for(k=0; k<MA_NP; k++)
        ctx->lq_prev[k]=av_malloc(sizeof(float)*frame_size);

    for(i=0; i<10; i++)
        ctx->lq_prev[0][i]=lq_init[i];

    for(i=0; i<10; i++)
        ctx->lsp_prev[i]=lsp_init[i];

    for(k=1; k<MA_NP; k++)
        for(i=0;i<frame_size; i++)
            ctx->lq_prev[k][i]=ctx->lq_prev[0][i];

    // Two subframes + PITCH_MAX inetries for last excitation signal data + ???
    ctx->exc_base=av_mallocz((frame_size*8+PITCH_MAX+INTERPOL_LEN)*sizeof(float));
    if(!ctx->exc_base)
        return AVERROR(ENOMEM);

    ctx->exc=ctx->exc_base+PITCH_MAX+INTERPOL_LEN;

    ctx->residual=av_mallocz((PITCH_MAX+ctx->subframe_size)*sizeof(float));
    /* random seed initialization (4.4.4) */
    ctx->rand_value=21845;

    //quantized prediction error
    for(i=0; i<4; i++)
        ctx->pred_vect_q[i] = -14;

    memset(ctx->syn_filter_data, 0, 10*sizeof(float));
    memset(ctx->res_filter_data, 0, 10*sizeof(float));

    //High-pass filter data
    ctx->hpf_f1=0.0;
    ctx->hpf_f2=0.0;
    ctx->hpf_z0=0;
    ctx->hpf_z1=0;

    avctx->frame_size=frame_size;
    return 0;
}

/**
 * G.729A decoder uninitialization
 * \param ctx private data structure
 */
static int ff_g729a_decoder_close(AVCodecContext *avctx)
{
    G729A_Context *ctx=avctx->priv_data;
    int k;

    av_free(ctx->residual);
    ctx->residual=NULL;

    av_free(ctx->exc_base);
    ctx->exc_base=NULL;
    ctx->exc=NULL;

    for(k=0; k<MA_NP; k++)
    {
        av_free(ctx->lq_prev[k]);
        ctx->lq_prev[k]=NULL;
    }
    return 0;
}

/**
 * \brief decode one G.729 frame into PCM samples
 * \param serial array if bits (0x81 - 1, 0x7F -0)
 * \param serial_size number of items in array
 * \param out_frame array for output PCM samples
 * \param out_frame_size maximum number of elements in output array
 */
static int  g729a_decode_frame_internal(void* context, short* out_frame, int out_frame_size, int *parm)
{
    G729A_Context* ctx=context;
    float lp[20];
    float lsp[10];
    int t;     ///< pitch delay, fraction part
    int k;     ///< pitch delay, integer part
    float* fc; ///< fixed codebooc vector
    float gp, gc;

    short* speech_buf; ///< reconstructed speech

    fc=av_mallocz(ctx->subframe_size * sizeof(float));
    speech_buf=av_mallocz(2*ctx->subframe_size * sizeof(short));

    ctx->data_error=0;
    ctx->bad_pitch=0;

    if(!g729_parity_check(parm[4], parm[5]))
        ctx->bad_pitch=1;

    if(ctx->data_error)
        g729_lsp_restore_from_previous(ctx, lsp);
    else
        g729_lsp_decode(ctx, parm[0], parm[1], parm[2], parm[3], lsp);

    g729_lp_decode(ctx, lsp, lp);

    /* first subframe */
    g729_decode_ac_delay_subframe1(ctx, parm[4], &k, &t);
    g729_decode_ac_vector(ctx, k, t, ctx->exc);

    if(ctx->data_error)
    {
        parm[6] = g729_random(ctx) & 0x1fff;
        parm[7] = g729_random(ctx) & 0x000f;
    }

    g729_decode_fc_vector(ctx, parm[6], parm[7], fc);
    g729_fix_fc_vector(ctx, k, fc);

    if(ctx->data_error)
    {
        g729_get_gain_from_previous(ctx, &gp, &gc);
        g729_update_gain(ctx);
    }
    else
    {
        g729_get_gain(ctx, parm[8], parm[9], fc, &gp, &gc);
    }
    g729_mem_update(ctx, fc, gp, gc, ctx->exc);
    g729_reconstruct_speech(ctx, lp, ctx->exc, speech_buf);
    ctx->subframe_idx++;

    /* second subframe */
    g729_decode_ac_delay_subframe2(ctx, parm[10], k, &k, &t);
    g729_decode_ac_vector(ctx, k, t, ctx->exc+ctx->subframe_size);

    if(ctx->data_error)
    {
        parm[11] = g729_random(ctx) & 0x1fff;
        parm[12] = g729_random(ctx) & 0x000f;
    }

    g729_decode_fc_vector(ctx, parm[11], parm[12], fc);
    g729_fix_fc_vector(ctx, k, fc);

    if(ctx->data_error)
    {
        g729_get_gain_from_previous(ctx, &gp, &gc);
        g729_update_gain(ctx);
    }
    else
    {
        g729_get_gain(ctx, parm[13], parm[14], fc, &gp, &gc);
    }
    g729_mem_update(ctx, fc, gp, gc, ctx->exc+ctx->subframe_size);
    g729_reconstruct_speech(ctx, lp+10, ctx->exc+ctx->subframe_size, speech_buf+ctx->subframe_size);
    ctx->subframe_idx++;

    //Save signal for using in next frame
    memmove(ctx->exc_base, ctx->exc_base+2*ctx->subframe_size, (PITCH_MAX+INTERPOL_LEN)*sizeof(float));

    /* Return reconstructed speech to caller */
    memcpy(out_frame, speech_buf, 2*ctx->subframe_size*sizeof(short));

    av_free(speech_buf);
    av_free(fc);
    return ctx->subframe_size;
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
static int g729_bytes2parm(G729A_Context *ctx, const uint8_t *buf, int buf_size, int *parm)
{
    GetBitContext gb;
    int l_frame=formats[ctx->format].bits_per_frame>>3;

    if(buf_size<l_frame)
        return AVERROR(EIO);

    init_get_bits(&gb, buf, buf_size);

    parm[0]=get_bits(&gb, L0_BITS); //L0
    parm[1]=get_bits(&gb, L1_BITS); //L1
    parm[2]=get_bits(&gb, L2_BITS); //L2
    parm[3]=get_bits(&gb, L3_BITS); //L3
    parm[4]=get_bits(&gb, P1_BITS); //P1
    parm[5]=get_bits(&gb, P0_BITS); //Parity
    parm[6]=get_bits(&gb, formats[ctx->format].fc_index_bits*FC_PULSE_COUNT+1); //C1
    parm[7]=get_bits(&gb, FC_PULSE_COUNT); //S1
    parm[5]=get_bits(&gb, GA_BITS); //GA1
    parm[5]=get_bits(&gb, GB_BITS); //GB1
    parm[4]=get_bits(&gb, P2_BITS); //P2
    parm[6]=get_bits(&gb, formats[ctx->format].fc_index_bits*FC_PULSE_COUNT+1); //C2
    parm[7]=get_bits(&gb, FC_PULSE_COUNT); //S2
    parm[5]=get_bits(&gb, GA_BITS); //GA2
    parm[5]=get_bits(&gb, GB_BITS); //GB2
    return 0;
}


static int ff_g729a_decode_frame(AVCodecContext *avctx,
                             void *data, int *data_size,
                             const uint8_t *buf, int buf_size)
{
    int parm[VECTOR_SIZE];
    G729A_Context *ctx=avctx->priv_data;
    int l_frame=formats[ctx->format].bits_per_frame;

    g729_bytes2parm(ctx, buf, buf_size, parm);

    *data_size=0;
    g729a_decode_frame_internal(ctx,(short*)data, l_frame*sizeof(short), parm);
    *data_size+=l_frame*sizeof(short);

    return buf_size;
}

AVCodec g729a_decoder = {
    "g729a",
    CODEC_TYPE_AUDIO,
    CODEC_ID_G729A,
    sizeof(G729A_Context),
    ff_g729a_decoder_init,
    NULL,
    ff_g729a_decoder_close,
    ff_g729a_decode_frame,
};

#ifdef G729A_NATIVE
/**
 * \brief decodes ITU's bitstream format frame intovector of  parameters
 * \param ctx private data structure
 * \param serial input bitstream
 * \param serial_size size of input bitstream buffer
 * \param parm output vector of parameters
 *
 * \return 0 if success, nonzero - otherwise
 */
static int g729_bitstream2parm(G729A_Context *ctx, short* serial, int serial_size, int *parm)
{
    int j;
    int idx=2;

    if(serial_size<2*ctx->subframe_size+2)
        return AVERROR(EIO);

    //L0
    parm[0]=0;
    for(j=0; j<L0_BITS; j++)
    {
        parm[0]<<= 1;
        parm[0] |= serial[idx++]==0x81?1:0;
    }
    //L1
    parm[1]=0;
    for(j=0; j<L1_BITS; j++)
    {
        parm[1]<<= 1;
        parm[1] |= serial[idx++]==0x81?1:0;
    }
    //L2
    parm[2]=0;
    for(j=0; j<L2_BITS; j++)
    {
        parm[2]<<= 1;
        parm[2] |= serial[idx++]==0x81?1:0;
    }
    //L3
    parm[3]=0;
    for(j=0; j<L3_BITS; j++)
    {
        parm[3]<<= 1;
        parm[3] |= serial[idx++]==0x81?1:0;
    }
    //P1
    parm[4]=0;
    for(j=0; j<P1_BITS; j++)
    {
        parm[4]<<= 1;
        parm[4] |= serial[idx++]==0x81?1:0;
    }
    //P0
    parm[5]=0;
    for(j=0; j<P0_BITS; j++)
    {
        parm[5]<<= 1;
        parm[5] |= serial[idx++]==0x81?1:0;
    }
    //C1
    parm[6]=0;
    for(j=0; j<formats[ctx->format].fc_index_bits*FC_PULSE_COUNT+1; j++)
    {
        parm[6]<<= 1;
        parm[6] |= serial[idx++]==0x81?1:0;
    }
    //S1
    parm[7]=0;
    for(j=0; j<FC_PULSE_COUNT; j++)
    {
        parm[7]<<= 1;
        parm[7] |= serial[idx++]==0x81?1:0;
    }
    //GA1
    parm[8]=0;
    for(j=0; j<GA_BITS; j++)
    {
        parm[8]<<= 1;
        parm[8] |= serial[idx++]==0x81?1:0;
    }
    //GB1
    parm[9]=0;
    for(j=0; j<GB_BITS; j++)
    {
        parm[9]<<= 1;
        parm[9] |= serial[idx++]==0x81?1:0;
    }
    //P2
    parm[10]=0;
    for(j=0; j<P2_BITS; j++)
    {
        parm[10]<<= 1;
        parm[10] |= serial[idx++]==0x81?1:0;
    }
    //C2
    parm[11]=0;
    for(j=0; j<formats[ctx->format].fc_index_bits*FC_PULSE_COUNT+1; j++)
    {
        parm[11]<<= 1;
        parm[11] |= serial[idx++]==0x81?1:0;
    }
    //S2
    parm[12]=0;
    for(j=0; j<FC_PULSE_COUNT; j++)
    {
        parm[12]<<= 1;
        parm[12] |= serial[idx++]==0x81?1:0;
    }
    //GA2
    parm[13]=0;
    for(j=0; j<GA_BITS; j++)
    {
        parm[13]<<= 1;
        parm[13] |= serial[idx++]==0x81?1:0;
    }
    //GB2
    parm[14]=0;
    for(j=0; j<GB_BITS; j++)
    {
        parm[14]<<= 1;
        parm[14] |= serial[idx++]==0x81?1:0;
    }
    return 0;
}

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
  ff_g729a_decoder_close(ctx);
  return 0;
}
int  g729a_decode_frame(AVCodecContext* avctx, short* serial, int serial_size, short* out_frame, int out_frame_size)
{
    int parm[VECTOR_SIZE];
    g729_bitstream2parm(avctx->priv_data, serial, 82, parm);
    return g729a_decode_frame_internal(avctx->priv_data, out_frame, out_frame_size, parm);
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
