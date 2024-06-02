/*
 *   HiTi Photo Printer CUPS backend
 *
 *   (c) 2019-2024 Solomon Peachy <pizza@shaftnet.org>
 *
 *   The latest version of this program can be found at:
 *
 *     https://git.shaftnet.org/gitea/slp/selphy_print.git
 *
 *   This program is free software; you can redistribute it and/or modify it
 *   under the terms of the GNU General Public License as published by the Free
 *   Software Foundation; either version 2 of the License, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 *   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *   for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, see <https://www.gnu.org/licenses/>.
 *
 *   SPDX-License-Identifier: GPL-2.0+
 *
 */

#define BACKEND hiti_backend

#include "backend_common.h"

/* For Integration into gutenprint */
#if defined(HAVE_CONFIG_H)
#include <config.h>
#endif

// We should use nanosleep everywhere properly.
#define __usleep(__x) { struct timespec t = { 0, (__x) * 1000 } ; nanosleep (&t, NULL); }

/* Private structures */
struct hiti_cmd {
	uint8_t hdr;    /* 0xa5 */
	uint16_t len;   /* (BE) everything after this field, minimum 3, max 6 */
	uint8_t status; /* see CMD_STATUS_* */
	uint16_t cmd;   /* CMD_*  (BE) */
	uint8_t payload[];  /* 0-3 items */
} __attribute__((packed));

#define CMD_STATUS_ERR     0x80
#define CMD_STATUS_OK      (0x40 | 0x10)

// XXX lower 4 bits are sub-status, unknown.

// Success
// 0x01 Seen with ERDC_RLC on p51x
// 0x02 Seen with EPC_SP on p51x, sometimes?
// 0x03 Seen with RDC_RS on p461

// Errors
// 0x08 Seen with EFM_RD on p51x, EFD_CHS on p525, ERDC_RRVC on p461
// 0x0b Seen with ESD_SEHT2 on p51x

/* Request Device Characteristics */
#define CMD_RDC_RS     0x0100 /* Request Summary */
#define CMD_RDC_ROC    0x0104 /* Request Option Characteristics (1 resp on p52x, not on p51x, 16 on p461 */

/* Printer Configuratio Control */
#define CMD_PCC_RP     0x0301 /* Reset Printer (1 arg) */
#define CMD_RCC_UNK    0x0310 /* XX Unknown */
#define CMD_PCC_STP    0x030F /* Set Target Printer (1 arg) XXX -- 00 == master or 01 == slave ? Or maybe it's a bank select for CMD_EFM_RNV? */

/* Request Device Status */
#define CMD_RDS_RSS    0x0400 /* Request Status Summary */
#define CMD_RDS_RIS    0x0401 /* Request Input Status */
#define CMD_RDS_RIA    0x0403 /* Request Input Alert */
#define CMD_RDS_RJA    0x0405 /* Request Jam Alert */
#define CMD_RDS_ROIRA  0x0406 /* Request Operator Intervention Alert */
#define CMD_RDS_RW     0x0407 /* Request Warnings */
#define CMD_RDS_DSRA   0x0408 /* Request Device Serviced Alerts */
#define CMD_RDS_SA     0x040A /* Request Service Alerts */
#define CMD_RDS_RPS    0x040B /* Request Printer Statistics */
#define CMD_RDS_RSUS   0x040C /* Request Supplies Status */

/* Job Control */
#define CMD_JC_SJ      0x0500 /* Start Job (3 arg) */
#define CMD_JC_EJ      0x0501 /* End Job (3 arg) */
#define CMD_JC_QJC     0x0502 /* Query Job Completed (5 arg) XX 6 byte resp (most), 2 (p461) */
#define CMD_JC_QQA     0x0503 /* Query Jobs Queued or Active (3 arg) */
#define CMD_JC_RSJ     0x0510 /* Resume Suspended Job (3 arg) XX */

/* Extended Read Device Characteristics */
#define CMD_ERDC_RS    0x8000 /* Request Summary */
#define CMD_ERDC_RCC   0x8001 /* Read Calibration Charcteristics */
#define CMD_ERDC_RPC   0x8005 /* Request Print Count (1 arg, 8 (51x) or 4 (52x,7xx) resp) */
#define CMD_ERDC_RLC   0x8006 /* Request LED calibration (10 on 52x, 6 on 461 */
#define CMD_ERDC_RSN   0x8007 /* Read Serial Number (1 arg) */
#define CMD_ERDC_C_RPCS 0x8008 /* CS Request Printer Correction Status */
#define CMD_ERDC_CTV   0x8008 /* Color Table Version XXX p51x, string (14 byte reply?), error on p461 */
#define CMD_ERDC_RPIDM 0x8009 /* Request PID and Model Code */
#define CMD_ERDC_UNKA  0x800A /* XX Unknown */
#define CMD_ERDC_UNKC  0x800C /* XX p51x Unknown, 4 byte resp (00 87 00 02) */
#define CMD_ERDC_UNKD  0x800D /* XX Unknown (1 arg) */
#define CMD_ERDC_RTLV  0x800E /* Request T/L Voltage */
#define CMD_ERDC_RRVC  0x800F /* Read Ribbon Vendor Code */
#define CMD_ERDC_UNK0  0x8010 /* Used when printer doesn't support RRVC? */
#define CMD_ERDC_UNK1  0x8011 /* Unknown Query RE, 1 arg == QUALITY? */
#define CMD_ERDC_RHA   0x801C /* Read Highlight Adjustment (6 resp) RE */

// 8008 seen in Windows Comm @ 3211  (0 len response)
// 8011 seen in Windows Comm @ 3369 (1 arg req (always 00), 4 len response)

/* Extended Format Data */
#define CMD_EFD_SF     0x8100 /* Sublimation Format */
#define CMD_EFD_CHS    0x8101 /* Color & Heating Setting (2 arg) -- Not P525 */
#define CMD_EFD_C_CHS  0x8102 /* CS Color Heating Setting (3 arg) */
#define CMD_EFD_C_SIID 0x8103 /* CS Set Input ID (1 arg) */

/* Extended Page Control */
#define CMD_EPC_SP     0x8200 /* Start Page */
#define CMD_EPC_EP     0x8201 /* End Page */
#define CMD_EPC_SYP    0x8202 /* Start Yellow Plane */
#define CMD_EPC_SMP    0x8204 /* Start Magenta Plane */
#define CMD_EPC_SCP    0x8206 /* Start Cyan Plane */

#define CMD_EPC_C_SYP  0x8202 /* CS Start Yellow Page */
#define CMD_EPC_C_SMP  0x8203 /* CS Start Magenta Page */
#define CMD_EPC_C_SCP  0x8204 /* CS Start Cyan Page */
#define CMD_EPC_C_SBP  0x8205 /* CS Start Black Page */
#define CMD_EPC_C_SKP  0x8206 /* CS Start K Resin Page */
#define CMD_EPC_C_SLP  0x8207 /* CS Start Lamination Page */
#define CMD_EPC_C_SOP  0x8208 /* CS Start Overcoat Page */
#define CMD_EPC_C_SY2P 0x8209 /* CS Start Yellow2 Page */
#define CMD_EPC_C_SM2P 0x820A /* CS Start Magenta2 Page */
#define CMD_EPC_C_SC2P 0x820B /* CS Start Cyan2 Page */
#define CMD_EPC_C_SB2P 0x820C /* CS Start Black2 Page */
#define CMD_EPC_C_SK2P 0x820D /* CS Start K Resin2 Page */
#define CMD_EPC_C_SL2P 0x820E /* CS Start Lamination2 Page */
#define CMD_EPC_C_SO2P 0x820F /* CS Start Overcoat2 Page */

/* Extended Send Data */
#define CMD_ESD_SEHT2  0x8303 /* Send Ext Heating Table (2 arg) */
#define CMD_ESD_SEHT   0x8304 /* Send Ext Heating Table XX */
#define CMD_ESD_UNK6   0x8306 /* Unknown, seen in FW update (1 byte payload) */
#define CMD_ESD_UNK7   0x8307 /* Unknown, seen in FW update (no payload) */
#define CMD_ESD_SD     0x8308 /* Send data (256 or 0xfc00 byte payload) */
#define CMD_ESD_SEPD   0x8309 /* Send Ext Print Data (2 arg) + struct */
#define CMD_ESD_SHCI   0x830A /* Unknown, seen on P51x (4 byte payload) */
#define CMD_ESD_SHPTC  0x830B /* Send Heating Parameters & Tone Curve (varying payload) */
#define CMD_ESD_C_SHPTC  0x830C /* CS Send Heating Parameters & Tone Curve XX (n arg) */

/* Extended Flash/NVram */
#define CMD_EFM_WCV    0x8403 /* Write Calibration Value (n arg) */
#define CMD_EFM_RNV    0x8405 /* Read NVRam (1 arg) -- arg is offset, 1 byte resp of data @ that offset. -- P51x */
#define CMD_EFM_UNK6   0x8406 /* Write NVRAM, maybe? */
#define CMD_EFM_RD     0x8408 /* Read single location (2 byte offset arg, 1 byte of data @ that offset -- not P51x */
#define CMD_EFM_UNKC   0x840C /* Write NVRAM, maybe? */
#define CMD_EFM_SHA    0x840E /* Set Highlight Adjustment (5 arg) -- XXX RE */

#define CMD_ERD_RTCV   0x8702 /* XX No idea what this does */

/* Extended Security Control */
#define CMD_ESC_SP     0x8900 /* Set Password */
#define CMD_ESC_SSM    0x8901 /* Set Security Mode */

/* Extended Debug Mode */
#define CMD_EDM_CVD    0xE002 /* Common Voltage Drop Values (n arg) */
#define CMD_EDM_CPP    0xE023 /* Clean Paper Path (1 arg) XX */
#define CMD_EDM_UNK    0xE028 /* XX Called if ERC_RTCV is supported. */
#define CMD_EDM_C_MC2CES 0xE02E /* CS Move card to Contact Encoder Station */
#define CMD_EDM_C_MC2MES 0xE02F /* CS Move card to Mag Encoder Station */
#define CMD_EDM_C_MC2CLES 0xE030 /* CS Move card to ContactLess Encoder Station */
#define CMD_EDM_C_MC2EB 0xE031 /* CS Move card to Eject Box */
#define CMD_EDM_C_MC2H 0xE037 /* CS Move card to Hopper */

#define CMD_UNK_UNK   0xE200

/* CMD_PCC_RP */
#define RESET_PRINTER 0x01
#define RESET_SOFT    0x02

/* 801C --> 0 args
        <-- 6 bytes: 00 YY MM CC 00 00  (YMC is +- 31 decimal)

   840E --> 5 args:  YY MM CC 00 00 (YMC is +- 31 decimal)
        <-- 1 arg:   00 (success, presumably)

  Highlight Correction.  Unclear if it's used by printer or by "driver"

*/

/* CMD_ERDC_RCC */
struct hiti_calibration {
	uint8_t horiz;
	uint8_t vert;
} __attribute__((packed));

/* CMD_ERDC_RPIDM */
struct hiti_rpidm {
	uint16_t usb_pid;  /* BE */
	uint8_t  region;   /* See hiti_regions */
} __attribute__((packed));

/* CMD_EDRC_RS */
struct hiti_erdc_rs {      /* All are BIG endian */
	uint8_t  tph_seg;  // Always 0x1e?
	uint16_t stride;   /* Head width: 1920 (6" models) 1280 (4" models) */
	uint16_t dpi_cols; /* fixed at 300 */
	uint16_t dpi_rows; /* fixed at 300 */
	uint16_t cols;     /* fixed at the printer's max print size width (eg 1844 for all 6" models) */
	uint16_t rows;     /* 1240 for 6x4" media */
	uint16_t vertUnitInstalled; // 0xa055 for P510S, 0xffff for rest
	uint8_t  y_speed;  /* Varies based on model, units unknown */
	uint8_t  m_speed;
	uint8_t  c_speed;
	uint8_t  o_speed;
	uint8_t  overheatTemp;
	uint8_t  heaterOffTemp;
	uint8_t  hwFeature1; // P52x bit 7 means "RI1" printhead
	uint8_t  fwFeature1; // bit 7 means KO function
	uint8_t  preheatTemp;

	// These might not be present */

	uint16_t dpi_rows2; /* 0x5c08 p520l/p510s */
	uint16_t dpi_rows3; /* 0x0020 p461 */
	uint8_t  hwFeature2; // 64/40/20  520l/510s/561
	uint8_t  overheatTempS;
	uint8_t  overheatTempOT;
} __attribute__((packed));

/* CMD_JC_* */
struct hiti_job {
	uint8_t  lun;    /* Logical Unit Number.  Leave at 0 */
	uint16_t jobid;  /* BE */
} __attribute__((packed));

/* CMD_JC_QQA */
#define MAX_JOBS 4
struct hiti_job_qqa {
	uint8_t  count;  /* 0-MAX_JOBS */
	struct {
		struct hiti_job job;
		uint8_t status;
	} row[MAX_JOBS];  /* Four jobs max outstanding */
} __attribute__((packed));

#define QQA_STATUS_PRINTING 0x00
#define QQA_STATUS_WAITING  0x01
#define QQA_STATUS_SUSPENDED 0x03
#define QQA_STATUS_ERROR     0x80 // ???

/* CMD_JC_QJC */
struct hiti_jc_qjc {
	uint8_t  lun;    /* Logical Unit Number.  Leave at 0 */
	uint16_t jobid;  /* BE */
	uint16_t jobid2; /* BE, set to 1? */
} __attribute__((packed));
// repsonse is 6 bytes.

// Roll Type:
// 5x3.5 1547 1072
// 6x4   1844 1240
// 6x4b  1844 1216  /" 6x3.94"
// 6x9   1844 2740
// 6x8/2 1844 2492
// 6x8   1844 2434
// 5x7   1548 2140
// 5x7/2 1548 2152
// 6x4/2 1844 1248
// 6x6    1844 1844
// 5x5    1540 1540 ? (1548?)
// 6x5    1844 1544
// 6x2    1844 ????

// Sheet type:
// 6x4    1818 1280

#define PRINT_TYPE_6x4      0
#define PRINT_TYPE_5x7      2
#define PRINT_TYPE_6x8      3
#define PRINT_TYPE_6x9      6
#define PRINT_TYPE_6x9_2UP  7
#define PRINT_TYPE_5x3_5    8
#define PRINT_TYPE_6x4_2UP  9
#define PRINT_TYPE_6x2     10
#define PRINT_TYPE_5x7_2UP 11

struct hiti_heattable_hdr_v1 {  /* P51x */
	uint8_t type;
	uint32_t len;  /* Length in 16-bit words, LE */
} __attribute((packed));

#define HITI_HEATTABLE_V1_Y    0x01
#define HITI_HEATTABLE_V1_M    0x02
#define HITI_HEATTABLE_V1_C    0x03
#define HITI_HEATTABLE_V1_U    0x04  // XXX unknown, maybe K?
#define HITI_HEATTABLE_V1_O    0x05
#define HITI_HEATTABLE_V1_OM   0x07
#define HITI_HEATTABLE_V1_CVD  0x08

#define HITI_HEATTABLE_V1_PLANESIZE 2050
#define HITI_HEATTABLE_V1_CVDSIZE   582

/* All fields are little endian */
struct hiti_heattable_entry_v2 {
	uint8_t   type;
	uint32_t  len;  /* in 16-bit words */
	uint32_t  offset;
} __attribute((packed));

struct hiti_heattable_hdr_v2 {
	uint8_t num_headers;
	struct hiti_heattable_entry_v2 entries[];
} __attribute((packed));

#define HEATTABLE_V2_MAX_SIZE (1024*128)

#define HEATTABLE_S_SIZE 4167   /* P461, P310, and some P32x */
#define HEATTABLE_S2_SIZE 4232  /* Some P32x */
                                            // size
#define HEATTABLE_V2_ID_HT_D_Y         0x01 // 522
#define HEATTABLE_V2_ID_HT_D_M         0x02 //
#define HEATTABLE_V2_ID_HT_D_C         0x03 //
#define HEATTABLE_V2_ID_HT_D_K         0x04 //
#define HEATTABLE_V2_ID_HT_R_RK        0x10 //
#define HEATTABLE_V2_ID_HT_R_R         0x11 //
#define HEATTABLE_V2_ID_HT_R_L         0x12 //
#define HEATTABLE_V2_ID_HT_R_FO        0x13 //
#define HEATTABLE_V2_ID_HT_D_O         0x20 //
#define HEATTABLE_V2_ID_HT_D_KO        0x21 //
#define HEATTABLE_V2_ID_HT_D_MO        0x22 //
#define HEATTABLE_V2_ID_CVD            0x40 // 582
#define HEATTABLE_V2_ID_VER_MAJOR      0x50 // 4
#define HEATTABLE_V2_ID_VER_MINOR      0x51 //
#define HEATTABLE_V2_ID_CT_INVERT      0x80 // 2562
#define HEATTABLE_V2_ID_CT_CLASSIC     0x81 //
#define HEATTABLE_V2_ID_CT_IDPASS      0x82 //
#define HEATTABLE_V2_ID_TC_COMPENSATE  0x90 // 26
#define HEATTABLE_V2_ID_HAC_Y          0xa1 // 1540
#define HEATTABLE_V2_ID_HAC_M          0xa2 //
#define HEATTABLE_V2_ID_HAC_C          0xa3 //
#define HEATTABLE_V2_ID_HAC_DK         0xa4 //
#define HEATTABLE_V2_ID_HAC_RK         0xa5 //
#define HEATTABLE_V2_ID_HAC_R          0xa6 //
#define HEATTABLE_V2_ID_HAC_O          0xa7 //
#define HEATTABLE_V2_ID_HAC_MO         0xa8 //
#define HEATTABLE_V2_ID_HAC_FO         0xa9 //
#define HEATTABLE_V2_ID_HAC_YMC        0xae //
#define HEATTABLE_V2_ID_HAC_ALL        0xaf //
#define HEATTABLE_V2_ID_LS_Y           0xb1 // 24 (Long Smear)
#define HEATTABLE_V2_ID_LS_M           0xb2 //
#define HEATTABLE_V2_ID_LS_C           0xb3 //
#define HEATTABLE_V2_ID_LS_DK          0xb4 //
#define HEATTABLE_V2_ID_LS_RK          0xb5 //
#define HEATTABLE_V2_ID_LS_R           0xb6 //
#define HEATTABLE_V2_ID_LS_O           0xb7 //
#define HEATTABLE_V2_ID_LS_MO          0xb8 //
#define HEATTABLE_V2_ID_LS_FO          0xb9 //
#define HEATTABLE_V2_ID_LS_YMC         0xbe //
#define HEATTABLE_V2_ID_LS_ALL         0xbf //
#define HEATTABLE_V2_ID_GL_Y           0xc1 // ?? (Ghost Line)
#define HEATTABLE_V2_ID_GL_M           0xc2 //
#define HEATTABLE_V2_ID_GL_C           0xc3 //
#define HEATTABLE_V2_ID_GL_DK          0xc4 //
#define HEATTABLE_V2_ID_GL_RK          0xc5 //
#define HEATTABLE_V2_ID_GL_R           0xc6 //
#define HEATTABLE_V2_ID_GL_O           0xc7 //
#define HEATTABLE_V2_ID_GL_MO          0xc8 //
#define HEATTABLE_V2_ID_GL_FO          0xc9 //
#define HEATTABLE_V2_ID_GL_YMC         0xce //
#define HEATTABLE_V2_ID_GL_ALL         0xcf //
#define HEATTABLE_V2_ID_EN_Y           0xd1 // 18  (Energy)
#define HEATTABLE_V2_ID_EN_M           0xd2 //
#define HEATTABLE_V2_ID_EN_C           0xd3 //
#define HEATTABLE_V2_ID_EN_DK          0xd4 //
#define HEATTABLE_V2_ID_EN_RK          0xd5 //
#define HEATTABLE_V2_ID_EN_R           0xd6 //
#define HEATTABLE_V2_ID_EN_O           0xd7 //
#define HEATTABLE_V2_ID_EN_MO          0xd8 //
#define HEATTABLE_V2_ID_EN_FO          0xd9 //
#define HEATTABLE_V2_ID_EN_YMC         0xde //
#define HEATTABLE_V2_ID_EN_ALL         0xdf //

#define HEATTABLE_V2_ID_EMBEDDED       0xf0 // 2
#define HEATTABLE_V2_ID_EMBEDDED_1     0x00 // varies
#define HEATTABLE_V2_ID_EMBEDDED_2     0x20 // varies

/* All fields are LE */
struct hiti_gpjobhdr {
	uint32_t cookie;  /* "GPHT" */
	uint32_t hdr_len; /* Including the whole thing */
	uint32_t model;   /* Model family, in decimal */
	uint32_t cols;
	uint32_t rows;
	uint32_t col_dpi;
	uint32_t row_dpi;
	uint32_t copies;
	uint32_t quality;  /* 0 for std, 1 for fine */
	uint32_t code;     /* PRINT_TYPE_* */
	uint32_t overcoat; /* 1 for matte, 0 for glossy */
	uint32_t payload_flag; /* See PAYLOAD_FLAG_* */
	uint32_t payload_len;
} __attribute__((packed));

#define PAYLOAD_FLAG_YMCPLANAR 0x01
#define PAYLOAD_FLAG_NOCORRECT 0x02
#define PAYLOAD_FLAG_MEDIAVER  0x03

#define HDR_COOKIE 0x54485047

/* CMD_EFD_SF for non-CS systems */
struct hiti_efd_sf {
/*@0 */	uint8_t  mediaType; /* PRINT_TYPE_?? */
/*@1 */	uint16_t cols_res;  /* BE, always 300dpi */
/*@3 */	uint16_t rows_res;  /* BE, always 300dpi */
/*@5 */	uint16_t cols;      /* BE */
/*@7 */	uint16_t rows;      /* BE */
/*@9 */	 int8_t  rows_offset; /* Has to do with H_Offset calibration */
/*@10*/	 int8_t  cols_offset; /* Has to do wiwth V_Offset calibration */
/*@11*/	uint8_t  colorSeq;  /* See SF_COLORSEQ_* */
/*@12*/	uint8_t  copies;
/*@13*/	uint8_t  printMode; /* See SF_PRINTMODE_* */
/*@14*/	uint8_t  zero[4]; /* P461 only */
} __attribute__((packed));

/* SF_COLORSEQ is actually:

    Y  0x01
    M  0x02
    C  0x04
    DK 0x08  (Dye Black)
    RK 0x10  (Resin Black)
    OM 0x20  (NOT CSxxx models)
    O  0x80  (On CSxx, means there is an explicit O plane)
*/
#define SF_COLORSEQ_YMCO     0x87
#define SF_COLORSEQ_MATTE    0xC0

#define SF_PRINTMODE_NORMAL  0x00
#define SF_PRINTMODE_SPLITK  0x01  /* CSxxx only */
#define SF_PRINTMODE_FINE    0x02  /* For P5xx/P7xx only */
#define SF_PRINTMODE_BASE    0x08  /* For P5xx/P7xx only */
#define SF_PRINTMODE_OBHEAT  0x10  /* Onboard Heat Comp, CSxxx only */

/* CMD_ESD_SEPD -- Note it's different from the usual command flow */
struct hiti_extprintdata {
	uint8_t  hdr; /* 0xa5 */
	uint16_t len; /* 24bit data length (+8) in BE format, first two bytes */
	uint8_t  status; /* 0x50 */
	uint16_t cmd; /* 0x8309, BE */
	uint8_t  lenb; /* LSB of length */
	uint16_t startLine;  /* Starting line number, BE */
	uint16_t numLines; /* Number of lines in block, BE, 3000 max. */
	uint8_t  payload[];  /* ie data length bytes */
} __attribute__((packed));

/* CMD_ESD_SEHT2 -- Note it's different from the usual command flow */
struct hiti_seht2 {
	uint8_t  hdr;  /* 0xa5 */
	uint16_t len;  /* 24-bit data length (+5) in BE format, first two bytes */
	uint8_t  status;  /* 0x50 */
	uint16_t cmd;  /* 0x8303, BE */
	uint8_t  lenb; /* LSB of length */
	uint8_t  plane;
} __attribute__((packed));

/* All multi-byte fields here are LE */
struct hiti_matrix {
/*@00*/	uint8_t  row0[16]; // all 00

/*@10*/	uint8_t  row1[6];  // 01 00 00 00 00 00
	uint16_t cuttercount;
	uint8_t  align_v;
	uint8_t  aligh_h;
	uint8_t  row1_2[6]; // all 00

/*@20*/	uint8_t  row2[16]; // no idea

/*@30*/	uint8_t  error_index0;  /* Value % 31 == NEWEST. Count back */
	uint8_t  errorcode[31];

/*@50*/	uint8_t  row5[16]; // all 00, except [8] which is a5.
/*@60*/	char     serno[16]; /* device serial number */

/*@70*/	uint16_t unclean_prints;
	uint16_t cleanat[15]; // XX Guess?

/*@90*/	uint16_t supply_motor;
	uint16_t take_motor;
	uint8_t row9[12]; // all 00 except last, which is 0xa5

/*@a0*/	uint16_t errorcount[31];
	uint8_t rowd[2]; // seems to be 00 cc ?

/*@e0*/	uint16_t tpc_4x6;
	uint16_t tpc_5x7;
	uint16_t tpc_6x8;
	uint16_t tpc_6x9;
	uint8_t unk_rowe[8]; // all 00

/*@f0*/	uint16_t apc_4x6;
	uint16_t apc_5x7;
	uint16_t apc_6x8;
	uint16_t apc_6x9;
	uint8_t unk_rowf[4]; // all 00
	uint8_t tphv_a;
	uint8_t tphv_d;
	uint8_t unk_rowf2[2]; // all 00

/*@100*/uint8_t unk_row10[16];
/*@110*/uint8_t unk_row11[16];
/*@120*/uint8_t unk_row12[16];
/*@130*/uint8_t unk_row13[16];
/*@140*/uint8_t unk_row14[16];
/*@150*/uint8_t unk_row15[16];
/*@160*/uint8_t unk_row16[16];
/*@170*/uint8_t unk_row17[16];
/*@180*/uint8_t unk_row18[16];
/*@190*/uint8_t unk_row19[16];
/*@1a0*/uint8_t unk_row1a[16];
/*@1b0*/uint8_t unk_row1b[16];
/*@1c0*/uint8_t unk_row1c[16];
/*@1d0*/uint8_t unk_row1d[16];
/*@1e0*/uint8_t unk_row1e[16];
/*@1f0*/uint8_t unk_row1f[16];
/*@200*/
} __attribute__((packed));

STATIC_ASSERT(sizeof(struct hiti_matrix) == 512);

struct hiti_ribbon {
	uint16_t unk;  // 01 08 (p461)
	uint8_t type;  /* RIBBON_TYPE_XXX */
	uint16_t unk2; // 00 07 (p461)
} __attribute__((packed));

#define RIBBON_TYPE_4x6    0x01
#define RIBBON_TYPE_5x7    0x02
#define RIBBON_TYPE_6x9    0x03
#define RIBBON_TYPE_6x8    0x04

struct hiti_paper {
	uint8_t unk;
	uint8_t type;  /* PAPER_TYPE_XXX */
	uint16_t unk2;
} __attribute__((packed));

#define PAPER_TYPE_X4INCH  0xf0  // XXX hack
#define PAPER_TYPE_5INCH   0x02
#define PAPER_TYPE_6INCH   0x01
#define PAPER_TYPE_NONE    0x00

/* Private data structure */
enum {
	HT_COLORMODE_INVERT = 0,
	HT_COLORMODE_CLASSIC = 1,
	HT_COLORMODE_IDPASS_VIVID = 2,
};

enum {
	HT_PLANE_Y =    0x01,
	HT_PLANE_M =    0x02,
	HT_PLANE_C =    0x04,
	HT_PLANE_K =    0x08,   // Dye K, eg on YMCKO ribbons?
	HT_PLANE_OG =   0x10,
	HT_PLANE_OK =   0x20,  // Glossy on YMCKO ribbons
	HT_PLANE_OM =   0x40,
	HT_PLANE_RK =  0x100, // Resin K on YMCKO
	HT_PLANE_R  =  0x200,  // Resin K only
	HT_PLANE_FO =  0x400,  // Flourescent
	HT_PLANE_L  =  0x800,  // Lamination on RESIN K
	HT_CVD      = 0x1000
};

struct hiti_printjob {
	struct dyesub_job_common common;

	uint8_t *databuf;
	uint32_t datalen;

	struct hiti_gpjobhdr hdr;

	int colormode;

	uint8_t *heattable_buf;
	int  heattable_len;

	struct hiti_heattable_v2 {
		uint8_t type;
		uint8_t *data;
		uint32_t len;
	} *heattable_v2;
	uint8_t num_heattable_entries;
};

enum {
	HEATTABLE_TYPE_V0, // P461, P320, P310, passed as-is
	HEATTABLE_TYPE_V1, // P51x
	HEATTABLE_TYPE_V2, // All others
};

struct hiti_ctx {
	struct dyesub_connection *conn;

	int jobid;
	int nodata;

	int sheet;
	int heattable_type;

	char serno[32];

	int  erdc_rpc_len;

	struct marker marker;
	char     version[256];
	char     id[256];
	uint8_t  matrix[512];  // XXX convert to struct matrix */
	struct hiti_ribbon ribbon;
	struct hiti_paper  paper;
	struct hiti_calibration calibration;
	uint8_t  led_calibration[10]; // XXX convert to struct
	uint8_t  unk_8010[15]; // XXX
	uint8_t  unk_800c[4]; // XXX
	struct hiti_erdc_rs erdc_rs;
	uint8_t  hilight_adj[6]; // XXX convert to struct, not P51x!
	uint8_t  rtlv[2];      /* XXX figure out conversion/math? */
	struct hiti_rpidm rpidm;
	uint16_t ribbonvendor; // see below.
	uint32_t media_remain; // XXX could be array?
};

#define RRVC_VENDOR_MASK  0xf000
#define RRVC_VERSION_MASK 0x003f

/* Prototypes */
static int hiti_doreset(struct hiti_ctx *ctx, uint8_t type);
static int hiti_query_job_qa(struct hiti_ctx *ctx, struct hiti_job *jobid, struct hiti_job_qqa *resp);
static int hiti_query_status(struct hiti_ctx *ctx, uint8_t *sts, uint32_t *err);
static int hiti_query_version(struct hiti_ctx *ctx);
static int hiti_query_matrix(struct hiti_ctx *ctx);
static int hiti_query_matrix_51x(struct hiti_ctx *ctx);
static int hiti_query_supplies(struct hiti_ctx *ctx);
static int hiti_query_tphv(struct hiti_ctx *ctx);
static int hiti_query_statistics(struct hiti_ctx *ctx);
static int hiti_query_calibration(struct hiti_ctx *ctx);
static int hiti_query_led_calibration(struct hiti_ctx *ctx);
static int hiti_query_ribbonvendor(struct hiti_ctx *ctx);
static int hiti_query_summary(struct hiti_ctx *ctx, struct hiti_erdc_rs *rds);
static int hiti_query_rpidm(struct hiti_ctx *ctx);
static int hiti_query_hilightadj(struct hiti_ctx *ctx);
static int hiti_query_unk8010(struct hiti_ctx *ctx);
static int hiti_query_unk800c(struct hiti_ctx *ctx);
static int hiti_query_counter(struct hiti_ctx *ctx, uint8_t arg, uint32_t *resp, int num);
static int hiti_query_markers(void *vctx, struct marker **markers, int *count);

static int hiti_query_serno(struct dyesub_connection *conn, char *buf, int buf_len);
static int hiti_parse_heattable_v2(struct hiti_printjob *job);
static int hiti_construct_heattable_v2(const struct hiti_printjob *job, int planes, int colormode, struct hiti_heattable_hdr_v2 *table);
static void *hiti_find_heattable_v2_entry(const struct hiti_printjob *job, int id, size_t *len);

static const char* hiti_modelcode(int type, int subtype) {
	switch (type) {
	case P_HITI_CS2XX:
		return "cd";
	case P_HITI_310:
		return "sl";
	case P_HITI_320:
		return "sp";
	case P_HITI_461:
		return "sn";
	case P_HITI_51X:
		return "ra";
	case P_HITI_520:
	case P_HITI_525:
		if (subtype)
			return "ri1";
		else
			return "ri";
	case P_HITI_530:
		return "rk";
	case P_HITI_720:
		return "rd";
	case P_HITI_750:
		return "rh";
	default:
		ERROR("Unknown HiTi type %d\n", type);
		return "XX";
	}
}

static int hiti_docmd(struct hiti_ctx *ctx, uint16_t cmdid, const uint8_t *buf, uint16_t buf_len, uint16_t *rsplen)
{
	uint8_t cmdbuf[2048];
	struct hiti_cmd *cmd = (struct hiti_cmd *)cmdbuf;
	int ret, num = 0;

	cmd->hdr = 0xa5;
	cmd->len = cpu_to_be16(buf_len + 3);
	cmd->status = CMD_STATUS_OK;
	cmd->cmd = cpu_to_be16(cmdid);
	if (buf && buf_len)
		memcpy(cmd->payload, buf, buf_len);

	/* Send over command */
	if ((ret = send_data(ctx->conn, (uint8_t*) cmd, buf_len + 3 + 3))) {
		return ret;
	}

	__usleep(10*1000);

	/* Read back command */
	ret = read_data(ctx->conn, cmdbuf, 6, &num);
	if (ret)
		return ret;

	if (num != 6) {
		ERROR("CMD Readback length mismatch (%d vs %d)!\n", num, 6);
		return CUPS_BACKEND_FAILED;
	}

	/* Compensate for hdr len */
	num = be16_to_cpu(cmd->len) - 3;

	if (num > *rsplen) {
		ERROR("Response too long for buffer (%d vs %d)!\n", num, *rsplen);
		*rsplen = 0;
		return CUPS_BACKEND_FAILED;
	}

	/* Check response */
	if (cmd->status & CMD_STATUS_ERR) {
		ERROR("Command %04x failed, code %02x\n", cmdid, cmd->status);
		return CUPS_BACKEND_FAILED;
	}

	*rsplen = num;

	return CUPS_BACKEND_OK;
}

static int hiti_docmd_resp(struct hiti_ctx *ctx, uint16_t cmdid,
			   const uint8_t *buf, uint8_t buf_len,
			   uint8_t *respbuf, uint16_t *resplen)
{
	int ret, num = 0;
	uint16_t cmd_resp_len = *resplen;

	ret = hiti_docmd(ctx, cmdid, buf, buf_len, &cmd_resp_len);
	if (ret)
		return ret;

	if (cmd_resp_len > *resplen) {
		ERROR("Response too long! (%d vs %d)\n", cmd_resp_len, *resplen);
		*resplen = 0;
		return CUPS_BACKEND_FAILED;
	}

	__usleep(10*1000);

	/* Read back the data*/
	int remain = *resplen;
	int total = 0;
	do {
		ret = read_data(ctx->conn, respbuf + total, remain, &num);
		if (ret)
			return ret;
		total += num;
		remain -= num;
	} while (remain > 0 && num == 64);

	/* Sanity check */
	if (total > *resplen) {
		ERROR("Response too long for buffer (%d vs %d)!\n", total, *resplen);
		*resplen = 0;
		return CUPS_BACKEND_FAILED;
	}

	*resplen = total;

	return CUPS_BACKEND_OK;
}

static int hiti_shptc(struct hiti_ctx *ctx, const uint8_t *buf, uint16_t buf_len)
{
	uint8_t cmdbuf[sizeof(struct hiti_cmd)];
	struct hiti_cmd *cmd = (struct hiti_cmd *)cmdbuf;
	int ret, num = 0;

	cmd->hdr = 0xa5;
	cmd->len = cpu_to_be16(buf_len + 3);
	cmd->status = CMD_STATUS_OK;
	cmd->cmd = cpu_to_be16(CMD_ESD_SHPTC);

	/* Send over command */
	if ((ret = send_data(ctx->conn, (uint8_t*) cmd, sizeof(*cmd)))) {
		return ret;
	}

	__usleep(10*1000);

	/* Read back command */
	ret = read_data(ctx->conn, cmdbuf, 6, &num);
	if (ret)
		return ret;

	if (num != 6) {
		ERROR("CMD Readback length mismatch (%d vs %d)!\n", num, 6);
		return CUPS_BACKEND_FAILED;
	}

	ret = send_data(ctx->conn, buf, buf_len);
	if (ret)
		return CUPS_BACKEND_FAILED;

	return CUPS_BACKEND_OK;
}

static int hiti_sepd(struct hiti_ctx *ctx, uint32_t buf_len,
		     uint16_t startLine, uint16_t numLines)
{
	uint8_t cmdbuf[sizeof(struct hiti_extprintdata)];
	struct hiti_extprintdata *cmd = (struct hiti_extprintdata *)cmdbuf;
	int ret, num = 0;

	buf_len += 8;

	cmd->hdr = 0xa5;
	cmd->len = cpu_to_be16(buf_len >> 8);
	cmd->status = CMD_STATUS_OK;
	cmd->cmd = cpu_to_be16(CMD_ESD_SEPD);
	cmd->lenb = buf_len & 0xff;
	cmd->startLine = cpu_to_be16(startLine);
	cmd->numLines = cpu_to_be16(numLines);

	/* Send over command */
	if ((ret = send_data(ctx->conn, (uint8_t*) cmd, sizeof(*cmd)))) {
		return ret;
	}

	__usleep(10*1000);

	/* Read back command */
	ret = read_data(ctx->conn, cmdbuf, 6, &num);
	if (ret)
		return ret;

	if (num != 6) {
		ERROR("CMD Readback length mismatch (%d vs %d)!\n", num, 6);
		return CUPS_BACKEND_FAILED;
	}
	return CUPS_BACKEND_OK;
}

#define STATUS_IDLE          0x00
#define STATUS0_POWERON      0x01
#define STATUS0_RESEND_DATA  0x04
#define STATUS0_BUSY         0x80
#define STATUS1_SUPPLIES     0x01
#define STATUS1_PAPERJAM     0x02
#define STATUS1_INPUT        0x08
#define STATUS2_WARNING      0x02
#define STATUS2_DEVSERVICE   0x04
#define STATUS2_OPERATOR     0x08

static const char *hiti_status(const uint8_t *sts)
{
	if (sts[2] & STATUS2_WARNING)
		return "Warning";
	else if (sts[2] & STATUS2_DEVSERVICE)
		return "Service Required";
	else if (sts[2] & STATUS2_OPERATOR)
		return "Operator Intervention Required";
	else if (sts[1] & STATUS1_PAPERJAM)
		return "Paper Jam";
	else if (sts[1] & STATUS1_INPUT)
		return "Input Alert";
	else if (sts[1] & STATUS1_SUPPLIES)
		return "Supply Alert";
	else if (sts[0] & STATUS0_RESEND_DATA)
		return "Resend Data";
	else if (sts[0] & STATUS0_BUSY)
		return "Busy";
	else if (sts[0] & STATUS0_POWERON)
		return "Powering On";
	else if (sts[0] == STATUS_IDLE)
		return "Accepting Jobs";
	else
		return "Unknown";
}

static const char *hiti_jobstatuses(uint8_t code)
{
	switch (code) {
	case QQA_STATUS_PRINTING:  return "Printing";
	case QQA_STATUS_WAITING:   return "Waiting";
	case QQA_STATUS_SUSPENDED: return "Suspended";
	case QQA_STATUS_ERROR: return "Unknown Error";
	default: return "Unknown";
	}
}

static const char* hiti_ribbontypes(uint8_t code)
{
	switch (code) {
	case RIBBON_TYPE_4x6: return "4x6";
	case RIBBON_TYPE_5x7: return "5x7";
	case RIBBON_TYPE_6x9: return "6x9";
	case RIBBON_TYPE_6x8: return "6x8";
	default: return "Unknown";
	}
}

static unsigned int hiti_ribboncounts(uint8_t code, uint8_t type)
{
	if (type == P_HITI_461 ||
	    type == P_HITI_320 ||
	    type == P_HITI_310) {
		switch(code) {
		case RIBBON_TYPE_4x6: return 60;
		default: return 999;
		}
	} else if (type == P_HITI_51X) {
		switch(code) {
		case RIBBON_TYPE_4x6: return 330;
		case RIBBON_TYPE_5x7: return 190;
		case RIBBON_TYPE_6x8: return 165;
		case RIBBON_TYPE_6x9: return 150;
		default: return 999;
		}
	} else if (type == P_HITI_520 || type == P_HITI_525 || type == P_HITI_530) {
		switch(code) {
		case RIBBON_TYPE_4x6: return 500;
		case RIBBON_TYPE_5x7: return 290;
		case RIBBON_TYPE_6x8: return 250;
		case RIBBON_TYPE_6x9: return 220; // XXX guess
		default: return 999;
		}
	}

	return 999;
}

static const char* hiti_papers(uint8_t code)
{
	switch (code) {
	case PAPER_TYPE_NONE : return "None";
	case PAPER_TYPE_5INCH: return "5 inch";
	case PAPER_TYPE_6INCH: return "6 inch";
	case PAPER_TYPE_X4INCH: return "4x6 sheet";
	default: return "Unknown";
	}
}

static const char* hiti_regions(uint8_t code)
{
	switch (code) {
	case 0x11: return "GB";
	case 0x12:
	case 0x22: return "CN";
	case 0x13: return "NA";
	case 0x14: return "SA";
	case 0x15: return "EU";
	case 0x16: return "IN";
	case 0x17: return "DB";
	case 0xf0: // Seen on P510S
	case 0x01: // Seen on P520L and P525L
	default:
		return "Unknown";
	}
}

/* Supposedly correct for P720, P728, and P520 */
static const char *hiti_errors(uint32_t code)
{
	switch(code) {
	case 0x00000000: return "None";

#if 0
		/* Not sure about these, might be driver-generated? */
	case 0x00000001: return "Processing data";
	case 0x00000002: return "Sending data";
	case 0x00000003: return "Printing";
	case 0x00000004: return "End page";
	case 0x00000005: return "??";
	case 0x00000006: return "Starting job";
	case 0x00000008: return "Tray missing";

	case 0x000000A1: return "Sending Y";
	case 0x000000A2: return "Sending M";
	case 0x000000A3: return "Sending C";
	case 0x000000A4: return "Sending O";
#endif
		/* Warning Alerts */
	case 0x000100FE: return "Paper type mismatch";
	case 0x000300FE: return "Buffer underrun when printing";
	case 0x000301FE: return "Command sequence error";
	case 0x000302FE: return "NAND flash unformatted";
	case 0x000303FE: return "NAND flash space insufficient";
	case 0x000304FE: return "Heating parameter table incompatible";
		/* Device Service Required Alerts */
	case 0x00030001: return "SRAM error";
	case 0x00030101: return "SDRAM error";
	case 0x00030201: return "ADC error";
	case 0x00030301: return "NVRAM R/W error";
	case 0x00030302: return "SDRAM checksum error";
	case 0x00030402: return "DSP code checksum error";
	case 0x00030501: return "Cam TPH error";
	case 0x00030502: return "NVRAM checksom error";
	case 0x00030601: return "Cam pinch error";
	case 0x00030602: return "SRAM checksum error";
	case 0x00030701: return "Firmware write error";
	case 0x00030702: return "Flash checksum error";
	case 0x00030802: return "Heating table checksum error";
	case 0x00030901: return "ADC error in slave printer";
	case 0x00030A01: return "Cam Platen error in slave printer";
	case 0x00030B01: return "NVRAM R/W error in slave printer";
	case 0x00030C02: return "NVRAM CRC error in slave printer";
	case 0x00030D02: return "SDRAM checksum error in slave printer";
	case 0x00030E02: return "SRAM checksum error in slave printer";
	case 0x00030F02: return "FLASH checksum error in slave printer";
	case 0x00031002: return "Wrong firmware checksum error in slave printer";
	case 0x00031101: return "Communication error with slave printer";
	case 0x00031201: return "NAND flash error";
	case 0x00031302: return "Cutter error";
		/* Operator Intervention Required Alerts */
	case 0x00050001: return "Cover open";  // XXX or no ribbon loaded on P461?
	case 0x00050101: return "Cover open";
	case 0x000502FE: return "Dust box needs cleaning";
		/* Supplies Alerts */
	case 0x00080004: return "Ribbon missing";
	case 0x00080007: return "Ribbon newly inserted";
	case 0x00080103: return "Ribbon exhausted";
	case 0x00080104: return "Ribbon exhausted";
	case 0x00080105: return "Ribbon malfunction";
	case 0x00080204: return "Ribbon missing in slave printer";
	case 0x00080207: return "Ribbon newly inserted in slave printer";
	case 0x000802FE: return "Ribbon IC error";
	case 0x00080303: return "Ribbon exhausted in slave printer";
	case 0x000803FE: return "Ribbon not authenticated";
	case 0x000804FE: return "Ribbon IC read/write error";
	case 0x000805FE: return "Ribbon IC read/write error in slave printer";
	case 0x000806FE: return "Unsupported ribbon";
	case 0x000807FE: return "Unsupported ribbon in slave printer";
	case 0x000808FE: return "Unknown ribbon";
	case 0x000809FE: return "Unknown ribbon in slave printer";
		/* Jam Alerts */
	case 0x00030000: return "Paper jam";
	case 0x0003000F: return "Paper jam";
	case 0x00030200: return "Paper jam in paper path 01";
	case 0x00030300: return "Paper jam in paper path 02";
	case 0x00030400: return "Paper jam in paper path 03";
	case 0x00030500: return "Paper jam in paper path 04";
	case 0x00030600: return "Paper jam in paper path 05";
	case 0x00030700: return "Paper jam in paper path 06";
	case 0x00030800: return "Paper jam in paper path 07";
	case 0x00030900: return "Paper jam in paper path 08";
	case 0x00030A00: return "Paper jam in paper path 09";
		/* Input Alerts */
	case 0x00000008: return "Paper box missing";
	case 0x00000100: return "Cover open";
	case 0x00000101: return "Cover open failure";
	case 0x00000200: return "Ribbon IC missing";
	case 0x00000201: return "Ribbon missing";
	case 0x00000202: return "Ribbon mismatch 01";
	case 0x00000203: return "Security check fail";
	case 0x00000204: return "Ribbon mismatch 02";
	case 0x00000205: return "Ribbon mismatch 03";
	case 0x00000300: return "Ribbon exhausted 01";
	case 0x00000301: return "Ribbon exhausted 02";
	case 0x00000302: return "Printing failure (jam?)";
	case 0x00000400: return "Paper exhausted 01";
	case 0x00000401: return "Paper exhausted 02";
	case 0x00000402: return "Paper not ready";
	case 0x00000500: return "Paper jam 01";
	case 0x00000501: return "Paper jam 02";
	case 0x00000502: return "Paper jam 03";
	case 0x00000503: return "Paper jam 04";
	case 0x00000504: return "Paper jam 05";
	case 0x00000600: return "Paper mismatch";
	case 0x00000700: return "Cam error 01";
	case 0x00000800: return "Cam error 02";
	case 0x00000900: return "NVRAM error";
	case 0x00001000: return "IC error";
	case 0x00001200: return "ADC error";
	case 0x00001300: return "FW Check Error";
	case 0x00001500: return "Cutter error";

	case 0x00007538: return "Device attached to printer";
	case 0x00007539: return "Printer is in mobile mode";
	case 0x00007540: return "Printer is in standalone mode";
	case 0x00007542: return "Firmware too old for Fine mode";
	case 0x00007543: return "Firmware too old for 2x6 mode";
	case 0x00007544: return "Firmware too old for Matte mode";
	case 0x00007545: return "Firmware too old";
	case 0x00007546: return "Firmware too old";

	case 0x00008000: return "Paper out or feeding error";
	case 0x00008008: return "Paper box missing";
	case 0x00008010: return "Paper tray/roll mismatch";
	case 0x00008011: return "Paper tray mismatch (need photo tray)";
	case 0x00008012: return "Paper tray mismatch (need sticker 4/2/4 tray)";
	case 0x00008013: return "Paper tray mismatch (need sticker 4x4 tray)";
	case 0x00008014: return "Paper tray mismatch (need sticker 1x1 tray)";
	case 0x00008015: return "Paper tray mismatch (need PVC tray)";
	case 0x00080200: return "Unsupported ribbon type";
	case 0x00080201: return "Ribbon type mismatch (need YMCO)";
	case 0x00080203: return "Ribbon type mismatch (need KO)";
	case 0x00080230: return "Ribbon type mismatch (need KO)";
	case 0x00080240: return "Ribbon type mismatch (need 4x6)";
	case 0x00080250: return "Ribbon type mismatch (need 5x7)";
	case 0x00080260: return "Ribbon type mismatch (need 6x8)";
//	case 0x10008000: return "Paper out or paper low";  /* XXX this won't work, high byte is cleared */

	default: return "Unknown";
	}
}

static int hiti_dump_options(struct hiti_ctx *ctx)
{
	int ret;
	uint8_t buf[256];
	uint16_t len = 256;
	int i;
	uint8_t *ptr;

	ret = hiti_docmd_resp(ctx, CMD_RDC_ROC, NULL, 0,
			      buf, &len);
	if (ret)
		return ret;

	ptr = buf;
	while (len > 0 && *ptr) {
		i = ptr[ptr[1]+2];
		ptr[ptr[1]+2] = 0;
		INFO("Option %02x: '%s'\n", *ptr, ptr+2);
		len -= ptr[1] + 2;
		ptr += ptr[1] + 2;
		*ptr = i;
	}


	return CUPS_BACKEND_OK;
}

static int hiti_get_info(struct hiti_ctx *ctx)
{
	int ret;

	ret = hiti_query_tphv(ctx);
	if (ret)
		return ret;
	ret = hiti_query_led_calibration(ctx);
	if (ret)
		return ret;

	INFO("Printer ID: %s\n", ctx->id);
	INFO("Printer Version: %s\n", ctx->version);
	INFO("Serial Number: %s\n", ctx->serno);

	if (ctx->conn->type != P_HITI_51X) {
		ret = hiti_dump_options(ctx);
		if (ret)
			return ret;
	}

	INFO("Calibration:  H: %d V: %d\n", ctx->calibration.horiz, ctx->calibration.vert);
	INFO("LED Calibration: %d %d %d / %d %d %d\n",
	     ctx->led_calibration[4], ctx->led_calibration[5],
	     ctx->led_calibration[6], ctx->led_calibration[7],
	     ctx->led_calibration[8], ctx->led_calibration[9]);
	INFO("TPH Voltage (T/L): %d %d\n", ctx->rtlv[0], ctx->rtlv[1]);
	hiti_query_markers(ctx, NULL, NULL);
	INFO("Region: %s (%02x)\n",
	     hiti_regions(ctx->rpidm.region),
		ctx->rpidm.region);

	if (ctx->conn->type != P_HITI_51X) {
		INFO("Highlight Adjustment (Y M C): %d %d %d\n",
		     ctx->hilight_adj[1], ctx->hilight_adj[2], ctx->hilight_adj[3]);
	}

	INFO("Status Summary: %d %dx%d %dx%d\n",
	     ctx->erdc_rs.stride,
	     ctx->erdc_rs.cols,
	     ctx->erdc_rs.rows,
	     ctx->erdc_rs.dpi_cols,
	     ctx->erdc_rs.dpi_rows);

	uint32_t buf[2] = {0,0};
	ret = hiti_query_counter(ctx, 1, buf, ctx->erdc_rpc_len);
	if (ret)
		return CUPS_BACKEND_FAILED;
	INFO("Total prints: %u\n", buf[0]);

	ret = hiti_query_counter(ctx, 2, buf, ctx->erdc_rpc_len);
	if (ret)
		return CUPS_BACKEND_FAILED;
	INFO("6x4 prints: %u\n", buf[0]);

	if (!ctx->sheet) {
		ret = hiti_query_counter(ctx, 3, buf, ctx->erdc_rpc_len);
		if (ret)
			return CUPS_BACKEND_FAILED;
		INFO("5x7 prints: %u\n", buf[0]);

		ret = hiti_query_counter(ctx, 4, buf, ctx->erdc_rpc_len);
		if (ret)
			return CUPS_BACKEND_FAILED;
		INFO("6x8 prints: %u\n", buf[0]);

		ret = hiti_query_counter(ctx, 5, buf, ctx->erdc_rpc_len);
		if (ret)
			return CUPS_BACKEND_FAILED;
		INFO("6x9 prints: %u\n", buf[0]);
	}

	// XXX other shit..

	return CUPS_BACKEND_OK;
}

/* Use jobid of 0 for "any" */
static int hiti_query_job_qa(struct hiti_ctx *ctx, struct hiti_job *jobid, struct hiti_job_qqa *resp)
{
	int ret;
	uint16_t len = sizeof(*resp);

	resp->count = 0;
	ret = hiti_docmd_resp(ctx, CMD_JC_QQA,
			      (uint8_t*) jobid, sizeof(*jobid),
			      (uint8_t*) resp, &len);
	if (ret)
		return ret;

	return CUPS_BACKEND_OK;
}

static int hiti_get_status(struct hiti_ctx *ctx)
{
	uint8_t sts[3];
	uint32_t err = 0;
	int ret, i;
	struct hiti_job_qqa qqa;

	hiti_query_markers(ctx, NULL, NULL);
	ret = hiti_query_status(ctx, sts, &err);
	if (ret)
		return ret;

	INFO("Printer ID: %s\n", ctx->id);
	INFO("Printer Version: %s\n", ctx->version);
	INFO("Serial Number: %s\n", ctx->serno);

	INFO("Printer Status: %s (%02x %02x %02x)\n",
	     hiti_status(sts), sts[0], sts[1], sts[2]);
	INFO("Printer Error: %s (%08x)\n",
	     hiti_errors(err), err);

	INFO("Media: %s (%02x / %04x) : %03u/%03u\n",
	     hiti_ribbontypes(ctx->ribbon.type),
	     ctx->ribbon.type,
	     ctx->ribbonvendor,
	     ctx->media_remain, hiti_ribboncounts(ctx->ribbon.type, ctx->conn->type));
	INFO("Paper: %s (%02x)\n",
	     hiti_papers(ctx->paper.type),
	     ctx->paper.type);

	/* Find out if we have any jobs outstanding */
	struct hiti_job job = { 0 };
	hiti_query_job_qa(ctx, &job, &qqa);
	for (i = 0 ; i < qqa.count ; i++) {
		INFO("JobID %02x %04x (status %s)\n",
		     qqa.row[i].job.lun,
		     be16_to_cpu(qqa.row[i].job.jobid),
		     hiti_jobstatuses(qqa.row[i].status));
	}

	// XXX other shit...?

	return CUPS_BACKEND_OK;
}

static void *hiti_init(void)
{
	struct hiti_ctx *ctx = malloc(sizeof(struct hiti_ctx));
	if (!ctx) {
		ERROR("Memory Allocation Failure!\n");
		return NULL;
	}
	memset(ctx, 0, sizeof(struct hiti_ctx));

	if (getenv("HITI_NODATA")) {
		ctx->nodata = 1;
	}
	return ctx;
}

static int hiti_attach(void *vctx, struct dyesub_connection *conn, uint8_t jobid)
{
	struct hiti_ctx *ctx = vctx;
	int ret;

	ctx->conn = conn;

	/* Ensure jobid is sane */
	ctx->jobid = (jobid & 0x7fff);
	if (!ctx->jobid)
		ctx->jobid++;

	if (ctx->conn->type == P_HITI_51X) {
		ctx->erdc_rpc_len = 2;
	} else {
		ctx->erdc_rpc_len = 1;
	}

	/* Sheet type */
	if (ctx->conn->type == P_HITI_461 ||
	    ctx->conn->type == P_HITI_320 ||
	    ctx->conn->type == P_HITI_310) {
		ctx->sheet = 1;
	}

	switch(ctx->conn->type) {
	case P_HITI_461:
	case P_HITI_320:
	case P_HITI_310:
		ctx->heattable_type = HEATTABLE_TYPE_V0;
		break;
	case P_HITI_51X:
		ctx->heattable_type = HEATTABLE_TYPE_V1;
		break;
	default:
		ctx->heattable_type = HEATTABLE_TYPE_V2;
	}

	if (test_mode < TEST_MODE_NOATTACH) {
		/* P520 firmware v1.19-v1.21 lose their minds when Linux
		   issues a routine CLEAR_ENDPOINT_HALT.  Printer can recover
		   if it is reset.  Unclear what the side effects are.. */
		if (ctx->conn->type == P_HITI_520)
			libusb_reset_device(ctx->conn->dev);

		if (!ctx->sheet) {
			ret = hiti_query_unk8010(ctx);
			if (ret)
				return ret;
		}
		if (ctx->conn->type == P_HITI_51X) {
			ret = hiti_query_unk800c(ctx);
			if (ret)
				return ret;
		}
		ret = hiti_query_version(ctx);
		if (ret)
			return ret;
		ret = hiti_query_supplies(ctx);
		if (ret)
			return ret;
		ret = hiti_query_calibration(ctx);
		if (ret)
			return ret;
		if (ctx->sheet) {
			ctx->paper.type = PAPER_TYPE_X4INCH;
			ctx->ribbonvendor = 0x1000; /* Hardcoded */
		} else {
			ret = hiti_query_ribbonvendor(ctx);
			if (ret)
				return ret;
		}
		ret = hiti_query_rpidm(ctx);
		if (ret)
			return ret;

		if (ctx->conn->type != P_HITI_51X && !ctx->sheet) {
			ret = hiti_query_hilightadj(ctx);
			if (ret)
				return ret;
		}

		ret = hiti_query_serno(ctx->conn, ctx->serno, sizeof(ctx->serno));
		if (ret)
			return ret;

		ret = hiti_query_summary(ctx, &ctx->erdc_rs);
		if (ret)
			return CUPS_BACKEND_FAILED;

		switch (ctx->conn->type) {
			// XXX P310/P322
		case P_HITI_320:
			if (strncmp(ctx->version, "1.04.0", 6) < 0)
				WARNING("Printer firmware %s out of date (vs %s), please update.\n", ctx->version, "v1.04.0");
			break;
		case P_HITI_461:
			if (strncmp(ctx->version, "1.12.0", 6) < 0)
				WARNING("Printer firmware %s out of date (vs %s), please update.\n", ctx->version, "v1.12.0");
			break;
		// XXX P510s: v2.05.0 is latest known so far...
		case P_HITI_520:
			if (strncmp(ctx->version, "1.22", 4) < 0 &&
			    strncmp(ctx->version, "1.17", 4) > 0)  /* V1.18 -> v1.21 have a known USB CLEAR_ENDPOINT_HALT issue */
				WARNING("Printer firmware %s has a known USB bug, please update to at least v1.22\n", ctx->version);
			else if (strncmp(ctx->version, "1.28", 4) < 0)
				WARNING("Printer firmware %s out of date (vs %s), please update.\n", ctx->version, "v1.28");
			break;
		case P_HITI_525:
			if (strncmp(ctx->version, "1.03.0.6", 8) < 0)
				WARNING("Printer firmware %s out of date (vs %s), please update.\n", ctx->version, "v1.03.0.6");
			break;
		case P_HITI_530:
			if (strncmp(ctx->version, "1.02.0", 6) < 0)
				WARNING("Printer firmware %s out of date (vs %s), please update.\n", ctx->version, "v1.02.0");
			break;
		case P_HITI_720:
			if (strncmp(ctx->version, "1.19", 4) < 0)
				WARNING("Printer firmware %s out of date (vs %s), please update.\n", ctx->version, "v1.19");
			break;
		case P_HITI_750:
			if (strncmp(ctx->version, "1.21.0", 6) < 0)
				WARNING("Printer firmware %s out of date (vs %s), please update.\n", ctx->version, "v1.21.0");
			break;
		default:
			break;
		}
		// do real stuff
	} else {
		if (ctx->sheet) {
			ctx->paper.type = PAPER_TYPE_X4INCH;
			ctx->erdc_rs.cols = 1280;
		} else {
			ctx->paper.type = PAPER_TYPE_6INCH;
			ctx->erdc_rs.cols = 1844;
		}

		ctx->ribbon.type = RIBBON_TYPE_4x6;
		ctx->ribbonvendor = 0x1005; /* CHC, type 2 */
		if (getenv("MEDIA_CODE") && strlen(getenv("MEDIA_CODE"))) {
			// set fake fw version?
			ctx->ribbon.type = strtol(getenv("MEDIA_CODE"), NULL, 16);
			if (ctx->ribbon.type == RIBBON_TYPE_5x7)
				ctx->paper.type = PAPER_TYPE_5INCH;
		}
	}

	ctx->marker.color = "#00FFFF#FF00FF#FFFF00";
	ctx->marker.name = hiti_ribbontypes(ctx->ribbon.type);
	ctx->marker.levelmax = hiti_ribboncounts(ctx->ribbon.type, ctx->conn->type);
	ctx->marker.levelnow = 0;

	if (ctx->sheet)
		ctx->marker.numtype = -1;
	else
		ctx->marker.numtype = ctx->ribbon.type;

	return CUPS_BACKEND_OK;
}

static void hiti_cleanup_job(const void *vjob) {
	const struct hiti_printjob *job = vjob;

	if (job->databuf)
		free(job->databuf);

	if (job->heattable_v2)
		free(job->heattable_v2);
	if (job->heattable_buf)
		free(job->heattable_buf);
	// job->num_heattable_entries = 0;

	free((void*)job);
}

static void *hiti_get_heat_data(struct hiti_ctx *ctx, uint8_t mode, int ribbonvendor, int *len)
{
	int ret;
	void *buf;

	int mediaver = ribbonvendor & RRVC_VERSION_MASK;
	int mediatype = ribbonvendor & RRVC_VENDOR_MASK;

	const char *modelname = hiti_modelcode(ctx->conn->type, ctx->erdc_rs.hwFeature1 & 0x80); // XXX P52x only
	const char *modename;
	const char *medianame;
	char fname_base[24];

	buf = malloc(HEATTABLE_V2_MAX_SIZE);
	if (!buf) {
		WARNING("Memory allocation failure!\n");
		return NULL;
	}

	if (mode) {
		modename = "q";
	} else {
		modename = "t";
	} // XXX 'p' 'r' and 'o' ??

	if (mediatype == 0x1000) {
		medianame = "c";
	} else {
		medianame = "h";
	} // XXX 'd' ??

	// XXX special case!  hea0qcra.bin/heatqcra.bin, and hea0tcra.bin/heattcra.bin, and they differ!

	DEBUG("Locating heat table for model %d, mode %02x, media %02x, ver %02x\n",
	      ctx->conn->type, mode, mediatype >> 8, mediaver);

	while (mediaver >= 0) {
		char full[2048];

		char ver;

		if (mediaver == 0)
			ver = 't';
		else if (mediaver < 10)
			ver = '0' + mediaver;
		else
			ver = 'a' + mediaver - 10;

		/* Build base name */
		snprintf(fname_base, sizeof(fname_base)-1, "hea%c%s%s%s.bin", ver, modename, medianame, modelname);

		snprintf(full, sizeof(full), "%s/%s", corrtable_path, fname_base);

		ret = dyesub_read_file2(full, buf, HEATTABLE_V2_MAX_SIZE, len, 1);
		if (!ret)
			break;

		mediaver--;
	}

	if (mediaver < 0) {
		WARNING("Unable to find suitable heat table file (%s) skipping\n", fname_base);
		free(buf);
		return NULL;
	}

	DEBUG("Using heat table in (%s)\n", fname_base);

	return buf;
}

#define CORRECTION_FILE_SIZE (33*33*33*3 + 2)
static uint8_t *hiti_get_correction_data(struct hiti_ctx *ctx, uint8_t mode, int colormode, int ribbonvendor)
{
	uint8_t *buf;
	int ret, len;

	int mediaver = ribbonvendor & RRVC_VERSION_MASK;
	int mediatype = ribbonvendor & RRVC_VENDOR_MASK;

	const char *modelname = hiti_modelcode(ctx->conn->type, ctx->erdc_rs.hwFeature1 & 0x80); // XXX P52x only
	const char *colorname;
	const char *modename;
	const char *medianame;
	char fname_base[24] = {0};

	buf = malloc(CORRECTION_FILE_SIZE);
	if (!buf) {
		WARNING("Memory allocation failure!\n");
		return NULL;
	}

	if (ctx->conn->type == P_HITI_CS2XX) {
		colorname = "B"; // XXX many more, investigate more carefully.
	} else if (colormode == HT_COLORMODE_CLASSIC) {
		colorname = "CL";
	} else if (colormode == HT_COLORMODE_IDPASS_VIVID) {
		colorname = "P";
	} else {
		colorname = "I";
	} // XXX PR, L, LR, C, SO, B, M, H, T, and more?

	if (mode) {
		modename = "Q";
	} else {
		modename = "P";
	}

	if (mediatype == 0x1000) {
		medianame = "C";
	} else {
		medianame = "M";
	} // XXX K/D ??

	DEBUG("Locating correction data for model %d, mode %02x, media %02x, ver %02x, color %02x\n",
	      ctx->conn->type, mode, mediatype >> 8, mediaver, colormode);

	/* Prefer file with explicit '0' version, fall back to one without it. */
	while (mediaver >= -1) {
		char full[2048];

		/* Build base name */
		if (mediaver >= 0)
			snprintf(fname_base, sizeof(fname_base)-1, "C%s%s%c%s%s.bin", medianame, modename,
				 mediaver < 10 ? '0' + mediaver : 'a' + mediaver - 10,
				 colorname,  modelname);
		else
			snprintf(fname_base, sizeof(fname_base)-1, "C%s%s%s%s.bin", medianame, modename, colorname,  modelname);

		snprintf(full, sizeof(full), "%s/%s", corrtable_path, fname_base);

		ret = dyesub_read_file2(full, buf, CORRECTION_FILE_SIZE, &len, 1);
		if (!ret && len == CORRECTION_FILE_SIZE) {
			break;
		}

		mediaver--;
	}

	if (mediaver < -1) {
		WARNING("Unable to find suitable correction data file (%s) skipping\n", fname_base);
		free(buf);
		return NULL;
	}

	DEBUG("Using correction data in (%s)\n", fname_base);

	return buf;
}

static int hiti_seht2(struct hiti_ctx *ctx, uint8_t plane,
		      const uint8_t *buf, uint32_t buf_len)
{
	uint8_t cmdbuf[sizeof(struct hiti_seht2)];
	struct hiti_seht2 *cmd = (struct hiti_seht2 *)cmdbuf;
	int ret, num = 0;

	buf_len += 5;

	cmd->hdr = 0xa5;
	cmd->len = cpu_to_be16(buf_len >> 8);
	cmd->status = CMD_STATUS_OK;
	cmd->cmd = cpu_to_be16(CMD_ESD_SEHT2);
	cmd->lenb = buf_len & 0xff;
	cmd->plane = plane;

	buf_len -= 5;

	/* Send over command */
	if ((ret = send_data(ctx->conn, (uint8_t*) cmd, sizeof(*cmd)))) {
		return ret;
	}

	__usleep(10*1000);

	/* Read back command */
	ret = read_data(ctx->conn, cmdbuf, 6, &num);
	if (ret)
		return ret;

	// XXX check resp length?

	/* Send payload, if any */
	if (buf_len) {
		ret = send_data(ctx->conn, buf, buf_len);
	}

	__usleep(200*1000);

	return ret;
}

static int hiti_cvd(struct hiti_ctx *ctx, const uint8_t *buf, uint32_t buf_len)
{
	uint8_t cmdbuf[sizeof(struct hiti_cmd)];
	struct hiti_cmd *cmd = (struct hiti_cmd *)cmdbuf;
	int ret, num = 0;

	cmd->hdr = 0xa5;
	cmd->len = cpu_to_be16(buf_len + 3);
	cmd->status = CMD_STATUS_OK;
	cmd->cmd = cpu_to_be16(CMD_EDM_CVD);

	/* Send over command */
	if ((ret = send_data(ctx->conn, (uint8_t*) cmd, sizeof(*cmd)))) {
		return ret;
	}

	__usleep(10*1000);

	/* Read back command */
	ret = read_data(ctx->conn, cmdbuf, 6, &num);
	if (ret)
		return ret;

	// XXX check resp length?

	/* Send payload, if any */
	if (buf_len) {
		ret = send_data(ctx->conn, buf, buf_len);
	}

	__usleep(200*1000);

	return ret;
}

static void *hiti_heattablev1_find_entry(uint8_t *data, int type, int totallen)
{
	int offset = 0;
	while (offset < totallen) {
		struct hiti_heattable_hdr_v1 *hdr = (struct hiti_heattable_hdr_v1 *) (data + offset);
		offset += sizeof(*hdr);

		if (hdr->type == type)
			return data + offset;
		offset += (le32_to_cpu(hdr->len) * 2);
	}
	return NULL;
}

static int hiti_send_heat_data_v1(struct hiti_ctx *ctx, void *heatdata, int len, uint8_t matte)
{
	uint8_t *y, *m, *c, *o, *om, *cvd;

	int ret;

	y = hiti_heattablev1_find_entry(heatdata, HITI_HEATTABLE_V1_Y, len);
	m = hiti_heattablev1_find_entry(heatdata, HITI_HEATTABLE_V1_M, len);
	c = hiti_heattablev1_find_entry(heatdata, HITI_HEATTABLE_V1_C, len);
	o = hiti_heattablev1_find_entry(heatdata, HITI_HEATTABLE_V1_O, len);
	om = hiti_heattablev1_find_entry(heatdata, HITI_HEATTABLE_V1_OM, len);
	cvd = hiti_heattablev1_find_entry(heatdata, HITI_HEATTABLE_V1_CVD, len);

	if (!y || !m || !c || !o || !om || !cvd) {
		ERROR("Heattable incomplete\n");
		return CUPS_BACKEND_FAILED;
	}

	/* Send over the heat tables */
	ret = hiti_seht2(ctx, 0, y, HITI_HEATTABLE_V1_PLANESIZE);
	if (!ret)
		ret = hiti_seht2(ctx, 1, m, HITI_HEATTABLE_V1_PLANESIZE);
	if (!ret)
		ret = hiti_seht2(ctx, 2, c, HITI_HEATTABLE_V1_PLANESIZE);
	if (!ret) {
		if (matte)
			ret = hiti_seht2(ctx, 3, om, HITI_HEATTABLE_V1_PLANESIZE);
		else
			ret = hiti_seht2(ctx, 3, o, HITI_HEATTABLE_V1_PLANESIZE);
	}

	/* And finally, send over the CVD data */
	if (!ret)
		ret = hiti_cvd(ctx, cvd, HITI_HEATTABLE_V1_CVDSIZE);

	return ret;
}

/* HiTi's funky interpolation table processing

   Note this is a standard "CUBE" LUT (33x33x33) so there are options
   for making this faster!
*/
struct rgb {
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

static uint32_t interp1089[33];
static uint32_t interp33[33];
static uint16_t interp256[256*9];

static void hiti_interp_init(void)
{
	int i;
	uint16_t *pre, *cur;

	for (i = 0 ; i < 33 ; i++) {
		interp1089[i] = i * 1089;
		interp33[i] = i * 33;
	}
	memset(interp256, 0, sizeof(interp256));
	pre = &interp256[0];
	cur = &interp256[256];

	for (i = 1 ; i < 9 ; i++) {
		int j;
		for (j = 0 ; j < 256 ; j++) {
			cur[j] = pre[j] + j;
		};
		pre += 256;
		cur += 256;
	}
}

/* src and dst are RGB tuples */
static void hiti_interp33_256(uint8_t *dst, const uint8_t *src, const uint8_t *pTable)
{
	struct rgb p1_pos, p2_pos, p3_pos, p4_pos;
	struct rgb p1_val, p2_val, p3_val, p4_val;
	uint8_t r_weight, g_weight, b_weight;
	uint16_t w1, w2, w3, w4;
	uint16_t *pw1, *pw2, *pw3, *pw4;
	uint32_t pos;

	/* Get Grid position */
	p1_pos.r = src[0] >> 3;
	p1_pos.g = src[1] >> 3;
	p1_pos.b = src[2] >> 3;

	p4_pos.r = p1_pos.r + 1;
	p4_pos.g = p1_pos.g + 1;
	p4_pos.b = p1_pos.b + 1;

	/* Weights */
	if (src[0] == 255)
		r_weight = 8;
	else
		r_weight = src[0] & 0x7;

	if (src[1] == 255)
		g_weight = 8;
	else
		g_weight = src[1] & 0x7;

	if (src[2] == 255)
		b_weight = 8;
	else
		b_weight = src[2] & 0x7;

	/* Work out relative weights and offsets */
	if (r_weight >= g_weight) {
		if (g_weight >= b_weight) { /* R > G > B */
			w1 = 8 - r_weight;
			w2 = r_weight - g_weight;
			w3 = g_weight - b_weight;
			w4 = b_weight;
			p2_pos.r = p1_pos.r + 1;
			p2_pos.g = p1_pos.g;
			p2_pos.b = p1_pos.b;
			p3_pos.r = p1_pos.r + 1;
			p3_pos.g = p1_pos.g + 1;
			p3_pos.b = p1_pos.b;
		} else {
			if (r_weight >= b_weight) { /* R > B > G */
				w1 = 8 - r_weight;
				w2 = r_weight - b_weight;
				w3 = b_weight - g_weight;
				w4 = g_weight;
				p2_pos.r = p1_pos.r + 1;
				p2_pos.g = p1_pos.g;
				p2_pos.b = p1_pos.b;
				p3_pos.r = p1_pos.r + 1;
				p3_pos.g = p1_pos.g;
				p3_pos.b = p1_pos.b + 1;
			} else { /* B > R > G */
				w1 = 8 - b_weight;
				w2 = b_weight - r_weight;
				w3 = r_weight - g_weight;
				w4 = g_weight;
				p2_pos.r = p1_pos.r;
				p2_pos.g = p1_pos.g;
				p2_pos.b = p1_pos.b + 1;
				p3_pos.r = p1_pos.r + 1;
				p3_pos.g = p1_pos.g;
				p3_pos.b = p1_pos.b + 1;
			}
		}
	} else {
		if (r_weight >= b_weight) { /* G > R > B */
			w1 = 8 - g_weight;
			w2 = g_weight - r_weight;
			w3 = r_weight - b_weight;
			w4 = b_weight;
			p2_pos.r = p1_pos.r;
			p2_pos.g = p1_pos.g + 1;
			p2_pos.b = p1_pos.b;
			p3_pos.r = p1_pos.r + 1;
			p3_pos.g = p1_pos.g + 1;
			p3_pos.b = p1_pos.b;
		} else {
			if (g_weight >= b_weight) { /* G > B > R */
				w1 = 8 - g_weight;
				w2 = g_weight - b_weight;
				w3 = b_weight - r_weight;
				w4 = r_weight;
				p2_pos.r = p1_pos.r;
				p2_pos.g = p1_pos.g + 1;
				p2_pos.b = p1_pos.b;
				p3_pos.r = p1_pos.r;
				p3_pos.g = p1_pos.g + 1;
				p3_pos.b = p1_pos.b + 1;
			} else { /* B > G > R */
				w1 = 8 - b_weight;
				w2 = b_weight - g_weight;
				w3 = g_weight - r_weight;
				w4 = r_weight;
				p2_pos.r = p1_pos.r;
				p2_pos.g = p1_pos.g;
				p2_pos.b = p1_pos.b + 1;
				p3_pos.r = p1_pos.r;
				p3_pos.g = p1_pos.g + 1;
				p3_pos.b = p1_pos.b + 1;
			}
		}
	}

	/* Work out values */
	pos = (interp1089[p1_pos.b] + interp33[p1_pos.g] + p1_pos.r) * 3;
	p1_val.r = pTable[pos];
	p1_val.g = pTable[pos + 1];
	p1_val.b = pTable[pos + 2];
	pos = (interp1089[p2_pos.b] + interp33[p2_pos.g] + p2_pos.r) * 3;
	p2_val.r = pTable[pos];
	p2_val.g = pTable[pos + 1];
	p2_val.b = pTable[pos + 2];
	pos = (interp1089[p3_pos.b] + interp33[p3_pos.g] + p3_pos.r) * 3;
	p3_val.r = pTable[pos];
	p3_val.g = pTable[pos + 1];
	p3_val.b = pTable[pos + 2];
	pos = (interp1089[p4_pos.b] + interp33[p4_pos.g] + p4_pos.r) * 3;
	p4_val.r = pTable[pos];
	p4_val.g = pTable[pos + 1];
	p4_val.b = pTable[pos + 2];

	/* Final offsets into interpolation table */
	pw1 = &interp256[w1 << 8];
	pw2 = &interp256[w2 << 8];
	pw3 = &interp256[w3 << 8];
	pw4 = &interp256[w4 << 8];

	/* And at long last.. final values */
	dst[0] = (pw1[p1_val.r] + pw2[p2_val.r] + pw3[p3_val.r] + pw4[p4_val.r]) >> 3;
	dst[1] = (pw1[p1_val.g] + pw2[p2_val.g] + pw3[p3_val.g] + pw4[p4_val.g]) >> 3;
	dst[2] = (pw1[p1_val.b] + pw2[p2_val.b] + pw3[p3_val.b] + pw4[p4_val.b]) >> 3;

}

static int hiti_read_parse(void *vctx, const void **vjob, int data_fd, int copies)
{
	struct hiti_ctx *ctx = vctx;
	struct hiti_printjob *job = NULL;
	int ret;

	if (!ctx)
		return CUPS_BACKEND_FAILED;

	job = malloc(sizeof(*job));
	if (!job) {
		ERROR("Memory allocation failure!\n");
		return CUPS_BACKEND_RETRY_CURRENT;
	}
	memset(job, 0, sizeof(*job));
	job->common.jobsize = sizeof(*job);
	job->common.copies = copies;

	/* Read in header */
	ret = read(data_fd, &job->hdr, sizeof(job->hdr));
	if (ret < 0 || ret != sizeof(job->hdr)) {
		hiti_cleanup_job(job);
		if (ret == 0)
			return CUPS_BACKEND_CANCEL;

		ERROR("Read failed (%d/%d)\n",
		      ret, (int)sizeof(job->hdr));
		perror("ERROR: Read failed");
		return ret;
	}

	/* Byteswap everything */
	{
		uint32_t *ptr = (uint32_t*) &job->hdr;
		int i;
		for (i = 0 ; i < (int)(sizeof(job->hdr) / sizeof(uint32_t)) ; i++)
			ptr[i] = le32_to_cpu(ptr[i]);
	}

	/* Sanity check header */
	if (job->hdr.hdr_len != sizeof(job->hdr)) {
		ERROR("Header length mismatch (%u/%d)!\n", job->hdr.hdr_len, (int)sizeof(job->hdr));
		hiti_cleanup_job(job);
		return CUPS_BACKEND_CANCEL;
	}
	if (job->hdr.cookie != HDR_COOKIE) {
		ERROR("Unrecognized header!\n");
		hiti_cleanup_job(job);
		return CUPS_BACKEND_CANCEL;
	}

	/* Use whicever copy count is larger */
	if (job->common.copies < (int)job->hdr.copies)
		job->common.copies = job->hdr.copies;

	/* Sanity check printer type vs job type */
	switch(ctx->conn->type)	{
	// XXX add P11x
	// XXX add P320/P310
	case P_HITI_461:
		if (job->hdr.model != 461) {
			ERROR("Unrecognized header!\n");
			hiti_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		break;
	case P_HITI_51X:
		if (job->hdr.model != 510) {
			ERROR("Unrecognized header!\n");
			hiti_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		break;
	case P_HITI_520:
	case P_HITI_525:
	case P_HITI_530:
		if (job->hdr.model != 520) {
			ERROR("Unrecognized header!\n");
			hiti_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		break;
	case P_HITI_720:
	case P_HITI_750:
		if (job->hdr.model != 720) {
			ERROR("Unrecognized header!\n");
			hiti_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		break;
	default:
		break;
	}

	/* Allocate a buffer */
	job->datalen = 0;
	job->databuf = malloc(job->hdr.payload_len);
	if (!job->databuf) {
		ERROR("Memory allocation failure!\n");
		hiti_cleanup_job(job);
		return CUPS_BACKEND_RETRY_CURRENT;
	}

	/* Read in data */
	uint32_t remain = job->hdr.payload_len;
	while (remain) {
		ret = read(data_fd, job->databuf + job->datalen, remain);
		if (ret < 0) {
			ERROR("Read failed (%d/%u/%u)\n",
			      ret, remain, job->datalen);
			perror("ERROR: Read failed");
			hiti_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		job->datalen += ret;
		remain -= ret;
	}

	/* Sanity check against paper */
	switch (ctx->paper.type) {
	case PAPER_TYPE_X4INCH:
		// XXX P461 is 1280
		// XXX P320/P310 ???
		// XXX P11x is ???
		if (job->hdr.cols != ctx->erdc_rs.cols) {
			ERROR("Illegal job on 4x6-inch paper!\n");
			hiti_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		break;
	case PAPER_TYPE_5INCH:
		if (!(job->hdr.payload_flag & PAYLOAD_FLAG_YMCPLANAR) &&
		    job->hdr.cols == 1548)
			break;
		/* Intentional fallthrough */
	case PAPER_TYPE_6INCH:
		if (job->hdr.cols != ctx->erdc_rs.cols) {
			ERROR("Illegal job on %d-inch paper!\n", ctx->paper.type == PAPER_TYPE_5INCH ? 5 : 6);
			hiti_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		break;
	default:
		ERROR("Unknown paper type (%d)!\n", ctx->paper.type);
		hiti_cleanup_job(job);
		return CUPS_BACKEND_CANCEL;
	}

	/* Sanity check against ribbon type */
	switch (ctx->ribbon.type) {
	case RIBBON_TYPE_4x6:
		if (job->hdr.code != PRINT_TYPE_6x4 &&
		    job->hdr.code != PRINT_TYPE_6x4_2UP &&
		    job->hdr.code != PRINT_TYPE_6x2) {
			ERROR("Invalid ribbon type vs job (%02x/%02x)\n",
			      ctx->ribbon.type, job->hdr.code);
			hiti_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		break;
	case RIBBON_TYPE_5x7:
		if (job->hdr.code != PRINT_TYPE_5x7 &&
		    job->hdr.code != PRINT_TYPE_5x3_5 &&
		    job->hdr.code != PRINT_TYPE_5x7_2UP) {
			ERROR("Invalid ribbon type vs job (%02x/%02x)\n",
			      ctx->ribbon.type, job->hdr.code);
			hiti_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		if (job->hdr.code == PRINT_TYPE_5x3_5)
			job->common.can_combine = 1;
		break;
	case RIBBON_TYPE_6x8:
		if (job->hdr.code != PRINT_TYPE_6x4 &&
		    job->hdr.code != PRINT_TYPE_6x4_2UP &&
		    job->hdr.code != PRINT_TYPE_6x8 &&
		    job->hdr.code != PRINT_TYPE_6x2 &&
		    job->hdr.code != PRINT_TYPE_6x9_2UP) {
			ERROR("Invalid ribbon type vs job (%02x/%02x)\n",
			      ctx->ribbon.type, job->hdr.code);
			hiti_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		if (job->hdr.code == PRINT_TYPE_6x4)
			job->common.can_combine = 1;
		break;
	case RIBBON_TYPE_6x9:
		if (job->hdr.code != PRINT_TYPE_6x4 &&
		    job->hdr.code != PRINT_TYPE_6x4_2UP &&
		    job->hdr.code != PRINT_TYPE_6x8 &&
		    job->hdr.code != PRINT_TYPE_6x2 &&
		    job->hdr.code != PRINT_TYPE_6x9 &&
		    job->hdr.code != PRINT_TYPE_6x9_2UP) {
			ERROR("Invalid ribbon type vs job (%02x/%02x)\n",
			      ctx->ribbon.type, job->hdr.code);
			hiti_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		if (job->hdr.code == PRINT_TYPE_6x4)
			job->common.can_combine = 1;
		break;
	default:
		ERROR("Unknown ribbon type (%d)!\n", ctx->ribbon.type);
		hiti_cleanup_job(job);
		return CUPS_BACKEND_CANCEL;
	}

	/* Set color mode.  XXX move into GP header somehow? */
	job->colormode = HT_COLORMODE_IDPASS_VIVID;

	int ribbonvendor = ctx->ribbonvendor;
	if (job->hdr.payload_flag & PAYLOAD_FLAG_MEDIAVER) {
		ctx->ribbonvendor &= ~RRVC_VERSION_MASK;
		ctx->ribbonvendor |= ((job->hdr.payload_flag >> 24) & RRVC_VERSION_MASK);
	}

	/* Convert input packed BGR data into YMC planar, if needed */
	if (!(job->hdr.payload_flag & PAYLOAD_FLAG_YMCPLANAR)) {
		/* Load up correction data, if requested */
		uint8_t *corrdata = NULL;
		if (!(job->hdr.payload_flag & PAYLOAD_FLAG_NOCORRECT)) {
			corrdata = hiti_get_correction_data(ctx, job->hdr.quality, job->colormode, ribbonvendor);
			if (corrdata) {
				hiti_interp_init();
				INFO("Running input data through correction tables\n");
			}
		}

		/* Printer expects FULL WIDTH data, even for 5" prints */
		int  pad1, pad2;
		pad1 = (ctx->erdc_rs.cols - job->hdr.cols) / 2;
		pad2 = ctx->erdc_rs.cols - job->hdr.cols - pad1;

		uint8_t *ymcbuf = malloc(job->hdr.rows * ctx->erdc_rs.cols * 3);
		uint32_t i, j;

		if (!ymcbuf) {
			hiti_cleanup_job(job);
			ERROR("Memory Allocation Failure!\n");
			return CUPS_BACKEND_FAILED;
		}

		for (i = 0 ; i < job->hdr.rows ; i++) {
			uint8_t *rowY = ymcbuf + ctx->erdc_rs.cols * i;
			uint8_t *rowM = ymcbuf + ctx->erdc_rs.cols * (job->hdr.rows + i);
			uint8_t *rowC = ymcbuf + ctx->erdc_rs.cols * (job->hdr.rows * 2 + i);

			/* Simple optimization */
			uint8_t oldrgb[3] = { 255, 255, 255 };
			uint8_t destrgb[3];

			if (corrdata) {
				hiti_interp33_256(destrgb, oldrgb, corrdata);
			}

			/* Leading Padding */
			memset(rowY, 0, pad1);
			rowY += pad1;
			memset(rowM, 0, pad1);
			rowM += pad1;
			memset(rowC, 0, pad1);
			rowC += pad1;

			for (j = 0 ; j < job->hdr.cols ; j++) {
				uint8_t rgb[3];
				uint32_t base = (job->hdr.cols * i + j) * 3;

				/* Input data is BGR */
				rgb[2] = job->databuf[base];
				rgb[1] = job->databuf[base + 1];
				rgb[0] = job->databuf[base + 2];

				if (corrdata) {
					if (rgb[0] == oldrgb[0] &&
					    rgb[1] == oldrgb[1] &&
					    rgb[2] == oldrgb[2]) {
						rgb[0] = destrgb[0];
						rgb[1] = destrgb[1];
						rgb[2] = destrgb[2];
					} else {
						oldrgb[0] = rgb[0];
						oldrgb[1] = rgb[1];
						oldrgb[2] = rgb[2];
						hiti_interp33_256(rgb, rgb, corrdata);
						destrgb[0] = rgb[0];
						destrgb[1] = rgb[1];
						destrgb[2] = rgb[2];
					}
				}

				/* Finally convert to YMC */
				rowY[j] = 255 - rgb[2];
				rowM[j] = 255 - rgb[1];
				rowC[j] = 255 - rgb[0];
			}

			/* Trailing Padding */
			memset(rowY + job->hdr.cols, 0, pad2);
			memset(rowM + job->hdr.cols, 0, pad2);
			memset(rowC + job->hdr.cols, 0, pad2);
		}

		/* Nuke the old BGR buffer and replace it with YMC buffer */
		free(job->databuf);
		job->databuf = ymcbuf;
		job->datalen = ctx->erdc_rs.cols * 3 * job->hdr.cols;
		job->hdr.cols = ctx->erdc_rs.cols;

		if (corrdata)
			free(corrdata);
	}

	/* Read in heat table for job */
	job->heattable_buf = hiti_get_heat_data(ctx, job->hdr.quality, ribbonvendor, &job->heattable_len);

	if (job->heattable_buf &&
	    ctx->heattable_type == HEATTABLE_TYPE_V2) {
		ret = hiti_parse_heattable_v2(job);
		if (ret) {
			WARNING("Failed to parse v2 heattable, ignoring!\n");
			job->heattable_len = 0;
		}
		uint32_t ver_maj = -1;
		uint32_t ver_min = -1;

		void *dat;
		size_t len;

		dat = hiti_find_heattable_v2_entry(job, HEATTABLE_V2_ID_VER_MAJOR, &len);
		if (dat) {
			memcpy(&ver_maj, dat, sizeof(ver_maj));
			ver_maj = le32_to_cpu(ver_maj);
		}
		dat = hiti_find_heattable_v2_entry(job, HEATTABLE_V2_ID_VER_MINOR, &len);
		if (dat) {
			memcpy(&ver_min, dat, sizeof(ver_min));
			ver_min = le32_to_cpu(ver_min);
		}
		INFO("Heattable version %08x.%08x\n", ver_maj, ver_min);

		/* Assemble SHPTC table for specific job requirements */
		void *shptc_buf = malloc(HEATTABLE_V2_MAX_SIZE);
		if (!shptc_buf) {
			ERROR("Memory allocation failure\n");
			return CUPS_BACKEND_FAILED;
		}
		struct hiti_heattable_hdr_v2 *table = shptc_buf;

		int planes = HT_PLANE_Y | HT_PLANE_M | HT_PLANE_C | HT_CVD;
		planes |= (job->hdr.overcoat) ? HT_PLANE_OM : HT_PLANE_OG;
		ret = hiti_construct_heattable_v2(job, planes, job->colormode, table);

		if (ret) {
			job->heattable_len = 0;
		} else {
			int datalen = 1 + table->num_headers * sizeof(struct hiti_heattable_entry_v2);

			for (int i = 0 ; i < table->num_headers; i++) {
				dat = hiti_find_heattable_v2_entry(job, table->entries[i].type, &len);
				table->entries[i].len = cpu_to_le32(len);
				table->entries[i].offset = cpu_to_le32(datalen);
				memcpy(((uint8_t*)shptc_buf)+datalen, dat, len);

				if (dyesub_debug)
					DEBUG("Job entry: %02x len %lu @ %d\n", table->entries[i].type, len, datalen);

				datalen += len;
			}
			free(job->heattable_buf);
			job->heattable_buf = shptc_buf;
			job->heattable_len = datalen;

			free(job->heattable_v2);
			job->heattable_v2 = NULL;
			job->num_heattable_entries = 0;
		}
	}

	*vjob = job;

	return CUPS_BACKEND_OK;
}

static int calc_offset(int val, int mid, int max, int step)
{
	if (val > max)
		val = max;
	else if (val < 0)
		val = 0;

	val -= mid;
	val *= step;

	return val;
}

static int hiti_main_loop(void *vctx, const void *vjob, int wait_for_return)
{
	struct hiti_ctx *ctx = vctx;

	int ret;
	uint32_t err = 0;
	uint8_t sts[3];
	struct hiti_job jobid;

	const struct hiti_printjob *job = vjob;

	if (!ctx)
		return CUPS_BACKEND_FAILED;
	if (!job)
		return CUPS_BACKEND_FAILED;

	INFO("Waiting for printer idle\n");

	do {
		ret = hiti_query_status(ctx, sts, &err);
		if (ret)
			return ret;

		/* If we have an error state, bail! */
		if (err) {
			ERROR("Printer reported alert: %08x (%s)\n",
			      err, hiti_errors(err));
			return CUPS_BACKEND_STOP;
		}

		/* If we're able to accept jobs, proceed */
		if (!(sts[0] & (STATUS0_POWERON|STATUS0_BUSY)))
			break;

		sleep(1);
	} while(1);

	dump_markers(&ctx->marker, 1, 0);

	uint16_t resplen = 0;
	uint16_t rows = job->hdr.rows;
	uint16_t cols = ((4*job->hdr.cols) + 3) / 4;

	// XXX these two only need to change if rows > 3000
	uint16_t startLine = 0;
	uint16_t numLines = rows;

	uint32_t sent = 0;

	/* Set up and send over Sublimation Format */
	struct hiti_efd_sf sf;
	sf.mediaType = job->hdr.code;
	sf.cols_res = cpu_to_be16(job->hdr.col_dpi);
	sf.rows_res = cpu_to_be16(job->hdr.row_dpi);
	sf.cols = cpu_to_be16(job->hdr.cols);
	sf.rows = cpu_to_be16(rows);
	sf.rows_offset = calc_offset(5, ctx->calibration.vert, 8, 4);
	sf.cols_offset = calc_offset(ctx->calibration.horiz, 6, 11, 4);
	sf.colorSeq = SF_COLORSEQ_YMCO | (job->hdr.overcoat ? SF_COLORSEQ_MATTE : 0);
	sf.copies = job->common.copies;
	sf.printMode = (ctx->conn->type == P_HITI_461) // XXX P320/P310
		? 0 : SF_PRINTMODE_BASE + (job->hdr.quality ? SF_PRINTMODE_FINE : 0);
	memset(sf.zero, 0, sizeof(sf.zero));
	ret = hiti_docmd(ctx, CMD_EFD_SF, (uint8_t*) &sf, // XXX P320/P310
			 ctx->conn->type == P_HITI_461 ? sizeof(sf) : sizeof(sf)-4 ,
			 &resplen);
	if (ret)
		return CUPS_BACKEND_FAILED;

	// XXX on P461:  (and maybe 320/310)
	// request warning
	// QJC
	// then SF again

	// XXX msg 8011 sent here on P52x (and maybe others?)

	/* Initialize jobid structure */
	jobid.lun = 0;
	jobid.jobid = cpu_to_be16(ctx->jobid);

	resplen = sizeof(jobid);
	ret = hiti_docmd_resp(ctx, CMD_JC_SJ, (uint8_t*) &jobid, sizeof(jobid),
			      (uint8_t*) &jobid, &resplen);
	if (ret)
		return CUPS_BACKEND_FAILED;

	INFO("Printer returned Job ID %04x\n", be16_to_cpu(jobid.jobid));

	if (job->heattable_len && !ctx->nodata) {
		switch(ctx->heattable_type) {
		case HEATTABLE_TYPE_V0:
			if (job->heattable_len == HEATTABLE_S_SIZE || job->heattable_len == HEATTABLE_S2_SIZE) {
				/* Send ESC_SHPTC with entire file contents */
				ret = hiti_shptc(ctx, job->heattable_buf, job->heattable_len);
			} else {
				ERROR("Unexpected heattable size (%d vs %d/%d)\n", job->heattable_len, HEATTABLE_S_SIZE, HEATTABLE_S2_SIZE);
				ret = CUPS_BACKEND_FAILED;
			}
			break;
		case HEATTABLE_TYPE_V1:
			ret = hiti_send_heat_data_v1(ctx, job->heattable_buf, job->heattable_len, job->hdr.overcoat);
			break;
		case HEATTABLE_TYPE_V2:
			/* Just send the computed blob over */
			ret = hiti_shptc(ctx, job->heattable_buf, job->heattable_len);
			break;
		}
#if 0
		if (ctx->conn->type == P_HITI_51X) {
			uint8_t esd_unka[4] = { 0x00, 0x87, 0x00, 0x02 }; // XXX figure me out eventually?
			ret = hiti_docmd(ctx, CMD_ESD_UNKA, esd_unka, sizeof(esd_unka), &resplen);
			if (ret)
				return CUPS_BACKEND_FAILED;
		}
#endif
	}

	/* If we don't have a heat file or the heat transfer failed, revert to default tables */
	if (!job->heattable_len || ret) {
		if (ctx->conn->type != P_HITI_525) {
			uint8_t chs[2] = { 0, 1 }; /* Reverts to default tables */
			resplen = 0;
			ret = hiti_docmd(ctx, CMD_EFD_CHS, chs, sizeof(chs), &resplen);
			if (ret)
				return CUPS_BACKEND_FAILED;
		}
	}

	/* Start Page */
	ret = hiti_docmd(ctx, CMD_EPC_SP, NULL, 0, &resplen);
	if (ret)
		return CUPS_BACKEND_FAILED;

resend_y:
	INFO("Sending yellow plane\n");
	ret = hiti_docmd(ctx, CMD_EPC_SYP, NULL, 0, &resplen);
	if (ret)
		return CUPS_BACKEND_FAILED;
	ret = hiti_sepd(ctx, rows * cols, startLine, numLines);
	if (ret)
		return CUPS_BACKEND_FAILED;
	ret = send_data(ctx->conn, job->databuf + sent, rows * cols);
	if (ret)
		return CUPS_BACKEND_FAILED;
	__usleep(200*1000);
	ret = hiti_query_status(ctx, sts, &err);
	if (ret)
		return ret;
	if (err) {
		ERROR("Printer reported alert: %08x (%s)\n",
		      err, hiti_errors(err));
		return CUPS_BACKEND_FAILED;
	}
	if (sts[0] & STATUS0_RESEND_DATA) {
		WARNING("Printer requested resend\n");
		goto resend_y;
	}
	sent += rows * cols;

resend_m:
	INFO("Sending magenta plane\n");
	ret = hiti_docmd(ctx, CMD_EPC_SMP, NULL, 0, &resplen);
	if (ret)
		return CUPS_BACKEND_FAILED;
	ret = hiti_sepd(ctx, rows * cols, startLine, numLines);
	if (ret)
		return CUPS_BACKEND_FAILED;
	ret = send_data(ctx->conn, job->databuf + sent, rows * cols);
	if (ret)
		return CUPS_BACKEND_FAILED;
	__usleep(200*1000);
	ret = hiti_query_status(ctx, sts, &err);
	if (ret)
		return ret;
	if (err) {
		ERROR("Printer reported alert: %08x (%s)\n",
		      err, hiti_errors(err));
		return CUPS_BACKEND_FAILED;
	}
	if (sts[0] & STATUS0_RESEND_DATA) {
		WARNING("Printer requested resend\n");
		goto resend_m;
	}
	sent += rows * cols;

resend_c:
	INFO("Sending cyan plane\n");
	ret = hiti_docmd(ctx, CMD_EPC_SCP, NULL, 0, &resplen);
	if (ret)
		return CUPS_BACKEND_FAILED;
	ret = hiti_sepd(ctx, rows * cols, startLine, numLines);
	if (ret)
		return CUPS_BACKEND_FAILED;
	ret = send_data(ctx->conn, job->databuf + sent, rows * cols);
	if (ret)
		return CUPS_BACKEND_FAILED;
	__usleep(200*1000);

	ret = hiti_query_status(ctx, sts, &err);
	if (ret)
		return ret;
	if (err) {
		ERROR("Printer reported alert: %08x (%s)\n",
		      err, hiti_errors(err));
		return CUPS_BACKEND_FAILED;
	}
	if (sts[0] & STATUS0_RESEND_DATA) {
		WARNING("Printer requested resend\n");
		goto resend_c;
	}
	sent += rows * cols;

	INFO("Sending Print start\n");
	ret = hiti_docmd(ctx, CMD_EPC_EP, NULL, 0, &resplen);
	if (ret)
		return CUPS_BACKEND_FAILED;

	resplen = 3;
	ret = hiti_docmd_resp(ctx, CMD_JC_EJ, (uint8_t*) &jobid, sizeof(jobid), (uint8_t*) &jobid, &resplen);
	if (ret)
		return CUPS_BACKEND_FAILED;

	INFO("Waiting for printer acknowledgement\n");
	do {
		struct hiti_job_qqa qqa;
		sleep(1);

		ret = hiti_query_status(ctx, sts, &err);
		if (ret)
			return ret;

		if (err) {
			ERROR("Printer reported alert: %08x (%s)\n",
			      err, hiti_errors(err));
			return CUPS_BACKEND_FAILED;
		}

		if (!wait_for_return) {
			INFO("Fast return mode enabled.\n");
			break;
		}

		/* See if our job is done.. */
		ret = hiti_query_job_qa(ctx, &jobid, &qqa);
		if (ret)
			return ret;

		/* If our job is complete.. */
		if (qqa.count == 0 || qqa.row[0].job.jobid == 0)
			break;

		for (int i = 0 ; i < qqa.count ; i++) {
			if (qqa.row[i].job.jobid == jobid.jobid) {
				if (qqa.row[i].status > QQA_STATUS_SUSPENDED) {
					ERROR("Printer reported abnormal job status %02x\n", qqa.row[i].status);
					return CUPS_BACKEND_FAILED;
				}
			}
		}
	} while(1);

	INFO("Print complete\n");

	return CUPS_BACKEND_OK;
}

static int hiti_dumpmatrix(struct hiti_ctx *ctx)
{
	int ret;

	if (ctx->conn->type == P_HITI_51X) {
		ret = hiti_query_matrix_51x(ctx);
	} else {
		ret = hiti_query_matrix(ctx);
	}
	if (ret)
		return CUPS_BACKEND_FAILED;

	int i;
	DEBUG("MAT ");
	for (i = 0 ; i < 512 ; i++) {
		if (i != 0 && (i % 16 == 0)) {
			DEBUG2("\n");
			DEBUG("    ");
		}
		DEBUG2("%02x ", ctx->matrix[i]);
	}
	DEBUG2("\n");

	return CUPS_BACKEND_OK;
}

static int hiti_cmdline_arg(void *vctx, int argc, char **argv)
{
	struct hiti_ctx *ctx = vctx;
	int i, j = 0;

	if (!ctx)
		return -1;

	while ((i = getopt(argc, argv, GETOPT_LIST_GLOBAL "irRsZ")) >= 0) {
		switch(i) {
		GETOPT_PROCESS_GLOBAL
		case 'i':
			hiti_get_info(ctx);
			break;
		case 'r':
			hiti_doreset(ctx, RESET_SOFT);
			break;
		case 'R':
			hiti_doreset(ctx, RESET_PRINTER);
			break;
		case 's':
			hiti_get_status(ctx);
			break;
		case 'Z':
			hiti_dumpmatrix(ctx);
			break;
		}

		if (j) return j;
	}

	return CUPS_BACKEND_OK;
}

static void hiti_cmdline(void)
{
	DEBUG("\t\t[ -i ]           # Query printer information\n");
	DEBUG("\t\t[ -r ]           # Soft Reset printer\n");
	DEBUG("\t\t[ -R ]           # Reset printer\n");
	DEBUG("\t\t[ -s ]           # Query printer status\n");
//      DEBUG("\t\t[ -Z ]           # Dump matrix table\n");
}

static int hiti_query_version(struct hiti_ctx *ctx)
{
	int ret;
	uint16_t len = 256;  /* P525 has 93 byte payload, P461 has 97, others are 79? */
	uint8_t buf[256];

	ret = hiti_docmd_resp(ctx, CMD_RDC_RS, NULL, 0, buf, &len);
	if (ret)
		return ret;

	/* Copy strings */
	strncpy(ctx->id, (char*) &buf[34], buf[33]);
	strncpy(ctx->version, (char*) &buf[34 + buf[33] + 1], sizeof(ctx->version));
	ctx->version[9] = 0;

	return CUPS_BACKEND_OK;
}

static int hiti_query_status(struct hiti_ctx *ctx, uint8_t *sts, uint32_t *err)
{
	int ret;
	uint16_t len = 3;
	uint16_t cmd;

	*err = 0;

	ret = hiti_docmd_resp(ctx, CMD_RDS_RSS, NULL, 0, sts, &len);
	if (ret)
		return ret;

	if (sts[2] & STATUS2_WARNING)
		cmd = CMD_RDS_RW;
	else if (sts[2] & STATUS2_DEVSERVICE)
		cmd = CMD_RDS_DSRA;
	else if (sts[2] & STATUS2_OPERATOR)
		cmd = CMD_RDS_ROIRA;
	else if (sts[1] & STATUS1_PAPERJAM)
		cmd = CMD_RDS_RJA;
	else if (sts[1] & STATUS1_INPUT)
		cmd = CMD_RDS_RIA;
	else if (sts[1] & STATUS1_SUPPLIES)
		cmd = CMD_RDS_SA;
	else
		cmd = 0;

	/* Query extended status, if needed */
	if (cmd) {
		uint8_t respbuf[17];  /* Enough for four errors */
		len = sizeof(respbuf);

		ret = hiti_docmd_resp(ctx, cmd, NULL, 0, respbuf, &len);
		if (ret)
			return ret;

		if (!respbuf[0])
			return CUPS_BACKEND_OK;

		if (respbuf[0] > 1) {
			WARNING("Multiple Alerts detected, only returning the first!\n");
		} else if (len > 8) {
			// XXX means we have ASCIIHEX in positions [5:8], convert to number..
			// eg "30 31 30 30" == Code 0100
		}

		memcpy(err, &respbuf[1], sizeof(*err));
		*err = be32_to_cpu(*err);
		*err >>= 8;
	}

	return CUPS_BACKEND_OK;
}

static int hiti_query_summary(struct hiti_ctx *ctx, struct hiti_erdc_rs *rds)
{
	int ret;
	uint16_t len = sizeof(*rds);

	ret = hiti_docmd_resp(ctx, CMD_ERDC_RS, NULL, 0, (uint8_t*)rds, &len);
	if (ret)
		return ret;

	rds->stride = be16_to_cpu(rds->stride);
	rds->dpi_cols = be16_to_cpu(rds->dpi_cols);
	rds->dpi_rows = be16_to_cpu(rds->dpi_rows);
	rds->cols = be16_to_cpu(rds->cols);
	rds->rows = be16_to_cpu(rds->rows);
	rds->vertUnitInstalled = be16_to_cpu(rds->vertUnitInstalled);
	rds->dpi_rows2 = be16_to_cpu(rds->dpi_rows2);
	rds->dpi_rows3 = be16_to_cpu(rds->dpi_rows3);
	return CUPS_BACKEND_OK;
}

static int hiti_query_rpidm(struct hiti_ctx *ctx)
{
	int ret;
	uint16_t len = sizeof(ctx->rpidm);

	ret = hiti_docmd_resp(ctx, CMD_ERDC_RPIDM, NULL, 0, (uint8_t*)&ctx->rpidm, &len);
	if (ret)
		return ret;

	ctx->rpidm.usb_pid = be16_to_cpu(ctx->rpidm.usb_pid);

	return CUPS_BACKEND_OK;
}

static int hiti_query_hilightadj(struct hiti_ctx *ctx)
{
	int ret;
	uint16_t len = sizeof(ctx->hilight_adj);

	ret = hiti_docmd_resp(ctx, CMD_ERDC_RHA, NULL, 0, ctx->hilight_adj, &len);
	if (ret)
		return ret;

	return CUPS_BACKEND_OK;
}

static int hiti_query_unk8010(struct hiti_ctx *ctx)
{
	int ret;
	uint16_t len = sizeof(ctx->unk_8010);

	ret = hiti_docmd_resp(ctx, CMD_ERDC_UNK0, NULL, 0, ctx->unk_8010, &len);
	if (ret)
		return ret;

	return CUPS_BACKEND_OK;
}

static int hiti_query_unk800c(struct hiti_ctx *ctx)
{
	int ret;
	uint16_t len = sizeof(ctx->unk_800c);

	ret = hiti_docmd_resp(ctx, CMD_ERDC_UNKC, NULL, 0, ctx->unk_800c, &len);
	if (ret)
		return ret;

	return CUPS_BACKEND_OK;
}

static int hiti_query_calibration(struct hiti_ctx *ctx)
{
	int ret;
	uint16_t len = sizeof(ctx->calibration);

	ret = hiti_docmd_resp(ctx, CMD_ERDC_RCC, NULL, 0, (uint8_t*)&ctx->calibration, &len);
	if (ret)
		return ret;

	return CUPS_BACKEND_OK;
}

static int hiti_query_led_calibration(struct hiti_ctx *ctx)
{
	int ret;
	uint16_t len;

	if (ctx->conn->type == P_HITI_461) { // XXx P320/P310
		len = sizeof(ctx->led_calibration) - 4;
		ret = hiti_docmd_resp(ctx, CMD_ERDC_RLC, NULL, 0, (uint8_t*)(&ctx->led_calibration)+4, &len);
	} else {
		len = sizeof(ctx->led_calibration);
		ret = hiti_docmd_resp(ctx, CMD_ERDC_RLC, NULL, 0, (uint8_t*)&ctx->led_calibration, &len);
	}
	if (ret)
		return ret;

	return CUPS_BACKEND_OK;
}

static int hiti_query_ribbonvendor(struct hiti_ctx *ctx)
{
	int ret;
	uint16_t len = 2;

	ret = hiti_docmd_resp(ctx, CMD_ERDC_RRVC, NULL, 0, (uint8_t*) &ctx->ribbonvendor, &len);
	if (ret)
		return ret;

	ctx->ribbonvendor = be16_to_cpu(ctx->ribbonvendor);

	return CUPS_BACKEND_OK;
}

static int hiti_query_tphv(struct hiti_ctx *ctx)
{
	int ret;
	uint16_t len = 2;

	ret = hiti_docmd_resp(ctx, CMD_ERDC_RTLV, NULL, 0, (uint8_t*) &ctx->rtlv, &len);
	if (ret)
		return ret;

	return CUPS_BACKEND_OK;
}

static int hiti_query_supplies(struct hiti_ctx *ctx)
{
	int ret;
	uint16_t len = sizeof(ctx->ribbon);
	uint8_t arg = 0;

	ret = hiti_docmd_resp(ctx, CMD_RDS_RSUS, &arg, sizeof(arg), (uint8_t*)&ctx->ribbon, &len);
	if (ret)
		return ret;

	len = sizeof(ctx->paper);
	ret = hiti_docmd_resp(ctx, CMD_RDS_RIS, NULL, 0, (uint8_t*)&ctx->paper, &len);
	if (ret)
		return ret;

	return CUPS_BACKEND_OK;
}

static int hiti_query_statistics(struct hiti_ctx *ctx)
{
	int ret;
	uint16_t len = 30;
	uint8_t buf[256];
	int i;

	ret = hiti_docmd_resp(ctx, CMD_RDS_RPS, NULL, 0, buf, &len);
	if (ret)
		return ret;

	for (i = 0 ; i < buf[0] && i*5+1 < len ; i+= 5) {
		/* uint8_t type
		   uint32_t val
		*/
		if (buf[1 + i*5] == 0x03) { // Remaining prints
			memcpy(&ctx->media_remain, &buf[1 + i*5 + 1], sizeof(ctx->media_remain));
			ctx->media_remain = be32_to_cpu(ctx->media_remain);
		}
	}

	return CUPS_BACKEND_OK;
}

static int hiti_doreset(struct hiti_ctx *ctx, uint8_t type)
{
	int ret;
	uint8_t buf[6];
	uint16_t len = 6;

	ret = hiti_docmd_resp(ctx, CMD_PCC_RP, &type, sizeof(type), buf, &len);
	if (ret)
		return ret;

	sleep(5);

	return CUPS_BACKEND_OK;
}

static int hiti_query_matrix(struct hiti_ctx *ctx)
{
	int ret;
	int i;
	uint16_t len = 1;

	for (i = 0 ; i < 512 ; i++) {
		uint16_t offset = cpu_to_be16(i);

		ret = hiti_docmd_resp(ctx, CMD_EFM_RD, (uint8_t*)&offset, sizeof(offset), &ctx->matrix[i], &len);
		if (ret)
			return ret;
	}

	return CUPS_BACKEND_OK;
}

static int hiti_query_matrix_51x(struct hiti_ctx *ctx)
{
	int ret;
	int i;
	uint16_t len = 1;

	uint8_t lun = 0;
	ret = hiti_docmd(ctx, CMD_PCC_STP, (uint8_t*)&lun, sizeof(lun), &len);
	if (ret)
		return ret;

	for (i = 0 ; i < 512 ; i++) {
		uint8_t offset = i % 256;
		if (offset == 0) {
			lun = i / 256;
			ret = hiti_docmd(ctx, CMD_PCC_STP, (uint8_t*)&lun, sizeof(lun), &len);
			if (ret)
				return ret;
		}
		ret = hiti_docmd_resp(ctx, CMD_EFM_RNV, (uint8_t*)&offset, sizeof(offset), &ctx->matrix[i], &len);
		if (ret)
			return ret;
	}

	lun = 0;
	ret = hiti_docmd(ctx, CMD_PCC_STP, (uint8_t*)&lun, sizeof(lun), &len);
	if (ret)
		return ret;


	return CUPS_BACKEND_OK;
}

static int hiti_query_counter(struct hiti_ctx *ctx, uint8_t arg, uint32_t *resp, int num)
{
	int ret;
	uint16_t len = sizeof(*resp) * num;

	ret = hiti_docmd_resp(ctx, CMD_ERDC_RPC, &arg, sizeof(arg),
			      (uint8_t*) resp, &len);
	if (ret)
		return ret;

	*resp = be32_to_cpu(*resp);

	return CUPS_BACKEND_OK;
}

static int hiti_query_serno(struct dyesub_connection *conn, char *buf, int buf_len)
{
	int ret;
	uint16_t rsplen = 18;
	uint8_t rspbuf[18];

	struct hiti_ctx ctx = {
		.conn = conn
	};

	uint8_t arg = sizeof(rspbuf);
	ret = hiti_docmd_resp(&ctx, CMD_ERDC_RSN, &arg, sizeof(arg), rspbuf, &rsplen);
	if (ret)
		return ret;

	/* Copy over serial number */
	strncpy(buf, (char*)rspbuf, buf_len);

	return CUPS_BACKEND_OK;
}

static int hiti_query_markers(void *vctx, struct marker **markers, int *count)
{
	struct hiti_ctx *ctx = vctx;
	int ret;

	if (markers)
		*markers = &ctx->marker;
	if (count)
		*count = 1;

	ret = hiti_query_statistics(ctx);
	if (ret)
		return ret;

	if (ctx->sheet) {
		ctx->marker.levelnow = CUPS_MARKER_UNKNOWN_OK;
		// XXX alter this, check for errors and return 0 instead.
	} else {
		ctx->marker.levelnow = ctx->media_remain;
	}

	return CUPS_BACKEND_OK;
}

static int hiti_query_stats(void *vctx, struct printerstats *stats)
{
	struct hiti_ctx *ctx = vctx;
	uint8_t sts[3];
	uint32_t err = 0;
	uint32_t tmp[2] = {0, 0}; /* Second only used for P51x */

	/* Update marker info */
	if (hiti_query_markers(ctx, NULL, NULL))
		return CUPS_BACKEND_FAILED;
	if (hiti_query_status(ctx, sts, &err))
		return CUPS_BACKEND_FAILED;

	stats->mfg = "HiTi";
	stats->model = ctx->id;
	stats->serial = ctx->serno;
	stats->fwver = ctx->version;

	stats->decks = 1;
	stats->mediatype[0] = ctx->marker.name;
	stats->levelmax[0] = ctx->marker.levelmax;
	stats->levelnow[0] = ctx->marker.levelnow;
	stats->name[0] = "Roll";
	stats->cnt_life[0] = 0;

	if (hiti_query_counter(ctx, 1, tmp, ctx->erdc_rpc_len))
		return CUPS_BACKEND_FAILED;

	stats->cnt_life[0] += tmp[0];

	if (err)
		stats->status[0] = strdup(hiti_errors(err));
	else
		stats->status[0] = strdup(hiti_status(sts));

	return CUPS_BACKEND_OK;
}

static void *hiti_find_heattable_v2_entry(const struct hiti_printjob *job, int id, size_t *len)
{
	int i;
	for (i = 0; i < job->num_heattable_entries ; i++) {
		if (job->heattable_v2[i].type == id) {
			*len = job->heattable_v2[i].len;
			return job->heattable_v2[i].data;
		}
	}
	*len = 0;
	return NULL;
}

static int hiti_parse_heattable_v2(struct hiti_printjob *job) {
	int i;
	struct hiti_heattable_hdr_v2 *hdr;
	uint32_t base = 0;
	uint8_t match = 0x00;  /* Or 0x20 when there's more than one? */

restart:
	hdr = (struct hiti_heattable_hdr_v2 *) job->heattable_buf;
	job->heattable_v2 = malloc(hdr->num_headers * sizeof(struct hiti_heattable_v2));
	if (!job->heattable_buf) {
		ERROR("Memory allocation failed!\n");
		return CUPS_BACKEND_FAILED;
	}

	job->num_heattable_entries = hdr->num_headers;

	for (i = 0 ; i < hdr->num_headers ; i++) {
		job->heattable_v2[i].type = hdr->entries[i].type;
		job->heattable_v2[i].data = job->heattable_buf + base + le32_to_cpu(hdr->entries[i].offset);
		job->heattable_v2[i].len = le32_to_cpu(hdr->entries[i].len) * 2;

		if (dyesub_debug)
		    DEBUG("Heattable entry: %02x len %d @ %d\n",
			  job->heattable_v2[i].type,
			  job->heattable_v2[i].len,
			  le32_to_cpu(hdr->entries[i].offset));

		if (hdr->entries[i].type == match) {
			if (dyesub_debug)
				DEBUG("Found embedded table type %0x02\n", match);
			base = le32_to_cpu(hdr->entries[i].offset);
			free(job->heattable_v2);
			goto restart;
		}
	}

	return CUPS_BACKEND_OK;
};

#define APPEND_ENTRY(__type)   do {	\
		data = hiti_find_heattable_v2_entry(job, __type, &len); \
		if (data) {						\
			table->entries[table->num_headers++].type = __type; \
		}							\
	} while(0)

#define APPEND_ENTRY_FAIL(__type)   do {	\
		data = hiti_find_heattable_v2_entry(job, __type, &len); \
		if (data) {						\
			table->entries[table->num_headers++].type = __type; \
		} else {						\
			ERROR("Missing required HEATv2 entry %02x\n", __type); \
			goto fail;					\
		}							\
	} while(0)

#define APPEND_ENTRY_FAIL2(__type, __type2)   do {			\
		data = hiti_find_heattable_v2_entry(job, __type, &len);	\
		if (data) {						\
			table->entries[table->num_headers++].type = __type; \
		} else {						\
			data = hiti_find_heattable_v2_entry(job, __type2, &len); \
			if (data) {					\
				table->entries[table->num_headers++].type = __type2; \
			} else {					\
				ERROR("Missing required HEATv2 entry %02x/%02x\n", __type,__type2); \
				goto fail;				\
			}						\
		}							\
	} while(0)

#define APPEND_ENTRY_FALLBACK2(__type, __type2, __var2)  do { \
	data = hiti_find_heattable_v2_entry(job, __type, &len); \
	if (data) {						\
		table->entries[table->num_headers++].type = __type;			\
	} else if (!__var2) {						\
		data = hiti_find_heattable_v2_entry(job, __type2, &len);	\
		if (data) {						\
			table->entries[table->num_headers++].type = __type2;		\
			__var2 = 1;					\
		}							\
	}								\
    } while(0)

#define APPEND_ENTRY_FALLBACK3(__type, __type2, __var2,  __type3, __var3)  do { \
	data = hiti_find_heattable_v2_entry(job, __type, &len); \
	if (data) {						\
		table->entries[table->num_headers++].type = __type;			\
	} else if (!__var2) {						\
		data = hiti_find_heattable_v2_entry(job, __type2, &len);	\
		if (data) {						\
			table->entries[table->num_headers++].type = __type2;		\
			__var2 = 1;					\
		} else if (!__var3) {					\
			data = hiti_find_heattable_v2_entry(job, __type3, &len); \
			if (data) {					\
				table->entries[table->num_headers++].type = __type3;	\
				__var3 = 1;				\
			}						\
		}							\
	}								\
    } while(0)

static int hiti_construct_heattable_v2(const struct hiti_printjob *job, int planes, int colormode, struct hiti_heattable_hdr_v2 *table)
{
    size_t len;
    void *data;

    int hac_ymc = 0, hac_all = 0;
    int ls_ymc = 0, ls_all = 0;
    int gl_ymc = 0, gl_all = 0;
    int en_ymc = 0, en_all = 0;

    table->num_headers = 0;

    /* Version is required */
    APPEND_ENTRY_FAIL(HEATTABLE_V2_ID_VER_MAJOR);
    APPEND_ENTRY_FAIL(HEATTABLE_V2_ID_VER_MINOR);

    if (planes & HT_PLANE_Y) {
	    APPEND_ENTRY_FAIL(HEATTABLE_V2_ID_HT_D_Y);
	    APPEND_ENTRY_FALLBACK3(HEATTABLE_V2_ID_HAC_Y, HEATTABLE_V2_ID_HAC_YMC, hac_ymc, HEATTABLE_V2_ID_HAC_ALL, hac_all);
	    APPEND_ENTRY_FALLBACK3(HEATTABLE_V2_ID_LS_Y, HEATTABLE_V2_ID_LS_YMC, ls_ymc, HEATTABLE_V2_ID_LS_ALL, ls_all);
	    APPEND_ENTRY_FALLBACK3(HEATTABLE_V2_ID_GL_Y, HEATTABLE_V2_ID_GL_YMC, gl_ymc, HEATTABLE_V2_ID_GL_ALL, gl_all);
	    APPEND_ENTRY_FALLBACK3(HEATTABLE_V2_ID_EN_Y, HEATTABLE_V2_ID_EN_YMC, en_ymc, HEATTABLE_V2_ID_EN_ALL, en_all);
    }
    if (planes & HT_PLANE_M) {
	    APPEND_ENTRY_FAIL(HEATTABLE_V2_ID_HT_D_M);
	    APPEND_ENTRY_FALLBACK3(HEATTABLE_V2_ID_HAC_M, HEATTABLE_V2_ID_HAC_YMC, hac_ymc, HEATTABLE_V2_ID_HAC_ALL, hac_all);
	    APPEND_ENTRY_FALLBACK3(HEATTABLE_V2_ID_LS_M, HEATTABLE_V2_ID_LS_YMC, ls_ymc, HEATTABLE_V2_ID_LS_ALL, ls_all);
	    APPEND_ENTRY_FALLBACK3(HEATTABLE_V2_ID_GL_M, HEATTABLE_V2_ID_GL_YMC, gl_ymc, HEATTABLE_V2_ID_GL_ALL, gl_all);
	    APPEND_ENTRY_FALLBACK3(HEATTABLE_V2_ID_EN_M, HEATTABLE_V2_ID_EN_YMC, en_ymc, HEATTABLE_V2_ID_EN_ALL, en_all);
    }
    if (planes & HT_PLANE_C) {
	    APPEND_ENTRY_FAIL(HEATTABLE_V2_ID_HT_D_C);
	    APPEND_ENTRY_FALLBACK3(HEATTABLE_V2_ID_HAC_C, HEATTABLE_V2_ID_HAC_YMC, hac_ymc, HEATTABLE_V2_ID_HAC_ALL, hac_all);
	    APPEND_ENTRY_FALLBACK3(HEATTABLE_V2_ID_LS_C, HEATTABLE_V2_ID_LS_YMC, ls_ymc, HEATTABLE_V2_ID_LS_ALL, ls_all);
	    APPEND_ENTRY_FALLBACK3(HEATTABLE_V2_ID_GL_C, HEATTABLE_V2_ID_GL_YMC, gl_ymc, HEATTABLE_V2_ID_GL_ALL, gl_all);
	    APPEND_ENTRY_FALLBACK3(HEATTABLE_V2_ID_EN_C, HEATTABLE_V2_ID_EN_YMC, en_ymc, HEATTABLE_V2_ID_EN_ALL, en_all);
    }
    if (planes & HT_PLANE_K) {
	    APPEND_ENTRY_FAIL(HEATTABLE_V2_ID_HT_D_K);
	    APPEND_ENTRY_FALLBACK3(HEATTABLE_V2_ID_HAC_DK, HEATTABLE_V2_ID_HAC_YMC, hac_ymc, HEATTABLE_V2_ID_HAC_ALL, hac_all);
	    APPEND_ENTRY_FALLBACK3(HEATTABLE_V2_ID_LS_DK, HEATTABLE_V2_ID_LS_YMC, ls_ymc, HEATTABLE_V2_ID_LS_ALL, ls_all);
	    APPEND_ENTRY_FALLBACK3(HEATTABLE_V2_ID_GL_DK, HEATTABLE_V2_ID_GL_YMC, gl_ymc, HEATTABLE_V2_ID_GL_ALL, gl_all);
	    APPEND_ENTRY_FALLBACK3(HEATTABLE_V2_ID_EN_DK, HEATTABLE_V2_ID_EN_YMC, en_ymc, HEATTABLE_V2_ID_EN_ALL, en_all);
    }

    if (planes & HT_PLANE_RK) {
	    APPEND_ENTRY_FAIL(HEATTABLE_V2_ID_HT_R_RK);
	    APPEND_ENTRY_FALLBACK3(HEATTABLE_V2_ID_HAC_RK, HEATTABLE_V2_ID_HAC_YMC, hac_ymc, HEATTABLE_V2_ID_HAC_ALL, hac_all);
	    APPEND_ENTRY_FALLBACK3(HEATTABLE_V2_ID_LS_RK, HEATTABLE_V2_ID_LS_YMC, ls_ymc, HEATTABLE_V2_ID_LS_ALL, ls_all);
	    APPEND_ENTRY_FALLBACK3(HEATTABLE_V2_ID_GL_RK, HEATTABLE_V2_ID_GL_YMC, gl_ymc, HEATTABLE_V2_ID_GL_ALL, gl_all);
	    APPEND_ENTRY_FALLBACK3(HEATTABLE_V2_ID_EN_RK, HEATTABLE_V2_ID_EN_YMC, en_ymc, HEATTABLE_V2_ID_EN_ALL, en_all);
    } else if (planes & HT_PLANE_R) {
	    APPEND_ENTRY_FAIL(HEATTABLE_V2_ID_HT_R_R);
	    APPEND_ENTRY_FALLBACK3(HEATTABLE_V2_ID_HAC_R, HEATTABLE_V2_ID_HAC_YMC, hac_ymc, HEATTABLE_V2_ID_HAC_ALL, hac_all);
	    APPEND_ENTRY_FALLBACK3(HEATTABLE_V2_ID_LS_R, HEATTABLE_V2_ID_LS_YMC, ls_ymc, HEATTABLE_V2_ID_LS_ALL, ls_all);
	    APPEND_ENTRY_FALLBACK3(HEATTABLE_V2_ID_GL_R, HEATTABLE_V2_ID_GL_YMC, gl_ymc, HEATTABLE_V2_ID_GL_ALL, gl_all);
	    APPEND_ENTRY_FALLBACK3(HEATTABLE_V2_ID_EN_R, HEATTABLE_V2_ID_EN_YMC, en_ymc, HEATTABLE_V2_ID_EN_ALL, en_all);
    }

    if (planes & HT_PLANE_OG) {
	    APPEND_ENTRY_FAIL2(HEATTABLE_V2_ID_HT_D_O, HEATTABLE_V2_ID_HT_D_KO);
	    APPEND_ENTRY_FALLBACK2(HEATTABLE_V2_ID_HAC_O, HEATTABLE_V2_ID_HAC_ALL, hac_all);
	    APPEND_ENTRY_FALLBACK2(HEATTABLE_V2_ID_LS_O, HEATTABLE_V2_ID_LS_ALL, ls_all);
	    APPEND_ENTRY_FALLBACK2(HEATTABLE_V2_ID_GL_O, HEATTABLE_V2_ID_GL_ALL, gl_all);
	    APPEND_ENTRY_FALLBACK2(HEATTABLE_V2_ID_EN_O, HEATTABLE_V2_ID_EN_ALL, en_all);
    } else if (planes & HT_PLANE_OK) {
	    APPEND_ENTRY_FAIL2(HEATTABLE_V2_ID_HT_D_KO, HEATTABLE_V2_ID_HT_D_O);
	    APPEND_ENTRY_FALLBACK2(HEATTABLE_V2_ID_HAC_O, HEATTABLE_V2_ID_HAC_ALL, hac_all);
	    APPEND_ENTRY_FALLBACK2(HEATTABLE_V2_ID_LS_O, HEATTABLE_V2_ID_LS_ALL, ls_all);
	    APPEND_ENTRY_FALLBACK2(HEATTABLE_V2_ID_GL_O, HEATTABLE_V2_ID_GL_ALL, gl_all);
	    APPEND_ENTRY_FALLBACK2(HEATTABLE_V2_ID_EN_O, HEATTABLE_V2_ID_EN_ALL, en_all);
    } else if (planes & HT_PLANE_OM) {
	    APPEND_ENTRY_FAIL(HEATTABLE_V2_ID_HT_D_MO);
	    APPEND_ENTRY_FALLBACK2(HEATTABLE_V2_ID_HAC_MO, HEATTABLE_V2_ID_HAC_ALL, hac_all);
	    APPEND_ENTRY_FALLBACK2(HEATTABLE_V2_ID_LS_MO, HEATTABLE_V2_ID_LS_ALL, ls_all);
	    APPEND_ENTRY_FALLBACK2(HEATTABLE_V2_ID_GL_MO, HEATTABLE_V2_ID_GL_ALL, gl_all);
	    APPEND_ENTRY_FALLBACK2(HEATTABLE_V2_ID_EN_MO, HEATTABLE_V2_ID_EN_ALL, en_all);
    }
    if (planes & HT_PLANE_FO) {
	    APPEND_ENTRY_FAIL(HEATTABLE_V2_ID_HT_R_FO);
	    APPEND_ENTRY_FALLBACK2(HEATTABLE_V2_ID_HAC_FO, HEATTABLE_V2_ID_HAC_ALL, hac_all);
	    APPEND_ENTRY_FALLBACK2(HEATTABLE_V2_ID_LS_FO, HEATTABLE_V2_ID_LS_ALL, ls_all);
	    APPEND_ENTRY_FALLBACK2(HEATTABLE_V2_ID_GL_FO, HEATTABLE_V2_ID_GL_ALL, gl_all);
	    APPEND_ENTRY_FALLBACK2(HEATTABLE_V2_ID_EN_FO, HEATTABLE_V2_ID_EN_ALL, en_all);
    }
    if (planes & HT_PLANE_L) {
	    APPEND_ENTRY_FAIL(HEATTABLE_V2_ID_HT_R_L);
    }

    if (planes & HT_CVD) {
	    APPEND_ENTRY_FAIL(HEATTABLE_V2_ID_CVD);
    }

    if (colormode == HT_COLORMODE_CLASSIC) {
	    APPEND_ENTRY_FAIL(HEATTABLE_V2_ID_CT_CLASSIC);
    } else if (colormode == HT_COLORMODE_IDPASS_VIVID) {
	    APPEND_ENTRY_FAIL(HEATTABLE_V2_ID_CT_IDPASS);
    } else {
	    APPEND_ENTRY_FAIL(HEATTABLE_V2_ID_CT_INVERT);
    }

    APPEND_ENTRY(HEATTABLE_V2_ID_TC_COMPENSATE);

    return 0;

fail:
    table->num_headers = 0;
    return 1;
}

static void hiti_teardown(void *vctx) {
	struct hiti_ctx *ctx = vctx;
	free(ctx);
}

#define JOB_EQUIV(__x)  if (job1->__x != job2->__x) goto done

static void *hiti_combine_jobs(const void *vjob1,
			       const void *vjob2)
{
	const struct hiti_printjob *job1 = vjob1;
	const struct hiti_printjob *job2 = vjob2;
	struct hiti_printjob *newjob = NULL;
	uint16_t newrows;
	uint16_t newpad;
	uint16_t newmode;

	if (!job1 || !job2)
		goto done;

	/* Make sure the two jobs can be combined */
	JOB_EQUIV(colormode);
	JOB_EQUIV(hdr.cols);
	JOB_EQUIV(hdr.rows);
	JOB_EQUIV(hdr.quality);
	JOB_EQUIV(hdr.code);
	JOB_EQUIV(hdr.overcoat);
	JOB_EQUIV(hdr.payload_flag);
	JOB_EQUIV(hdr.payload_len);

	if (job1->hdr.cols == 1548 && job1->hdr.rows == 1072) {
		/* 2x 3.5x5" -> 1x 5x7" cut */
		newrows = 2152;
		newpad = 8;
		newmode = PRINT_TYPE_5x7_2UP;
	} else if (job1->hdr.cols == 1844 && job1->hdr.rows == 1240) {
		/* 2x 4x6 -> 1x 8x6" cut */
		newrows = 2492;
		newpad = 12;
		newmode = PRINT_TYPE_6x9_2UP;
	} else {
		goto done;
	}

	newpad *= job1->hdr.cols;

	/* At this point it's kosher to proceed */

	DEBUG("Combining jobs to save media\n");

	/* Allocate new job, duplicate job1's parameters */
        newjob = malloc(sizeof(*newjob));
        if (!newjob) {
                ERROR("Memory allocation failure!\n");
                goto done;
        }
        memcpy(newjob, job1, sizeof(*newjob));

	newjob->heattable_buf = malloc(job1->heattable_len);
        if (!newjob->heattable_buf) {
                ERROR("Memory allocation failure!\n");
		hiti_cleanup_job(newjob);
		newjob = NULL;
                goto done;
        }
	memcpy(newjob->heattable_buf, job1->heattable_buf, job1->heattable_len);

	/* Set up new job, where it differs */
	newjob->hdr.rows = newrows;
	newjob->hdr.code = newmode;
	newjob->datalen = 0;
	newjob->databuf = malloc(newjob->hdr.rows * newjob->hdr.cols * 3);

	if (!newjob->databuf) {
		ERROR("Memory allocation failed!\n");
		hiti_cleanup_job(newjob);
		newjob = NULL;
                goto done;
	}

	uint32_t planelen = job1->hdr.rows * job1->hdr.cols;

	/* Copy over Y planes */
	memcpy(newjob->databuf + newjob->datalen, job1->databuf, planelen);
	newjob->datalen += planelen;
	memset(newjob->databuf + newjob->datalen, 0, newpad);
	newjob->datalen += newpad;;
	memcpy(newjob->databuf + newjob->datalen, job2->databuf, planelen);
	newjob->datalen += planelen;

	/* Copy over M planes */
	memcpy(newjob->databuf + newjob->datalen, job1->databuf + planelen, planelen);
	newjob->datalen += planelen;
	memset(newjob->databuf + newjob->datalen, 0, newpad);
	newjob->datalen += newpad;;
	memcpy(newjob->databuf + newjob->datalen, job2->databuf + planelen, planelen);
	newjob->datalen += planelen;

	/* Copy over C planes */
	memcpy(newjob->databuf + newjob->datalen, job1->databuf + planelen + planelen, planelen);
	newjob->datalen += planelen;
	memset(newjob->databuf + newjob->datalen, 0, newpad);
	newjob->datalen += newpad;;
	memcpy(newjob->databuf + newjob->datalen, job2->databuf + planelen + planelen, planelen);
	newjob->datalen += planelen;

	/* All done */

done:
	return newjob;
}

#undef JOB_EQUIV

static const char *hiti_prefixes[] = {
	"hiti", // Family name
	"hiti-p52x", /* Just in case */
	NULL
};

static const struct device_id hiti_devices[] = {
	{ 0x0d16, 0x0309, P_HITI_CS2XX, NULL, "hiti-cs200e"},
	{ 0x0d16, 0x030a, P_HITI_CS2XX, NULL, "hiti-cs220e"},
	{ 0x0d16, 0x030b, P_HITI_CS2XX, NULL, "hiti-cs230e"},
	{ 0x0d16, 0x030c, P_HITI_CS2XX, NULL, "hiti-cs250e"},
	{ 0x0d16, 0x030d, P_HITI_CS2XX, NULL, "hiti-cs290e"},
	{ 0x0d16, 0x0007, P_HITI_51X, NULL, "hiti-p510k"},
	{ 0x0d16, 0x000b, P_HITI_51X, NULL, "hiti-p510l"},
	{ 0x0d16, 0x000d, P_HITI_51X, NULL, "hiti-p518a"},
	{ 0x0d16, 0x010e, P_HITI_51X, NULL, "hiti-p510s"},
	{ 0x0d16, 0x0111, P_HITI_51X, NULL, "hiti-p510si"},
	{ 0x0d16, 0x0112, P_HITI_51X, NULL, "hiti-p518s"},
	{ 0x0d16, 0x0502, P_HITI_520, NULL, "hiti-p520l"},
	{ 0x0d16, 0x0503, P_HITI_310, NULL, "hiti-p310l"},
	{ 0x0d16, 0x050a, P_HITI_310, NULL, "hiti-p310w"},
	{ 0x0d16, 0x050c, P_HITI_320, NULL, "hiti-p320w"},
	{ 0x0d16, 0x0509, P_HITI_461, NULL, "hiti-p461"},
	{ 0x0d16, 0x050e, P_HITI_525, NULL, "hiti-p525l"},
	{ 0x0d16, 0x000f, P_HITI_530, NULL, "hiti-p530d"},
	{ 0x0d16, 0x0009, P_HITI_720, NULL, "hiti-p720l"},
	{ 0x0d16, 0x000a, P_HITI_720, NULL, "hiti-p728l"},
	{ 0x0d16, 0x0501, P_HITI_750, NULL, "hiti-p750l"},
	{ 0x0d16, 0xc000, P_HITI_51X, NULL, "yashica-yp120"},
	{ 0x0d16, 0xd000, P_HITI_51X, NULL, "touchtunes-p510tt"},
	{ 0, 0, 0, NULL, NULL}
/*
#define USB_PID_HITI_P110S   0x0110
#define USB_PID_HITI_X610    0x0800
*/

};

const struct dyesub_backend hiti_backend = {
	.name = "HiTi Photo Printers",
	.version = "0.75",
	.uri_prefixes = hiti_prefixes,
	.cmdline_usage = hiti_cmdline,
	.cmdline_arg = hiti_cmdline_arg,
	.init = hiti_init,
	.teardown = hiti_teardown,
	.attach = hiti_attach,
	.cleanup_job = hiti_cleanup_job,
	.read_parse = hiti_read_parse,
	.main_loop = hiti_main_loop,
	.query_serno = hiti_query_serno,
	.query_markers = hiti_query_markers,
	.query_stats = hiti_query_stats,
	.combine_jobs = hiti_combine_jobs,
	.devices = hiti_devices,
};

/* TODO:

   - Figure out 5x6, 6x5, and 6x6 prints (need 6x8 or 6x9 media!)
   - Confirm 6x2" print dimensions (windows?)
   - Job control (QJC, RSJ) -- and canceling?
   - Set highlight adjustment & H/V alignment from cmdline
      * Figure out how to set H/V alignment!
   - Figure out if driver needs to consume highlight adjustment
      * Feed into gamma correction?
      * Feed [un]modified into some printer cmd?
   - Figure out Windows spool format (probably never)
   - GP Spool parsing improvements
      * Add additional 'reserved' fields for future use?
   - Further performance optimizations in color conversion code
      * Rework to take advantage of auto-vectorization?
      * Pre-compute then cache entire map on disk?
      * Use external "Cube LUT" implementation?
   - Commands UNK_8008, UNK_8010, UNK_800C UNK_8011, EST_SEHT, CMD_EDM_*
   - Test with P720, P750
   - Incorporate changes for CS-series card printers
   - More "Matrix table" decoding work
   - More work on other "sheet type" models
     - P110S   ("P4x6" 1248x1836)
     - P310/P320 series ("P4x6" 1280*1818)
     - Others?
   - More work on remaining "roll type" models
     - P530D
     - X610
     - Others?
   - More work on P461 Prinhome
     - Quality mode
     - Windows supports media types 2 & 3 too, manually specified
   - Figure out when to use Heat tables with 'p', 'm', and 'r' quality tags
   - Figure out what the H, M, and T quality codes mean on correction files
   - Figure out how to send over the "raw" matte overcoat data.  (Partial matte is now possible?)

*/
