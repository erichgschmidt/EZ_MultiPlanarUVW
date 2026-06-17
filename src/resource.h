#pragma once

#ifndef IDC_STATIC
#define IDC_STATIC (-1)
#endif

#define IDD_PANEL   101
#define IDS_PARAMS    1

// ---------------------------------------------------------------------------
// Per-layer string IDs (9 params per layer × 4 layers)
// ---------------------------------------------------------------------------
#define IDS_EN1     10
#define IDS_CH1     11
#define IDS_TYPE1   12
#define IDS_UT1     13
#define IDS_VT1     14
#define IDS_UOFF1   15
#define IDS_VOFF1   16
#define IDS_FLIPU1  17
#define IDS_FLIPV1  18

#define IDS_EN2     20
#define IDS_CH2     21
#define IDS_TYPE2   22
#define IDS_UT2     23
#define IDS_VT2     24
#define IDS_UOFF2   25
#define IDS_VOFF2   26
#define IDS_FLIPU2  27
#define IDS_FLIPV2  28

#define IDS_EN3     30
#define IDS_CH3     31
#define IDS_TYPE3   32
#define IDS_UT3     33
#define IDS_VT3     34
#define IDS_UOFF3   35
#define IDS_VOFF3   36
#define IDS_FLIPU3  37
#define IDS_FLIPV3  38

#define IDS_EN4     40
#define IDS_CH4     41
#define IDS_TYPE4   42
#define IDS_UT4     43
#define IDS_VT4     44
#define IDS_UOFF4   45
#define IDS_VOFF4   46
#define IDS_FLIPU4  47
#define IDS_FLIPV4  48

// Blend math
#define IDS_SHARP        50
#define IDS_INVERTZ      51
#define IDS_WALLXBIAS    52
#define IDS_WALLYBIAS    53
#define IDS_FLOORBIAS    54
#define IDS_CEILBIAS     55
#define IDS_FLOORSTART   56
#define IDS_CEILSTART    57
#define IDS_HARDDOM      58
#define IDS_HARDTHRESH   59

// Output
#define IDS_BLENDCH      60
#define IDS_PREVIEWCH    61
#define IDS_PREVIEWMODE  62
#define IDS_SIGNEDFIX    64
#define IDS_WRITEPREVIEW 63

// ---------------------------------------------------------------------------
// Control IDs  (per-layer base = 1000 + N*100)
// ---------------------------------------------------------------------------

// Layer 1 = Wall X
#define IDC_CHK_EN_1      1100
#define IDC_EDIT_CH_1     1101
#define IDC_SPIN_CH_1     1102
#define IDC_COMBO_TYPE_1  1103
#define IDC_EDIT_UT_1     1104
#define IDC_SPIN_UT_1     1105
#define IDC_EDIT_VT_1     1106
#define IDC_SPIN_VT_1     1107
#define IDC_EDIT_UOFF_1   1108
#define IDC_SPIN_UOFF_1   1109
#define IDC_EDIT_VOFF_1   1110
#define IDC_SPIN_VOFF_1   1111
#define IDC_CHK_FLIPU_1   1112
#define IDC_CHK_FLIPV_1   1113

// Layer 2 = Wall Y
#define IDC_CHK_EN_2      1200
#define IDC_EDIT_CH_2     1201
#define IDC_SPIN_CH_2     1202
#define IDC_COMBO_TYPE_2  1203
#define IDC_EDIT_UT_2     1204
#define IDC_SPIN_UT_2     1205
#define IDC_EDIT_VT_2     1206
#define IDC_SPIN_VT_2     1207
#define IDC_EDIT_UOFF_2   1208
#define IDC_SPIN_UOFF_2   1209
#define IDC_EDIT_VOFF_2   1210
#define IDC_SPIN_VOFF_2   1211
#define IDC_CHK_FLIPU_2   1212
#define IDC_CHK_FLIPV_2   1213

// Layer 3 = Floor
#define IDC_CHK_EN_3      1300
#define IDC_EDIT_CH_3     1301
#define IDC_SPIN_CH_3     1302
#define IDC_COMBO_TYPE_3  1303
#define IDC_EDIT_UT_3     1304
#define IDC_SPIN_UT_3     1305
#define IDC_EDIT_VT_3     1306
#define IDC_SPIN_VT_3     1307
#define IDC_EDIT_UOFF_3   1308
#define IDC_SPIN_UOFF_3   1309
#define IDC_EDIT_VOFF_3   1310
#define IDC_SPIN_VOFF_3   1311
#define IDC_CHK_FLIPU_3   1312
#define IDC_CHK_FLIPV_3   1313

// Layer 4 = Ceiling
#define IDC_CHK_EN_4      1400
#define IDC_EDIT_CH_4     1401
#define IDC_SPIN_CH_4     1402
#define IDC_COMBO_TYPE_4  1403
#define IDC_EDIT_UT_4     1404
#define IDC_SPIN_UT_4     1405
#define IDC_EDIT_VT_4     1406
#define IDC_SPIN_VT_4     1407
#define IDC_EDIT_UOFF_4   1408
#define IDC_SPIN_UOFF_4   1409
#define IDC_EDIT_VOFF_4   1410
#define IDC_SPIN_VOFF_4   1411
#define IDC_CHK_FLIPU_4   1412
#define IDC_CHK_FLIPV_4   1413

// Blend math (base 1500)
#define IDC_EDIT_SHARP        1500
#define IDC_SPIN_SHARP        1501
#define IDC_EDIT_HARDTHRESH   1502
#define IDC_SPIN_HARDTHRESH   1503
#define IDC_EDIT_WALLXBIAS    1504
#define IDC_SPIN_WALLXBIAS    1505
#define IDC_EDIT_WALLYBIAS    1506
#define IDC_SPIN_WALLYBIAS    1507
#define IDC_EDIT_FLOORBIAS    1508
#define IDC_SPIN_FLOORBIAS    1509
#define IDC_EDIT_CEILBIAS     1510
#define IDC_SPIN_CEILBIAS     1511
#define IDC_EDIT_FLOORSTART   1512
#define IDC_SPIN_FLOORSTART   1513
#define IDC_EDIT_CEILSTART    1514
#define IDC_SPIN_CEILSTART    1515
#define IDC_CHK_INVERTZ       1516
#define IDC_CHK_HARDDOM       1517

// Output (base 1600)
#define IDC_EDIT_BLENDCH      1600
#define IDC_SPIN_BLENDCH      1601
#define IDC_EDIT_PREVIEWCH    1602
#define IDC_SPIN_PREVIEWCH    1603
#define IDC_COMBO_PREVMODE    1604
#define IDC_CHK_SIGNEDFIX     1606
#define IDC_CHK_WRITEPREVIEW  1607

// ===========================================================================
// EZ BoxTri AO  —  second modifier class (own dialog + IDs)
// ===========================================================================
#define IDD_PANEL_AO     102

// AO string IDs (100+)
#define IDS_AO_PARAMS    100
#define IDS_AO_CH        101
#define IDS_AO_CAVITY    102
#define IDS_AO_HEIGHT    103
#define IDS_AO_DOWN      104
#define IDS_AO_STRENGTH  105
#define IDS_AO_GRAY      106
#define IDS_AO_PREVIEW   107
#define IDS_AO_INVERT    108
#define IDS_AO_CONVEX    109
#define IDS_AO_CURVMAG   110
#define IDS_AO_UPFACING  111
#define IDS_AO_ROUGHNESS 112
#define IDS_AO_COMBINE   113

// AO control IDs (2000+)
#define IDC_AO_EDIT_CH        2000
#define IDC_AO_SPIN_CH        2001
#define IDC_AO_EDIT_CAVITY    2002
#define IDC_AO_SPIN_CAVITY    2003
#define IDC_AO_EDIT_HEIGHT    2004
#define IDC_AO_SPIN_HEIGHT    2005
#define IDC_AO_EDIT_DOWN      2006
#define IDC_AO_SPIN_DOWN      2007
#define IDC_AO_EDIT_STRENGTH  2008
#define IDC_AO_SPIN_STRENGTH  2009
#define IDC_AO_CHK_GRAY       2010
#define IDC_AO_CHK_PREVIEW    2011
#define IDC_AO_CHK_INVERT     2012
#define IDC_AO_EDIT_CONVEX    2013
#define IDC_AO_SPIN_CONVEX    2014
#define IDC_AO_EDIT_CURVMAG   2015
#define IDC_AO_SPIN_CURVMAG   2016
#define IDC_AO_EDIT_UPFACING  2017
#define IDC_AO_SPIN_UPFACING  2018
#define IDC_AO_EDIT_ROUGHNESS 2019
#define IDC_AO_SPIN_ROUGHNESS 2020
#define IDC_AO_COMBO_COMBINE  2021

// ===========================================================================
// EZ Procedural Masks  —  third modifier class (own dialog + IDs)
// ===========================================================================
#define IDD_PANEL_MASKS  103

// Masks string IDs (120+)
#define IDS_MK_PARAMS    120
#define IDS_MK_TARGETCH  121
#define IDS_MK_CONTRAST  122
#define IDS_MK_BLUR      123
#define IDS_MK_INVERTALL 124
#define IDS_MK_OVERCRANK 125
#define IDS_MK_PREVIEW   126
#define IDS_MK_SRCR      127
#define IDS_MK_WR        128
#define IDS_MK_INVR      129
#define IDS_MK_SRCG      130
#define IDS_MK_WG        131
#define IDS_MK_INVG      132
#define IDS_MK_SRCB      133
#define IDS_MK_WB        134
#define IDS_MK_INVB      135
#define IDS_MK_RAYSAMP   136
#define IDS_MK_RAYDIST   137

// Masks control IDs (2100+)
#define IDC_MK_EDIT_TARGETCH  2100
#define IDC_MK_SPIN_TARGETCH  2101
#define IDC_MK_EDIT_CONTRAST  2102
#define IDC_MK_SPIN_CONTRAST  2103
#define IDC_MK_EDIT_BLUR      2104
#define IDC_MK_SPIN_BLUR      2105
#define IDC_MK_CHK_INVERTALL  2106
#define IDC_MK_CHK_OVERCRANK  2107
#define IDC_MK_CHK_PREVIEW    2108
// R channel
#define IDC_MK_COMBO_SRCR     2110
#define IDC_MK_EDIT_WR        2111
#define IDC_MK_SPIN_WR        2112
#define IDC_MK_CHK_INVR       2113
// G channel
#define IDC_MK_COMBO_SRCG     2120
#define IDC_MK_EDIT_WG        2121
#define IDC_MK_SPIN_WG        2122
#define IDC_MK_CHK_INVG       2123
// B channel
#define IDC_MK_COMBO_SRCB     2130
#define IDC_MK_EDIT_WB        2131
#define IDC_MK_SPIN_WB        2132
#define IDC_MK_CHK_INVB       2133
// Ray AO
#define IDC_MK_EDIT_RAYSAMP   2140
#define IDC_MK_SPIN_RAYSAMP   2141
#define IDC_MK_EDIT_RAYDIST   2142
#define IDC_MK_SPIN_RAYDIST   2143

// ===========================================================================
// EZ Smoothing Groups  —  fourth modifier class (own dialog + IDs)
// ===========================================================================
#define IDD_PANEL_SMOOTH 104

// Smoothing string IDs (140+)
#define IDS_SM_PARAMS    140
#define IDS_SM_ANGLE     141
#define IDS_SM_CURVWT    142
#define IDS_SM_MINISLAND 143
#define IDS_SM_RESPECT   144
#define IDS_SM_SELONLY   145
#define IDS_SM_CLEARNORM 146

// Smoothing control IDs (2200+)
#define IDC_SM_EDIT_ANGLE     2200
#define IDC_SM_SPIN_ANGLE     2201
#define IDC_SM_EDIT_CURVWT    2202
#define IDC_SM_SPIN_CURVWT    2203
#define IDC_SM_EDIT_MINISLAND 2204
#define IDC_SM_SPIN_MINISLAND 2205
#define IDC_SM_CHK_RESPECT    2206
#define IDC_SM_CHK_SELONLY    2207
#define IDC_SM_CHK_CLEARNORM  2208

// ===========================================================================
// EZ AO (simple)  —  fifth modifier class (own dialog + IDs)
// ===========================================================================
#define IDD_PANEL_AOLITE 105

// strings (160+)
#define IDS_AL_PARAMS    160
#define IDS_AL_CH        161
#define IDS_AL_CAVITY    162
#define IDS_AL_HEIGHT    163
#define IDS_AL_DOWN      164
#define IDS_AL_STRENGTH  165
#define IDS_AL_GRAY      166
#define IDS_AL_INVERT    167
#define IDS_AL_PREVIEW   168
#define IDS_AL_COMBINE   169

// controls (2300+)
#define IDC_AL_EDIT_CH        2300
#define IDC_AL_SPIN_CH        2301
#define IDC_AL_EDIT_CAVITY    2302
#define IDC_AL_SPIN_CAVITY    2303
#define IDC_AL_EDIT_HEIGHT    2304
#define IDC_AL_SPIN_HEIGHT    2305
#define IDC_AL_EDIT_DOWN      2306
#define IDC_AL_SPIN_DOWN      2307
#define IDC_AL_EDIT_STRENGTH  2308
#define IDC_AL_SPIN_STRENGTH  2309
#define IDC_AL_CHK_GRAY       2310
#define IDC_AL_CHK_INVERT     2311
#define IDC_AL_CHK_PREVIEW    2312
#define IDC_AL_COMBO_COMBINE  2313
