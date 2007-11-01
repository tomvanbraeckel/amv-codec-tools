/*
 * G.729 Annex A decoder
 * Copyright (c) 2007 Vladimir Voroshilov
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
#include <math.h>
#include <stdio.h>

#define VECTOR_SIZE 15
#define MA_NP 4

#define FFSWAP(type,a,b) do{type SWAP_tmp= b; b= a; a= SWAP_tmp;}while(0)
#define FFMAX(a,b) ((a) > (b) ? (a) : (b))
#define FFMIN(a,b) ((a) > (b) ? (b) : (a))

typedef struct
{
    int format;             ///< format index from formats array
    int subframe_size;      ///< number of samples produced from one subframe
    int data_error;         ///< data error detected during decoding
    int* exc_base;          ///< past excitation signal buffer
    int* exc;               ///< start of past excitation data in buffer
    int intT2_prev;         ///< int(T2) value of previous frame (4.1.3)
    float *lq_prev[MA_NP];  ///< l[i], LSP quantizer output (3.2.4)
    float lsp_prev[10];     ///< q[i], LSP coefficients from previous frame (3.2.5)
    float pred_vect_q[4];   ///< quantized prediction error
    float gain_pitch;       ///< Pitch gain of previous subframe (3.8) [GAIN_PITCH_MIN ... GAIN_PITCH_MAX]
    short syn_filter_data[10];
    float g[40];            ///< gain coefficient (4.2.4)
    int rand_seed;          ///< seed for random number generator (4.4.4)
    int prev_mode;
}  G729A_Context;

//Stability constants (3.2.4)
#define LSFQ_MIN 0.005
#define LSFQ_MAX 3.135
#define LSFQ_DIFF_MIN 0.0391

/* Gain pitch maximum and minimum (3.8) */
#define GAIN_PITCH_MIN 0.2
#define GAIN_PITCH_MAX 0.8

#define PITCH_MIN 20
#define PITCH_MAX 143
#define INTERPOL_LEN 11

/**
 * L1 codebook (10-dimensional, with 128 entries (3.2.4)
 */
static const float cb_L1[128][10] = {
  {0.181396, 0.264648, 0.457886, 1.107666, 1.481201, 1.702148, 2.195190, 2.340454, 2.586670, 2.663574},
  {0.211182, 0.322266, 0.421143, 0.594482, 0.747803, 0.961426, 1.909668, 2.174927, 2.477295, 2.673584},
  {0.191406, 0.275391, 0.376953, 0.594971, 1.350464, 1.634888, 2.234741, 2.355103, 2.576782, 2.653931},
  {0.211548, 0.306641, 0.409790, 0.574707, 0.851685, 1.256836, 2.078125, 2.191895, 2.337036, 2.484131},
  {0.212891, 0.297363, 0.403809, 1.065796, 1.273438, 1.465698, 1.906006, 2.031128, 2.607300, 2.674927},
  {0.218018, 0.289185, 0.411621, 0.551880, 0.829468, 1.582397, 2.157471, 2.317871, 2.545776, 2.641602},
  {0.199097, 0.296997, 0.410278, 0.772461, 1.307251, 1.466431, 1.620728, 1.697266, 2.373169, 2.574219},
  {0.181763, 0.288574, 0.401733, 0.762939, 1.126343, 1.269897, 1.689819, 1.864990, 2.163208, 2.618530},
  {0.228149, 0.309204, 0.424194, 0.532837, 1.117188, 1.771606, 1.941895, 2.077881, 2.515991, 2.613647},
  {0.252686, 0.369263, 0.528931, 0.714600, 0.952759, 1.126831, 1.293579, 1.958862, 2.454712, 2.665283},
  {0.233154, 0.326294, 0.417358, 0.520142, 1.363281, 1.844604, 2.023560, 2.147339, 2.357178, 2.473755},
  {0.139282, 0.221558, 0.320312, 0.564331, 0.792847, 1.170410, 1.705078, 2.005371, 2.362183, 2.598389},
  {0.267578, 0.387085, 0.574585, 0.708984, 1.331055, 1.525879, 1.728760, 1.912109, 2.578613, 2.659790},
  {0.156982, 0.232788, 0.311035, 0.421509, 1.168701, 1.460449, 1.950439, 2.117188, 2.403687, 2.745972},
  {0.234497, 0.332031, 0.562012, 0.815918, 1.404175, 1.585938, 1.751709, 1.863037, 2.074829, 2.537964},
  {0.250488, 0.336792, 0.475708, 0.640381, 0.810303, 1.253296, 1.932861, 2.052490, 2.215454, 2.645874},
  {0.219482, 0.304810, 0.685669, 1.397583, 1.609985, 1.795776, 2.081299, 2.221069, 2.478882, 2.585693},
  {0.123169, 0.201050, 0.352661, 0.696899, 1.164673, 1.508057, 1.859253, 2.257568, 2.559326, 2.689575},
  {0.368164, 0.463135, 0.659912, 0.911743, 1.524414, 1.707031, 1.871094, 1.993896, 2.435547, 2.537964},
  {0.268921, 0.371094, 0.463379, 0.664307, 1.463257, 1.649414, 1.822632, 1.998291, 2.179688, 2.295288},
  {0.355469, 0.523926, 0.975098, 1.168457, 1.411377, 1.616699, 1.776855, 2.017700, 2.441895, 2.572388},
  {0.349243, 0.440308, 0.723022, 0.858643, 1.127197, 1.471436, 1.675903, 2.204102, 2.473389, 2.560303},
  {0.374634, 0.526245, 0.728394, 0.899292, 1.401611, 1.550171, 1.746704, 1.981567, 2.237915, 2.340332},
  {0.297119, 0.446899, 0.593994, 0.707764, 1.267456, 1.430908, 1.592896, 1.912598, 2.302490, 2.420776},
  {0.246582, 0.317993, 0.471191, 1.128052, 1.620483, 1.787598, 1.954346, 2.087280, 2.352051, 2.472046},
  {0.229126, 0.342896, 0.438232, 0.574585, 1.349609, 1.518677, 1.906982, 2.095703, 2.290161, 2.430054},
  {0.257202, 0.350708, 0.448364, 0.707886, 1.657593, 1.792847, 1.945557, 2.084595, 2.305908, 2.420776},
  {0.196777, 0.278809, 0.359375, 0.436035, 1.003296, 1.703979, 1.943848, 2.104370, 2.269531, 2.455688},
  {0.295410, 0.385254, 0.798584, 1.246948, 1.472290, 1.652100, 1.868286, 2.008301, 2.284790, 2.426758},
  {0.203491, 0.318848, 0.431396, 0.639282, 1.283325, 1.427734, 1.579590, 2.050537, 2.204346, 2.365479},
  {0.291504, 0.368286, 0.590698, 1.139282, 1.393188, 1.553955, 1.833984, 1.983398, 2.130005, 2.279907},
  {0.228882, 0.340088, 0.516479, 0.771484, 1.061279, 1.238892, 1.438599, 2.076782, 2.271484, 2.436523},
  {0.082886, 0.172241, 0.568115, 0.977295, 1.397217, 1.617310, 1.924194, 2.212769, 2.485474, 2.632690},
  {0.224365, 0.316895, 0.436768, 0.562500, 0.689697, 1.376221, 1.752319, 1.939209, 2.512085, 2.655518},
  {0.159058, 0.238647, 0.292358, 0.405518, 1.467651, 1.680176, 1.938843, 2.206665, 2.463379, 2.591797},
  {0.175537, 0.256592, 0.325073, 0.422607, 1.016602, 1.264893, 1.680054, 2.105469, 2.408691, 2.727539},
  {0.104980, 0.232422, 0.744385, 0.949097, 1.198120, 1.465698, 1.809204, 2.039673, 2.415405, 2.579590},
  {0.204224, 0.332397, 0.452148, 0.747681, 0.936035, 1.153198, 1.670288, 1.763062, 2.507080, 2.652710},
  {0.152100, 0.225708, 0.354248, 0.550293, 0.881470, 1.551514, 1.810913, 1.991455, 2.360229, 2.773438},
  {0.186157, 0.275879, 0.471436, 0.690796, 0.896240, 1.434082, 1.632080, 1.762939, 2.202637, 2.604248},
  {0.145996, 0.225342, 0.378906, 0.862183, 1.339355, 1.575317, 1.808350, 2.079712, 2.431885, 2.763184},
  {0.262085, 0.379150, 0.546265, 0.794800, 1.004272, 1.192017, 1.340820, 1.484497, 2.315796, 2.600098},
  {0.193481, 0.293579, 0.365479, 0.492676, 1.401489, 1.608521, 1.772339, 1.883667, 2.437378, 2.597046},
  {0.217041, 0.328125, 0.441162, 0.571289, 1.155396, 1.350586, 1.522583, 1.992188, 2.409912, 2.539062},
  {0.227295, 0.315674, 0.426270, 0.820190, 1.429199, 1.588379, 1.753418, 1.968750, 2.393799, 2.493286},
  {0.170288, 0.263184, 0.325806, 0.413330, 1.294800, 1.480103, 1.661865, 2.039185, 2.316406, 2.608276},
  {0.176270, 0.258423, 0.401123, 0.760864, 1.150269, 1.584595, 1.830811, 1.935181, 2.098145, 2.668091},
  {0.244629, 0.353394, 0.461792, 0.597778, 0.752930, 0.890747, 1.539185, 2.007446, 2.355591, 2.620239},
  {0.182495, 0.349487, 0.776367, 0.988770, 1.391479, 1.742065, 1.941162, 2.161987, 2.499878, 2.692993},
  {0.303223, 0.380127, 0.697998, 0.866333, 1.025391, 1.540039, 1.717896, 1.812378, 2.506714, 2.611816},
  {0.295898, 0.400024, 0.646484, 0.767090, 1.378174, 1.575073, 1.955811, 2.137207, 2.359985, 2.475952},
  {0.313110, 0.461182, 0.654297, 0.853149, 1.072021, 1.272949, 1.756592, 1.921631, 2.169189, 2.653076},
  {0.332886, 0.413086, 0.807251, 1.129639, 1.286865, 1.493652, 1.788452, 1.914917, 2.450439, 2.575928},
  {0.233887, 0.360474, 0.765869, 0.987305, 1.185303, 1.333618, 1.512695, 2.006104, 2.442627, 2.585815},
  {0.413086, 0.532959, 0.652954, 0.935913, 1.364746, 1.538696, 1.699341, 1.870605, 2.429321, 2.533447},
  {0.375366, 0.522827, 0.726440, 0.930054, 1.172363, 1.343994, 1.511719, 1.709717, 2.521729, 2.624146},
  {0.213745, 0.299683, 0.628296, 1.216553, 1.418579, 1.608398, 1.799194, 2.010498, 2.537598, 2.655762},
  {0.176025, 0.267090, 0.406494, 0.831665, 1.089966, 1.481323, 1.767090, 1.868408, 2.396851, 2.507812},
  {0.280029, 0.353394, 0.496826, 0.980835, 1.493286, 1.637695, 1.802002, 2.119995, 2.313477, 2.403320},
  {0.236450, 0.324585, 0.561768, 0.817505, 1.107300, 1.570190, 1.733032, 1.859131, 1.958862, 2.304321},
  {0.252808, 0.325073, 0.514648, 1.152954, 1.328979, 1.500488, 1.702759, 1.819946, 2.348145, 2.483032},
  {0.212402, 0.304077, 0.425781, 0.993408, 1.178711, 1.361450, 1.612061, 1.792969, 2.550781, 2.674194},
  {0.268433, 0.351685, 0.570679, 1.040894, 1.226929, 1.392578, 1.762085, 1.887573, 2.098389, 2.514282},
  {0.237183, 0.364746, 0.509888, 0.737183, 0.912842, 1.041992, 1.731079, 1.898315, 2.151123, 2.634155},
  {0.222778, 0.387573, 0.862061, 1.198486, 1.565430, 1.885986, 2.237549, 2.423828, 2.664795, 2.735840},
  {0.300781, 0.371826, 0.588623, 0.729614, 0.939453, 1.879639, 2.042236, 2.154053, 2.513184, 2.602539},
  {0.311279, 0.414185, 0.647583, 0.844727, 1.249390, 1.719116, 2.214722, 2.343140, 2.524536, 2.604492},
  {0.366577, 0.463745, 0.649536, 0.785767, 0.966675, 1.421265, 1.929932, 2.056396, 2.211792, 2.316895},
  {0.421753, 0.507446, 0.834717, 1.000854, 1.205688, 1.503174, 1.941528, 2.053955, 2.435181, 2.550293},
  {0.372559, 0.460083, 0.597046, 0.709229, 0.851685, 1.236084, 1.805176, 1.951904, 2.413696, 2.551758},
  {0.448120, 0.531738, 0.711304, 0.854126, 1.032715, 1.475098, 1.727783, 1.823608, 2.349487, 2.493042},
  {0.331543, 0.449707, 0.640381, 0.816162, 1.033081, 1.220825, 1.512939, 1.724976, 1.971436, 2.414062},
  {0.237427, 0.322021, 0.504150, 0.975952, 1.750244, 1.901367, 2.082153, 2.222412, 2.468872, 2.563110},
  {0.281250, 0.357422, 0.503174, 0.588867, 0.688477, 1.603882, 1.931763, 2.067627, 2.454590, 2.570068},
  {0.219727, 0.307129, 0.408936, 0.637085, 1.636475, 1.946777, 2.150635, 2.263184, 2.506226, 2.594238},
  {0.175293, 0.271484, 0.336060, 0.554932, 1.178833, 1.372681, 1.852661, 1.991821, 2.134888, 2.335815},
  {0.283081, 0.353882, 0.607910, 0.846680, 1.025879, 1.646606, 1.898682, 1.987427, 2.474365, 2.552612},
  {0.266968, 0.356323, 0.562744, 0.717163, 0.902100, 1.532715, 1.713013, 2.050049, 2.563232, 2.657349},
  {0.272827, 0.356812, 0.625122, 0.764038, 0.988647, 1.658813, 1.872559, 1.994629, 2.188354, 2.460815},
  {0.215454, 0.322021, 0.457886, 0.699463, 0.962280, 1.233887, 1.664185, 1.882202, 2.051758, 2.267334},
  {0.422363, 0.700806, 1.171387, 1.433350, 1.759399, 1.962891, 2.218384, 2.330322, 2.544556, 2.636841},
  {0.455933, 0.540283, 0.756714, 0.898804, 1.129150, 1.768677, 1.957397, 2.078369, 2.425903, 2.548340},
  {0.429810, 0.583252, 0.840698, 1.059570, 1.552368, 1.748291, 1.947021, 2.203369, 2.461670, 2.581177},
  {0.261353, 0.362305, 0.838013, 0.982788, 1.221924, 1.606323, 1.808228, 1.936157, 2.139648, 2.277222},
  {0.506348, 0.748047, 1.102051, 1.327026, 1.548584, 1.709595, 1.950195, 2.100586, 2.390991, 2.514038},
  {0.537476, 0.655151, 0.809814, 1.021851, 1.240601, 1.415894, 1.826538, 1.993530, 2.195068, 2.291016},
  {0.499390, 0.657471, 0.836426, 1.070557, 1.411499, 1.622314, 1.919922, 2.066650, 2.326172, 2.453857},
  {0.335205, 0.442505, 0.646851, 0.916016, 1.252686, 1.395508, 1.607910, 1.890869, 2.059937, 2.137939},
  {0.274414, 0.434082, 1.042358, 1.292725, 1.546021, 1.793945, 2.015991, 2.175781, 2.474121, 2.593628},
  {0.156128, 0.239258, 0.478516, 0.951294, 1.239380, 1.800903, 2.031982, 2.214233, 2.524292, 2.620361},
  {0.297852, 0.424194, 0.822388, 1.056396, 1.488037, 1.780762, 2.089722, 2.188110, 2.332764, 2.438843},
  {0.229370, 0.306885, 0.548950, 0.924316, 1.222778, 1.824707, 1.970337, 2.062622, 2.245728, 2.365234},
  {0.342285, 0.450195, 0.914307, 1.231201, 1.369385, 1.551636, 1.990601, 2.132568, 2.450806, 2.578857},
  {0.246948, 0.327393, 0.472778, 1.009277, 1.251831, 1.421509, 1.853882, 2.087646, 2.315063, 2.415527},
  {0.344604, 0.440063, 0.709839, 1.049194, 1.231079, 1.400024, 2.022461, 2.131592, 2.289307, 2.426270},
  {0.348022, 0.449341, 0.644531, 0.933594, 1.119751, 1.261963, 1.826294, 1.971191, 2.143433, 2.255127},
  {0.164551, 0.322876, 0.711182, 1.072388, 1.296387, 1.566284, 1.984253, 2.236206, 2.579712, 2.757080},
  {0.261353, 0.370605, 0.524048, 0.742432, 0.926880, 1.297485, 2.094482, 2.201294, 2.620361, 2.695801},
  {0.196289, 0.289917, 0.413086, 0.839600, 1.217041, 1.370483, 2.066406, 2.154541, 2.463989, 2.578125},
  {0.338623, 0.441406, 0.612061, 0.800415, 0.950684, 1.093628, 2.083496, 2.234131, 2.384888, 2.507568},
  {0.236084, 0.587524, 0.757324, 0.880371, 1.096069, 1.423950, 1.951782, 2.174194, 2.493408, 2.649292},
  {0.279297, 0.428101, 0.614868, 0.835083, 1.010498, 1.176514, 1.839111, 2.011841, 2.643188, 2.711670},
  {0.360229, 0.460327, 0.595459, 0.925049, 1.100586, 1.257080, 1.768799, 1.860596, 2.468628, 2.562256},
  {0.397461, 0.584839, 0.805786, 0.918091, 1.055176, 1.184937, 1.635498, 1.962646, 2.331787, 2.471802},
  {0.223022, 0.319092, 0.425537, 0.737183, 1.483032, 1.687378, 1.976440, 2.109619, 2.615112, 2.690552},
  {0.122070, 0.208008, 0.366455, 0.773315, 1.034058, 1.281738, 1.816162, 2.072632, 2.444580, 2.737671},
  {0.200928, 0.279053, 0.379517, 0.884399, 1.402954, 1.561401, 2.053711, 2.156616, 2.317017, 2.468506},
  {0.208496, 0.305298, 0.404663, 0.822388, 1.065552, 1.211426, 1.963989, 2.087036, 2.242920, 2.431274},
  {0.320190, 0.428467, 0.546631, 0.689087, 1.203857, 1.356812, 1.857788, 2.205444, 2.390503, 2.488037},
  {0.307373, 0.419189, 0.577148, 0.779785, 0.986572, 1.133423, 1.606689, 2.244019, 2.419312, 2.508789},
  {0.210693, 0.290894, 0.499268, 0.769409, 0.952759, 1.567993, 1.783691, 2.149414, 2.352173, 2.463501},
  {0.349121, 0.455933, 0.590576, 0.737793, 0.885498, 1.025635, 1.712769, 1.999634, 2.201782, 2.369385},
  {0.518433, 0.731567, 0.970703, 1.195312, 1.506592, 1.788696, 2.139526, 2.291748, 2.542847, 2.648804},
  {0.427490, 0.494507, 0.693359, 0.830688, 0.994385, 1.458130, 2.032349, 2.129395, 2.489014, 2.632324},
  {0.384644, 0.597290, 0.720093, 0.878662, 1.393799, 1.595825, 1.846191, 2.157349, 2.504883, 2.668579},
  {0.483398, 0.591797, 0.723389, 0.886108, 1.075562, 1.285278, 1.911743, 2.021484, 2.221191, 2.463745},
  {0.549194, 0.806152, 0.980957, 1.129272, 1.318848, 1.541382, 1.938477, 2.137695, 2.443848, 2.569092},
  {0.518921, 0.676392, 0.812256, 1.015381, 1.208496, 1.426514, 1.843262, 2.086548, 2.511230, 2.647339},
  {0.460083, 0.650269, 0.960083, 1.142578, 1.304199, 1.442627, 1.667480, 1.875732, 2.286743, 2.427002},
  {0.376343, 0.484497, 0.762695, 0.991333, 1.196045, 1.342041, 1.512817, 1.670654, 2.183594, 2.332153},
  {0.333374, 0.570068, 0.862183, 1.123169, 1.385010, 1.676636, 2.059937, 2.294556, 2.537476, 2.729492},
  {0.144897, 0.271851, 0.578247, 0.880615, 1.174561, 1.542114, 1.880371, 2.193359, 2.473389, 2.872681},
  {0.233276, 0.302368, 0.477905, 1.232666, 1.417969, 1.581421, 1.980347, 2.092041, 2.352295, 2.530396},
  {0.215332, 0.307495, 0.474487, 0.847656, 1.116943, 1.536865, 1.984619, 2.073242, 2.187988, 2.250366},
  {0.170898, 0.448486, 0.870483, 1.064209, 1.304688, 1.526855, 1.917480, 2.161987, 2.407227, 2.571777},
  {0.283447, 0.375122, 0.523315, 0.989746, 1.148315, 1.297363, 1.936279, 2.037720, 2.406494, 2.621338},
  {0.321045, 0.407593, 0.580811, 1.020508, 1.254150, 1.383423, 1.572266, 2.120850, 2.346313, 2.433594},
  {0.210083, 0.314575, 0.677856, 0.878296, 1.056030, 1.304443, 1.839478, 2.069458, 2.283081, 2.432739},
};
/**
 * L2 codebook (10-dimensional, with 32 entries (3.2.4)
 */
static const float cb_L2[32][5] = {
  {-0.053101, -0.099487, -0.090576,  0.126099, -0.063232},
  {-0.101685, -0.108765,  0.056519, -0.000977, -0.152710},
  {-0.124634,  0.028198, -0.037354,  0.039185, -0.026855},
  { 0.006958, -0.024170, -0.041382, -0.004028, -0.179199},
  { 0.020874, -0.042725,  0.035889,  0.202637,  0.055298},
  {-0.085571, -0.102783, -0.007080,  0.115967,  0.108887},
  { 0.071289,  0.003784, -0.035278,  0.043457, -0.040649},
  {-0.013306, -0.098633,  0.028198,  0.009399, -0.010620},
  {-0.104858,  0.150879,  0.067139,  0.104248,  0.087158},
  {-0.107056, -0.116455, -0.152344, -0.036499,  0.025879},
  {-0.009399,  0.041992, -0.075684,  0.093140,  0.050415},
  {-0.038330, -0.037476, -0.031250, -0.153809, -0.052368},
  { 0.086792,  0.084595,  0.063599,  0.079346,  0.159302},
  {-0.013672, -0.033081, -0.061035,  0.115479,  0.211548},
  { 0.070190, -0.001221, -0.057129, -0.024292,  0.134399},
  { 0.017700, -0.034790, -0.156250, -0.048584,  0.004395},
  {-0.138306, -0.101929,  0.164795,  0.156738, -0.011597},
  {-0.178101, -0.151001,  0.050781, -0.026001,  0.056885},
  {-0.001831,  0.008057,  0.057129,  0.124390, -0.091309},
  {-0.041260,  0.018066,  0.176392,  0.009155, -0.092773},
  { 0.047485,  0.029175,  0.191406,  0.119751,  0.013794},
  {-0.038086, -0.011963,  0.115845,  0.003784,  0.134766},
  { 0.137573,  0.071289,  0.101929,  0.033813, -0.141479},
  { 0.065796, -0.013916,  0.104492, -0.060181,  0.027222},
  { 0.268188,  0.285278,  0.154785,  0.081787,  0.037109},
  {-0.194824,  0.067139,  0.097778, -0.055664, -0.006836},
  { 0.140869,  0.072388, -0.009399,  0.151001, -0.003784},
  { 0.048462,  0.068115,  0.024780, -0.097290, -0.112183},
  { 0.040771,  0.180054,  0.077148, -0.009766,  0.005859},
  {-0.066528, -0.040283, -0.052368, -0.083008,  0.138306},
  { 0.161133,  0.100952, -0.048584, -0.070312,  0.041626},
  {-0.019897,  0.082275, -0.001343, -0.108154,  0.064819},
};

/**
 * L3 codebook (10-dimensional, with 32 entries (3.2.4)
 */
static const float cb_L3[32][5] = {
  { 0.071045, -0.146606,  0.101196,  0.010498,  0.046997},
  { 0.177002,  0.008789, -0.028198,  0.105469,  0.080688},
  {-0.019897, -0.064209, -0.092041, -0.199341,  0.032593},
  { 0.069946,  0.097168, -0.020630, -0.077026,  0.099609},
  { 0.063354,  0.035522,  0.019409, -0.078125, -0.158203},
  { 0.189087,  0.087280,  0.064331, -0.087158, -0.023560},
  {-0.055786,  0.074707, -0.034546, -0.168579, -0.090454},
  {-0.041992,  0.163696,  0.132690, -0.079834, -0.069458},
  {-0.066284, -0.213867, -0.023804, -0.011963, -0.033691},
  {-0.028687, -0.088867,  0.115845,  0.185181,  0.109253},
  { 0.061279, -0.044189, -0.117188, -0.058960,  0.169189},
  { 0.054932, -0.056885, -0.013184,  0.123291,  0.271362},
  {-0.003418, -0.046143,  0.090820, -0.122681,  0.029297},
  { 0.033081, -0.001831,  0.110962, -0.031616,  0.206055},
  {-0.123413,  0.070923, -0.006470, -0.091187,  0.107178},
  {-0.060791, -0.168091,  0.002197, -0.054199,  0.181030},
  { 0.123901, -0.027100,  0.054077,  0.045410, -0.043213},
  { 0.081665,  0.080444,  0.200195,  0.113770,  0.065186},
  { 0.169067, -0.022217, -0.110718, -0.088013, -0.031982},
  { 0.069458,  0.152222,  0.041138,  0.050781, -0.014771},
  { 0.045044, -0.122437, -0.061890, -0.071655, -0.110352},
  { 0.008789, -0.017212,  0.178833,  0.007690, -0.095825},
  { 0.025391,  0.036743, -0.107666,  0.014282, -0.049316},
  {-0.111328,  0.076050, -0.009277,  0.033691, -0.053711},
  {-0.032593, -0.064087,  0.017090,  0.107666, -0.016968},
  {-0.085083,  0.105591,  0.129395,  0.050415,  0.054443},
  { 0.070923, -0.126587, -0.109253,  0.081665,  0.036255},
  { 0.000366,  0.084473, -0.035645,  0.128174,  0.095459},
  {-0.129517, -0.059082,  0.044189, -0.072876, -0.104004},
  {-0.144287, -0.090820,  0.163574,  0.031982,  0.007690},
  {-0.094482, -0.058960, -0.152222, -0.008545,  0.011963},
  {-0.137329, -0.032349, -0.029541,  0.088379,  0.114014},
};


/**
 * interpolation filter b30 (3.7.1)
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
#define GA_BITS 3
#define GA_CB_SIZE (1<<GA_BITS)
static const float cb_GA[GA_CB_SIZE][2] =
{
  { 0.197876,  1.214478}, //5
  { 0.094666,  0.296021}, //1
  { 0.163452,  3.315674}, //7
  { 0.117249,  1.134155}, //4
  { 0.111755,  0.613037}, //2
  { 0.000061,  0.185059}, //0
  { 0.021729,  1.801270}, //6
  { 0.003479,  0.659668}, //3
};

/**
 * GB codebook (3.9.2)
 */
#define GB_BITS 4
#define GB_CB_SIZE (1<<GB_BITS)
static const float cb_GB[GB_CB_SIZE][2] = {
  { 0.313843,  0.072266}, //2
  { 1.055847,  0.227173}, //14
  { 0.375977,  0.292358}, //3
  { 0.983398,  0.414062}, //13
  { 0.050415,  0.244751}, //0
  { 1.158020,  0.724487}, //15
  { 0.121704,  0.000000}, //1
  { 0.942017,  0.028931}, //12
  { 0.645325,  0.362061}, //6
  { 0.923584,  0.599854}, //10
  { 0.706116,  0.145996}, //7
  { 0.866333,  0.198975}, //9
  { 0.493835,  0.593384}, //4
  { 0.925354,  1.742676}, //11
  { 0.556641,  0.064087}, //5
  { 0.809326,  0.397461}, //8
};

/**
 * MA predictor (3.2.4)
 */
static const float ma_predictor[2][4][10] = {
  {
    { 0.256989,  0.277985,  0.279999,  0.273590,  0.275696,  0.276398,  0.267487,  0.267792,  0.277893,  0.264679},
    { 0.214172,  0.219391,  0.233093,  0.222992,  0.227173,  0.225189,  0.214783,  0.212280,  0.211487,  0.209595},
    { 0.166992,  0.152283,  0.156677,  0.157990,  0.160095,  0.156891,  0.158875,  0.155487,  0.147400,  0.157074},
    { 0.123779,  0.092499,  0.079773,  0.092285,  0.088989,  0.082794,  0.100983,  0.098785,  0.087189,  0.105988},
  },
  {
    { 0.235992,  0.240479,  0.249878,  0.249481,  0.251678,  0.259094,  0.263580,  0.262482,  0.255096,  0.230988},
    { 0.128479,  0.092499,  0.077881,  0.105988,  0.118286,  0.117584,  0.127686,  0.126770,  0.119293,  0.121094},
    { 0.098083,  0.058899,  0.040070,  0.065399,  0.076080,  0.072784,  0.084076,  0.082581,  0.077576,  0.089081},
    { 0.092285,  0.048584,  0.028687,  0.049774,  0.052582,  0.048187,  0.062073,  0.063599,  0.058380,  0.079376},
  }
};

/**
 * ma_predicot_sum[i] := 1-sum{1}{4}{ma_predictor[k][i]}
 */

static const float ma_predictor_sum[2][10] = {
  { 0.237976,  0.257782,  0.250397,  0.253082,  0.247986,  0.258698,  0.257782,  0.265594,  0.275970,  0.262573},
  { 0.445099,  0.559479,  0.603394,  0.529297,  0.501282,  0.502289,  0.462494,  0.464478,  0.489594,  0.479370},
};


/**
 * MA prediction coefficients (3.9.1, near Equation 69)
 */
static const float ma_prediction_coeff[4] =
{
  0.68, 0.58, 0.34, 0.19
};

/*
-------------------------------------------------------------------------------
    Formats description
-------------------------------------------------------------------------------
*/
/// Number of pulses in fixed-codebook vector
#define FC_PULSE_COUNT 4

static const struct{
    char* name;
    int sample_rate;
    char frame_size;
    char fc_index_bits;
    char vector_bits[VECTOR_SIZE];
    char silence_compression;
} formats[]={
  {"8Kb/s",   8000, 80, 3, {1,7,5,5,8,1,/*fc_index_bits*/3*FC_PULSE_COUNT+1,FC_PULSE_COUNT,GA_BITS,GB_BITS,5,3+3+3+4, FC_PULSE_COUNT,GA_BITS,GB_BITS}, 0},
#ifdef G729_SUPPORT_4400
// Note: 
  {"4.4Kb/s", 4400, 88, 4, {1,7,5,5,8,1,/*fc_index_bits*/4*FC_PULSE_COUNT+1,FC_PULSE_COUNT,GA_BITS,GB_BITS,5,4+4+4+5, FC_PULSE_COUNT,GA_BITS,GB_BITS}, 0},
#endif //G729_SUPPORT_4400
  { NULL,     0,    0,  0, {0,0,0,0,0,0, 0, 0,0,0,0, 0, 0,0,0}, 0}
};
/*
-------------------------------------------------------------------------------
          Internal routines
------------------------------------------------------------------------------
*/

#define Q15_BASE (1<<15)
#define FL_Q(a,Q_BASE) ((int32_t)((a)*(Q15_BASE)))
#define Q_FL(a,Q_BASE) (((1.0)*(a))/(Q15_BASE))

#define Q_MUL(a,b,Q_BASE) (((int32_t)(a))*(b)/(Q_BASE))
#define Q_DIV(a,b, Q_BASE) ((int32_t)(a))*(Q_BASE)/(b))


#define FL2FP(a) FL_Q(a, Q15_BASE)
#define FP2FL(a) Q_FL(a, Q15_BASE)
#define FP_MUL(a,b) Q_MUL(a,b, Q15_BASE)
#define FP_DIV(a,b) Q_DIV(a,b, Q15_BASE)
/**
 * \brief pseudo random number generator
 */
static inline uint16_t g729a_random(G729A_Context* ctx)
{
    return ctx->rand_seed = (uint16_t)(31821 * (uint32_t)ctx->rand_seed + 13849 + ctx->rand_seed);
}


static void dmp_d(char* name, float* arr, int size)
{
    int i;
    printf("%s: ",name);
    for(i=0; i<size; i++)
    {
        printf("%9f ", arr[i]);
    }
    printf("\n");
}
static void dmp_fp16(char* name, short* arr, int size, int base)
{
    int i;
    printf("%s: ",name);
    for(i=0; i<size; i++)
    {
        printf("%9f ", (1.0*arr[i])/(1<<base));
    }
    printf("\n");
}
static void dmp_fp32(char* name, int* arr, int size, int base)
{
    int i;
    printf("%s: ",name);
    for(i=0; i<size; i++)
    {
        printf("%9f ", (1.0*arr[i])/(1<<base));
    }
    printf("\n");
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
static void g729a_decode_ac_delay_subframe1(G729A_Context* ctx, int P1, int* intT, int* frac)
{
    /* if no parity error */
    if(!ctx->data_error)
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
}

/**
 * \brief Decoding of the adaptive-codebook vector delay for second subframe (4.1.3)
 * \param ctx private data structure
 * \param P1 Pitch delay second subframe 
 * \param T1 first subframe's vector delay integer part 
 * \param intT [out] integer part of delay
 * \param frac [out] fractional part of delay [-1, 0, 1]
 */
static void g729a_decode_ac_delay_subframe2(G729A_Context* ctx, int P2, int intT1, int* intT, int* frac)
{

    int tmin=FFMIN(FFMAX(intT1-5, PITCH_MIN)+9, PITCH_MAX)-9;
    
    *intT=(P2+2)/3-1;
    *frac=P2-2-3*(*intT);

    *intT+=tmin;

    ctx->intT2_prev=*intT;
}

/**
 * \brief Decoding of the adaptive-codebook vector (4.1.3)
 * \param ctx private data structure
 * \param k pitch delat, integer part
 * \param t pitch delay, fraction paart [-1, 0, 1]
 * \param ac_v buffer to store decoded vector into
 */
static void g729a_decode_ac_vector(G729A_Context* ctx, int k, int t, int* ac_v)
{
    int n, i;
    float v;

    t++;

    //t [0, 1, 2]
    //k [PITCH_MIN-1; PITCH_MAX]
    for(n=0; n<ctx->subframe_size; n++)
    {
        /* 3.7.1, Equation 40 */
        v=0;
        for(i=0; i<10; i++)
        {
            v+=ctx->exc[n-k+i]*b30[t+3*i];
            v+=ctx->exc[n-k+i+1]*b30[3-t+3*i];
        }
        ac_v[n]=round(v);
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
static void g729a_decode_fc_vector(G729A_Context* ctx, int C, int S, float* fc_v)
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
        //overflow can occure in 4.4k case
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
    //overflow can occure in 4.4k case
    if(index>=ctx->subframe_size)
    {
        ctx->data_error=1;
        return;        
    }
    fc_v[ index ] = (accS&1) ? 1 : -1;
}

/**
 * \brief fixed codebook vector modification if delay is less than 40
 * \param T pitch delay to check
 * \param fc_v [in/out] fixed codebook vector to change
 *
 * \remark if T>=subframe_size no changes to vector are made
 */
static void g729a_fix_fc_vector(G729A_Context *ctx, int T, float* fc_v)
{
    int i;

    if(T>=ctx->subframe_size)
        return;

    for(i=T; i<ctx->subframe_size;i++)
        fc_v[i]+=fc_v[i-T]*ctx->gain_pitch;
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
static void g729a_get_gain(G729A_Context *ctx, int nGA, int nGB, float* fc_v, float* gp, float* gc)
{
    float energy=0;
    int i;

    /* 3.9.1, Equation 66 */
    for(i=0; i<ctx->subframe_size; i++)
        energy+=fc_v[i]*fc_v[i];

    energy=30-10.*log(energy/40.0)/M_LN10; //FIXME: should there be subframe_size ?

    /* 3.9.1, Equation 69 */
    for(i=0; i<4; i++)
        energy+= ctx->pred_vect_q[i] * ma_prediction_coeff[i];

    /* 3.9.1, Equation 71 */
    energy = exp(M_LN10*energy/20); //FIXME: should there be subframe_size/2 ?

    // shift prediction error vector
    for(i=3; i>0; i--)
        ctx->pred_vect_q[i]=ctx->pred_vect_q[i-1];

    /* 3.9.1, Equation 72 */
    ctx->pred_vect_q[0]=20*log(cb_GA[nGA][1]+cb_GB[nGB][1])/M_LN10; //FIXME: should there be subframe_size/2 ?

    /* 3.9.1, Equation 73 */
    *gp = cb_GA[nGA][0]+cb_GB[nGB][0];           // quantized adaptive-codebook gain (gain code)
    
    /* 3.9.1, Equation 74 */
    *gc = energy*(cb_GA[nGA][1]+cb_GB[nGB][1]);  //quantized fixed-codebook gain (gain pitch)

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
static void g729a_mem_update(G729A_Context *ctx, float *fc_v, float gp, float gc, int* exc)
{
    int i;

    for(i=0; i<ctx->subframe_size; i++)
        exc[i]=round(exc[i]*gp+fc_v[i]*gc);
}

/**
 * \brief Computing the reconstructed speech (4.1.6)
 * \param ctx private data structure
 * \param lp LP filter coefficients
 * \param exc excitation
 * \param speech reconstructed speech buffer (ctx->subframe_size items)
 */
static void g729a_reconstruct_speech(G729A_Context *ctx, float *lp, int* exc, short* speech)
{
    float* tmp_speech_buf=calloc(1,(ctx->subframe_size+10)*sizeof(float));
    float* tmp_speech=tmp_speech_buf+10;
    int i,n;

    for(i=0;i<10;i++)
        tmp_speech_buf[i]= ctx->syn_filter_data[i];

    /* 4.1.6, Equation 77  */
    for(n=0; n<ctx->subframe_size; n++)
    {
        tmp_speech[n]=exc[n];
        for(i=0; i<10; i++)
            tmp_speech[n]-= lp[i]*tmp_speech[n-i-1];
    }

    for(i=0; i<ctx->subframe_size; i++)
        speech[i]=round(tmp_speech[i]);

    free(tmp_speech_buf);

    /* FIXME: line below shold be used only if reconstruction completed successfully */
    memcpy(ctx->syn_filter_data, speech+ctx->subframe_size-10, 10*sizeof(short));
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
static void g729a_lsp_decode(G729A_Context* ctx, int16_t L0, int16_t L1, int16_t L2, int16_t L3, float* lsfq)
{
    int i,j,k;
    float J[2]={0.0012, 0.0006};
    float lq[10];
    int32_t mode_index;
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
    }

    /* Rotate lq_prev */
    tmp=ctx->lq_prev[0];
    for(k=1; k<MA_NP; k++)
        ctx->lq_prev[k-1]=ctx->lq_prev[k];
    ctx->lq_prev[MA_NP-1]=tmp;
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

    for(i=0;i<10; i++)
        lsfq[i]=cos(lsfq[i]);
}

/**
 * \brief LSP to LP conversion (3.2.6)
 * \param ctx private data structure
 * \param q LSP coefficients
 * \param a decoded LP coefficients
 */
static void g729a_lsp2lp(G729A_Context* ctx, float* q, float* a)
{
    int i,j, qidx=0, fidx=0;
    float f1[6];
    float f2[6];

    float ff1[5];
    float ff2[5];

    f1[0]=1.0;
    f2[0]=1.0;
    f1[1]=-2*q[0];
    f2[1]=-2*q[1];
    qidx+=2;

    for(i=2; i<=5; i++)
    {
        f1[i]=f1[i-2];
        f2[i]=f2[i-2];
 
        for(j=i; j>1; j--)
        {
            f1[j]+=-2*q[qidx]  * f1[j-1] + f1[j-2];
            f2[j]+=-2*q[qidx+1]* f2[j-1] + f2[j-2];
        }
        f1[1]-=2*q[qidx];
        f2[1]-=2*q[qidx+1];
        qidx+=2;
    }

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
static void g729a_lp_decode(G729A_Context* ctx, float* lsp_curr, float* lp)
{
    float lsp[10];
    int i;

    /* LSP values for first subframe (3.2.5, Equation 24)*/
    for(i=0;i<10;i++)
        lsp[i]=(lsp_curr[i]+ctx->lsp_prev[i])/2;

    g729a_lsp2lp(ctx, lsp, lp);

    /* LSP values for second subframe (3.2.5)*/
    for(i=0;i<10;i++)
        lsp[i]=lsp_curr[i];

    g729a_lsp2lp(ctx, lsp, lp+10);

    /* saving LSP coefficients for using in next frame */
    for(i=0;i<10;i++)
        ctx->lsp_prev[i]=lsp[i];
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
void* g729a_decoder_init()
{
    G729A_Context* ctx=calloc(1, sizeof(G729A_Context));
    int interpol_filt_len=11;
    int frame_size=10;
    int i,k;

    /* stub */
    ctx->format=0;
    ctx->subframe_size=formats[ctx->format].frame_size>>1;
    /* Decoder initialization. 4.3, Table 9 */

    /* 
    Pitch gain of previous subframe. 

    (EE) This does not comply with specification, but reference
         and Intel decoder uses here minimum sharpen value instead of maximum. */
    ctx->gain_pitch=GAIN_PITCH_MIN;

    /* gain coefficients */
    ctx->g[0]=1.0;
    for(i=1; i<40;i++)
        ctx->g[i]=0.0;

    /* LSP coefficients */
    for(k=0; k<MA_NP; k++)
        ctx->lq_prev[k]=malloc(sizeof(float)*frame_size);

#if 0
    /*
     This is initialization from specification. But it produces different result from
     reference decoder
    */
    for(i=0; i<frame_size;i++)
    {
        ctx->lq_prev[0][i]=(i+1)*M_PI/(frame_size+1);
        ctx->lsp_prev[i]=cos(ctx->lq_prev[0][i]);
    }
#else
    /* 
      This is code from reference decoder. Have to be reimplemented 
    */
    ctx->lq_prev[0][0]=4*FP2FL(2339);
    ctx->lq_prev[0][1]=4*FP2FL(4679);
    ctx->lq_prev[0][2]=4*FP2FL(7018);
    ctx->lq_prev[0][3]=4*FP2FL(9358);
    ctx->lq_prev[0][4]=4*FP2FL(11698);
    ctx->lq_prev[0][5]=4*FP2FL(14037);
    ctx->lq_prev[0][6]=4*FP2FL(16377);
    ctx->lq_prev[0][7]=4*FP2FL(18717);
    ctx->lq_prev[0][8]=4*FP2FL(21056);
    ctx->lq_prev[0][9]=4*FP2FL(23396);

    ctx->lsp_prev[0]=FP2FL(30000);
    ctx->lsp_prev[1]=FP2FL(26000);
    ctx->lsp_prev[2]=FP2FL(21000);
    ctx->lsp_prev[3]=FP2FL(15000);
    ctx->lsp_prev[4]=FP2FL(8000);
    ctx->lsp_prev[5]=FP2FL(0);
    ctx->lsp_prev[6]=FP2FL(-8000);
    ctx->lsp_prev[7]=FP2FL(-15000);
    ctx->lsp_prev[8]=FP2FL(-21000);
    ctx->lsp_prev[9]=FP2FL(-26000);
#endif

    for(k=1; k<MA_NP; k++)
        for(i=0;i<frame_size; i++)
            ctx->lq_prev[k][i]=ctx->lq_prev[0][i];

    // Two subframes + PITCH_MAX inetries for last excitation signal data + ???
    ctx->exc_base=calloc(sizeof(int), frame_size*8+PITCH_MAX+INTERPOL_LEN);
    if(!ctx->exc_base)
        return NULL;

    ctx->exc=ctx->exc_base+PITCH_MAX+INTERPOL_LEN;
    
    /* random seed initialization (4.4.4) */
    ctx->rand_seed=21845;

    //quantized prediction error
    for(i=0; i<4; i++)
        ctx->pred_vect_q[i] = -14;

    return ctx;
}

/**
 * G.729A decoder uninitialization
 * \param ctx private data structure
 */
void g729a_decoder_uninit(void *context)
{
    G729A_Context* ctx=context;
    int k;

    if(ctx->exc_base) free(ctx->exc_base);
    ctx->exc_base=NULL;
    ctx->exc=NULL;

    for(k=0; k<MA_NP; k++)
    {
        if(ctx->lq_prev[k]) free(ctx->lq_prev[k]);
        ctx->lq_prev[k]=NULL;
    }
}

/**
 * \brief decode one G.729 frame into PCM samples
 * \param serial array if bits (0x81 - 1, 0x7F -0)
 * \param serial_size number of items in array
 * \param out_frame array for output PCM samples
 * \param out_frame_size maximumnumber of elements in output array
 */
int  g729a_decode_frame(void* context, short* serial, int serial_size, short* out_frame, int out_frame_size)
{
    G729A_Context* ctx=context;
    int parm[VECTOR_SIZE];
    int idx=2;
    int i,j;
    float lp[20];
    float lsp[10];
    int t;     ///< pitch delay, fraction part
    int k;     ///< pitch delay, integer part
    float* fc; ///< fixed codebooc vector
    float gp, gc;

    short* speech_buf; ///< reconstructed speech

    fc=calloc(1, ctx->subframe_size*sizeof(float));
    speech_buf=calloc(1, 2*ctx->subframe_size*sizeof(short));

    ctx->data_error=0;

    for(i=0; i<VECTOR_SIZE; i++)
    {
        if(formats[ctx->format].vector_bits[i]>16)
            return 0;
        parm[i]=0;
        for(j=0; j<formats[ctx->format].vector_bits[i]; j++)
        {
            parm[i]<<= 1;
            parm[i] |= serial[idx++]==0x81?1:0;
        }
    }

    if(!g729_parity_check(parm[4], parm[5]))
        ctx->data_error=1;

    /* stub: error concealment routine required */
    if(ctx->data_error)
        return 0;

    g729a_lsp_decode(ctx, parm[0], parm[1], parm[2], parm[3], lsp);
    g729a_lp_decode(ctx, lsp, lp);

    /* first subframe */
    g729a_decode_ac_delay_subframe1(ctx, parm[4], &k, &t);
    g729a_decode_ac_vector(ctx, k, t, ctx->exc);
    g729a_decode_fc_vector(ctx, parm[6], parm[7], fc);
    g729a_fix_fc_vector(ctx, k, fc);
    g729a_get_gain(ctx, parm[8], parm[9], fc, &gp, &gc);
    g729a_mem_update(ctx, fc, gp, gc, ctx->exc);
    g729a_reconstruct_speech(ctx, lp, ctx->exc, speech_buf);

    /* second subframe */
    g729a_decode_ac_delay_subframe2(ctx, parm[10], k, &k, &t);
    g729a_decode_ac_vector(ctx, k, t, ctx->exc+ctx->subframe_size);
    g729a_decode_fc_vector(ctx, parm[11], parm[12], fc);
    g729a_fix_fc_vector(ctx, k, fc);
    g729a_get_gain(ctx, parm[13], parm[14], fc, &gp, &gc);
    g729a_mem_update(ctx, fc, gp, gc, ctx->exc+ctx->subframe_size);
    g729a_reconstruct_speech(ctx, lp+10, ctx->exc+ctx->subframe_size, speech_buf+ctx->subframe_size);

    //Save signal for using in next frame
    memmove(ctx->exc_base, ctx->exc_base+2*ctx->subframe_size, (PITCH_MAX+INTERPOL_LEN)*sizeof(int));

    /* Return reconstructed speech to caller */
    memcpy(out_frame, speech_buf, 2*ctx->subframe_size*sizeof(short));

    free(speech_buf);
    free(fc);
    return ctx->subframe_size;
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
