#pragma once

#ifndef IDC_STATIC
#define IDC_STATIC (-1)
#endif

#define IDD_PANEL    101
#define IDS_PARAMS     1

// ---------------------------------------------------------------------------
// String IDs  (10 per row, rows 1-6)
// ---------------------------------------------------------------------------
#define IDS_EN1    10
#define IDS_CH1    11
#define IDS_PR1    12
#define IDS_UT1    13
#define IDS_VT1    14
#define IDS_UOFF1  15
#define IDS_VOFF1  16
#define IDS_FLIPU1 17
#define IDS_FLIPV1 18
#define IDS_SWAP1  19

#define IDS_EN2    20
#define IDS_CH2    21
#define IDS_PR2    22
#define IDS_UT2    23
#define IDS_VT2    24
#define IDS_UOFF2  25
#define IDS_VOFF2  26
#define IDS_FLIPU2 27
#define IDS_FLIPV2 28
#define IDS_SWAP2  29

#define IDS_EN3    30
#define IDS_CH3    31
#define IDS_PR3    32
#define IDS_UT3    33
#define IDS_VT3    34
#define IDS_UOFF3  35
#define IDS_VOFF3  36
#define IDS_FLIPU3 37
#define IDS_FLIPV3 38
#define IDS_SWAP3  39

#define IDS_EN4    40
#define IDS_CH4    41
#define IDS_PR4    42
#define IDS_UT4    43
#define IDS_VT4    44
#define IDS_UOFF4  45
#define IDS_VOFF4  46
#define IDS_FLIPU4 47
#define IDS_FLIPV4 48
#define IDS_SWAP4  49

#define IDS_EN5    50
#define IDS_CH5    51
#define IDS_PR5    52
#define IDS_UT5    53
#define IDS_VT5    54
#define IDS_UOFF5  55
#define IDS_VOFF5  56
#define IDS_FLIPU5 57
#define IDS_FLIPV5 58
#define IDS_SWAP5  59

#define IDS_EN6    60
#define IDS_CH6    61
#define IDS_PR6    62
#define IDS_UT6    63
#define IDS_VT6    64
#define IDS_UOFF6  65
#define IDS_VOFF6  66
#define IDS_FLIPU6 67
#define IDS_FLIPV6 68
#define IDS_SWAP6  69

// Seam options
#define IDS_THRESH   70
#define IDS_MERGE    71
#define IDS_PARK_NM  72
#define IDS_PARKU    73
#define IDS_PARKV    74

// Blend output
#define IDS_EN_BLEND 80
#define IDS_CH_BLEND 81
#define IDS_POWER    82
#define IDS_SHOW     83

// ---------------------------------------------------------------------------
// Control IDs  (base = 1000 + N*100, N = 1..6)
// ---------------------------------------------------------------------------

// Row 1
#define IDC_CHK_EN_1     1100
#define IDC_EDIT_CH_1    1101
#define IDC_SPIN_CH_1    1102
#define IDC_COMBO_PR_1   1103
#define IDC_EDIT_UT_1    1104
#define IDC_SPIN_UT_1    1105
#define IDC_EDIT_VT_1    1106
#define IDC_SPIN_VT_1    1107
#define IDC_EDIT_UOFF_1  1108
#define IDC_SPIN_UOFF_1  1109
#define IDC_EDIT_VOFF_1  1110
#define IDC_SPIN_VOFF_1  1111
#define IDC_CHK_FLIPU_1  1112
#define IDC_CHK_FLIPV_1  1113
#define IDC_CHK_SWAP_1   1114

// Row 2
#define IDC_CHK_EN_2     1200
#define IDC_EDIT_CH_2    1201
#define IDC_SPIN_CH_2    1202
#define IDC_COMBO_PR_2   1203
#define IDC_EDIT_UT_2    1204
#define IDC_SPIN_UT_2    1205
#define IDC_EDIT_VT_2    1206
#define IDC_SPIN_VT_2    1207
#define IDC_EDIT_UOFF_2  1208
#define IDC_SPIN_UOFF_2  1209
#define IDC_EDIT_VOFF_2  1210
#define IDC_SPIN_VOFF_2  1211
#define IDC_CHK_FLIPU_2  1212
#define IDC_CHK_FLIPV_2  1213
#define IDC_CHK_SWAP_2   1214

// Row 3
#define IDC_CHK_EN_3     1300
#define IDC_EDIT_CH_3    1301
#define IDC_SPIN_CH_3    1302
#define IDC_COMBO_PR_3   1303
#define IDC_EDIT_UT_3    1304
#define IDC_SPIN_UT_3    1305
#define IDC_EDIT_VT_3    1306
#define IDC_SPIN_VT_3    1307
#define IDC_EDIT_UOFF_3  1308
#define IDC_SPIN_UOFF_3  1309
#define IDC_EDIT_VOFF_3  1310
#define IDC_SPIN_VOFF_3  1311
#define IDC_CHK_FLIPU_3  1312
#define IDC_CHK_FLIPV_3  1313
#define IDC_CHK_SWAP_3   1314

// Row 4
#define IDC_CHK_EN_4     1400
#define IDC_EDIT_CH_4    1401
#define IDC_SPIN_CH_4    1402
#define IDC_COMBO_PR_4   1403
#define IDC_EDIT_UT_4    1404
#define IDC_SPIN_UT_4    1405
#define IDC_EDIT_VT_4    1406
#define IDC_SPIN_VT_4    1407
#define IDC_EDIT_UOFF_4  1408
#define IDC_SPIN_UOFF_4  1409
#define IDC_EDIT_VOFF_4  1410
#define IDC_SPIN_VOFF_4  1411
#define IDC_CHK_FLIPU_4  1412
#define IDC_CHK_FLIPV_4  1413
#define IDC_CHK_SWAP_4   1414

// Row 5
#define IDC_CHK_EN_5     1500
#define IDC_EDIT_CH_5    1501
#define IDC_SPIN_CH_5    1502
#define IDC_COMBO_PR_5   1503
#define IDC_EDIT_UT_5    1504
#define IDC_SPIN_UT_5    1505
#define IDC_EDIT_VT_5    1506
#define IDC_SPIN_VT_5    1507
#define IDC_EDIT_UOFF_5  1508
#define IDC_SPIN_UOFF_5  1509
#define IDC_EDIT_VOFF_5  1510
#define IDC_SPIN_VOFF_5  1511
#define IDC_CHK_FLIPU_5  1512
#define IDC_CHK_FLIPV_5  1513
#define IDC_CHK_SWAP_5   1514

// Row 6
#define IDC_CHK_EN_6     1600
#define IDC_EDIT_CH_6    1601
#define IDC_SPIN_CH_6    1602
#define IDC_COMBO_PR_6   1603
#define IDC_EDIT_UT_6    1604
#define IDC_SPIN_UT_6    1605
#define IDC_EDIT_VT_6    1606
#define IDC_SPIN_VT_6    1607
#define IDC_EDIT_UOFF_6  1608
#define IDC_SPIN_UOFF_6  1609
#define IDC_EDIT_VOFF_6  1610
#define IDC_SPIN_VOFF_6  1611
#define IDC_CHK_FLIPU_6  1612
#define IDC_CHK_FLIPV_6  1613
#define IDC_CHK_SWAP_6   1614

// Seam options
#define IDC_EDIT_THRESH  1700
#define IDC_SPIN_THRESH  1701
#define IDC_CHK_MERGE    1702
#define IDC_CHK_PARK     1703
#define IDC_EDIT_PARKU   1704
#define IDC_SPIN_PARKU   1705
#define IDC_EDIT_PARKV   1706
#define IDC_SPIN_PARKV   1707

// Blend output
#define IDC_CHK_EN_BLEND  1800
#define IDC_EDIT_CH_BLEND 1801
#define IDC_SPIN_CH_BLEND 1802
#define IDC_EDIT_POWER    1803
#define IDC_SPIN_POWER    1804
#define IDC_CHK_SHOW      1805
