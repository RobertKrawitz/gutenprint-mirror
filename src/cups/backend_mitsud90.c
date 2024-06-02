/*
 *   Mitsubishi CP-D90DW Photo Printer CUPS backend
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

#define BACKEND mitsud90_backend

#include "backend_common.h"
#include "backend_mitsu.h"

/* CPM1 stuff */
#define CPM1_LAMINATE_STRIDE 1852

#define CPM1_LAMINATE_FILE "M1_MAT02.raw"
#define CPM1_CPC_FNAME "CPM1_N1.csv"
#define CPM1_CPC_G1_FNAME "CPM1_G1.csv"
#define CPM1_CPC_G5_FNAME "CPM1_G5.csv"
#define CPM1_CPC_G5_VIVID_FNAME "CPM1_G5_vivid.csv"
#define CPM1_LUT_FNAME "CPM1_NL.lut"

/* D90 LUT for when we are doing panoramas */
#define CPD90_LUT_FNAME "CPD90L01.lut"
#define CPD90_CPC_FNAME "CPD90_N1.csv"
#define CPD90_CPC_3_1_FNAME "CP90_3_1.csv"
#define CPD90_CPC_3_2_FNAME "CP90_3_2.csv"

/* ASK500 stuff -- Note the lack of LUT or G5_vivid! */
#define ASK5_LAMINATE_FILE "ASK5_MAT.raw"
#define ASK5_CPC_FNAME "ASK5_N1.csv"
#define ASK5_CPC_G1_FNAME "ASK5_G1.csv"
#define ASK5_CPC_G5_FNAME "ASK5_G5.csv"

/* Printer data structures */
#define COM_STATUS_TYPE_MODEL   0x01 // 10, null-terminated ASCII. 'CPD90D'
#define W5K_STATUS_TYPE_MODEL   0x01 // 5, ASCII, non-terminated: 'W5000'
#define COM_STATUS_TYPE_x02     0x02 // 1, 0x5f ?
#define CM1_STATUS_TYPE_ISERIAL 0x03 // 24, full iSerial string in UTF16(LE)
#define CM1_STATUS_TYPE_SERIAL  0x04 // 6, serial number only (ascii)
#define W5K_STATUS_TYPE_SERIAL  0x06 // 8, 36 30 31 34 30 35 43 20 (601405M )
#define W5K_STATUS_TYPE_x07     0x07 // 6, 30 38 46 35 46 37       (08F5F7)
#define W5K_STATUS_TYPE_x08     0x08 // 9, 00 00 00 00 00 00 00 00 00
#define W5K_STATUS_TYPE_x09     0x09 // 9, 00 00 00 00 00 00 00 00 00
#define CM1_STATUS_TYPE_FW_0a   0x0a // 8, 34 34 38 41 31 32 29 f4 (448A12)
#define COM_STATUS_TYPE_FW_LOADER 0x0b // 8, 34 31 34 42 31 31 a7 de (414D11)
#define COM_STATUS_TYPE_FW_MAIN  0x0c // 8, 34 31 35 41 38 31 86 bf (415A81)
#define COM_STATUS_TYPE_FW_FPGA  0x0d // 8, 34 31 36 41 35 31 dc 8a (416A51)
#define COM_STATUS_TYPE_FW_TBL   0x0e // 8, 34 31 37 45 31 31 e7 e6 (417E11)
#define COM_STATUS_TYPE_FW_TAG  0x0f // 8, 34 31 38 41 31 32 6c 64 (418A12)

#define W5K_STATUS_TYPE_FW_LUT  0x10 // 8, 33 37 30 41 32 34 91 22 (370A24)
#define COM_STATUS_TYPE_FW_SATIN 0x11 // 8, 34 32 31 51 31 31 74 f2 (421Q11)
#define W5K_STATUS_TYPE_FW_12   0x12 // 8, 30 30 30 30 30 30 30 30 (000000)
#define COM_STATUS_TYPE_FW_MECH 0x13 // 8, 34 31 39 45 31 31 15 bf (419E11) NOT W5K
#define W5K_STATUS_TYPE_x15     0x15 // 2, 00 00
#define COM_STATUS_TYPE_ERROR   0x16 // 11 (see below)
#define COM_STATUS_TYPE_MECHA   0x17 // 2  (see below)
#define W5K_STAUTS_TYPE_x1a     0x1a // 1,  00  (error flag?  0x10 seems to be a
#define COM_STATUS_TYPE_x1e     0x1e // 1, power state or time?  (x00)
#define COM_STATUS_TYPE_TEMP    0x1f // 1  (see below)

#define W5K_STATUS_TYPE_x20     0x20 // 1,  00
#define COM_STATUS_TYPE_x22     0x22 // 2,  all 0  (NOT W5K)
#define W5K_STATUS_TYPE_x23     0x23 // 16, all 0
#define W5K_STATUS_TYPE_x24     0x24 // 26, all 0
#define W5K_STATUS_TYPE_x25     0x25 // 16, all 0
#define COM_STATUS_TYPE_JOBID   0x28 // 2, _next_ Job ID.
#define COM_STATUS_TYPE_INKID   0x29 // 8,  e0 07 00 00 21 e6 b3 22 or e0 07 80 96 3f 28 12 2d or e0 07 00 00 2b e8 db bd
#define COM_STATUS_TYPE_MEDIA   0x2a // 10 (see below)
#define COM_STATUS_TYPE_x2b     0x2b // 2, all 0
#define W5K_STATUS_TYPE_INKID2  0x2b // 8, 0c 57 dc 00 [a1 18] 00 00
#define COM_STATUS_TYPE_x2c     0x2c // 2, 00 56 (D90) 00 23 (M1)
#define W5K_STATUS_TYPE_x2c     0x2c // 12,06 00 00 00 00 00 00 00 40 54 33 01
#define W5K_STATUS_TYPE_x2d     0x2d // 4, all 0

#define W5K_STATUS_TYPE_x30     0x30 // 2, all 0
#define W5K_STATUS_TYPE_x31     0x31 // 2, 00 44
#define W5K_STATUS_TYPE_x33     0x33 // 12, all 0
#define W5K_STATUS_TYPE_x34     0x34 // 2, 00 4f
#define W5K_STATUS_TYPE_x35     0x35 // 2, 02
#define W5K_STATUS_TYPE_x37     0x37 // 2, 02
#define W5K_STATUS_CNT_HEAD     0x3d // 4
#define W5K_STATUS_CNT_SERVICE  0x3e // 4
#define W5K_STATUS_CNT_PRINTED  0x3f // 4

#define W5K_STATUS_TYPE_x40     0x40 // 4, 00 00 3f b0 (seen it go up and down)
#define W5K_STATUS_TYPE_x45     0x45 // 4, 00 00 00 84 (counter?) <-- incremetns one per print
#define W5K_STATUS_TYPE_DENSITY 0x46 // 2
#define W5K_STATUS_TYPE_TPHR    0x4b // 2 // Thermal Print Head Resistance
#define W5K_STATUS_TYPE_TPHSN   0x4c // 8 // Thermal Print Head Serno (ASCII, non-terminated)

#define W5K_STATUS_TYPE_x52     0x52 // 1, 42
#define W5K_STATUS_CNT_CUTTER   0x5b

#define W5K_STATUS_CNT_SLITTER  0x60
#define COM_STATUS_TYPE_x65     0x65 // 50, see below (sensors?)
#define W5K_STATUS_TYPE_x65     0x65 // 54, see below
#define W5K_STATUS_TYPE_LENGTHS 0x6f // 16, BLANKLENA_s16, BLANKLENB_s16, 00 00, MARGINLEN s16, 00 00, CUTLEN_s16, 00 00 00 00

#define W5K_STATUS_TYPE_x70     0x70 // 16, 0a 07 ff 0b 00 00 00 00 00 00 00 00 00 00 00 00
#define W5K_STATUS_TYPE_MOTOR3  0x71 // 16, NN NN NN NN TT TT TT TT  00 00 00 00 00 00 00 00
#define W5K_STATUS_TYPE_MOTOR5  0x72 // 16, NN NN NN NN TT TT TT TT  00 00 00 00 00 00 00 00
#define W5K_STATUS_TYPE_MOTOR1  0x73 // 4, CC CC CC CC

#define D90_STATUS_TYPE_ISEREN  0x82 // 1,  80 (iserial disabled)
#define COM_STATUS_TYPE_x83     0x83 // 1,  00
#define W5K_STATUS_TYPE_x83     0x83 // 2, 00 80
#define D90_STATUS_TYPE_x84     0x84 // 1,  00
#define W5K_STATUS_TYPE_x84     0x84 // 2, 00 0a
#define D90_STATUS_TYPE_x85     0x85 // 2, 00 ?? BE, wait time? combined total of 5.
#define W5K_STATUS_TYPE_x85     0x85 // 2, 00 04
#define W5K_STATUS_TYPE_x86     0x86 // 2, 01 2c
#define W5K_STATUS_TYPE_DMAXY   0x87 // 1
#define W5K_STATUS_TYPE_DMINY   0x88 // 1
#define W5K_STATUS_TYPE_DMAXM   0x89 // 1
#define W5K_STATUS_TYPE_DMINM   0x8a // 1
#define W5K_STATUS_TYPE_DMAXC   0x8b // 1
#define W5K_STATUS_TYPE_DMINC   0x8c // 1
#define W5K_STATUS_TYPE_LUT     0x8d // 120, 53 4c 54 41 58 32 37 30 -- "SLTAX270" for older media type (vs "SLTA7180" for new media type), followed by 14 more entries for B1-7/C1-7
#define W5K_STATUS_TYPE_x8e     0x8e // 8, all ff

#define W5K_STATUS_TYPE_x97     0x97 // 2, 00 01

#define W5K_STATUS_TYPE_xc0     0xc0 // 1, 00
#define W5K_STATUS_TYPE_xc1     0xc1 // 1, 00
#define W5K_STATUS_TYPE_xc2     0xc2 // 1, 00
#define W5K_STATUS_TYPE_xc3     0xc3 // 1, 00
#define W5K_STATUS_TYPE_xc4     0xc4 // 1, 00
#define W5K_STATUS_TYPE_xc5     0xc5 // 1, 00
#define W5K_STATUS_TYPE_xc6     0xc6 // 1, 00
#define W5K_STATUS_TYPE_xc7     0xc7 // 1, 00
#define W5K_STATUS_TYPE_xc8     0xc8 // 1, 00
#define W5K_STATUS_TYPE_xc9     0xc9 // 1, 00
#define W5K_STATUS_TYPE_xca     0xca // 1, 00
#define W5K_STATUS_TYPE_xcb     0xcb // 1, 00
#define W5K_STATUS_TYPE_xcf     0xcf // 1, 00

struct mitsud90_fw_resp_single {
	uint8_t  version[6];
	uint16_t csum;
} __attribute__((packed));

struct mitsud90_media_resp {
	uint8_t  hdr[4];  /* e4 47 44 30 */
	struct {
		uint8_t  brand;
		uint8_t  type;
		uint8_t  unk_a[2];
		uint16_t capacity; /* BE */
		uint16_t remain;  /* BE */
		uint8_t  unk_b[2];
	} __attribute__((packed)) media; /* COM_STATUS_TYPE_MEDIA */
} __attribute__((packed));

struct mitsud90_status_resp {
	uint8_t  hdr[4];  /* e4 47 44 30 */
	/* COM_STATUS_TYPE_ERROR */
	uint8_t  code[2]; /* 00 is ok, nonzero is error */
	uint8_t  unk[9];
	/* COM_STATUS_TYPE_MECHA */
	uint8_t  mecha[2];
	/* COM_STATUS_TYPE_TEMP */
	uint8_t  temp;
} __attribute__((packed));

struct mitsud90_info_resp {
	uint8_t  hdr[4];  /* e4 47 44 30 */
	uint8_t  model[10];
	uint8_t  x02;
	struct mitsud90_fw_resp_single fw_vers[7];
	uint8_t  x1e;
	uint8_t  x22[2];
	uint16_t jobid;
	uint8_t  inkid[8];
	uint8_t  x2b[2];
	uint8_t  x2c[2];
	uint8_t  x65[50];
	uint8_t  iserial;
	uint8_t  x83;
	uint8_t  x84;
} __attribute__((packed));

struct mitsud90_fwver_resp {
	uint8_t  hdr[4];  /* e4 47 44 30 */
	struct mitsud90_fw_resp_single fw_ver;
} __attribute((packed));

struct mitsum1_info_resp {
	uint8_t  hdr[4];  /* e4 47 44 30 */
	uint8_t  model[10];
	uint8_t  x02;
	struct mitsud90_fw_resp_single fw_vers[8];
	uint8_t  x1e;
	uint8_t  x22[2];
	uint16_t jobid;
	uint8_t  inkid[8];
	uint8_t  x2b[2];
	uint8_t  x2c[2];
	uint8_t  x65[50];
	uint8_t  x83;
} __attribute__((packed));

struct mitsuw5k_info_resp {
	uint8_t  hdr[4];  /* e4 47 44 30 */
	uint8_t  model[5];
	uint8_t  x02;
	struct mitsud90_fw_resp_single fw_vers[7];
	uint8_t  x1a;
	uint8_t  x1e;
	uint8_t  inkid[8];
	uint8_t  x2b[8];
	uint8_t  x2c[12];
	uint32_t cnt_head;
	uint32_t cnt_service;
	uint32_t cnt_printed;
	uint32_t cnt_cutter;
	uint32_t cnt_slitter;
#if 0
	uint32_t x40;
	uint32_t x45;
	uint8_t  x65[54];
	uint8_t  x83[2];
	uint8_t  x84[2];
#endif
} __attribute__((packed));

#define D90_MECHA_STATUS_IDLE         0x00
#define W5K_MECHA_STATUS_PRINTING     0x20
#define D90_MECHA_STATUS_PRINTING     0x50
#define D90_MECHA_STATUS_INIT         0x80
#define D90_MECHA_STATUS_INIT_FEEDCUT 0x10

#define D90_MECHA_STATUS_PRINT_FEEDING 0x10  // feeding ?
#define D90_MECHA_STATUS_PRINT_x20     0x20  // ??
#define D90_MECHA_STATUS_PRINT_PRE_Y   0x21  // pre Y ?
#define D90_MECHA_STATUS_PRINT_Y       0x22  // Y ?
#define D90_MECHA_STATUS_PRINT_PRE_M   0x23  // pre M ?
#define D90_MECHA_STATUS_PRINT_M       0x24  // M ?
#define D90_MECHA_STATUS_PRINT_PRE_C   0x25  // pre C ? guess!
#define D90_MECHA_STATUS_PRINT_C       0x26  // C ?
#define D90_MECHA_STATUS_PRINT_PRE_OC  0x27  // pre OC ? guess!
#define D90_MECHA_STATUS_PRINT_OC      0x28  // O C?
#define D90_MECHA_STATUS_PRINT_x2f     0x2f  // ??
#define D90_MECHA_STATUS_PRINT_x38     0x38  // eject ?

#define D90_ERROR_STATUS_OK         0x00
#define D90_ERROR_STATUS_OK_WARMING 0x40
#define D90_ERROR_STATUS_OK_COOLING 0x80
#define D90_ERROR_STATUS_RIBBON     0x21
#define D90_ERROR_STATUS_PAPER      0x22
#define D90_ERROR_STATUS_PAP_RIB    0x23
#define D90_ERROR_STATUS_OPEN       0x29

struct mitsud90_job_query {
	uint8_t  hdr[4];  /* 1b 47 44 31 */
	uint16_t jobid;   /* BE */
} __attribute__((packed));

struct mitsud90_job_resp {
	uint8_t  hdr[4];  /* e4 47 44 31 */
	uint8_t  unk1;
	uint8_t  unk2;
	uint16_t unk3;
} __attribute__((packed));

struct mitsud90_job_hdr {
	uint8_t  hdr[6]; /* 1b 53 50 30 00 33 */
	uint16_t cols;   /* BE */
	uint16_t rows;   /* BE */
	uint8_t  waittime; /* 0-100 */
	uint8_t  unk[3]; /* 00 00 01 */ // XXX 00 01 might be the jobid?
	uint8_t  margincut; /* 1 for enabled, 0 for disabled */
	uint8_t  numcuts; /* # of cuts (0-3) but 0-8 legal */
/*@0x10*/
	struct {
		uint16_t position;  // BE, @ center
		uint8_t  margincut; /* 0 for double cut, 1 for single */
		uint8_t  zeropad;
	} cutlist[8] __attribute__((packed));  /* 3 is current legal max */
/*@x30*/uint8_t  overcoat;  /* 0 glossy, matte is 2 (D90) or 3 (M1) */
	uint8_t  quality;   /* 0 is automatic, 5 is "fast" on M1 */
	uint8_t  colorcorr; /* Always 1 on M1 */
	uint8_t  sharp_h;   /* Always 0 on M1 */
	uint8_t  sharp_v;   /* Always 0 on M1 */
	uint8_t  zero_b[5]; /* 0 on D90, on M1, zero_b[3] is the not-raw flag */
	struct {
/* @x3a */	uint8_t  on;      /* 0x01 when pano is on / always 0x02 on M1 / 0x03 on D90 panorama that needs backend processing */
		uint8_t  zero_a;
		uint8_t  total;   /* 2 or 3 */
		uint8_t  page;    /* 1, 2, 3 */
		uint16_t rows;    /* always 0x097c (BE), ie 2428 ie 8" print */
/* @x40 */	uint16_t rows2;   /* Always 0x30 less than pano_rows */
		uint16_t zero_b;  /* 0x0000 */
		uint16_t overlap; /* always 0x0258, ie 600 or 2 inches */
		uint8_t  unk[4];  /* 00 0c 00 06 */
	} pano __attribute__((packed));
	uint8_t zero_c[6];
/*@x50*/uint8_t unk_m1;   /* 00 on d90 & m1 Linux, 01 on m1 (windows) */
	uint8_t rgbrate;  /* M1 only, see below */
	uint8_t oprate;   /* M1 only, see below */
	uint8_t zero_fill[429];
} __attribute__((packed));

struct mitsud90_plane_hdr {
	uint8_t  hdr[6]; /* 1b 5a 54 01 00 09 */
	uint16_t origin_cols;  /* Leave at 0 */
	uint16_t origin_rows;  /* Leave at 0 */
	uint16_t cols;  /* BE */
	uint16_t rows;  /* BE */
	uint8_t  zero_a[6];
	uint16_t lamcols; /* BE (M1 only, OC=3) should be cols+origin_cols */
	uint16_t lamrows; /* BE (M1 only, OC=3) should be rows+origin_rows+12 */
	uint8_t  zero_b[8];
	uint8_t  unk_m1[8]; /* 07 e4 02 19 xx xx xx 00 always incrementing. timestamp? Only seen from win-generated jobs? */
	uint8_t  zero_fill[472];
} __attribute__((packed));

struct mitsud90_job_footer {
	uint8_t hdr[4]; /* 1b 42 51 31 */
	uint16_t seconds; /* BE, 0x0005 by default (windows), 0x00ff means don't wait */
} __attribute__((packed));

struct mitsud90_memcheck {
	uint8_t  hdr[6]; /* 1b 47 44 33 00 33 */
	uint16_t cols;   /* BE */
	uint16_t rows;   /* BE */
	uint8_t  waittime; /* 0-100 */
	uint8_t  unk[3]; /* 00 00 01  */
	uint8_t  zero_fill[498];
} __attribute__((packed));

struct mitsud90_memcheck_resp {
	uint8_t  hdr[4];   /* e4 47 44 43 */
	uint8_t  size_bad; /* 0x00 is ok */
	uint8_t  mem_bad;  /* 0x00 is ok */
} __attribute__((packed));

struct mitsuw5k_job_hdr {
	uint8_t hdr[4]; // 1b 53 50 30
	uint16_t cols;
	uint16_t rows;
	uint8_t  cuts;  // 0-2
	uint16_t cut1;
	uint16_t cut2;
	uint8_t single;  // 1:single page, 0:duplex
	uint8_t	finish;  // 0:gloss, 1:semi-gloss 2:matte
	uint8_t	finishback; //	0:gloss, 1:semi-gloss, 2:matte,	ff:none
	uint8_t	unk[2];  // XXX 01 00, 01 01, 01 02 seen on windows driver, latter two with duplex of some sort.  00 00 hardcoded with linux driver.
	uint8_t	colorconv;  //	1: off,	0: internal
	uint8_t  sharp_H;    // 0-8 or 0xff (ie printer)
	uint8_t  sharp_V;    // 0-8 or 0xff (ie printer)
/*@21*/	uint8_t  pad[512-21];
} __attribute__((packed));

struct mitsuw5k_plane_hdr {
	int8_t  hdr[4]; // 1b 5a 54 01
	uint8_t  zero[4];
	uint16_t cols;
	uint16_t rows;
	uint8_t  zero2;
	uint8_t  printout;  // 1 to start printing
	uint8_t  zero3;
/*@15*/	uint8_t  pad[512-15];
} __attribute__((packed));

struct mitsud90_generic_hdr { /* See GENERIC GET/SET below */
	uint8_t  hdr[3];   /* 1b 61 36 */
	uint8_t  getset;   /* 36 get, 30 set */
	uint8_t  type[2];  /* 41 be, 45 ba, etc */
	uint32_t len;      /* Payload len, BE */
	uint8_t  index[4]; /* 00 00 ?? ?? */
	uint8_t  term1[4]; /* ff ff ?? ?? */
	uint8_t  term2[4]; /* ff ff ?? ?? */
} __attribute((packed));

struct mitsum1_errorlog_item {
	uint8_t unk[256];
	/* Printer dumps:  0000000 0000 0000 0000000000 0000 00 */
} __attribute((packed));

struct mitsum1_getinfo_resp {
	struct mitsud90_generic_hdr hdr;        /* @   0 */
	uint32_t cnt_prints;                    /* @  22 */
	uint32_t cnt_total;
	uint32_t cnt_unka; // increased by 97 for a single 8" print
	uint32_t cnt_cutter;
	uint32_t cnt_unkb[25];
	uint8_t  unk[1472-29*4];                /* @ 138 */
	struct mitsum1_errorlog_item items[20]; /* @1494 */
	/* End @6614 bytes */
} __attribute((packed));

static const char *mitsud90_mecha_statuses(const uint8_t *code)
{
	switch (code[0]) {
	case D90_MECHA_STATUS_IDLE:
		return "Idle";
	case W5K_MECHA_STATUS_PRINTING:
		// codes seen:
		// 22  30 31 32 33 34 35 37 38  <-- Side A?
		// 42  50 51 52 53    55 57 58  <-- Side B?
		// 60  <-- Finish?
		return "Printing (Unknown)";
	case D90_MECHA_STATUS_PRINTING:
		switch (code[1]) {
		case D90_MECHA_STATUS_PRINT_FEEDING:
		case D90_MECHA_STATUS_PRINT_x20: // XXX
			return "Feeding Media";
		case D90_MECHA_STATUS_PRINT_PRE_Y:
		case D90_MECHA_STATUS_PRINT_Y:
			return "Printing Yellow";
		case D90_MECHA_STATUS_PRINT_PRE_M:
		case D90_MECHA_STATUS_PRINT_M:
			return "Printing Magenta";
		case D90_MECHA_STATUS_PRINT_PRE_C:
		case D90_MECHA_STATUS_PRINT_C:
			return "Printing Cyan";
		case D90_MECHA_STATUS_PRINT_PRE_OC:
		case D90_MECHA_STATUS_PRINT_OC:
			return "Applying Overcoat";
		case D90_MECHA_STATUS_PRINT_x2f:
		case D90_MECHA_STATUS_PRINT_x38:
			return "Ejecting Media?"; // XXX
		default:
			return "Printing (Unknown)";
		}
	case D90_MECHA_STATUS_INIT:
		if (code[1] == D90_MECHA_STATUS_INIT_FEEDCUT)
			return "Feed & Cut paper";
		else
			return "Initializing";
	default:
		return "Unknown";
	}
}

static const char *mitsud90_error_codes(const uint8_t *code)
{
	switch(code[0]) {
	case D90_ERROR_STATUS_OK:
		if (code[1] & D90_ERROR_STATUS_OK_WARMING)
			return "Heating";
		else if (code[1] & D90_ERROR_STATUS_OK_COOLING)
			return "Cooling Down";
		else
			return "Idle";
	case D90_ERROR_STATUS_RIBBON:
		switch (code[1]) {
		case 0x00:
			return "Ribbon exhausted";
		case 0x10:
			return "Insufficient remaining ribbon";
		case 0x20:
			return "Ribbon Cue Timeout";
		case 0x30:
			return "Cannot Cue Ribbon";
		case 0x90:
			return "No ribbon";
		default:
			return "Unknown Ribbon Error";
		}
	case D90_ERROR_STATUS_PAPER:
		switch (code[1]) {
		case 0x00:
			return "No paper";
		case 0x02:
			return "Paper exhausted";
		default:
			return "Unknown Paper Error";
		}
	case D90_ERROR_STATUS_PAP_RIB:
		switch (code[1]) {
		case 0x00:
			return "Ribbon/Paper mismatch";
		case 0x90:
			return "Ribbon/Job mismatch";
		default:
			return "Unknown ribbon match error";
		}
	case 0x26:
		return "Illegal Ribbon";
	case 0x28:
		return "Cut Bin Missing";
	case D90_ERROR_STATUS_OPEN:
		switch (code[1]) {
		case 0x00:
			return "Printer Open during Stop";
		case 0x10:
			return "Printer Open during Initialization";
		case 0x90:
			return "Printer Open during Printing";
		default:
			return "Unknown Door error";
		}
	case 0x2f:
		return "Printer turned off during printing";
	case 0x31:
		return "Ink feed stop";
	case 0x32:
		return "Ink Skip 1 timeout";
	case 0x33:
		return "Ink Skip 2 timeout";
	case 0x34:
		return "Ink Sticking";
	case 0x35:
		return "Ink return stop";
	case 0x36:
		return "Ink Rewind timeout";
	case 0x37:
		return "Winding sensing error";
	case 0x40:
	case 0x41:
	case 0x42:
	case 0x43:
	case 0x44:
		return "Paper Jam";
	case 0x60:
		if (code[1] == 0x20)
			return "Preheat error";
		else if (code[1] == 0x04)
			return "Humidity sensor error";
		else if (code[1] & 0x1f)
			return "Thermistor error";
		else
			return "Unknown error";
	case 0x61:
		if (code[1] == 0x00)
			return "Color Sensor Error";
		else if (code[1] & 0x10)
			return "Matte OP Error";
		else
			return "Unknown error";
	case 0x62:
		return "Data Transfer error";
	case 0x63:
		return "EEPROM error";
	case 0x64:
		return "Flash access error";
	case 0x65:
		return "FPGA configuration error";
	case 0x66:
		return "Power voltage Error";
	case 0x67:
		return "RFID access error";
	case 0x68:
		if (code[1] == 0x00)
			return "Fan Lock Error";
		else if (code[1] == 0x90)
			return "MDA Error";
		else
			return "Unknown error";
	case 0x69:
		if (code[1] == 0x10)
			return "DDR Error";
		else if (code[1] == 0x00)
			return "Firmware Error";
		else
			return "Unknown error";
	case 0x70:
	case 0x71:
	case 0x73:
	case 0x74:
	case 0x75:
		return "Mechanical Error (check ribbon and power cycle)";
	case 0x82:
		return "USB Timeout";
	case 0x83:
		return "Illegal paper size";
	case 0x84:
		return "Illegal parameter";
	case 0x85:
		return "Job Cancel";
	case 0x89:
		return "Last Job Error";
	default:
		return "Unknown";
	}
}

static void mitsud90_dump_status(struct mitsud90_status_resp *resp)
{
	INFO("Error Status: %s (%02x %02x) -- %02x %02x %02x %02x  %02x %02x %02x %02x  %02x\n",
	     mitsud90_error_codes(resp->code),
	     resp->code[0], resp->code[1],
	     resp->unk[0], resp->unk[1], resp->unk[2], resp->unk[3],
	     resp->unk[4], resp->unk[5], resp->unk[6], resp->unk[7],
	     resp->unk[8]);
	INFO("Printer Status: %s (%02x %02x)\n",
	     mitsud90_mecha_statuses(resp->mecha),
	     resp->mecha[0], resp->mecha[1]);
	INFO("Temperature Status: %s\n",
	     mitsu_temperatures(resp->temp));
}

/* Private data structure */
struct mitsud90_printjob {
	struct dyesub_job_common common;

	uint8_t *databuf;
	uint32_t datalen;

	int is_raw;
	int is_pano;
	int is_duplex;

	int m1_colormode;

	struct mitsud90_job_hdr hdr;

	int has_footer;
	struct mitsud90_job_footer footer;

	int has_start;
};

struct mitsud90_ctx {
	struct dyesub_connection *conn;

	struct marker marker;

	struct mitsud90_media_resp media;
	char serno[9]; /* 8+null */
	char fwver_main[7]; /* 6+null */
	char fwver_mech[7]; /* 6+null */

	/* Used in parsing.. */
	struct mitsud90_job_footer holdover;
	int holdover_on;
	int duplex_on;
	int pano_page;

	/* For the CP-M1 family */
	struct mitsu_lib lib;
};

static int mitsud90_query_media(struct mitsud90_ctx *ctx, struct mitsud90_media_resp *resp)
{
	uint8_t cmdbuf[8];
	uint8_t cmdlen = 0;

	int ret, num;

	cmdbuf[cmdlen++] = 0x1b;
	cmdbuf[cmdlen++] = 0x47;
	cmdbuf[cmdlen++] = 0x44;
	cmdbuf[cmdlen++] = 0x30;
	if (ctx->conn->type != P_MITSU_W5000) {
		cmdbuf[cmdlen++] = 0;
		cmdbuf[cmdlen++] = 0;
	}
	cmdbuf[cmdlen++] = 0x01;  /* Number of commands */
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_MEDIA;

	if ((ret = send_data(ctx->conn,
			     cmdbuf, cmdlen)))
		return ret;
	memset(resp, 0, sizeof(*resp));

	ret = read_data(ctx->conn,
			(uint8_t*) resp, sizeof(*resp), &num);

	if (ret < 0)
		return ret;
	if (num != sizeof(*resp)) {
		ERROR("Short Read! (%d/%d)\n", num, (int)sizeof(*resp));
		return 4;
	}

	return CUPS_BACKEND_OK;
}

static int mitsud90_query_status(struct mitsud90_ctx *ctx, struct mitsud90_status_resp *resp)
{
	uint8_t cmdbuf[10];
	uint8_t cmdlen = 0;
	int ret, num;

	cmdbuf[cmdlen++] = 0x1b;
	cmdbuf[cmdlen++] = 0x47;
	cmdbuf[cmdlen++] = 0x44;
	cmdbuf[cmdlen++] = 0x30;
	if (ctx->conn->type != P_MITSU_W5000) {
		cmdbuf[cmdlen++] = 0;
		cmdbuf[cmdlen++] = 0;
	}
	cmdbuf[cmdlen++] = 0x03;  /* Number of commands */
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_ERROR;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_MECHA;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_TEMP;

	if ((ret = send_data(ctx->conn,
			     cmdbuf, cmdlen)))
		return ret;
	memset(resp, 0, sizeof(*resp));

	ret = read_data(ctx->conn,
			(uint8_t*) resp, sizeof(*resp), &num);

	if (ret < 0)
		return ret;
	if (num != sizeof(*resp)) {
		ERROR("Short Read! (%d/%d)\n", num, (int)sizeof(*resp));
		return 4;
	}

	return CUPS_BACKEND_OK;
}

static int mitsud90_query_fwver(struct mitsud90_ctx *ctx)
{
	uint8_t cmdbuf[8];
	uint8_t cmdlen = 0;
	int ret, num;
	struct mitsud90_fwver_resp resp;

	cmdbuf[cmdlen++] = 0x1b;
	cmdbuf[cmdlen++] = 0x47;
	cmdbuf[cmdlen++] = 0x44;
	cmdbuf[cmdlen++] = 0x30;
	if (ctx->conn->type != P_MITSU_W5000) {
		cmdbuf[cmdlen++] = 0;
		cmdbuf[cmdlen++] = 0;
	}
	cmdbuf[cmdlen++] = 1;  /* Number of commands */
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_FW_MAIN;

	if ((ret = send_data(ctx->conn,
			     cmdbuf, cmdlen)))
		return ret;
	memset(&resp, 0, sizeof(resp));

	ret = read_data(ctx->conn,
			(uint8_t*) &resp, sizeof(resp), &num);

	if (ret)
		return CUPS_BACKEND_FAILED;

	memcpy(ctx->fwver_main, resp.fw_ver.version, 6);
	ctx->fwver_main[6] = 0;

	if (ctx->conn->type != P_MITSU_W5000) {
		cmdbuf[cmdlen-1] = COM_STATUS_TYPE_FW_MECH;

		if ((ret = send_data(ctx->conn,
				     cmdbuf, cmdlen)))
			return ret;
		memset(&resp, 0, sizeof(resp));

		ret = read_data(ctx->conn,
				(uint8_t*) &resp, sizeof(resp), &num);

		if (ret)
			return CUPS_BACKEND_FAILED;

		memcpy(ctx->fwver_mech, resp.fw_ver.version, 6);
		ctx->fwver_mech[6] = 0;
	}

	return CUPS_BACKEND_OK;
}

static int mitsuw5k_get_serno(struct mitsud90_ctx *ctx)
{
	uint8_t cmdbuf[12];
	uint8_t cmdlen = 0;
	int ret, num;
	struct mitsud90_fwver_resp resp;

	cmdbuf[cmdlen++] = 0x1b;
	cmdbuf[cmdlen++] = 0x47;
	cmdbuf[cmdlen++] = 0x44;
	cmdbuf[cmdlen++] = 0x30;
	cmdbuf[cmdlen++] = 1;  /* Number of commands */
	cmdbuf[cmdlen++] = W5K_STATUS_TYPE_SERIAL;

	if ((ret = send_data(ctx->conn,
			     cmdbuf, cmdlen)))
		return ret;
	memset(&resp, 0, sizeof(resp));

	ret = read_data(ctx->conn,
			cmdbuf, sizeof(cmdbuf), &num);

	if (ret)
		return CUPS_BACKEND_FAILED;

	memcpy(ctx->serno, cmdbuf + 4, sizeof(cmdbuf)-4);
	ctx->serno[sizeof(cmdbuf)-4-1] = 0;

	return CUPS_BACKEND_OK;
}

static int mitsud90_get_serno(struct mitsud90_ctx *ctx)
{
	uint8_t cmdbuf[32];
	uint8_t cmdlen = 0;
	int ret, num;

	/* Send Request */
	cmdbuf[cmdlen++] = 0x1b;
	cmdbuf[cmdlen++] = 0x61;
	cmdbuf[cmdlen++] = 0x36;
	cmdbuf[cmdlen++] = 0x36;
	cmdbuf[cmdlen++] = 0x41;
	cmdbuf[cmdlen++] = 0xbe;
	cmdbuf[cmdlen++] = 0x00;
	cmdbuf[cmdlen++] = 0x00;

	cmdbuf[cmdlen++] = 0x00;
	cmdbuf[cmdlen++] = 0x06;
	cmdbuf[cmdlen++] = 0x00;
	cmdbuf[cmdlen++] = 0x00;
	cmdbuf[cmdlen++] = 0x00;
	cmdbuf[cmdlen++] = 0x30;
	cmdbuf[cmdlen++] = 0xff;
	cmdbuf[cmdlen++] = 0xff;

	cmdbuf[cmdlen++] = 0xff;
	cmdbuf[cmdlen++] = 0xf9;
	cmdbuf[cmdlen++] = 0xff;
	cmdbuf[cmdlen++] = 0xff;
	cmdbuf[cmdlen++] = 0xff;
	cmdbuf[cmdlen++] = 0xcf;

	if ((ret = send_data(ctx->conn,
			     cmdbuf, cmdlen)))
		return ret;

	ret = read_data(ctx->conn,
			cmdbuf, sizeof(cmdbuf), &num);

	/* Store it */
	memcpy(ctx->serno, &cmdbuf[22], 6);
	ctx->serno[6] = 0;

	return ret;
}

static int mitsud90_get_counter(struct mitsud90_ctx *ctx, uint8_t type, uint32_t *val)
{
	uint8_t cmdbuf[32];
	uint8_t cmdlen = 0;
	int ret, num;

	/* Send Request */
	cmdbuf[cmdlen++] = 0x1b;
	cmdbuf[cmdlen++] = 0x61;
	cmdbuf[cmdlen++] = 0x36;
	cmdbuf[cmdlen++] = 0x36;
	cmdbuf[cmdlen++] = 0x45;
	cmdbuf[cmdlen++] = 0xba;
	cmdbuf[cmdlen++] = 0x00;
	cmdbuf[cmdlen++] = 0x00;

	cmdbuf[cmdlen++] = 0x00;
	cmdbuf[cmdlen++] = 0x04;
	cmdbuf[cmdlen++] = 0x00;
	cmdbuf[cmdlen++] = 0x00;
	cmdbuf[cmdlen++] = 0x06;
	cmdbuf[cmdlen++] = type ? 0x44: 0x40;
	cmdbuf[cmdlen++] = 0xff;
	cmdbuf[cmdlen++] = 0xff;

	cmdbuf[cmdlen++] = 0xff;
	cmdbuf[cmdlen++] = type ? 0xfb : 0xfe;
	cmdbuf[cmdlen++] = 0xff;
	cmdbuf[cmdlen++] = 0xff;
	cmdbuf[cmdlen++] = 0xf9;
	cmdbuf[cmdlen++] = type ? 0xbb: 0xbf;

	if ((ret = send_data(ctx->conn,
			     cmdbuf, cmdlen)))
		return ret;

	ret = read_data(ctx->conn,
			cmdbuf, sizeof(cmdbuf), &num);

	*val = le32_to_cpu(*((uint32_t*)&cmdbuf[22]));

	return ret;
}

/* Generic functions */

static void *mitsud90_init(void)
{
	struct mitsud90_ctx *ctx = malloc(sizeof(struct mitsud90_ctx));
	if (!ctx) {
		ERROR("Memory Allocation Failure!\n");
		return NULL;
	}
	memset(ctx, 0, sizeof(struct mitsud90_ctx));

	return ctx;
}

static int mitsud90_attach(void *vctx, struct dyesub_connection *conn, uint8_t jobid)
{
	struct mitsud90_ctx *ctx = vctx;

	UNUSED(jobid);

	ctx->conn = conn;

	if (test_mode < TEST_MODE_NOATTACH) {
		if (mitsud90_query_media(ctx, &ctx->media))
			return CUPS_BACKEND_FAILED;
		if (ctx->conn->type == P_MITSU_W5000) {
			if (mitsuw5k_get_serno(ctx))
				return CUPS_BACKEND_FAILED;
		} else {
			if (mitsud90_get_serno(ctx))
				return CUPS_BACKEND_FAILED;
		}
		if (mitsud90_query_fwver(ctx))
			return CUPS_BACKEND_FAILED;
	} else {
		ctx->media.media.brand = 0xff;
		ctx->media.media.type = 0x0f;
		ctx->media.media.capacity = cpu_to_be16(230);
		ctx->media.media.remain = cpu_to_be16(200);
		ctx->fwver_main[0] = 0;
		ctx->fwver_mech[0] = 0;
	}

	ctx->marker.color = "#00FFFF#FF00FF#FFFF00";
	ctx->marker.numtype = ctx->media.media.type;
	ctx->marker.name = mitsu_media_types(ctx->conn->type, ctx->media.media.brand, ctx->media.media.type);
	ctx->marker.levelmax = be16_to_cpu(ctx->media.media.capacity);
	ctx->marker.levelnow = be16_to_cpu(ctx->media.media.remain);

	if (ctx->conn->type == P_MITSU_M1 ||
	    ctx->conn->type == P_FUJI_ASK500) {  // || D90 & pano
#if defined(WITH_DYNAMIC)
		/* Attempt to open the library */
		if (mitsu_loadlib(&ctx->lib, ctx->conn->type))
#endif
			WARNING("Dynamic library not loaded, will be unable to print.\n");
	} else if (ctx->conn->type == P_MITSU_D90) {
#if defined(WITH_DYNAMIC)
		/* Attempt to open the library */
		if (mitsu_loadlib(&ctx->lib, ctx->conn->type))
#endif
			WARNING("Dynamic library not loaded, panorama printing may not work.\n");
	}

	if (ctx->conn->type == P_MITSU_M1) {
		/* Known types:
		   CP-M1A/E v1.00 is 450B11 (ME is 454C11)
		       ""   v?.?? is 450E11 (ME is 454E11)
		       ""   v1.30 is 450F12 (ME is 454F11)
		      M15   v1.10 is 450C41 (ME is 454E11)
		       ""   v1.20 is 450D42 (ME is 454F11)
		*/
		// XXX All CP-M1 variants appear to share same USB VID/PID.
		// Unclear if any functional or runtime difference.
		/* Compare MECHA firmware here since it's shared between M1A/M1E/M15 */
		if (strncmp(ctx->fwver_mech, "454F11", 6) < 0)
			WARNING("Printer FW out of date.  Highly recommeding updating M1A/M1E to v1.30 or newer, and M15 to v1.20 or newer\n");
	} else if (ctx->conn->type == P_MITSU_D90) {
		// XXX figure out if printer can handle panorama!  (FW "2.10" is needed)
		//
		// CP-D90DW v2.10 is 415A81/415G11 (revA/B)   ME is 419E11
		// CP-D90DW-P v2.10 is 415B94/415E54 (revA/B) ME is 419E42
		// D90DW-P and D90DW share same USB VID/PID. Unclear if any functional or runtime difference.
	} else if (ctx->conn->type == P_FUJI_ASK500) {
		// Completely unknown.
	} else if (ctx->conn->type == P_MITSU_W5000) {
		// W5000 MA:53C21S T:55A21@ F:369B21
	}

	return CUPS_BACKEND_OK;
}

static void mitsud90_teardown(void *vctx) {
	struct mitsud90_ctx *ctx = vctx;

	if (!ctx)
		return;

	if (ctx->pano_page) {
		WARNING("Panorama state left dangling!\n");
	}

	if (ctx->conn->type == P_MITSU_M1 ||
	    ctx->conn->type == P_FUJI_ASK500) {
		mitsu_destroylib(&ctx->lib);
	}

	free(ctx);
}

static void mitsud90_cleanup_job(const void *vjob)
{
	const struct mitsud90_printjob *job = vjob;

	if (job->databuf)
		free(job->databuf);

	free((void*)job);
}

/* Sanity check some stuff */
STATIC_ASSERT(sizeof(struct mitsud90_job_hdr) == 512);
STATIC_ASSERT(sizeof(struct mitsud90_memcheck) == 512);
STATIC_ASSERT(sizeof(struct mitsud90_plane_hdr) == 512);
STATIC_ASSERT(sizeof(struct mitsuw5k_plane_hdr) == 512);
STATIC_ASSERT(sizeof(struct mitsuw5k_job_hdr) == 512);
STATIC_ASSERT(sizeof(struct mitsud90_generic_hdr) == 22);
STATIC_ASSERT(sizeof(struct mitsum1_getinfo_resp) == 6614);

static int mitsud90_main_loop(void *vctx, const void *vjob, int wait_for_return);

static int mitsud90_panorama_splitjob(struct mitsud90_printjob *injob, struct mitsud90_printjob **newjobs)
{
	uint8_t *panels[3] = { NULL, NULL, NULL };
	uint16_t panel_rows[3] = { 0, 0, 0 };
	uint16_t overlap_rows = 600;
	uint16_t pad_rows = 14;
	uint8_t numpanels;
	uint16_t cols;
	uint16_t inrows;
	uint16_t max_rows = 2428; /* 6x8" only */
	int i;

	cols = be16_to_cpu(injob->hdr.cols);
	inrows = be16_to_cpu(injob->hdr.rows);

	/* Work out parameters */
	if (inrows == 6028) {
		numpanels = 3;
	} else if (inrows == 4228) {
		numpanels = 2;
	} else {
		ERROR("Invalid panorama row count (%d)\n", inrows);
		return CUPS_BACKEND_CANCEL;
	}

	/* Work out panel sizes... */
	for (int src_rows = inrows - 2 * pad_rows, i = 0 ; src_rows > 0; ) {
		panel_rows[i] = (src_rows < max_rows) ? src_rows : max_rows - 2*pad_rows;
		src_rows -= panel_rows[i];
		i++;

		if (i < numpanels)
			src_rows += overlap_rows;
	}

	/* Allocate and set up new jobs and buffers */
	for (i = 0 ; i < numpanels ; i++) {
		newjobs[i] = malloc(sizeof(struct mitsud90_printjob));
		if (!newjobs[i]) {
			ERROR("Memory allocation failure");
			return CUPS_BACKEND_RETRY_CURRENT;
		}
		panel_rows[i] += 2 * pad_rows;
		panels[i] = malloc((cols * panel_rows[i] * 3) + sizeof(struct mitsud90_plane_hdr));
		if (!panels[i]) {
			ERROR("Memory allocation failure");
			return CUPS_BACKEND_RETRY_CURRENT;
		}
		/* Fill in job header differences */
		memcpy(newjobs[i], injob, sizeof(struct mitsud90_printjob));
		newjobs[i]->databuf = panels[i];
		newjobs[i]->hdr.rows = cpu_to_be16(panel_rows[i]);
		newjobs[i]->hdr.sharp_h = 0; // XXX do sharpening _before_ split
		newjobs[i]->hdr.sharp_v = 0;
		newjobs[i]->hdr.pano.on = 1;
		newjobs[i]->hdr.pano.total = numpanels;
		newjobs[i]->hdr.pano.page = i + 1;
		newjobs[i]->hdr.pano.rows = cpu_to_be16(panel_rows[i]);
		newjobs[i]->hdr.pano.rows2 = cpu_to_be16(panel_rows[i] - 0x30);
		newjobs[i]->hdr.pano.overlap = cpu_to_be16(overlap_rows);
		newjobs[i]->hdr.pano.unk[1] = 0x0c;
		newjobs[i]->hdr.pano.unk[3] = 0x06;
		newjobs[i]->has_footer = 0;
		panel_rows[i] -= 2 * pad_rows;

		/* Fill in plane header differences */
		memcpy(newjobs[i]->databuf, injob->databuf, sizeof(struct mitsud90_plane_hdr));
		struct mitsud90_plane_hdr *phdr = (struct mitsud90_plane_hdr*)panels[i];
		phdr->rows = cpu_to_be16(panel_rows[i]);
		if (phdr->lamrows)
			phdr->lamrows = cpu_to_be16(panel_rows[i] + 12);
		panels[i] += sizeof(struct mitsud90_plane_hdr);
	}
	/* Last panel gets the footer, if any */
	if (injob->has_footer) {
		newjobs[numpanels - 1]->has_footer = injob->has_footer;
		memcpy(&newjobs[numpanels-1]->footer, &injob->footer, sizeof(injob->footer));
	}

	dyesub_pano_split_rgb8(injob->databuf, cols, numpanels,
			       overlap_rows, pad_rows,
			       panel_rows, panels);

	return CUPS_BACKEND_OK;
}

static int mitsud90_read_parse(void *vctx, const void **vjob, int data_fd, int copies) {
	struct mitsud90_ctx *ctx = vctx;
	int i, remain;

	struct mitsud90_printjob *job;

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
	uint8_t *hptr = (uint8_t*) &job->hdr;
	uint16_t hremain = sizeof(struct mitsud90_job_hdr);

	/* Make sure there's no holdover */
	if (ctx->holdover_on) {
		memcpy(hptr, &ctx->holdover, sizeof(ctx->holdover));
		hremain -= sizeof(ctx->holdover);
		ctx->holdover_on = 0;
	}
	/* Read the rest */
	while (hremain) {
		i = read(data_fd, hptr, hremain);
		if (i == 0) {
			mitsud90_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		if (i < 0) {
			mitsud90_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		hremain -= i;
		hptr += i;
	}

	/* Sanity check header */
	if (job->hdr.hdr[0] != 0x1b ||
	    job->hdr.hdr[1] != 0x53 ||
	    job->hdr.hdr[2] != 0x50 ||
	    job->hdr.hdr[3] != 0x30 ) {
		ERROR("Unrecognized data format (%02x%02x%02x%02x)!\n",
		      job->hdr.hdr[0], job->hdr.hdr[1],
		      job->hdr.hdr[2], job->hdr.hdr[3]);
		mitsud90_cleanup_job(job);
		return CUPS_BACKEND_CANCEL;
	}

	/* Initial parsing */
	if (ctx->conn->type == P_MITSU_M1 ||
	    ctx->conn->type == P_FUJI_ASK500) {
		/* See if it's a special gutenprint "not-raw" job */
		job->is_raw = !job->hdr.zero_b[3];
		job->hdr.zero_b[3] = 0;
	} else if (ctx->conn->type == P_MITSU_D90) {
		if (job->hdr.zero_b[3] && job->hdr.pano.on == 0x03) {
			job->is_pano = 1;
			job->hdr.zero_b[3] = 0;
			job->hdr.pano.on = 0; /* Will get inserted later */
		} else if (be16_to_cpu(job->hdr.rows) > 2729) {
			job->is_pano = 1;
			job->hdr.pano.on = 0;
		}

	} else if (ctx->conn->type == P_MITSU_W5000) {
		struct mitsuw5k_job_hdr *hdr = (struct mitsuw5k_job_hdr*) &job->hdr;

		remain = be16_to_cpu(hdr->cols) * be16_to_cpu(hdr->rows) * 3;

		/* Add in the plane header */
		remain += sizeof(struct mitsuw5k_plane_hdr);

		if (!hdr->single)
			job->is_duplex = 1;

		/* Note the job has an OPTIONAL start block.
		   We check for that later */

		goto read_data;
	}

	/* Sanity check panorama parameters */
	if (job->hdr.pano.on &&
	    ctx->conn->type == P_MITSU_D90) {
		if ((job->hdr.pano.total < 2 &&
		     job->hdr.pano.total > 3) ||
		    (job->hdr.pano.page < 1 &&
		     job->hdr.pano.page > 3) ||
		    job->hdr.pano.page != (ctx->pano_page + 1) ||
		    (job->hdr.pano.rows != 2428) ||
		    be16_to_cpu(job->hdr.pano.rows2 != (2428-0x30)) ||
		    be16_to_cpu(job->hdr.pano.overlap != 600)
			) {
			ERROR("Invalid panorama job parameters\n");
			mitsud90_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
	}

	/* Sanity check cutlist */
	if (job->hdr.numcuts > 3) {
		ERROR("Cut list too long!\n");
		mitsud90_cleanup_job(job);
		return CUPS_BACKEND_CANCEL;
	}
	if (job->hdr.numcuts >= 1) {
		int rows = be16_to_cpu(job->hdr.rows);
		for (i = 0 ; i < job->hdr.numcuts ; i++) {
			int min_size;
			int position = be16_to_cpu(job->hdr.cutlist[i].position);
			int last_position = (i == 0) ? 0 : be16_to_cpu(job->hdr.cutlist[i-1].position);

			if (i == 0)
				min_size = 613;
			else
				min_size = (job->hdr.cutlist[i-1].margincut) ? 601 : 660; // XXX inverted?

			if ((position - last_position) < min_size) {
				ERROR("Minumum cut#%d length is %d rows\n", i, min_size);
				mitsud90_cleanup_job(job);
				return CUPS_BACKEND_CANCEL;
			}
			if ((rows - position) < min_size) {
				ERROR("Cut#%d is too close to end\n", i);
				mitsud90_cleanup_job(job);
				return CUPS_BACKEND_CANCEL;
			}
		}
	}

	remain = be16_to_cpu(job->hdr.cols) * be16_to_cpu(job->hdr.rows) * 3;
	if (job->is_raw)
		remain *= 2;
	/* Add in the plane header */
	remain += sizeof(struct mitsud90_plane_hdr);

read_data:
	/* Allocate ourselves a payload buffer */
	job->databuf = malloc(remain + 1024);
	if (!job->databuf) {
		ERROR("Memory allocation failure!\n");
		mitsud90_cleanup_job(job);
		return CUPS_BACKEND_RETRY_CURRENT;
	}
	job->datalen = 0;

	/* Now read in the rest */
	while(remain) {
		i = read(data_fd, job->databuf + job->datalen, remain);
		if (i == 0) {
			mitsud90_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		if (i < 0) {
			mitsud90_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		job->datalen += i;
		remain -= i;
	}

	/* Read in the footer.  Hopefully... */
	i = read(data_fd, (uint8_t*)&job->footer, sizeof(job->footer));
	if (i == 0) {
		mitsud90_cleanup_job(job);
		return CUPS_BACKEND_CANCEL;
	}
	if (i < 0) {
		mitsud90_cleanup_job(job);
		return CUPS_BACKEND_CANCEL;
	}

	if (ctx->conn->type == P_MITSU_W5000) {
		if (job->footer.hdr[0] != 0x1b ||
		    job->footer.hdr[1] != 0x5a ||
		    job->footer.hdr[2] != 0x54 ||
		    job->footer.hdr[3] != 0x01) {
			memcpy(&ctx->holdover, &job->footer, sizeof(job->footer));
		        ctx->holdover_on = 1;
		} else {
			uint8_t tmpbuf[512];
			job->has_start = 1;
			ctx->holdover_on = 0;

			/* read in remaining footer and discard */
			remain = sizeof(tmpbuf)-sizeof(job->footer);
			while(remain) {
				i = read(data_fd, tmpbuf, remain);
				if (i == 0) {
					mitsud90_cleanup_job(job);
					return CUPS_BACKEND_CANCEL;
				}
				if (i < 0) {
					mitsud90_cleanup_job(job);
					return CUPS_BACKEND_CANCEL;
				}
				job->datalen += i;
				remain -= i;
			}
		}
	} else {
		/* See if this is a job footer.  If it is, keep, else holdover. */
		if (job->footer.hdr[0] != 0x1b ||
		    job->footer.hdr[1] != 0x42 ||
		    job->footer.hdr[2] != 0x51 ||
		    job->footer.hdr[3] != 0x31) {
			memcpy(&ctx->holdover, &job->footer, sizeof(job->footer));
		        ctx->holdover_on = 1;
			// XXX generate a footer!
		} else {
			job->has_footer = 1;
			ctx->holdover_on = 0;
		}
	}

	/* CP-M1 has... other considerations */
	if (((ctx->conn->type == P_MITSU_M1 ||
	      ctx->conn->type == P_FUJI_ASK500) && !job->is_raw) ||
	    (ctx->conn->type == P_MITSU_D90 && job->is_pano)) {
		if (!ctx->lib.dl_handle) {
			ERROR("!!! Image Processing Library not found, aborting!\n");
			mitsud90_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}

		job->m1_colormode = job->hdr.colorcorr;

		if (job->m1_colormode == 0) {
			const char *lutfname = NULL;

			if (ctx->conn->type == P_MITSU_M1) {
				lutfname = CPM1_LUT_FNAME;
			}
			if (ctx->conn->type == P_MITSU_D90) {
				lutfname = CPD90_LUT_FNAME;
			}

			/* NOTE: No LUT for ASK-500 yet */
			if (lutfname) {
				int ret = mitsu_apply3dlut_packed(&ctx->lib, lutfname,
								  job->databuf + sizeof(struct mitsud90_plane_hdr),
								  be16_to_cpu(job->hdr.cols),
								  be16_to_cpu(job->hdr.rows),
								  be16_to_cpu(job->hdr.cols) * 3, COLORCONV_RGB);
				if (ret) {
					mitsud90_cleanup_job(job);
					return ret;
				}
			}
		}
		job->hdr.colorcorr = 1; // XXX not sure if right for ASK500?
	}

	if (job->is_pano) {
		int rval;

		// XXX do any print sharpening here!  If possible..
		rval = mitsud90_panorama_splitjob(job, (struct mitsud90_printjob**)vjob);
		/* Clean up original parsed job regardless */
		mitsud90_cleanup_job(job);

		return rval;
	} else {
		*vjob = job;
	}

	return CUPS_BACKEND_OK;
}

static int cpm1_fillmatte(struct mitsud90_printjob *job)
{
	int ret;
	int rows, cols;

	struct mitsud90_plane_hdr *phdr = (struct mitsud90_plane_hdr *) job->databuf;

	rows = be16_to_cpu(job->hdr.rows) + 12;
	cols = be16_to_cpu(job->hdr.cols);

	/* Fill in matte data */
	ret = mitsu_readlamdata(CPM1_LAMINATE_FILE, CPM1_LAMINATE_STRIDE,
				job->databuf, &job->datalen,
				rows, cols, 1);

	if (ret)
		return ret;

	/* Update plane header and overall length */
	phdr->lamcols = cpu_to_be16(cols);
	phdr->lamrows = cpu_to_be16(rows);

	return CUPS_BACKEND_OK;
}

static int mitsud90_main_loop(void *vctx, const void *vjob, int wait_for_return) {
	struct mitsud90_ctx *ctx = vctx;
	struct mitsud90_status_resp resp;
	uint8_t last_status[2] = {0xff, 0xff};

	int sent;
	int ret;
	int copies;

	struct mitsud90_printjob *job = (struct mitsud90_printjob *)vjob;

	if (!ctx)
		return CUPS_BACKEND_FAILED;
	if (!job)
		return CUPS_BACKEND_FAILED;
	copies = job->common.copies;

	/* Handle panorama state */
	if (ctx->conn->type == P_MITSU_D90) {
		if (job->hdr.pano.on) {
			ctx->pano_page++;
			if (job->hdr.pano.page != ctx->pano_page) {
				ERROR("Invalid panorama state (page %d of %d)\n",
				      ctx->pano_page, job->hdr.pano.page);
				return CUPS_BACKEND_FAILED;
			}
			if (copies > 1) {
				WARNING("Cannot print non-collated copies of a panorama job\n");
				copies = 1;
			}
		} else if (ctx->pano_page) {
			/* Clean up panorama state */
			WARNING("Dangling panorama state!\n");
			ctx->pano_page = 0;
		}
	} else {
		ctx->pano_page = 0;
	}

	if ((ctx->conn->type == P_MITSU_M1 ||
	     ctx->conn->type == P_FUJI_ASK500) && !job->is_raw) {
		struct BandImage input;
		struct BandImage output;
		struct M1CPCData *cpc;

		input.origin_rows = input.origin_cols = 0;
		input.rows = be16_to_cpu(job->hdr.rows);
		input.cols = be16_to_cpu(job->hdr.cols);
		input.imgbuf = job->databuf + sizeof(struct mitsud90_plane_hdr);
		input.bytes_per_row = input.cols * 3;

		/* Allocate new buffer, with extra room for header */
		uint8_t *convbuf = malloc(input.rows * input.cols * sizeof(uint16_t) * 3 + (job->hdr.overcoat? (input.rows + 12) * input.cols + CPM1_LAMINATE_STRIDE / 2 : 0) + sizeof(struct mitsud90_plane_hdr));
		if (!convbuf) {
			ERROR("Memory allocation Failure!\n");
			return CUPS_BACKEND_RETRY_CURRENT;
		}

		output.origin_rows = output.origin_cols = 0;
		output.rows = input.rows;
		output.cols = input.cols;
		output.imgbuf = convbuf + sizeof(struct mitsud90_plane_hdr);
		output.bytes_per_row = output.cols * 3 * sizeof(uint16_t);

		/* Copy over the plane header */
		memcpy(convbuf, job->databuf, sizeof(struct mitsud90_plane_hdr));

		// Do CContrastConv prior to RGBRate
		job->hdr.rgbrate = ctx->lib.M1_CalcRGBRate(input.rows,
							   input.cols,
							   input.imgbuf);

		/* Color modes: 0 LUT, NOMATCH
		                1 NOLUT, MATCH  <-- ie use with external ICC profile!
                                2 NOLUT, NOMATCH */

		const char *gammatab;

		if (ctx->conn->type == P_FUJI_ASK500) {
			if (job->m1_colormode == 1) {
				gammatab = ASK5_CPC_G5_FNAME;
			} else { /* Mode 0 or 2 */
				gammatab = ASK5_CPC_G1_FNAME;
			}
			cpc = ctx->lib.M1_GetCPCData(corrtable_path, ASK5_CPC_FNAME, gammatab);
		} else if (ctx->conn->type == P_MITSU_D90) {
			// XXX not used in this code path yet...
			// Note that these CSVs are actually BGR->YMC gamma!
			if (job->m1_colormode == 1)
			    gammatab = CPD90_CPC_3_1_FNAME;
			else
			    gammatab = CPD90_CPC_3_2_FNAME;
			cpc = ctx->lib.M1_GetCPCData(corrtable_path, CPD90_CPC_FNAME, gammatab);
		} else {
			if (job->m1_colormode == 1) {
				gammatab = CPM1_CPC_G5_FNAME;
			} else if (job->m1_colormode == 3) {
				gammatab = CPM1_CPC_G5_VIVID_FNAME;
			} else { /* Mode 0 or 2 */
				gammatab = CPM1_CPC_G1_FNAME;
			}
			cpc = ctx->lib.M1_GetCPCData(corrtable_path, CPM1_CPC_FNAME, gammatab);
		}

		if (!cpc) {
			ERROR("Cannot read data tables\n");
			free(convbuf);
			return CUPS_BACKEND_FAILED;
		}

		/* Do gamma conversion */
		if (ctx->conn->type != P_MITSU_D90)
			ctx->lib.M1_Gamma8to14(cpc, &input, &output);

		if (job->hdr.sharp_h || job->hdr.sharp_v) {
			/* 0 is off, 1-7 corresponds to level 0-6 */
			int sharp = ((job->hdr.sharp_h > job->hdr.sharp_v) ? job->hdr.sharp_h : job->hdr.sharp_v) - 1;
			job->hdr.sharp_h = 0;
			job->hdr.sharp_v = 0;

			/* And do the sharpening */
			if (ctx->lib.M1_CLocalEnhancer(cpc, sharp, &output)) {
				ERROR("CLocalEnhancer failed (out of memory?)\n");
				free(convbuf);
				ctx->lib.M1_DestroyCPCData(cpc);
				return CUPS_BACKEND_RETRY_CURRENT;
			}
		}

		/* We're done with the CPC data */
		ctx->lib.M1_DestroyCPCData(cpc);

#if (__BYTE_ORDER == __BIG_ENDIAN)
		/* Convert data to LITTLE ENDIAN if needed */
		int i;
		uint16_t *ptr = output.imgbuf;
		for (i = 0; i < output.rows * output.cols ; i ++) {
			ptr[i] = cpu_to_le16(i);
		}
#endif

		free(job->databuf);
		job->databuf = convbuf;
		job->datalen = sizeof(struct mitsud90_plane_hdr) + input.rows * input.cols * sizeof(uint16_t) * 3;

		/* Deal with lamination settings */
		if (job->hdr.overcoat == 3) {
			int pre_matte_len = job->datalen;
			ret = cpm1_fillmatte(job);
			if (ret) {
				mitsud90_cleanup_job(job);
				return ret;
			}
			job->hdr.oprate = ctx->lib.M1_CalcOpRateMatte(output.rows,
								      output.cols,
								      job->databuf + pre_matte_len);
		} else {
			job->hdr.oprate = ctx->lib.M1_CalcOpRateGloss(output.rows,
								      output.cols);
		}

		job->is_raw = 1;
	}

	/* Bypass */
	if (test_mode >= TEST_MODE_NOPRINT)
		return CUPS_BACKEND_OK;

	INFO("Waiting for printer idle...\n");

top:
	sent = 0;

	// XXX Figure out if printer is asleep, and wake it up if necessary.

	/* Query status, wait for idle or error out */
	do {
		if (mitsud90_query_status(ctx, &resp))
			return CUPS_BACKEND_FAILED;

		if (resp.code[0] != D90_ERROR_STATUS_OK) {
			ERROR("Printer reported error condition: %s (%02x %02x)\n",
			      mitsud90_error_codes(resp.code), resp.code[0], resp.code[1]);
			return CUPS_BACKEND_STOP;
		}

		if (resp.code[1] & D90_ERROR_STATUS_OK_WARMING ||
		    resp.temp & D90_ERROR_STATUS_OK_WARMING ) {
			INFO("Printer warming up\n");
			sleep(1);
			continue;
		}
		if (resp.code[1] & D90_ERROR_STATUS_OK_COOLING ||
			   resp.temp & D90_ERROR_STATUS_OK_COOLING) {
			INFO("Printer cooling down\n");
			sleep(1);
			continue;
		}

		if (resp.mecha[0] != last_status[0] ||
		    resp.mecha[1] != last_status[1]) {
			INFO("Printer status: %s\n",
			     mitsud90_mecha_statuses(resp.mecha));
			last_status[0] = resp.mecha[0];
			last_status[1] = resp.mecha[1];
		}

		/* Send memory check, but NOT on W5000 */
		if (ctx->conn->type != P_MITSU_W5000) {
			struct mitsud90_memcheck mem;
			struct mitsud90_memcheck_resp mem_resp;
			int num;

			memcpy(&mem, &job->hdr, sizeof(mem));
			mem.hdr[0] = 0x1b;
			mem.hdr[1] = 0x47;
			mem.hdr[2] = 0x44;
			mem.hdr[3] = 0x33;

			if ((ret = send_data(ctx->conn,
					     (uint8_t*) &mem, sizeof(mem))))
				return CUPS_BACKEND_FAILED;

			ret = read_data(ctx->conn,
					(uint8_t*)&mem_resp, sizeof(mem_resp), &num);

			if (ret < 0)
				return ret;
			if (num != sizeof(mem_resp)) {
				ERROR("Short Read! (%d/%d)\n", num, (int)sizeof(mem_resp));
				return CUPS_BACKEND_FAILED;
			}
			if (mem_resp.size_bad || mem_resp.mem_bad == 0xff) {
				ERROR("Printer reported bad print params (%02x/%02x)\n", mem_resp.size_bad, mem_resp.mem_bad);
				return CUPS_BACKEND_CANCEL;
			}

			if (mem_resp.mem_bad) {
				ERROR("Printer buffers full, retrying!\n");
				sleep(1);
				continue;
			} else {
				break;
			}
		} else if (resp.mecha[0] != D90_MECHA_STATUS_IDLE) {
			/* On W5K, just wait for printer to be idle */
			sleep(1);
			continue;
		} else {
			break;
		}
	} while(1);

	/* Send job header */
	if ((ret = send_data(ctx->conn,
			     (uint8_t*) &job->hdr, sizeof(job->hdr))))
		return CUPS_BACKEND_FAILED;

	/* Send Plane header */
	if ((ret = send_data(ctx->conn,
			     job->databuf + sent, sizeof(job->hdr))))
		return CUPS_BACKEND_FAILED;
	sent += sizeof(job->hdr);

	/* Send payload */
	if ((ret = send_data(ctx->conn,
			     job->databuf + sent, job->datalen - sent)))
		return CUPS_BACKEND_FAILED;
//	sent += (job->datalen - sent);

	if (ctx->conn->type == P_MITSU_W5000) {
		/* Duplex handling */
		if (job->is_duplex) {
			ctx->duplex_on++;
			copies = 1;
		}

		if (ctx->duplex_on == 2)
			ctx->duplex_on = 0;

		if (!ctx->duplex_on || job->has_start) {
			/* Sent job START */
			struct mitsuw5k_plane_hdr start = {
				.hdr = { 0x1b, 0x5a, 0x54, 0x01 },
				.printout = 1
			};
			if ((ret = send_data(ctx->conn,
			     (uint8_t*)&start, sizeof(start))))
				return CUPS_BACKEND_FAILED;
		} else {
			goto skip_duplex;
		}
	} else if (job->has_footer) {
		if ((ret = send_data(ctx->conn,
				     (uint8_t*) &job->footer, sizeof(job->footer))))
			return CUPS_BACKEND_FAILED;

		/* Initiating printing means we're done parsing panorama */
		if (ctx->pano_page)
			ctx->pano_page = 0;
	}

	/* Work out printer delay/countdown */
	int countdown = (job->has_footer) ? be16_to_cpu(job->footer.seconds) : job->hdr.waittime;
	if (countdown >= 0xff)
		countdown = 0;

	/* If we're printing multiple copies, don't wait */
	if (copies > 1)
		countdown = 0;

	/* If we're told to not bother waiting, don't? */
	if (!wait_for_return)
		countdown = 0;

	// XXX Alternatively, don't wait if printer is not idle?
	// should we _ever_ wait?  Since each "job" is currently standalone..

	if (countdown)
		INFO("Job includes wait for %d seconds before starting\n", countdown);

	/* Wait for completion */
	do {
		sleep(1);

		if (mitsud90_query_status(ctx, &resp))
			return CUPS_BACKEND_FAILED;

		if (resp.code[0] != D90_ERROR_STATUS_OK) {
			ERROR("Printer reported error condition: %s (%02x %02x)\n",
			      mitsud90_error_codes(resp.code), resp.code[0], resp.code[1]);
			return CUPS_BACKEND_STOP;
		}

		if (resp.mecha[0] != last_status[0] ||
		    resp.mecha[1] != last_status[1]) {
			INFO("Printer status: %s\n",
			     mitsud90_mecha_statuses(resp.mecha));
			last_status[0] = resp.mecha[0];
			last_status[1] = resp.mecha[1];
		}

		/* Terminate when printing complete */
		if (resp.mecha[0] == D90_MECHA_STATUS_IDLE) {
			if (countdown-- <= 0)
				break;
		}

		if (!wait_for_return && copies <= 1) { /* Copies generated by backend? */
			INFO("Fast return mode enabled.\n");
			break;
		}
	} while(1);

	/* Clean up */
	if (terminate)
		copies = 1;

	INFO("Print complete (%d copies remaining)\n", copies - 1);

	if (copies && --copies) {
		goto top;
	}

skip_duplex:
	return CUPS_BACKEND_OK;
}

static int mitsud90_query_job(struct mitsud90_ctx *ctx, uint16_t jobid,
	struct mitsud90_job_resp *resp)
{
	struct mitsud90_job_query req;
	int ret, num;

	// XXX does this work on W5000?
	req.hdr[0] = 0x1b;
	req.hdr[1] = 0x47;
	req.hdr[2] = 0x44;
	req.hdr[3] = 0x31;
	req.jobid = cpu_to_be16(jobid);

	if ((ret = send_data(ctx->conn,
			     (uint8_t*) &req, sizeof(req))))
		return ret;
	memset(resp, 0, sizeof(*resp));
	ret = read_data(ctx->conn,
			(uint8_t*) resp, sizeof(*resp), &num);

	if (ret < 0)
		return ret;
	if (num != sizeof(*resp)) {
		ERROR("Short Read! (%d/%d)\n", num, (int)sizeof(*resp));
		return 4;
	}

	return CUPS_BACKEND_OK;
}

static int mitsud90_get_jobstatus(struct mitsud90_ctx *ctx, uint16_t jobid)
{
	struct mitsud90_job_resp resp;

	if (mitsud90_query_job(ctx, jobid, &resp))
		return CUPS_BACKEND_FAILED;

	INFO("Job Status:  %04x = %02x/%02x/%04x\n",
	     jobid, resp.unk1, resp.unk2, be16_to_cpu(resp.unk3));

	return CUPS_BACKEND_OK;
}

static int mitsud90_get_media(struct mitsud90_ctx *ctx)
{
	if (mitsud90_query_media(ctx, &ctx->media))
		return CUPS_BACKEND_FAILED;

	INFO("Media Type:  %s (%02x/%02x)\n",
	     mitsu_media_types(ctx->conn->type, ctx->media.media.brand, ctx->media.media.type),
	     ctx->media.media.brand,
	     ctx->media.media.type);
	INFO("Prints Remaining:  %03d/%03d\n",
	     be16_to_cpu(ctx->media.media.remain),
	     be16_to_cpu(ctx->media.media.capacity));

	return CUPS_BACKEND_OK;
}

static int mitsud90_get_status(struct mitsud90_ctx *ctx)
{
	struct mitsud90_status_resp resp;

	if (mitsud90_query_status(ctx, &resp))
		return CUPS_BACKEND_FAILED;

	mitsud90_dump_status(&resp);

	return CUPS_BACKEND_OK;
}

static int mitsud90_get_info(struct mitsud90_ctx *ctx)
{
	uint8_t cmdbuf[26];
	uint8_t cmdlen = 0;
	int ret, num;
	struct mitsud90_info_resp resp;

	cmdbuf[cmdlen++] = 0x1b;
	cmdbuf[cmdlen++] = 0x47;
	cmdbuf[cmdlen++] = 0x44;
	cmdbuf[cmdlen++] = 0x30;
	if (ctx->conn->type != P_MITSU_W5000) {
		cmdbuf[cmdlen++] = 0;
		cmdbuf[cmdlen++] = 0;
	}
	cmdbuf[cmdlen++] = 19;  /* Number of commands */

	cmdbuf[cmdlen++] = COM_STATUS_TYPE_MODEL;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_x02;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_FW_LOADER;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_FW_MAIN;

	cmdbuf[cmdlen++] = COM_STATUS_TYPE_FW_FPGA;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_FW_TBL;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_FW_TAG;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_FW_SATIN;

	cmdbuf[cmdlen++] = COM_STATUS_TYPE_FW_MECH;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_x1e;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_x22;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_JOBID;

	cmdbuf[cmdlen++] = COM_STATUS_TYPE_INKID;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_x2b;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_x2c;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_x65;

	cmdbuf[cmdlen++] = D90_STATUS_TYPE_ISEREN;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_x83;
	cmdbuf[cmdlen++] = D90_STATUS_TYPE_x84;

	if ((ret = send_data(ctx->conn,
			     cmdbuf, cmdlen)))
		return ret;
	memset(&resp, 0, sizeof(resp));

	ret = read_data(ctx->conn,
			(uint8_t*) &resp, sizeof(resp), &num);

	if (ret < 0)
		return ret;
	if (num != sizeof(resp)) {
		ERROR("Short Read! (%d/%d)\n", num, (int)sizeof(resp));
		return CUPS_BACKEND_FAILED;
	}

	/* start dumping output */
	memset(cmdbuf, 0, sizeof(cmdbuf));
	memcpy(cmdbuf, resp.model, sizeof(resp.model));
	INFO("Model: %s\n", (char*)cmdbuf);
	INFO("Serial: %s\n", ctx->serno);
	for (num = 0; num < 7 ; num++) {
		memset(cmdbuf, 0, sizeof(cmdbuf));
		memcpy(cmdbuf, resp.fw_vers[num].version, sizeof(resp.fw_vers[num].version));
		INFO("FW Component %02d: %s (%04x)\n",
		     num, cmdbuf, be16_to_cpu(resp.fw_vers[num].csum));
	}
	INFO("TYPE_02: %02x\n", resp.x02);
	INFO("TYPE_1e: %02x\n", resp.x1e);
	INFO("TYPE_22: %02x %02x\n", resp.x22[0], resp.x22[1]);
	INFO("Job ID: %04x\n", be16_to_cpu(resp.jobid));
	INFO("Ink ID: %02x %02x %02x %02x %02x %02x %02x %02x\n",
	     resp.inkid[0], resp.inkid[1], resp.inkid[2], resp.inkid[3],
	     resp.inkid[4], resp.inkid[5], resp.inkid[6], resp.inkid[7]);
	INFO("TYPE_2b: %02x %02x\n", resp.x2b[0], resp.x2b[1]);
	INFO("TYPE_2c: %02x %02x\n", resp.x2c[0], resp.x2c[1]);

	INFO("TYPE_65:");
	for (num = 0; num < 50 ; num++) {
		DEBUG2(" %02x", resp.x65[num]);
	}
	DEBUG2("\n");
	INFO("iSerial: %s\n", resp.iserial ? "Disabled" : "Enabled");
	INFO("TYPE_83: %02x\n", resp.x83);
	INFO("TYPE_84: %02x\n", resp.x84);

	// XXX what about resume, wait time, "cut limit", sleep time ?

	return CUPS_BACKEND_OK;
}

static int mitsum1_getinfo_dump(struct mitsud90_ctx *ctx, struct mitsum1_getinfo_resp *resp)
{
	struct mitsud90_generic_hdr hdr;

	int ret, num;

	hdr.hdr[0] = 0x1b;
	hdr.hdr[1] = 0x61;
	hdr.hdr[2] = 0x36;
	hdr.getset = 0x36;
	hdr.type[0] = 0x45;
	hdr.type[1] = 0xba;
	hdr.len = cpu_to_be32(0x19c0);
	hdr.index[0] = 0;
	hdr.index[1] = 0;
	hdr.index[2] = 0x06;
	hdr.index[3] = 0x40;
	hdr.term1[0] = 0xff;
	hdr.term1[1] = 0xff;
	hdr.term1[2] = 0xe6;
	hdr.term1[3] = 0x3f;
	hdr.term2[0] = 0xff;
	hdr.term2[1] = 0xff;
	hdr.term2[2] = 0xf9;
	hdr.term2[3] = 0xbf;

	if ((ret = send_data(ctx->conn,
			     (uint8_t*)&hdr, sizeof(hdr))))
		return ret;
	memset(resp, 0, sizeof(*resp));

	ret = read_data(ctx->conn,
			(uint8_t*) resp, sizeof(*resp), &num);

	if (ret < 0)
		return ret;
	if (num != sizeof(*resp)) {
		ERROR("Short Read! (%d/%d)\n", num, (int)sizeof(*resp));
		return CUPS_BACKEND_FAILED;
	}

	return CUPS_BACKEND_OK;
}

static int mitsuw5k_get_info(struct mitsud90_ctx *ctx, struct mitsuw5k_info_resp *resp)
{
	uint8_t cmdbuf[32];
	uint8_t cmdlen = 0;
	int ret, num;

	cmdbuf[cmdlen++] = 0x1b;
	cmdbuf[cmdlen++] = 0x47;
	cmdbuf[cmdlen++] = 0x44;
	cmdbuf[cmdlen++] = 0x30;
	if (ctx->conn->type != P_MITSU_W5000) {
		cmdbuf[cmdlen++] = 0;
		cmdbuf[cmdlen++] = 0;
	}
	cmdbuf[cmdlen++] = 19;  /* Number of commands */

	cmdbuf[cmdlen++] = W5K_STATUS_TYPE_MODEL;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_x02;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_FW_LOADER;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_FW_MAIN;

	cmdbuf[cmdlen++] = COM_STATUS_TYPE_FW_FPGA;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_FW_TBL;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_FW_TAG;
	cmdbuf[cmdlen++] = W5K_STATUS_TYPE_FW_LUT;

	cmdbuf[cmdlen++] = COM_STATUS_TYPE_FW_SATIN;
	cmdbuf[cmdlen++] = W5K_STAUTS_TYPE_x1a;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_x1e;

	cmdbuf[cmdlen++] = COM_STATUS_TYPE_INKID;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_x2b;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_x2c;
	cmdbuf[cmdlen++] = W5K_STATUS_CNT_HEAD;

	cmdbuf[cmdlen++] = W5K_STATUS_CNT_SERVICE;
	cmdbuf[cmdlen++] = W5K_STATUS_CNT_PRINTED;
	cmdbuf[cmdlen++] = W5K_STATUS_CNT_CUTTER;
	cmdbuf[cmdlen++] = W5K_STATUS_CNT_SLITTER;

#if 0
	cmdbuf[cmdlen++] = W5K_STATUS_TYPE_x40;
	cmdbuf[cmdlen++] = W5K_STATUS_TYPE_x45;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_x65;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_x83;
	cmdbuf[cmdlen++] = D90_STATUS_TYPE_x84;
#endif

	if ((ret = send_data(ctx->conn,
			     cmdbuf, cmdlen)))
		return ret;
	memset(resp, 0, sizeof(*resp));

	ret = read_data(ctx->conn,
			(uint8_t*) resp, sizeof(*resp), &num);

	if (ret < 0)
		return ret;
	if (num != sizeof(*resp)) {
		ERROR("Short Read! (%d/%d)\n", num, (int)sizeof(*resp));
		return CUPS_BACKEND_FAILED;
	}

	return CUPS_BACKEND_OK;
}

static int mitsuw5k_dump_info(struct mitsud90_ctx *ctx, struct mitsuw5k_info_resp *resp) {
	uint8_t cmdbuf[26];
	int num;

	/* start dumping output */
	memset(cmdbuf, 0, sizeof(cmdbuf));
	memcpy(cmdbuf, resp->model, sizeof(resp->model));
	INFO("Model: %s\n", (char*)cmdbuf);
	INFO("Serial: %s\n", ctx->serno);
	for (num = 0; num < 7 ; num++) {
		memset(cmdbuf, 0, sizeof(cmdbuf));
		memcpy(cmdbuf, resp->fw_vers[num].version, sizeof(resp->fw_vers[num].version));
		INFO("FW Component %02d: %s (%04x)\n",
		     num, cmdbuf, be16_to_cpu(resp->fw_vers[num].csum));
	}
	INFO("TYPE_02: %02x\n", resp->x02);
	INFO("TYPE_1a: %02x\n", resp->x1a);
	INFO("TYPE_1e: %02x\n", resp->x1e);
	INFO("Ink ID: %02x %02x %02x %02x %02x %02x %02x %02x\n",
	     resp->inkid[0], resp->inkid[1], resp->inkid[2], resp->inkid[3],
	     resp->inkid[4], resp->inkid[5], resp->inkid[6], resp->inkid[7]);
	INFO("TYPE_2b: %02x %02x %02x %02x %02x %02x %02x %02x\n",
	     resp->x2b[0], resp->x2b[1], resp->x2b[2], resp->x2b[3],
	     resp->x2b[4], resp->x2b[5], resp->x2b[6], resp->x2b[7]);
	INFO("TYPE_2c: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
	     resp->x2c[0], resp->x2c[1], resp->x2c[2], resp->x2c[3],
	     resp->x2c[4], resp->x2c[5], resp->x2c[6], resp->x2c[7],
	     resp->x2c[8], resp->x2c[9], resp->x2c[10], resp->x2c[11]);

	INFO("Head Count: %u\n", be32_to_cpu(resp->cnt_head));
	INFO("Service Count: %u\n", be32_to_cpu(resp->cnt_service));
	INFO("Print Count: %u\n", be32_to_cpu(resp->cnt_printed));
	INFO("Cutter Count: %u\n", be32_to_cpu(resp->cnt_cutter));
	INFO("Slitter Count: %u\n", be32_to_cpu(resp->cnt_slitter));

#if 0
	INFO("TYPE_40: %08x\n", be32_to_cpu(resp->x40));
	INFO("TYPE_45: %08x\n", be32_to_cpu(resp->x45));
	INFO("TYPE_65:");
	for (num = 0; num < 54 ; num++) {
		DEBUG2(" %02x", resp->x65[num]);
	}
	DEBUG2("\n");
	INFO("TYPE_83: %02x %02x\n", resp->x83[0], resp->x83[1]);
	INFO("TYPE_84: %02x %02x\n", resp->x84[1], resp->x84[2]);
#endif

	// XXX what about resume, wait time, "cut limit", sleep time ?

	return CUPS_BACKEND_OK;
}

static int mitsum1_get_info(struct mitsud90_ctx *ctx)
{
	uint8_t cmdbuf[25];
	uint8_t cmdlen = 0;
	int ret, num;
	struct mitsum1_info_resp resp;

	cmdbuf[cmdlen++] = 0x1b;
	cmdbuf[cmdlen++] = 0x47;
	cmdbuf[cmdlen++] = 0x44;
	cmdbuf[cmdlen++] = 0x30;
	if (ctx->conn->type != P_MITSU_W5000) {
		cmdbuf[cmdlen++] = 0;
		cmdbuf[cmdlen++] = 0;
	}
	cmdbuf[cmdlen++] = 18;  /* Number of commands */

	cmdbuf[cmdlen++] = COM_STATUS_TYPE_MODEL;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_x02;
	cmdbuf[cmdlen++] = CM1_STATUS_TYPE_FW_0a;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_FW_LOADER;

	cmdbuf[cmdlen++] = COM_STATUS_TYPE_FW_MAIN;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_FW_FPGA;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_FW_TBL;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_FW_TAG;

	cmdbuf[cmdlen++] = COM_STATUS_TYPE_FW_SATIN;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_FW_MECH;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_x1e;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_x22;

	cmdbuf[cmdlen++] = COM_STATUS_TYPE_JOBID;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_INKID;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_x2b;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_x2c;

	cmdbuf[cmdlen++] = COM_STATUS_TYPE_x65;
	cmdbuf[cmdlen++] = COM_STATUS_TYPE_x83;

	if ((ret = send_data(ctx->conn,
			     cmdbuf, cmdlen)))
		return ret;
	memset(&resp, 0, sizeof(resp));

	ret = read_data(ctx->conn,
			(uint8_t*) &resp, sizeof(resp), &num);

	if (ret < 0)
		return ret;
	if (num != sizeof(resp)) {
		ERROR("Short Read! (%d/%d)\n", num, (int)sizeof(resp));
		return 4;
	}

	/* start dumping output */
	memset(cmdbuf, 0, sizeof(cmdbuf));
	memcpy(cmdbuf, resp.model, sizeof(resp.model));
	INFO("Model: %s\n", (char*)cmdbuf);
	INFO("Serial: %s\n", ctx->serno);
	for (num = 0; num < 8 ; num++) {
		memset(cmdbuf, 0, sizeof(cmdbuf));
		memcpy(cmdbuf, resp.fw_vers[num].version, sizeof(resp.fw_vers[num].version));
		INFO("FW Component %02d: %s (%04x)\n",
		     num, cmdbuf, be16_to_cpu(resp.fw_vers[num].csum));
	}
	INFO("TYPE_02: %02x\n", resp.x02);
	INFO("TYPE_1e: %02x\n", resp.x1e);
	INFO("TYPE_22: %02x %02x\n", resp.x22[0], resp.x22[1]);
	INFO("Job ID: %04x\n", be16_to_cpu(resp.jobid));
	INFO("Ink ID: %02x %02x %02x %02x %02x %02x %02x %02x\n",
	     resp.inkid[0], resp.inkid[1], resp.inkid[2], resp.inkid[3],
	     resp.inkid[4], resp.inkid[5], resp.inkid[6], resp.inkid[7]);
	INFO("TYPE_2b: %02x %02x\n", resp.x2b[0], resp.x2b[1]);
	INFO("TYPE_2c: %02x %02x\n", resp.x2c[0], resp.x2c[1]);

	INFO("TYPE_65:");
	for (num = 0; num < 50 ; num++) {
		DEBUG2(" %02x", resp.x65[num]);
	}
	DEBUG2("\n");
	INFO("TYPE_83: %02x\n", resp.x83);

	// XXX what about resume, wait time, "cut limit", sleep time ?

	uint32_t cntr = 0;
//	mitsud90_get_counter(ctx, 0, &cntr);
//	INFO("Head Prints: %d\n", cntr);
	mitsud90_get_counter(ctx, 1, &cntr);
	INFO("Cutter count: %d\n", cntr);

	struct mitsum1_getinfo_resp resp2;
	mitsum1_getinfo_dump(ctx, &resp2);
	cntr = le32_to_cpu(resp2.cnt_prints);
	INFO("Head Prints: %d\n", cntr);

	return CUPS_BACKEND_OK;
}

static int mitsud90_dumpall(struct mitsud90_ctx *ctx)
{
	int i;
	uint8_t cmdbuf[8];
	uint8_t cmdlen = 0;
	uint8_t buf[256];

	cmdbuf[cmdlen++] = 0x1b;
	cmdbuf[cmdlen++] = 0x47;
	cmdbuf[cmdlen++] = 0x44;
	cmdbuf[cmdlen++] = 0x30;
	if (ctx->conn->type != P_MITSU_W5000) {
		cmdbuf[cmdlen++] = 0;
		cmdbuf[cmdlen++] = 0;
	}
	cmdbuf[cmdlen++] = 1;  /* Number of commands */

	for (i = 0 ; i < 256 ; i++) {
		int num, ret;

		cmdbuf[cmdlen] = i;

		if ((ret = send_data(ctx->conn,
				     cmdbuf, cmdlen + 1)))
			return ret;
		memset(buf, 0, sizeof(buf));

		ret = read_data(ctx->conn,
				buf, sizeof(buf), &num);

		if (ret < 0)
			continue;

		if (num > 4) {
			DEBUG("TYPE %02x LEN: %d\n", i, num - 4);
			DEBUG("<--");
			for (ret = 4; ret < num ; ret ++) {
				DEBUG2(" %02x", buf[ret]);
			}
			DEBUG2("\n");
		}
	}

	return CUPS_BACKEND_OK;
}

static int mitsud90_test_print(struct mitsud90_ctx *ctx, int type)
{
	uint8_t cmdbuf[16];
	int ret, num = 0;
	uint8_t resp[256];

	// XXX does this work on W5000?

	/* Send Test ON */
	memset(cmdbuf, 0, 8);
	cmdbuf[0] = 0x1b;
	cmdbuf[1] = 0x76;
	cmdbuf[2] = 0x54;
	cmdbuf[3] = 0x45;
	cmdbuf[4] = 0x53;
	cmdbuf[5] = 0x54;
	cmdbuf[6] = 0x4f;
	cmdbuf[7] = 0x4e;
	if ((ret = send_data(ctx->conn,
			     cmdbuf, 8)))
		return ret;

	memset(resp, 0, sizeof(resp));

	ret = read_data(ctx->conn,
			resp, sizeof(resp), &num);  // always e4 44 4f 4e 45

	if (ret) return ret;

	/* Send Test print. */
	memset(cmdbuf, 0x00, 16);
	cmdbuf[0] = 0x1b;
	cmdbuf[1] = 0x61;
	cmdbuf[2] = 0x36;
	cmdbuf[3] = 0x31;
	cmdbuf[7] = 0x02;

	switch(type) {
	default:
	case 0: /* Test Print */
		cmdbuf[15] = 0x01;
		break;
	case 1: /* Solid Black */
		cmdbuf[4] = 0x02;
		cmdbuf[9] = 0xFF;
		cmdbuf[15] = 0x01;
		break;
	case 2: /* Solid Gray */
		cmdbuf[4] = 0x02;
		cmdbuf[9] = 0x80;
		cmdbuf[15] = 0x01;
		break;
	case 3: /* Head Pattern */
		cmdbuf[4] = 0x01;
		cmdbuf[10] = 0x01;
		cmdbuf[15] = 0x01;
		break;
	case 4: /* Color Bar */
		cmdbuf[4] = 0x04;
		cmdbuf[15] = 0x01;
		break;
	case 5: /* Vertical Alignment */
		cmdbuf[4] = 0x02;
		cmdbuf[9] = 0x80;
		cmdbuf[15] = 0x02;
		break;
	case 6: /* Horizontal Alignment; Grey Cross */
		cmdbuf[4] = 0x02;
		cmdbuf[9] = 0x80;
		cmdbuf[15] = 0x01;
		break;
	case 7: /* Solid Gray 1 */
		cmdbuf[4] = 0x02;
		cmdbuf[9] = 0x80;
		cmdbuf[10] = 0x01;
		cmdbuf[15] = 0x01;
		break;
	case 8: /* Solid Gray 2 */
		cmdbuf[4] = 0x02;
		cmdbuf[9] = 0x80;
		cmdbuf[10] = 0x02;
		cmdbuf[15] = 0x01;
		break;
	case 9: /* Solid Gray 3 */
		cmdbuf[4] = 0x02;
		cmdbuf[9] = 0x80;
		cmdbuf[10] = 0x04;
		cmdbuf[15] = 0x01;
		break;
	}
	if ((ret = send_data(ctx->conn,
			     cmdbuf, 16)))
		return ret;

	ret = read_data(ctx->conn,
			resp, sizeof(resp), &num); /* Get 5 back */

	return ret;
}

static int mitsud90_query_serno(struct dyesub_connection *conn, char *buf, int buf_len)
{
	struct mitsud90_ctx ctx = {
		.conn = conn,
	};

	int ret;

	UNUSED(buf_len);

	if (conn->type == P_MITSU_W5000)
		ret = mitsuw5k_get_serno(&ctx);
	else
		ret = mitsud90_get_serno(&ctx);

	/* Copy it */
	memcpy(buf, ctx.serno, sizeof(ctx.serno));

	return ret;
}
static int mitsud90_set_iserial(struct mitsud90_ctx *ctx, uint8_t enabled)
{
	uint8_t cmdbuf[23];
	uint8_t cmdlen = 0;
	int ret, num;

	// XXX DOES NOT WORK ON W5K

	enabled = (enabled) ? 0: 0x80;

	/* Send Parameter.. */
	cmdbuf[cmdlen++] = 0x1b;
	cmdbuf[cmdlen++] = 0x31;
	cmdbuf[cmdlen++] = 0x36;
	cmdbuf[cmdlen++] = 0x30;
	cmdbuf[cmdlen++] = 0x41;
	cmdbuf[cmdlen++] = 0xbe;
	cmdbuf[cmdlen++] = 0x00;
	cmdbuf[cmdlen++] = 0x00;

	cmdbuf[cmdlen++] = 0x00;
	cmdbuf[cmdlen++] = 0x01;
	cmdbuf[cmdlen++] = 0x00;
	cmdbuf[cmdlen++] = 0x00;
	cmdbuf[cmdlen++] = 0x00;
	cmdbuf[cmdlen++] = 0x11;
	cmdbuf[cmdlen++] = 0xff;
	cmdbuf[cmdlen++] = 0xff;

	cmdbuf[cmdlen++] = 0xff;
	cmdbuf[cmdlen++] = 0xfe;
	cmdbuf[cmdlen++] = 0xff;
	cmdbuf[cmdlen++] = 0xff;
	cmdbuf[cmdlen++] = 0xff;
	cmdbuf[cmdlen++] = 0xee;
	cmdbuf[cmdlen++] = enabled;

	if ((ret = send_data(ctx->conn,
			     cmdbuf, cmdlen)))
		return ret;

	ret = read_data(ctx->conn,
			cmdbuf, sizeof(cmdbuf), &num);

	return ret;
}

static int mitsud90_set_sleeptime(struct mitsud90_ctx *ctx, uint16_t time)
{
	uint8_t cmdbuf[24];
	uint8_t cmdlen = 0;
	int ret;

	/* 255 minutes max, according to RE work */
	if (time > 255)
		time = 255;

	// XXX does this work on W5000?

	/* Send Parameter.. */
	cmdbuf[cmdlen++] = 0x1b;
	cmdbuf[cmdlen++] = 0x31;
	cmdbuf[cmdlen++] = 0x36;
	cmdbuf[cmdlen++] = 0x30;
	cmdbuf[cmdlen++] = 0x41;
	cmdbuf[cmdlen++] = 0xbe;
	cmdbuf[cmdlen++] = 0x00;
	cmdbuf[cmdlen++] = 0x00;

	cmdbuf[cmdlen++] = 0x00;
	cmdbuf[cmdlen++] = 0x02;
	cmdbuf[cmdlen++] = 0x00;
	cmdbuf[cmdlen++] = 0x00;
	cmdbuf[cmdlen++] = 0x05;
	cmdbuf[cmdlen++] = 0x02;
	cmdbuf[cmdlen++] = 0xff;
	cmdbuf[cmdlen++] = 0xff;

	cmdbuf[cmdlen++] = 0xff;
	cmdbuf[cmdlen++] = 0xfd;
	cmdbuf[cmdlen++] = 0xff;
	cmdbuf[cmdlen++] = 0xff;
	cmdbuf[cmdlen++] = 0xfa;
	cmdbuf[cmdlen++] = 0xff;
	cmdbuf[cmdlen++] = (time >> 8) & 0xff;
	cmdbuf[cmdlen++] = time & 0xff;

	if ((ret = send_data(ctx->conn,
			     cmdbuf, cmdlen)))
		return ret;

	/* No response */

	return CUPS_BACKEND_OK;
}

static void mitsud90_cmdline(void)
{
	DEBUG("\t\t[ -i ]           # Query printer info\n");
	DEBUG("\t\t[ -j jobid ]     # Query job status\n");
	DEBUG("\t\t[ -k time ]      # Set sleep time in minutes\n");
	DEBUG("\t\t[ -m ]           # Query printer media\n");
	DEBUG("\t\t[ -s ]           # Query printer status\n");
	DEBUG("\t\t[ -x 0|1 ]       # Enable/disable iSerial reporting (D90 only)\n");
//	DEBUG("\t\t[ -T 0-9 ]       # Test print\n");
//	DEBUG("\t\t[ -Z ]           # Dump all parameters\n");
}

static int mitsud90_cmdline_arg(void *vctx, int argc, char **argv)
{
	struct mitsud90_ctx *ctx = vctx;
	int i, j = 0;

	if (!ctx)
		return -1;

	while ((i = getopt(argc, argv, GETOPT_LIST_GLOBAL "ij:k:msT:x:Z")) >= 0) {
		switch(i) {
		GETOPT_PROCESS_GLOBAL
		case 'i':
			switch (ctx->conn->type) {
			case P_MITSU_M1:
				j = mitsum1_get_info(ctx);
				break;
			case P_MITSU_W5000: {
				struct mitsuw5k_info_resp resp;
				j = mitsuw5k_get_info(ctx, &resp);
				if (!j)
					mitsuw5k_dump_info(ctx, &resp);
				break;
			}
			case P_MITSU_D90:
			default:
				j = mitsud90_get_info(ctx);
				break;
			}
			break;
		case 'j':
			j = mitsud90_get_jobstatus(ctx, atoi(optarg));
			break;
		case 'k':
			j = mitsud90_set_sleeptime(ctx, atoi(optarg));
			break;
		case 'm':
			j = mitsud90_get_media(ctx);
			break;
		case 's':
			j = mitsud90_get_status(ctx);
			break;
		case 'T':
			j = mitsud90_test_print(ctx, atoi(optarg));
			break;
		case 'x':
			if (ctx->conn->type == P_MITSU_D90)
				j = mitsud90_set_iserial(ctx, atoi(optarg));
			break;
		case 'Z':
			j = mitsud90_dumpall(ctx);
			break;
		default:
			break;  /* Ignore completely */
		}

		if (j) return j;
	}

	return CUPS_BACKEND_OK;
}

static int mitsud90_query_markers(void *vctx, struct marker **markers, int *count)
{
	struct mitsud90_ctx *ctx = vctx;

	if (markers) *markers = &ctx->marker;
	if (count) *count = 1;

	if (mitsud90_query_media(ctx, &ctx->media))
		return CUPS_BACKEND_FAILED;

	ctx->marker.levelnow = be16_to_cpu(ctx->media.media.remain);

	return CUPS_BACKEND_OK;
}

static int mitsud90_query_stats(void *vctx, struct printerstats *stats)
{
	struct mitsud90_ctx *ctx = vctx;
	struct mitsud90_status_resp resp;

	if (mitsud90_query_markers(ctx, NULL, NULL))
		return CUPS_BACKEND_FAILED;
	if (mitsud90_query_status(ctx, &resp))
		return CUPS_BACKEND_FAILED;

	switch (ctx->conn->type) {
	case P_MITSU_D90:
		stats->mfg = "Mitsubishi";
		stats->model = "CP-D90 family";
		break;
	case P_MITSU_M1:
		stats->mfg = "Mitsubishi";
		stats->model = "CP-M1 family";
		break;
	case P_MITSU_W5000:
		stats->mfg = "Mitsubishi";
		stats->model = "CP-W5000 family";
		break;
	case P_FUJI_ASK500:
		stats->mfg = "Fujifilm";
		stats->model = "AK500";
		break;
	default:
		stats->model = "Unknown!";
		stats->mfg = "Unknown!";
		break;
	}

	switch (ctx->conn->type) {
	case P_MITSU_W5000: {
		struct mitsuw5k_info_resp resp2;
		mitsuw5k_get_info(ctx, &resp2);
		stats->cnt_life[0] = be32_to_cpu(resp2.cnt_printed);
		break;
	}
	case P_MITSU_M1:
	case P_FUJI_ASK500: {
		struct mitsum1_getinfo_resp resp2;
		mitsum1_getinfo_dump(ctx, &resp2);
		stats->cnt_life[0] = le32_to_cpu(resp2.cnt_prints);
	}
	default:
// XXX should work for D90
//	uint32_t head = 0;
//	mitsud90_get_counter(ctx, 0, &head);
//	stats->cnt_life[0] = head;
		break;
	}

	stats->serial = ctx->serno;
	stats->fwver = ctx->fwver_main;

	stats->decks = 1;

	stats->name[0] = "Roll";
	if (resp.code[0] != D90_ERROR_STATUS_OK)
		stats->status[0] = strdup(mitsud90_error_codes(resp.code));
	else if (resp.code[1] & D90_ERROR_STATUS_OK_WARMING ||
		 resp.temp & D90_ERROR_STATUS_OK_WARMING)
		stats->status[0] = strdup("Warming up");
	else if (resp.code[1] & D90_ERROR_STATUS_OK_COOLING ||
		 resp.temp & D90_ERROR_STATUS_OK_COOLING)
		stats->status[0] = strdup("Cooling down");
	else
		stats->status[0] = strdup(mitsud90_mecha_statuses(resp.mecha));

	stats->mediatype[0] = ctx->marker.name;
	stats->levelmax[0] = ctx->marker.levelmax;
	stats->levelnow[0] = ctx->marker.levelnow;

	return CUPS_BACKEND_OK;
}

static const char *mitsud90_prefixes[] = {
	"mitsud90", /* Family Name */
	NULL
};

static const struct device_id mitsud90_devices[] = {
	{ 0x06d3, 0x3b60, P_MITSU_D90, NULL, "mitsubishi-d90dw"},
	{ 0x06d3, 0x3b80, P_MITSU_M1, NULL, "mitsubishi-cpm1"},
	{ 0x06d3, 0x3b80, P_MITSU_M1, NULL, "mitsubishi-cpm15"}, // Duplicate for the M15
	{ 0x06d3, 0x3b50, P_MITSU_W5000, NULL, "mitsubishi-cpw5000"},
//	{ 0x04cb, 0x1234, P_FUJI_ASK500, NULL, "fujifilm-ask500"},
	{ 0, 0, 0, NULL, NULL}
};

/* Exported */
const struct dyesub_backend mitsud90_backend = {
	.name = "Mitsubishi CP-D90/CP-M1/CP-W5000",
	.version = "0.52"  " (lib " LIBMITSU_VER ")",
	.flags = BACKEND_FLAG_DUMMYPRINT,
	.uri_prefixes = mitsud90_prefixes,
	.devices = mitsud90_devices,
	.cmdline_arg = mitsud90_cmdline_arg,
	.cmdline_usage = mitsud90_cmdline,
	.init = mitsud90_init,
	.attach = mitsud90_attach,
	.teardown = mitsud90_teardown,
	.cleanup_job = mitsud90_cleanup_job,
	.read_parse = mitsud90_read_parse,
	.main_loop = mitsud90_main_loop,
	.query_serno = mitsud90_query_serno,
	.query_markers = mitsud90_query_markers,
	.query_stats = mitsud90_query_stats,
};

/* ToDo:

     * consolidate M1 vs D90 vs W5000 info query/dump more efficiently?
     * better job control (job id, active job, etc?)
     * Print counters on M1 and D90 (need to validate the D90 in particular)
     * sleep and waking up
     * cut limits?
     * Validate Fujifilm ASK500 support & spool format (likely moot now)
     * Validate Panorama mode on D90
     * W5000 validate media type vs loaded LUT, if possible?
        Non-HG media:
          e0 07 00 00 00 eb e8 de bd / 18 a1  (00192 061 / 15962 078) LUT SLTAX270
	HG media:
	  ??? LUT SLTA7180
     * Job combining (esp for W5000; M1 too maybe?)

 */

/*
   Mitsubishi CP-D90DW data format

   All multi-byte values are BIG endian

 [[ PAGE HEADER ]]

   1b 53 50 30 00 33 XX XX  YY YY TT 00 00 01 MM NN  XX XX == COLS, YY XX ROWS (BE)
   ?? ?? ?? ?? ?? ?? ?? ??  ?? ?? ?? ?? 00 00 00 00  NN == num of cuts, ?? see below
   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  MM == 0 for no margin cut, 1 for margin cut
   QQ RR SS HH VV 00 00 00  00 00 ZZ 00 JJ II 09 7c  QQ == 02 matte (D90) or 03 (M1), 00 glossy,
   09 4c 00 00 02 58 00 0c  00 06 00 00 00 00 00 00  RR == 00 auto, (D90: 03 == fine, 02 == superfine), (M1: 05 == Fast)
   Z0 Z1 Z2 00 00 00 00 00  00 00 00 00 00 00 00 00  SS == 00 colorcorr, 01 == none (01 on M1 or D90 if LUT done in driver)
                                                     HH/VV sharpening for Horiz/Vert, 0-8, 0 is off, 4 is normal (always 00 on M1)
                                                     TT is waittime (100 max, always 100 on D90)
						     ZZ is 0x02 on M1, D90 see PANORAMA
						     Z0 is 0x01 (M1 windows) (00 Linux and d90 UNK!)
						     Z1 is RGB Rate (M1)
						     Z2 is OP Rate (M1)
  [pad to 512b]

                normal  == rows  00  00 00 00 00  00 00 00 00
                4x6div2 == 1226  01  02 65 01 00  00 00 00 00
                8x6div2 == 2488  01  04 be 00 00  00 00 00 00

		    guesses based on SDK docs:

		9x6div2 == 2728  01  05 36 01 00  00 00 00 00  00 00 00 00
		9x6div3 == 2724  02  03 90 01 00  07 14 00 00  00 00 00 00
		9x6div4 == 2628  03  02 97 01 00  05 22 00 00  07 ad 00 00

    PANORAMA [ZZ 00 03 03] onwards, only shows in 8x20" PANORAMA prints.  Assume 2" overlap.
    ZZ == 00 (normal) or 01 (panorama)
    JJ == 02 03 (num of panorama panels)
    II == 01 02 03 (which panel # in panorama!)
    [02 58] == 600, aka 2" * 300dpi?
    [09 4c] == 2380  (48 less than 8 size? (trim length on ends?)
    [09 7c] == 2428  (ie 8" print)

     (6x20 == 1852x6036)
     (6x14 == 1852x4232)

     3*8" panels == 2428*3=7284.  -6036 = 1248.  /2 = 624 (0x270)

 [[ DATA PLANE HEADER ]]

   1b 5a 54 01 00 09 00 00  00 00 CC CC RR RR 00 00
   00 00 00 00 LC LC LR LR
   ...
   [pad to 512b]

   CC CC cols (BE)
   RR RR rows (BE)
   LC LC lamination columns (BE, M1 only, same as cols)
   LR LR lamination rows (BE, M1 only, rows + 12d )

   D90 family:
    data is *RGB* packed, @ 8bpp.  No padding to 512b!
   M1 family:
    data is *RGB* packed, @16bpp, LITTLE ENDIAN.  No padding to 512b!
    optional matte data is 8bpp, follows immediately.

 [[FOOTER]]

   1b 42 51 31 00 TT                  ## TT == secs to wait for second print, 0xff disables?

 ****************************************************

Comms Protocol for D90, CP-M1, and CP-W5000 families

 [[ STATUS QUERIES ]]

-> 1b 47 44 30 00 00 XX [ A1, A2, A3, ... ]   ** D90 & M1
-> 1b 47 44 30 XX [ A1, A2, A3, ... ]         ** W5000
<- e4 47 44 30 [ R1, R2, R3, ... ]

  XX is the number of items to query.  Each item has a unique code and a
  fixed response length.  item's repsonse is concatenated onto the response.

  For item codes and responses, see *_STATUS_TYPE_* definitions above.

 [[ GENERIC GET/SET ]]

-> 1b 61 36 QQ T1 T2 LL LL   QQ == 0x30 (set) 0x36 (get)
   LL LL 00 00 VV VV ff ff   LL == length (32-bit BE)
   M1 M1 ff ff M2 M2         T1 = type1 (41, 45, others?)
<- e4 61 36 QQ TT TT 00 00   T2 = type2 (be, ba, 00, others?)
   LL LL 00 00 VV VV ff ff   VV VV = index/variable?
   M1 M1 ff ff M2 M2 ?? ??   ?? == data (length LL)

  -----------------------------------------------
   T1 T2  LL LL  VV VV  M1 M1  M2 M2   Meaning

   41 be  00 01  00 10  ff fe  ff ef   34v Adjustment (0x00->0xff)
   41 be  00 01  00 11  ff fe  ff ee   USB iSerial on/off  (0x80 off, 0x00 on, D90 only?)
   41 be  00 06  00 30  ff f9  ff cf   Ascii serial number
   45 00  00 01  05 05  ff fe  fa f8   Wait time (seconds)
   45 ba  00 06  06 00  ff f9  fc ff   M1 motor current (6 values)
   45 ba  00 04  07 00  ff fb  f8 ff   GetTotalPrintCount2 (? Not M1)
   45 ba  00 04  07 10  ff fb  f8 ef   GetTotalPrintCount  (? Not M1)
   45 ba  00 02  02 40  ff fd  fd bf   Density (6800d -> 9000d)
   45 ba  00 01  02 47  ff fe  fd 87   Horizontal Position (0x00->0xff)
   45 ba  19 c0  06 40  e6 3f  f9 bf   "Read Info" (BIG payload!) (M1)
   45 ba  00 06  03 00  ff f9  fc ff   M1 Adj (F/SF/UF, two bytes each?)
   45 ba  00 06  03 10  ff f9  fc ef   Vertical Position (A/B/C combined)
   45 ba  00 02  03 16  ff fd  fc e9   Feed (default 43402d)
   45 ba  00 10  04 10  ff ef  fb ef   M3 Adj (unknown value)
   45 ba  00 02  05 02  ff fd  fa fd   Sleep Time          (LE value)
   45 ba  00 01  05 04  ff fe  fa fb   ???    on/off
   45 ba  00 01  05 06  ff fe  fa f9   Resume on/off (0x80 off, 0x00 on)
   45 ba  00 01  05 07  ff fe  fa f8   Cutter on/off (0x80 off, 0x00 on)
   45 ba  00 04  06 40  ff fe  f9 bf   Head Count   (? Not M1)
   45 ba  00 04  06 44  ff fb  f9 bb   Cutter Count (LE!)
   45 ba  14 00  0c 00  eb ff  f3 ff   Error History (BIG payload) (d90?)

 [[ JOB STATUS QUERY ?? ]]

-> 1b 47 44 31 00 00 JJ JJ  Jobid?
<- e4 47 44 31 XX YY ZZ ZZ  No idea... maybe remaining prints?

 [[ WAKE UP PRINTER ]]

-> 1b 45 57 55

 [[ SANITY CHECK PRINT ARGUMENTS / MEM CHECK ]]

-> 1b 47 44 33 00 33 07 3c  04 ca 64 00 00 01 00 00
   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
   00 00 00 04 04 00 00 00  00 00 00 00 00 00 00 00
   [[ pad to 512 ]]

   ... 07 3c onwards is the same as main payload header.

<- e4 47 44 43 XX YY

   ... possibly the same as the D70's "memorystatus"
       XX == 00 size ok, 01 bad size, ff out of range
       YY == 00 memory ok, 01 memory full, 02 driver setting, ff out of range

 [[ UNKNOWN (seen in SDK; alt footer?) ]]

   1b 42 61 32 00 00

 [[ UNKNOWN (seen in SDK) ]]

   1b 44 43 41  4e 43 45 4c  00 00 00 00      : \ESC D CANCEL

 [[ UNKNOWON (seen in SDK) ]]

   1b 42 51 32 00 ff       [ Footer of some sort ? ]

 ALSO SEEN:

   1b 61 36 39 43 00   "AdjustColSCmd"
   1b 61 36 37 39 43   "GetColSCmd"  (12 len payload)
   1b 6a 30 71 31 31 42 38 "SetM1AdjCmd"
   1b 6a 36 34 31 00   "GetM1AdjCmd"
   1b 6a 31 32 51 30 38 30 30 30 30 31 "M1AdjSolidGreyCmd"  ???
   1b 61 36 34 50 00   "PaperSensorAdjCmd"
   1b 61 36 37 34 50   "PaperSensorGetCmd" (24 len payload)
   1b 61 36 36 45 ba   Read EEProm  (16 byte cmd payload, 0x8000 max len)
   1b 61 36 30 45 ba   Write EEProm
   1b 47 44 30 00 00 01 65  Read Sensors (streams?)

 [ request x65 examples on the D90: ]

   ac 80 00 01 bb b8 fe 48 05 13 5d 9c 00 33 00 00  00 00 00 00 00 00 00 00 00 00 02 39 00 00 00 00  03 13 00 02 10 40 00 00 00 00 00 00 05 80 00 3a  00 00
   aa 79 00 01 bb b7 fe 47 05 13 5d 9c 01 2f 00 68  00 00 00 00 00 00 00 00 00 00 02 08 00 00 00 00  03 14 00 02 10 40 00 00 00 00 00 00 05 80 00 3a  00 00
 [ power cycle ]
   a3 5d 00 01 ba ba fe 43 04 13 5d 9c 00 00 00 00  00 00 00 00 00 00 00 00 00 00 02 0c 00 00 00 00  03 0f 00 03 10 40 00 00 00 00 00 00 05 80 00 3a  00 00
   a3 5d 00 01 ba ba fe 42 04 13 5d 9c 01 08 00 87  00 00 00 00 00 00 00 00 00 00 01 e5 00 00 00 00  03 0f 00 03 10 40 00 00 00 00 00 00 05 80 00 3a  00 00
   a2 5d 00 01 ba ba fe 42 06 13 5d 9c 01 08 00 87  00 00 00 00 00 00 00 00 00 00 01 d1 00 00 00 00  03 0f 00 03 10 40 00 00 00 00 00 00 05 80 00 3a  00 00
 [ power cycle ]
   a2 5c 00 01 ba ba fe 42 06 13 5d 9c 00 00 00 00  00 00 00 00 00 00 00 00 00 00 01 e0 00 00 00 00  03 0f 00 03 10 40 00 00 00 00 00 00 05 80 00 3a  00 00
   a2 5d 00 01 ba ba fe 41 04 13 5d 9c 01 08 00 89  00 00 00 00 00 00 00 00 00 00 01 c9 00 00 00 00  03 0f 00 03 10 40 00 00 00 00 00 00 05 80 00 3a  00 00

 [ x65 on the M1 ]
   00 00 01 f2 00 07 00 00 00 0f 00 a7 02 9f 03 91  00 00 00 00 00 00 02 36 00 07 03 ff 02 07 03 ff  03 4c 00 01 10 00 00 00 00 00 00 00 05 80 00 24  04 00

   00 00 00 f3 00 00 00 00 00 0f 00 27 01 8a 00 00  00 00 00 00 00 00 02 3b 00 07 03 fe 02 23 03 ff  03 46 00 01 10 00 00 00 00 00 00 00 05 80 00 24  04 00
   00 00 01 f3 00 00 00 00 00 0f 00 a7 02 48 03 6b  00 00 00 00 00 00 02 2d 00 08 03 fd 01 bf 03 ff  03 46 00 01 10 00 00 00 00 00 00 00 05 80 00 24  00 00

  D90 Panorama data table files ("CP90PAN??.dat")

  struct d90_panodata {   // All fields are LE
    uint32_t header;          // @0     0x00000007 (ie number of ymc tuple useds)
    uint32_t [3][16] table1;  // @4     YMC values
    uint32_t pad;             // @192   0x00000000
    uint32_t header2;         // @196   0x00000011  (ie number of bgr tuples used)
    uint32_t [3][17] table2;  // @200   BGR values
    double   table3[600][184] // @408   TBD
    double   unk[]            // @110808 TBD
    uint8_t  footer[8]        // @71208408 "PA17424a"
  };

    -- Table 3 seems to be a set of 600 row blocks  (1 per overlap row?)
       184 == 92 sets of L/R coeffients?
       ...30ea for Y/M/C, with 2 left over?

 */
