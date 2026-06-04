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
