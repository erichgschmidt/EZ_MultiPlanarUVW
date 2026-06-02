#pragma once

#ifndef IDC_STATIC
#define IDC_STATIC (-1)
#endif

// Dialog
#define IDD_PANEL                   101

// ---------------------------------------------------------------------------
// String IDs
// ---------------------------------------------------------------------------
#define IDS_PARAMS                    1

// Row params (6 rows x 5 params)
#define IDS_EN1   10
#define IDS_CH1   11
#define IDS_PR1   12
#define IDS_UT1   13
#define IDS_VT1   14

#define IDS_EN2   20
#define IDS_CH2   21
#define IDS_PR2   22
#define IDS_UT2   23
#define IDS_VT2   24

#define IDS_EN3   30
#define IDS_CH3   31
#define IDS_PR3   32
#define IDS_UT3   33
#define IDS_VT3   34

#define IDS_EN4   40
#define IDS_CH4   41
#define IDS_PR4   42
#define IDS_UT4   43
#define IDS_VT4   44

#define IDS_EN5   50
#define IDS_CH5   51
#define IDS_PR5   52
#define IDS_UT5   53
#define IDS_VT5   54

#define IDS_EN6   60
#define IDS_CH6   61
#define IDS_PR6   62
#define IDS_UT6   63
#define IDS_VT6   64

// Global UV
#define IDS_UOFFSET                  70
#define IDS_VOFFSET                  71
#define IDS_FLIPU                    72
#define IDS_FLIPV                    73
#define IDS_SWAPUV                   74

// Seam options
#define IDS_NORMAL_THRESHOLD         80
#define IDS_MERGE_ISLANDS            81
#define IDS_PARK_NON_MATCHING        82
#define IDS_PARK_U                   83
#define IDS_PARK_V                   84

// Blend output
#define IDS_ENABLE_BLEND             90
#define IDS_CHANNEL_BLEND            91
#define IDS_BLEND_POWER              92
#define IDS_SHOW_BLEND               93

// ---------------------------------------------------------------------------
// Control IDs — Projection rows
// Pattern: row N → 1N00 base
// ---------------------------------------------------------------------------

// Row 1
#define IDC_CHK_EN1                 1100
#define IDC_EDIT_CH1                1101
#define IDC_SPIN_CH1                1102
#define IDC_EDIT_PR1                1103
#define IDC_SPIN_PR1                1104
#define IDC_EDIT_UT1                1105
#define IDC_SPIN_UT1                1106
#define IDC_EDIT_VT1                1107
#define IDC_SPIN_VT1                1108

// Row 2
#define IDC_CHK_EN2                 1200
#define IDC_EDIT_CH2                1201
#define IDC_SPIN_CH2                1202
#define IDC_EDIT_PR2                1203
#define IDC_SPIN_PR2                1204
#define IDC_EDIT_UT2                1205
#define IDC_SPIN_UT2                1206
#define IDC_EDIT_VT2                1207
#define IDC_SPIN_VT2                1208

// Row 3
#define IDC_CHK_EN3                 1300
#define IDC_EDIT_CH3                1301
#define IDC_SPIN_CH3                1302
#define IDC_EDIT_PR3                1303
#define IDC_SPIN_PR3                1304
#define IDC_EDIT_UT3                1305
#define IDC_SPIN_UT3                1306
#define IDC_EDIT_VT3                1307
#define IDC_SPIN_VT3                1308

// Row 4
#define IDC_CHK_EN4                 1400
#define IDC_EDIT_CH4                1401
#define IDC_SPIN_CH4                1402
#define IDC_EDIT_PR4                1403
#define IDC_SPIN_PR4                1404
#define IDC_EDIT_UT4                1405
#define IDC_SPIN_UT4                1406
#define IDC_EDIT_VT4                1407
#define IDC_SPIN_VT4                1408

// Row 5
#define IDC_CHK_EN5                 1500
#define IDC_EDIT_CH5                1501
#define IDC_SPIN_CH5                1502
#define IDC_EDIT_PR5                1503
#define IDC_SPIN_PR5                1504
#define IDC_EDIT_UT5                1505
#define IDC_SPIN_UT5                1506
#define IDC_EDIT_VT5                1507
#define IDC_SPIN_VT5                1508

// Row 6
#define IDC_CHK_EN6                 1600
#define IDC_EDIT_CH6                1601
#define IDC_SPIN_CH6                1602
#define IDC_EDIT_PR6                1603
#define IDC_SPIN_PR6                1604
#define IDC_EDIT_UT6                1605
#define IDC_SPIN_UT6                1606
#define IDC_EDIT_VT6                1607
#define IDC_SPIN_VT6                1608

// Global UV
#define IDC_EDIT_UOFFSET            1700
#define IDC_SPIN_UOFFSET            1701
#define IDC_EDIT_VOFFSET            1702
#define IDC_SPIN_VOFFSET            1703
#define IDC_CHK_FLIPU               1704
#define IDC_CHK_FLIPV               1705
#define IDC_CHK_SWAPUV              1706

// Seam options
#define IDC_EDIT_THRESHOLD          1800
#define IDC_SPIN_THRESHOLD          1801
#define IDC_CHK_MERGE               1802
#define IDC_CHK_PARK                1803
#define IDC_EDIT_PARKU              1804
#define IDC_SPIN_PARKU              1805
#define IDC_EDIT_PARKV              1806
#define IDC_SPIN_PARKV              1807

// Blend output
#define IDC_CHK_ENABLE_BLEND        1900
#define IDC_EDIT_CHANNEL_BLEND      1901
#define IDC_SPIN_CHANNEL_BLEND      1902
#define IDC_EDIT_BLEND_POWER        1903
#define IDC_SPIN_BLEND_POWER        1904
#define IDC_CHK_SHOW_BLEND          1905
