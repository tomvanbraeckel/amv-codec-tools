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

#define VECTOR_SIZE 15
#define MA_NP 4

/*
-------------------------------------------------------------------------------
    Formats description
-------------------------------------------------------------------------------
*/
/// Number of pulses in fixed-codebook vector
#define FC_PULSE_COUNT 4
/**
 * GA codebook (3.9.2)
 */
#define GA_BITS 3
/*
 * GB codebook (3.9.2)
 */
#define GB_BITS 4

static const struct{
    char* name;
    int sample_rate;
    char frame_size; //bits
    char fc_index_bits;
    char vector_bits[VECTOR_SIZE];
    char silence_compression;
} formats[]={
  {"8Kb/s",   8000, 80, 3, {1,7,5,5,8,1,/*fc_index_bits*/3*FC_PULSE_COUNT+1,FC_PULSE_COUNT,GA_BITS,GB_BITS,5,/*fc_index_bits*/3*FC_PULSE_COUNT+1, FC_PULSE_COUNT,GA_BITS,GB_BITS}, 0},
#ifdef G729_SUPPORT_4400
// Note: may not work
  {"4.4Kb/s", 4400, 88, 4, {1,7,5,5,8,1,/*fc_index_bits*/4*FC_PULSE_COUNT+1,FC_PULSE_COUNT,GA_BITS,GB_BITS,5,/*fc_index_bits*/4*FC_PULSE_COUNT+1, FC_PULSE_COUNT,GA_BITS,GB_BITS}, 0},
#endif //G729_SUPPORT_4400
  { NULL,     0,    0,  0, {0,0,0,0,0,0, 0, 0,0,0,0, 0, 0,0,0}, 0}
};

typedef struct
{
    int format;             ///< format index from formats array
    int subframe_size;      ///< number of samples produced from one subframe
    int data_error;         ///< data error detected during decoding
    int bad_pitch;          ///< parity check failed
    float* exc_base;          ///< past excitation signal buffer
    float* exc;               ///< start of past excitation data in buffer
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
    int rand_seed;          ///< seed for random number generator (4.4.4)
    int prev_mode;
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
 * L1 codebook (10-dimensional, with 128 entries (3.2.4)
 */
static const float cb_L1[128][10] = {
  {0.1814, 0.2647, 0.4580, 1.1077, 1.4813, 1.7022, 2.1953, 2.3405, 2.5867, 2.6636},
  {0.2113, 0.3223, 0.4212, 0.5946, 0.7479, 0.9615, 1.9097, 2.1750, 2.4773, 2.6737},
  {0.1915, 0.2755, 0.3770, 0.5950, 1.3505, 1.6349, 2.2348, 2.3552, 2.5768, 2.6540},
  {0.2116, 0.3067, 0.4099, 0.5748, 0.8518, 1.2569, 2.0782, 2.1920, 2.3371, 2.4842},
  {0.2129, 0.2974, 0.4039, 1.0659, 1.2735, 1.4658, 1.9061, 2.0312, 2.6074, 2.6750},
  {0.2181, 0.2893, 0.4117, 0.5519, 0.8295, 1.5825, 2.1575, 2.3179, 2.5458, 2.6417},
  {0.1991, 0.2971, 0.4104, 0.7725, 1.3073, 1.4665, 1.6208, 1.6973, 2.3732, 2.5743},
  {0.1818, 0.2886, 0.4018, 0.7630, 1.1264, 1.2699, 1.6899, 1.8650, 2.1633, 2.6186},
  {0.2282, 0.3093, 0.4243, 0.5329, 1.1173, 1.7717, 1.9420, 2.0780, 2.5160, 2.6137},
  {0.2528, 0.3693, 0.5290, 0.7146, 0.9528, 1.1269, 1.2936, 1.9589, 2.4548, 2.6653},
  {0.2332, 0.3263, 0.4174, 0.5202, 1.3633, 1.8447, 2.0236, 2.1474, 2.3572, 2.4738},
  {0.1393, 0.2216, 0.3204, 0.5644, 0.7929, 1.1705, 1.7051, 2.0054, 2.3623, 2.5985},
  {0.2677, 0.3871, 0.5746, 0.7091, 1.3311, 1.5260, 1.7288, 1.9122, 2.5787, 2.6598},
  {0.1570, 0.2328, 0.3111, 0.4216, 1.1688, 1.4605, 1.9505, 2.1173, 2.4038, 2.7460},
  {0.2346, 0.3321, 0.5621, 0.8160, 1.4042, 1.5860, 1.7518, 1.8631, 2.0749, 2.5380},
  {0.2505, 0.3368, 0.4758, 0.6405, 0.8104, 1.2533, 1.9329, 2.0526, 2.2155, 2.6459},
  {0.2196, 0.3049, 0.6857, 1.3976, 1.6100, 1.7958, 2.0813, 2.2211, 2.4789, 2.5857},
  {0.1232, 0.2011, 0.3527, 0.6969, 1.1647, 1.5081, 1.8593, 2.2576, 2.5594, 2.6896},
  {0.3682, 0.4632, 0.6600, 0.9118, 1.5245, 1.7071, 1.8712, 1.9939, 2.4356, 2.5380},
  {0.2690, 0.3711, 0.4635, 0.6644, 1.4633, 1.6495, 1.8227, 1.9983, 2.1797, 2.2954},
  {0.3555, 0.5240, 0.9751, 1.1685, 1.4114, 1.6168, 1.7769, 2.0178, 2.4420, 2.5724},
  {0.3493, 0.4404, 0.7231, 0.8587, 1.1272, 1.4715, 1.6760, 2.2042, 2.4735, 2.5604},
  {0.3747, 0.5263, 0.7284, 0.8994, 1.4017, 1.5502, 1.7468, 1.9816, 2.2380, 2.3404},
  {0.2972, 0.4470, 0.5941, 0.7078, 1.2675, 1.4310, 1.5930, 1.9126, 2.3026, 2.4208},
  {0.2467, 0.3180, 0.4712, 1.1281, 1.6206, 1.7876, 1.9544, 2.0873, 2.3521, 2.4721},
  {0.2292, 0.3430, 0.4383, 0.5747, 1.3497, 1.5187, 1.9070, 2.0958, 2.2902, 2.4301},
  {0.2573, 0.3508, 0.4484, 0.7079, 1.6577, 1.7929, 1.9456, 2.0847, 2.3060, 2.4208},
  {0.1968, 0.2789, 0.3594, 0.4361, 1.0034, 1.7040, 1.9439, 2.1044, 2.2696, 2.4558},
  {0.2955, 0.3853, 0.7986, 1.2470, 1.4723, 1.6522, 1.8684, 2.0084, 2.2849, 2.4268},
  {0.2036, 0.3189, 0.4314, 0.6393, 1.2834, 1.4278, 1.5796, 2.0506, 2.2044, 2.3656},
  {0.2916, 0.3684, 0.5907, 1.1394, 1.3933, 1.5540, 1.8341, 1.9835, 2.1301, 2.2800},
  {0.2289, 0.3402, 0.5166, 0.7716, 1.0614, 1.2389, 1.4386, 2.0769, 2.2715, 2.4366},
  {0.0829, 0.1723, 0.5682, 0.9773, 1.3973, 1.6174, 1.9242, 2.2128, 2.4855, 2.6327},
  {0.2244, 0.3169, 0.4368, 0.5625, 0.6897, 1.3763, 1.7524, 1.9393, 2.5121, 2.6556},
  {0.1591, 0.2387, 0.2924, 0.4056, 1.4677, 1.6802, 1.9389, 2.2067, 2.4635, 2.5919},
  {0.1756, 0.2566, 0.3251, 0.4227, 1.0167, 1.2649, 1.6801, 2.1055, 2.4088, 2.7276},
  {0.1050, 0.2325, 0.7445, 0.9491, 1.1982, 1.4658, 1.8093, 2.0397, 2.4155, 2.5797},
  {0.2043, 0.3324, 0.4522, 0.7477, 0.9361, 1.1533, 1.6703, 1.7631, 2.5071, 2.6528},
  {0.1522, 0.2258, 0.3543, 0.5504, 0.8815, 1.5516, 1.8110, 1.9915, 2.3603, 2.7735},
  {0.1862, 0.2759, 0.4715, 0.6908, 0.8963, 1.4341, 1.6322, 1.7630, 2.2027, 2.6043},
  {0.1460, 0.2254, 0.3790, 0.8622, 1.3394, 1.5754, 1.8084, 2.0798, 2.4319, 2.7632},
  {0.2621, 0.3792, 0.5463, 0.7948, 1.0043, 1.1921, 1.3409, 1.4845, 2.3159, 2.6002},
  {0.1935, 0.2937, 0.3656, 0.4927, 1.4015, 1.6086, 1.7724, 1.8837, 2.4374, 2.5971},
  {0.2171, 0.3282, 0.4412, 0.5713, 1.1554, 1.3506, 1.5227, 1.9923, 2.4100, 2.5391},
  {0.2274, 0.3157, 0.4263, 0.8202, 1.4293, 1.5884, 1.7535, 1.9688, 2.3939, 2.4934},
  {0.1704, 0.2633, 0.3259, 0.4134, 1.2948, 1.4802, 1.6619, 2.0393, 2.3165, 2.6083},
  {0.1763, 0.2585, 0.4012, 0.7609, 1.1503, 1.5847, 1.8309, 1.9352, 2.0982, 2.6681},
  {0.2447, 0.3535, 0.4618, 0.5979, 0.7530, 0.8908, 1.5393, 2.0075, 2.3557, 2.6203},
  {0.1826, 0.3496, 0.7764, 0.9888, 1.3915, 1.7421, 1.9412, 2.1620, 2.4999, 2.6931},
  {0.3033, 0.3802, 0.6981, 0.8664, 1.0254, 1.5401, 1.7180, 1.8124, 2.5068, 2.6119},
  {0.2960, 0.4001, 0.6465, 0.7672, 1.3782, 1.5751, 1.9559, 2.1373, 2.3601, 2.4760},
  {0.3132, 0.4613, 0.6544, 0.8532, 1.0721, 1.2730, 1.7566, 1.9217, 2.1693, 2.6531},
  {0.3329, 0.4131, 0.8073, 1.1297, 1.2869, 1.4937, 1.7885, 1.9150, 2.4505, 2.5760},
  {0.2340, 0.3605, 0.7659, 0.9874, 1.1854, 1.3337, 1.5128, 2.0062, 2.4427, 2.5859},
  {0.4131, 0.5330, 0.6530, 0.9360, 1.3648, 1.5388, 1.6994, 1.8707, 2.4294, 2.5335},
  {0.3754, 0.5229, 0.7265, 0.9301, 1.1724, 1.3440, 1.5118, 1.7098, 2.5218, 2.6242},
  {0.2138, 0.2998, 0.6283, 1.2166, 1.4187, 1.6084, 1.7992, 2.0106, 2.5377, 2.6558},
  {0.1761, 0.2672, 0.4065, 0.8317, 1.0900, 1.4814, 1.7672, 1.8685, 2.3969, 2.5079},
  {0.2801, 0.3535, 0.4969, 0.9809, 1.4934, 1.6378, 1.8021, 2.1200, 2.3135, 2.4034},
  {0.2365, 0.3246, 0.5618, 0.8176, 1.1073, 1.5702, 1.7331, 1.8592, 1.9589, 2.3044},
  {0.2529, 0.3251, 0.5147, 1.1530, 1.3291, 1.5005, 1.7028, 1.8200, 2.3482, 2.4831},
  {0.2125, 0.3041, 0.4259, 0.9935, 1.1788, 1.3615, 1.6121, 1.7930, 2.5509, 2.6742},
  {0.2685, 0.3518, 0.5707, 1.0410, 1.2270, 1.3927, 1.7622, 1.8876, 2.0985, 2.5144},
  {0.2373, 0.3648, 0.5099, 0.7373, 0.9129, 1.0421, 1.7312, 1.8984, 2.1512, 2.6342},
  {0.2229, 0.3876, 0.8621, 1.1986, 1.5655, 1.8861, 2.2376, 2.4239, 2.6648, 2.7359},
  {0.3009, 0.3719, 0.5887, 0.7297, 0.9395, 1.8797, 2.0423, 2.1541, 2.5132, 2.6026},
  {0.3114, 0.4142, 0.6476, 0.8448, 1.2495, 1.7192, 2.2148, 2.3432, 2.5246, 2.6046},
  {0.3666, 0.4638, 0.6496, 0.7858, 0.9667, 1.4213, 1.9300, 2.0564, 2.2119, 2.3170},
  {0.4218, 0.5075, 0.8348, 1.0009, 1.2057, 1.5032, 1.9416, 2.0540, 2.4352, 2.5504},
  {0.3726, 0.4602, 0.5971, 0.7093, 0.8517, 1.2361, 1.8052, 1.9520, 2.4137, 2.5518},
  {0.4482, 0.5318, 0.7114, 0.8542, 1.0328, 1.4751, 1.7278, 1.8237, 2.3496, 2.4931},
  {0.3316, 0.4498, 0.6404, 0.8162, 1.0332, 1.2209, 1.5130, 1.7250, 1.9715, 2.4141},
  {0.2375, 0.3221, 0.5042, 0.9760, 1.7503, 1.9014, 2.0822, 2.2225, 2.4689, 2.5632},
  {0.2813, 0.3575, 0.5032, 0.5889, 0.6885, 1.6040, 1.9318, 2.0677, 2.4546, 2.5701},
  {0.2198, 0.3072, 0.4090, 0.6371, 1.6365, 1.9468, 2.1507, 2.2633, 2.5063, 2.5943},
  {0.1754, 0.2716, 0.3361, 0.5550, 1.1789, 1.3728, 1.8527, 1.9919, 2.1349, 2.3359},
  {0.2832, 0.3540, 0.6080, 0.8467, 1.0259, 1.6467, 1.8987, 1.9875, 2.4744, 2.5527},
  {0.2670, 0.3564, 0.5628, 0.7172, 0.9021, 1.5328, 1.7131, 2.0501, 2.5633, 2.6574},
  {0.2729, 0.3569, 0.6252, 0.7641, 0.9887, 1.6589, 1.8726, 1.9947, 2.1884, 2.4609},
  {0.2155, 0.3221, 0.4580, 0.6995, 0.9623, 1.2339, 1.6642, 1.8823, 2.0518, 2.2674},
  {0.4224, 0.7009, 1.1714, 1.4334, 1.7595, 1.9629, 2.2185, 2.3304, 2.5446, 2.6369},
  {0.4560, 0.5403, 0.7568, 0.8989, 1.1292, 1.7687, 1.9575, 2.0784, 2.4260, 2.5484},
  {0.4299, 0.5833, 0.8408, 1.0596, 1.5524, 1.7484, 1.9471, 2.2034, 2.4617, 2.5812},
  {0.2614, 0.3624, 0.8381, 0.9829, 1.2220, 1.6064, 1.8083, 1.9362, 2.1397, 2.2773},
  {0.5064, 0.7481, 1.1021, 1.3271, 1.5486, 1.7096, 1.9503, 2.1006, 2.3911, 2.5141},
  {0.5375, 0.6552, 0.8099, 1.0219, 1.2407, 1.4160, 1.8266, 1.9936, 2.1951, 2.2911},
  {0.4994, 0.6575, 0.8365, 1.0706, 1.4116, 1.6224, 1.9200, 2.0667, 2.3262, 2.4539},
  {0.3353, 0.4426, 0.6469, 0.9161, 1.2528, 1.3956, 1.6080, 1.8909, 2.0600, 2.1380},
  {0.2745, 0.4341, 1.0424, 1.2928, 1.5461, 1.7940, 2.0161, 2.1758, 2.4742, 2.5937},
  {0.1562, 0.2393, 0.4786, 0.9513, 1.2395, 1.8010, 2.0320, 2.2143, 2.5243, 2.6204},
  {0.2979, 0.4242, 0.8224, 1.0564, 1.4881, 1.7808, 2.0898, 2.1882, 2.3328, 2.4389},
  {0.2294, 0.3070, 0.5490, 0.9244, 1.2229, 1.8248, 1.9704, 2.0627, 2.2458, 2.3653},
  {0.3423, 0.4502, 0.9144, 1.2313, 1.3694, 1.5517, 1.9907, 2.1326, 2.4509, 2.5789},
  {0.2470, 0.3275, 0.4729, 1.0093, 1.2519, 1.4216, 1.8540, 2.0877, 2.3151, 2.4156},
  {0.3447, 0.4401, 0.7099, 1.0493, 1.2312, 1.4001, 2.0225, 2.1317, 2.2894, 2.4263},
  {0.3481, 0.4494, 0.6446, 0.9336, 1.1198, 1.2620, 1.8264, 1.9712, 2.1435, 2.2552},
  {0.1646, 0.3229, 0.7112, 1.0725, 1.2964, 1.5663, 1.9843, 2.2363, 2.5798, 2.7572},
  {0.2614, 0.3707, 0.5241, 0.7425, 0.9269, 1.2976, 2.0945, 2.2014, 2.6204, 2.6959},
  {0.1963, 0.2900, 0.4131, 0.8397, 1.2171, 1.3705, 2.0665, 2.1546, 2.4640, 2.5782},
  {0.3387, 0.4415, 0.6121, 0.8005, 0.9507, 1.0937, 2.0836, 2.2342, 2.3849, 2.5076},
  {0.2362, 0.5876, 0.7574, 0.8804, 1.0961, 1.4240, 1.9519, 2.1742, 2.4935, 2.6493},
  {0.2793, 0.4282, 0.6149, 0.8352, 1.0106, 1.1766, 1.8392, 2.0119, 2.6433, 2.7117},
  {0.3603, 0.4604, 0.5955, 0.9251, 1.1006, 1.2572, 1.7688, 1.8607, 2.4687, 2.5623},
  {0.3975, 0.5849, 0.8059, 0.9182, 1.0552, 1.1850, 1.6356, 1.9627, 2.3318, 2.4719},
  {0.2231, 0.3192, 0.4256, 0.7373, 1.4831, 1.6874, 1.9765, 2.1097, 2.6152, 2.6906},
  {0.1221, 0.2081, 0.3665, 0.7734, 1.0341, 1.2818, 1.8162, 2.0727, 2.4446, 2.7377},
  {0.2010, 0.2791, 0.3796, 0.8845, 1.4030, 1.5615, 2.0538, 2.1567, 2.3171, 2.4686},
  {0.2086, 0.3053, 0.4047, 0.8224, 1.0656, 1.2115, 1.9641, 2.0871, 2.2430, 2.4313},
  {0.3203, 0.4285, 0.5467, 0.6891, 1.2039, 1.3569, 1.8578, 2.2055, 2.3906, 2.4881},
  {0.3074, 0.4192, 0.5772, 0.7799, 0.9866, 1.1335, 1.6068, 2.2441, 2.4194, 2.5089},
  {0.2108, 0.2910, 0.4993, 0.7695, 0.9528, 1.5681, 1.7838, 2.1495, 2.3522, 2.4636},
  {0.3492, 0.4560, 0.5906, 0.7379, 0.8855, 1.0257, 1.7128, 1.9997, 2.2019, 2.3694},
  {0.5185, 0.7316, 0.9708, 1.1954, 1.5066, 1.7887, 2.1396, 2.2918, 2.5429, 2.6489},
  {0.4276, 0.4946, 0.6934, 0.8308, 0.9944, 1.4582, 2.0324, 2.1294, 2.4891, 2.6324},
  {0.3847, 0.5973, 0.7202, 0.8787, 1.3938, 1.5959, 1.8463, 2.1574, 2.5050, 2.6687},
  {0.4835, 0.5919, 0.7235, 0.8862, 1.0756, 1.2853, 1.9118, 2.0215, 2.2213, 2.4638},
  {0.5492, 0.8062, 0.9810, 1.1293, 1.3189, 1.5415, 1.9385, 2.1378, 2.4439, 2.5691},
  {0.5190, 0.6764, 0.8123, 1.0154, 1.2085, 1.4266, 1.8433, 2.0866, 2.5113, 2.6474},
  {0.4602, 0.6503, 0.9602, 1.1427, 1.3043, 1.4427, 1.6676, 1.8758, 2.2868, 2.4271},
  {0.3764, 0.4845, 0.7627, 0.9914, 1.1961, 1.3421, 1.5129, 1.6707, 2.1836, 2.3322},
  {0.3334, 0.5701, 0.8622, 1.1232, 1.3851, 1.6767, 2.0600, 2.2946, 2.5375, 2.7295},
  {0.1449, 0.2719, 0.5783, 0.8807, 1.1746, 1.5422, 1.8804, 2.1934, 2.4734, 2.8728},
  {0.2333, 0.3024, 0.4780, 1.2327, 1.4180, 1.5815, 1.9804, 2.0921, 2.3524, 2.5304},
  {0.2154, 0.3075, 0.4746, 0.8477, 1.1170, 1.5369, 1.9847, 2.0733, 2.1880, 2.2504},
  {0.1709, 0.4486, 0.8705, 1.0643, 1.3047, 1.5269, 1.9175, 2.1621, 2.4073, 2.5718},
  {0.2835, 0.3752, 0.5234, 0.9898, 1.1484, 1.2974, 1.9363, 2.0378, 2.4065, 2.6214},
  {0.3211, 0.4077, 0.5809, 1.0206, 1.2542, 1.3835, 1.5723, 2.1209, 2.3464, 2.4336},
  {0.2101, 0.3146, 0.6779, 0.8783, 1.0561, 1.3045, 1.8395, 2.0695, 2.2831, 2.4328},
};
/**
 * L2 codebook (10-dimensional, with 32 entries (3.2.4)
 */
static const float cb_L2[32][5] = {
  {-0.0532, -0.0995, -0.0906,  0.1261, -0.0633},
  {-0.1017, -0.1088,  0.0566, -0.0010, -0.1528},
  {-0.1247,  0.0283, -0.0374,  0.0393, -0.0269},
  { 0.0070, -0.0242, -0.0415, -0.0041, -0.1793},
  { 0.0209, -0.0428,  0.0359,  0.2027,  0.0554},
  {-0.0856, -0.1028, -0.0071,  0.1160,  0.1089},
  { 0.0713,  0.0039, -0.0353,  0.0435, -0.0407},
  {-0.0134, -0.0987,  0.0283,  0.0095, -0.0107},
  {-0.1049,  0.1510,  0.0672,  0.1043,  0.0872},
  {-0.1071, -0.1165, -0.1524, -0.0365,  0.0260},
  {-0.0094,  0.0420, -0.0758,  0.0932,  0.0505},
  {-0.0384, -0.0375, -0.0313, -0.1539, -0.0524},
  { 0.0869,  0.0847,  0.0637,  0.0794,  0.1594},
  {-0.0137, -0.0332, -0.0611,  0.1156,  0.2116},
  { 0.0703, -0.0013, -0.0572, -0.0243,  0.1345},
  { 0.0178, -0.0349, -0.1563, -0.0487,  0.0044},
  {-0.1384, -0.1020,  0.1649,  0.1568, -0.0116},
  {-0.1782, -0.1511,  0.0509, -0.0261,  0.0570},
  {-0.0019,  0.0081,  0.0572,  0.1245, -0.0914},
  {-0.0413,  0.0181,  0.1764,  0.0092, -0.0928},
  { 0.0476,  0.0292,  0.1915,  0.1198,  0.0139},
  {-0.0382, -0.0120,  0.1159,  0.0039,  0.1348},
  { 0.1376,  0.0713,  0.1020,  0.0339, -0.1415},
  { 0.0658, -0.0140,  0.1046, -0.0603,  0.0273},
  { 0.2683,  0.2853,  0.1549,  0.0819,  0.0372},
  {-0.1949,  0.0672,  0.0978, -0.0557, -0.0069},
  { 0.1409,  0.0724, -0.0094,  0.1511, -0.0039},
  { 0.0485,  0.0682,  0.0248, -0.0974, -0.1122},
  { 0.0408,  0.1801,  0.0772, -0.0098,  0.0059},
  {-0.0666, -0.0403, -0.0524, -0.0831,  0.1384},
  { 0.1612,  0.1010, -0.0486, -0.0704,  0.0417},
  {-0.0199,  0.0823, -0.0014, -0.1082,  0.0649},
};

/**
 * L3 codebook (10-dimensional, with 32 entries (3.2.4)
 */
static const float cb_L3[32][5] = {
  { 0.0711, -0.1467,  0.1012,  0.0106,  0.0470},
  { 0.1771,  0.0089, -0.0282,  0.1055,  0.0808},
  {-0.0200, -0.0643, -0.0921, -0.1994,  0.0327},
  { 0.0700,  0.0972, -0.0207, -0.0771,  0.0997},
  { 0.0634,  0.0356,  0.0195, -0.0782, -0.1583},
  { 0.1892,  0.0874,  0.0644, -0.0872, -0.0236},
  {-0.0558,  0.0748, -0.0346, -0.1686, -0.0905},
  {-0.0420,  0.1638,  0.1328, -0.0799, -0.0695},
  {-0.0663, -0.2139, -0.0239, -0.0120, -0.0338},
  {-0.0288, -0.0889,  0.1159,  0.1852,  0.1093},
  { 0.0614, -0.0443, -0.1172, -0.0590,  0.1693},
  { 0.0550, -0.0569, -0.0133,  0.1233,  0.2714},
  {-0.0035, -0.0462,  0.0909, -0.1227,  0.0294},
  { 0.0332, -0.0019,  0.1110, -0.0317,  0.2061},
  {-0.1235,  0.0710, -0.0065, -0.0912,  0.1072},
  {-0.0609, -0.1682,  0.0023, -0.0542,  0.1811},
  { 0.1240, -0.0271,  0.0541,  0.0455, -0.0433},
  { 0.0817,  0.0805,  0.2003,  0.1138,  0.0653},
  { 0.1691, -0.0223, -0.1108, -0.0881, -0.0320},
  { 0.0695,  0.1523,  0.0412,  0.0508, -0.0148},
  { 0.0451, -0.1225, -0.0619, -0.0717, -0.1104},
  { 0.0088, -0.0173,  0.1789,  0.0078, -0.0959},
  { 0.0254,  0.0368, -0.1077,  0.0143, -0.0494},
  {-0.1114,  0.0761, -0.0093,  0.0338, -0.0538},
  {-0.0327, -0.0642,  0.0172,  0.1077, -0.0170},
  {-0.0851,  0.1057,  0.1294,  0.0505,  0.0545},
  { 0.0710, -0.1266, -0.1093,  0.0817,  0.0363},
  { 0.0004,  0.0845, -0.0357,  0.1282,  0.0955},
  {-0.1296, -0.0591,  0.0443, -0.0729, -0.1041},
  {-0.1443, -0.0909,  0.1636,  0.0320,  0.0077},
  {-0.0945, -0.0590, -0.1523, -0.0086,  0.0120},
  {-0.1374, -0.0324, -0.0296,  0.0885,  0.1141},
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
 */
static const float ma_predictor[2][4][10] = {
  {
    { 0.2570,    0.2780,    0.2800,    0.2736,    0.2757,    0.2764,    0.2675,    0.2678,    0.2779,    0.2647},
    { 0.2142,    0.2194,    0.2331,    0.2230,    0.2272,    0.2252,    0.2148,    0.2123,    0.2115,    0.2096},
    { 0.1670,    0.1523,    0.1567,    0.1580,    0.1601,    0.1569,    0.1589,    0.1555,    0.1474,    0.1571},
    { 0.1238,    0.0925,    0.0798,    0.0923,    0.0890,    0.0828,    0.1010,    0.0988,    0.0872,    0.1060},
  },
  {
    { 0.2360,    0.2405,    0.2499,    0.2495,    0.2517,    0.2591,    0.2636,    0.2625,    0.2551,    0.2310},
    { 0.1285,    0.0925,    0.0779,    0.1060,    0.1183,    0.1176,    0.1277,    0.1268,    0.1193,    0.1211},
    { 0.0981,    0.0589,    0.0401,    0.0654,    0.0761,    0.0728,    0.0841,    0.0826,    0.0776,    0.0891},
    { 0.0923,    0.0486,    0.0287,    0.0498,    0.0526,    0.0482,    0.0621,    0.0636,    0.0584,    0.0794}
  }
};

/**
 * ma_predicot_sum[i] := 1-sum{1}{4}{ma_predictor[k][i]}
 */
static const float ma_predictor_sum[2][10] = {
  { 0.2380000054836, 0.2578000128269, 0.2504000067711, 0.2531000375748, 0.2480000108480, 0.2587000429630, 0.2577999532223, 0.2656000256538, 0.2760000228882, 0.2625999450684},
  { 0.4451000094414, 0.5595000386238, 0.6034000515938, 0.5292999744415, 0.5012999176979, 0.5023000240326, 0.4625000357628, 0.4645000100136, 0.4895999729633, 0.4793999791145}
};

/**
 * MA prediction coefficients (3.9.1, near Equation 69)
 */
static const float ma_prediction_coeff[4] =
{
  0.68, 0.58, 0.34, 0.19
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
    return ctx->rand_seed = (uint16_t)(31821 * (uint32_t)ctx->rand_seed + 13849 + ctx->rand_seed);
}


/**
 * \brief Check parity bit (3.7.2)
 * \param P1 Pitch delay first subframe
 * \param P0 Parity bit for Pitch delay
 *
 * \return 1 if parity check is ok, 0 - otherwise
 */
int g729_parity_check(int P1, int P0)
{
    int P=P1>>2;
    int S=P0&1;
    int i;

    for(i=0; i<6; i++)
    {
        S ^= P&1;
        P >>= 1;
    }
    S ^= 1;
    return (!S);
}

/**
 * \brief Decoding of the adaptive-codebook vector delay for first subframe (4.1.3)
 * \param ctx private data structure
 * \param P1 Pitch delay first subframe
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
 * \param P1 Pitch delay second subframe
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
        energy+= ctx->pred_vect_q[i] * ma_prediction_coeff[i];

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
    float* tmp_buf=av_mallocz((10+ctx->subframe_size)*sizeof(float));
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
    float* residual_filt_buf=av_mallocz((ctx->subframe_size+10)*sizeof(float));
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

    av_free(residual_filt_buf);
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
    const float az[3] = {0.93980581, -1.8795834,  0.93980581};
    const float af[3] = {1.00000000,  1.9330735, -0.93589199};
    float z_2=0;

    float f_0=0;
    int i;

    for(i=0; i<ctx->subframe_size; i++)
    {
        z_2=ctx->hpf_z1;
        ctx->hpf_z1=ctx->hpf_z0;
        ctx->hpf_z0=speech[i];

        f_0 = ctx->hpf_f1*af[1]+ctx->hpf_f2*af[2] + ctx->hpf_z0*az[0]+ctx->hpf_z1*az[1]+z_2*az[2];
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
    float* tmp_speech_buf=av_mallocz((ctx->subframe_size+10)*sizeof(float));
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

    av_free(tmp_speech_buf);

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
        lq[i]=lsfq[i];
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
        lq[i]  =cb_L1[L1][i  ] + cb_L2[L2][i];
        lq[i+5]=cb_L1[L1][i+5] + cb_L3[L3][i];
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

    for(ctx->format=0; formats[ctx->format].name; ctx->format++)
        if(formats[ctx->format].sample_rate==avctx->sample_rate)
            break;
    if(!formats[ctx->format].name){
        av_log(avctx, AV_LOG_ERROR, "Sample rate %d is not supported\n", avctx->sample_rate);
        return -1;
    }
    frame_size=formats[ctx->format].frame_size>>3; //frame_size is in bits

    ctx->subframe_size=formats[ctx->format].frame_size>>1;

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
    ctx->rand_seed=21845;

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
    int i;
    int l_frame=formats[ctx->format].frame_size>>3;

    if(buf_size<l_frame)
        return AVERROR(EIO);

    init_get_bits(&gb, buf, buf_size);

    for(i=0; i<VECTOR_SIZE; i++)
    {
        if(formats[ctx->format].vector_bits[i]>16)
            return AVERROR(EIO);
        parm[i]=get_bits(&gb, formats[ctx->format].vector_bits[i]);
    }
    return 0;
}


static int ff_g729a_decode_frame(AVCodecContext *avctx,
                             void *data, int *data_size,
                             const uint8_t *buf, int buf_size)
{
    int parm[VECTOR_SIZE];
    G729A_Context *ctx=avctx->priv_data;
    int l_frame=formats[ctx->format].frame_size;

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
    int i,j;
    int idx=2;

    if(serial_size<2*ctx->subframe_size+2)
        return AVERROR(EIO);

    for(i=0; i<VECTOR_SIZE; i++)
    {
        if(formats[ctx->format].vector_bits[i]>16)
            return AVERROR(EIO);
        parm[i]=0;
        for(j=0; j<formats[ctx->format].vector_bits[i]; j++)
        {
            parm[i]<<= 1;
            parm[i] |= serial[idx++]==0x81?1:0;
        }
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
