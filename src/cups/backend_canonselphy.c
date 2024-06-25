/*
 *   Canon SELPHY ES/CP series CUPS backend
 *
 *   (c) 2007-2024 Solomon Peachy <pizza@shaftnet.org>
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

#define BACKEND canonselphy_backend

#include "backend_common.h"

#define P_ES40_CP790 (P_END + 1) // used for detection only

#define READBACK_LEN 12

struct printer_data {
	int  type;  /* P_??? */
	const char *model; /* eg "SELPHY ES1" */
	uint16_t init_length;
	uint16_t  foot_length;
	int16_t init_readback[READBACK_LEN];
	int16_t ready_y_readback[READBACK_LEN];
	int16_t ready_m_readback[READBACK_LEN];
	int16_t ready_c_readback[READBACK_LEN];
	int16_t done_c_readback[READBACK_LEN];
	uint8_t clear_error[READBACK_LEN];
	uint8_t clear_error_len;
	int16_t paper_codes[256];
	int8_t  pgcode_offset;  /* Offset into printjob for paper type */
	int8_t  paper_code_offset; /* Offset in readback for paper type */
	int8_t  paper_code_offset2; /* Offset in readback for paper type (2nd) */
	uint8_t (*error_detect)(const uint8_t *rdbuf);
	const char    *(*pgcode_names)(const uint8_t *rdbuf, const struct printer_data *printer, int *numtype);
};

static const char *generic_pgcode_names(const uint8_t *rdbuf, const struct printer_data *printer, int *numtype)
{
	uint8_t pgcode = 0, pgcode2 = 0;

	if (printer->paper_code_offset != -1)
		pgcode = rdbuf[printer->paper_code_offset];
	if (printer->paper_code_offset2 != -1)
		pgcode2 = rdbuf[printer->paper_code_offset2];

	*numtype = pgcode & 0xf;

	switch(pgcode & 0xf) {
	case 0x01: return "P";
	case 0x02: return "L";
	case 0x03: return pgcode2 ? "Cl" : "C";
	case 0x04: return "W";
	case 0x0f: return "None";
	default: return "Unknown";
	}
}

static uint8_t es1_error_detect(const uint8_t *rdbuf)
{
	if (rdbuf[1] == 0x01) {
		if (rdbuf[9] == 0x00)
			ERROR("Cover open!\n");
		else
			ERROR("Unknown error %02x\n", rdbuf[9]);
		return 1;
	} else if (rdbuf[4] == 0x01 && rdbuf[5] == 0xff &&
		   rdbuf[6] == 0xff && rdbuf[7] == 0xff) {
		ERROR("No media loaded!\n");
		return 1;
	} else if (rdbuf[0] == 0x0f) {
		ERROR("Out of media!\n");
		return 1;
	}

	return CUPS_BACKEND_OK;
}

static uint8_t es2_error_detect(const uint8_t *rdbuf)
{
	if (rdbuf[0] == 0x16 &&
	    rdbuf[1] == 0x01) {
		ERROR("Printer cover open!\n");
		return 1;
	}

	if (rdbuf[0] == 0x02 &&
	    rdbuf[4] == 0x05 &&
	    rdbuf[5] == 0x05 &&
	    rdbuf[6] == 0x02) {
		ERROR("No media loaded!\n");
		return 1;
	}

	if (rdbuf[0] == 0x14) {
		ERROR("Out of media!\n");
		return 1;
	}

	return CUPS_BACKEND_OK;
}

static uint8_t es3_error_detect(const uint8_t *rdbuf)
{
	if (rdbuf[8] == 0x01) {
		if (rdbuf[10] == 0x0f)
			ERROR("Communications Error\n");
		else if (rdbuf[10] == 0x01)
			ERROR("No media loaded!\n");
		else
			ERROR("Unknown error - %02x + %02x\n",
			      rdbuf[8], rdbuf[10]);
		return 1;
	} else if (rdbuf[8] == 0x03 &&
		   rdbuf[10] == 0x02) {
		ERROR("No media loaded!\n");
		return 1;
	} else if (rdbuf[8] == 0x08 &&
		   rdbuf[10] == 0x04) {
		ERROR("Printer cover open!\n");
		return 1;
	} else if (rdbuf[8] == 0x05 &&
		   rdbuf[10] == 0x01) {
		ERROR("Incorrect media loaded!\n");
		return 1;
	}

	if (rdbuf[8] || rdbuf[10]) {
		ERROR("Unknown error - %02x + %02x\n",
		      rdbuf[8], rdbuf[10]);
		return 1;
	}

	return CUPS_BACKEND_OK;
}

static uint8_t es40_error_detect(const uint8_t *rdbuf)
{
	/* ES40 */
	if (!rdbuf[3])
		return CUPS_BACKEND_OK;

	if (rdbuf[3] == 0x01)
		ERROR("Generic communication error\n");
	else if (rdbuf[3] == 0x32)
		ERROR("Cover open or media empty!\n");
	else
		ERROR("Unknown error - %02x\n", rdbuf[3]);

	return 1;
}

static const char *cp790_pgcode_names(const uint8_t *rdbuf, const struct printer_data *printer, int *numtype)
{
	UNUSED(printer);
	UNUSED(numtype);

	switch(rdbuf[5]) {
	case 0x00: return "P";
	case 0x01: return "L";
	case 0x02: return "C";
	case 0x03: return "W";
	case 0x0f: return "None";
	default: return "Unknown";
	}
}

static uint8_t cp790_error_detect(const uint8_t *rdbuf)
{
	/* CP790 */
	if (rdbuf[5] == 0xff) {
		ERROR("No ribbon loaded!\n");
		return 1;
	} else if (rdbuf[4] == 0xff) {
		ERROR("No paper tray loaded!\n");
		return 1;
	} else if (rdbuf[3]) {
		if ((rdbuf[3] & 0xf) == 0x02) // 0x12 0x22
			ERROR("No paper tray loaded!\n");
		else if ((rdbuf[3] & 0xf) == 0x03) // 0x13 0x23
			ERROR("Empty paper tray or feed error!\n");
		else if (rdbuf[3] == 0x11)
			ERROR("Paper feed error!\n");
		else if (rdbuf[3] == 0x21)
			ERROR("Ribbon depleted!\n");
		else
			ERROR("Unknown error - %02x\n", rdbuf[3]);
		return 1;
	}

	return CUPS_BACKEND_OK;
}

static const char *cp10_pgcode_names(const uint8_t *rdbuf, const struct printer_data *printer, int *numtype)
{
	UNUSED(rdbuf);
	UNUSED(printer);

	*numtype = 3;
	return "C";   /* Printer only supports one media type */
}

static uint8_t cp10_error_detect(const uint8_t *rdbuf)
{
	if (!rdbuf[2])
		return CUPS_BACKEND_OK;

	if (rdbuf[2] == 0x80)
		ERROR("No ribbon loaded\n");
	else if (rdbuf[2] == 0x08)
		ERROR("Ribbon depleted!\n");
	else if (rdbuf[2] == 0x01)
		ERROR("No paper loaded!\n");
	else
		ERROR("Unknown error - %02x\n", rdbuf[2]);
	return 1;
}

static uint8_t cpxxx_error_detect(const uint8_t *rdbuf)
{
	if (!rdbuf[2])
		return CUPS_BACKEND_OK;

	if (rdbuf[2] == 0x01)
		ERROR("Paper feed problem!\n");
	else if (rdbuf[2] == 0x04)
		ERROR("Ribbon problem!\n");
	else if (rdbuf[2] == 0x08)
		ERROR("Ribbon depleted!\n");
	else
		ERROR("Unknown error - %02x\n", rdbuf[2]);
	return 1;
}

static struct printer_data selphy_printers[] = {
	{ .type = P_ES1,
	  .model = "SELPHY ES1",
	  .init_length = 12,
	  .foot_length = 0,
	  .init_readback = { 0x02, 0x00, 0x00, 0x00, 0x02, 0x01, -1, 0x01, 0x00, 0x00, 0x00, 0x00 },
	  .ready_y_readback = { 0x04, 0x00, 0x01, 0x00, 0x02, 0x01, -1, 0x01, 0x00, 0x00, 0x00, 0x00 },
	  .ready_m_readback = { 0x04, 0x00, 0x03, 0x00, 0x02, 0x01, -1, 0x01, 0x00, 0x00, 0x00, 0x00 },
	  .ready_c_readback = { 0x04, 0x00, 0x07, 0x00, 0x02, 0x01, -1, 0x01, 0x00, 0x00, 0x00, 0x00 },
	  .done_c_readback = { 0x04, 0x00, 0x00, 0x00, 0x02, 0x01, -1, 0x01, 0x00, 0x00, 0x00, 0x00 },
	  .clear_error = { 0x40, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	  .clear_error_len = 12,
	  .pgcode_offset = 3,
	  .paper_code_offset = 6,
	  .paper_code_offset2 = -1,
	  .error_detect = es1_error_detect,
	  .pgcode_names = generic_pgcode_names,
	},
	{ .type = P_ES2_20,
	  .model = "SELPHY ES2/ES20",
	  .init_length = 16,
	  .foot_length = 0,
	  .init_readback = { 0x02, 0x00, 0x00, 0x00, -1, 0x00, -1, -1, 0x00, 0x00, 0x00, 0x00 },
	  .ready_y_readback = { 0x03, 0x00, 0x01, 0x00, -1, 0x00, -1, -1, 0x00, 0x00, 0x00, 0x00 },
	  .ready_m_readback = { 0x06, 0x00, 0x03, 0x00, -1, 0x00, -1, -1, 0x00, 0x00, 0x00, 0x00 },
	  .ready_c_readback = { 0x09, 0x00, 0x07, 0x00, -1, 0x00, -1, -1, 0x00, 0x00, 0x00, 0x00 },
	  .done_c_readback = { 0x09, 0x00, 0x00, 0x00, -1, 0x00, -1, -1, 0x00, 0x00, 0x00, 0x00 },
	  .clear_error = { 0x40, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	  .clear_error_len = 12,
	  .pgcode_offset = 2,
	  .paper_code_offset = 4,
	  .paper_code_offset2 = 6,
	  .error_detect = es2_error_detect,
	  .pgcode_names = generic_pgcode_names,
	},
	{ .type = P_ES3_30,
	  .model = "SELPHY ES3/ES30",
	  .init_length = 16,
	  .foot_length = 12,
	  .init_readback = { 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 },
	  .ready_y_readback = { 0x01, 0xff, 0x01, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 },
	  .ready_m_readback = { 0x03, 0xff, 0x02, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 },
	  .ready_c_readback = { 0x05, 0xff, 0x03, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 },
	  .done_c_readback = { 0x00, 0xff, 0x10, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 },
	  .clear_error = { 0x40, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	  .clear_error_len = 12,
	  .pgcode_offset = 2,
	  .paper_code_offset = -1,
	  .paper_code_offset2 = -1,
	  .error_detect = es3_error_detect,
	  .pgcode_names = NULL,
	},
	{ .type = P_ES40,
	  .model = "SELPHY ES40",
	  .init_length = 16,
	  .foot_length = 12,
	  .init_readback = { 0x00, 0x00, -1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, -1 },
	  .ready_y_readback = { 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, -1 },
	  .ready_m_readback = { 0x00, 0x03, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, -1 },
	  .ready_c_readback = { 0x00, 0x05, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, -1 },
	  .done_c_readback = { 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, -1 },
	  .clear_error = { 0x40, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	  .clear_error_len = 12,
	  .pgcode_offset = 2,
	  .paper_code_offset = 11,
	  .paper_code_offset2 = -1,
	  .error_detect = es40_error_detect,
	  .pgcode_names = generic_pgcode_names,
	},
	{ .type = P_CP790,
	  .model = "SELPHY CP790",
	  .init_length = 16,
	  .foot_length = 12,
	  .init_readback = { 0x00, 0x00, -1, 0x00, -1, -1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 },
	  .ready_y_readback = { 0x00, 0x01, 0x01, 0x00, -1, -1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 },
	  .ready_m_readback = { 0x00, 0x03, 0x02, 0x00, -1, -1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 },
	  .ready_c_readback = { 0x00, 0x05, 0x03, 0x00, -1, -1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 },
	  .done_c_readback = { 0x00, 0x00, 0x10, 0x00, -1, -1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 },
	  .clear_error = { 0x40, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	  .clear_error_len = 12,
	  .pgcode_offset = 2,
	  .paper_code_offset = -1, /* Uses a different technique */
	  .paper_code_offset2 = -1,
	  .error_detect = cp790_error_detect,
	  .pgcode_names = cp790_pgcode_names,
	},
	{ .type = P_CPGENERIC,
	  .model = "SELPHY CP Series (!CP-10/CP790)",
	  .init_length = 12,
	  .foot_length = 0,  /* CP900 has four-byte NULL footer that can be safely ignored */
	  .init_readback = { 0x01, 0x00, 0x00, 0x00, -1, 0x00, -1, -1, 0x00, 0x00, 0x00, -1 },
	  .ready_y_readback = { 0x02, 0x00, 0x00, 0x00, 0x70, 0x00, -1, -1, 0x00, 0x00, 0x00, -1 },
	  .ready_m_readback = { 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, -1, -1, 0x00, 0x00, 0x00, -1 },
	  .ready_c_readback = { 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, -1, -1, 0x00, 0x00, 0x00, -1 },
	  .done_c_readback = { 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, -1, -1, 0x00, 0x00, 0x00, -1 },
	  .clear_error = { 0x40, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	  .clear_error_len = 12,
	  .pgcode_offset = 3,
	  .paper_code_offset = 6,
	  .paper_code_offset2 = -1,
	  .error_detect = cpxxx_error_detect,
	  .pgcode_names = generic_pgcode_names,
	},
	{ .type = P_CP10,
	  .model = "SELPHY CP-10",
	  .init_length = 12,
	  .foot_length = 0,
	  .init_readback = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	  .ready_y_readback = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	  .ready_m_readback = { 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	  .ready_c_readback = { 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	  .done_c_readback = { 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	  .clear_error = { 0x40, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	  .clear_error_len = 12,
	  .pgcode_offset = 2,
	  .paper_code_offset = -1,
	  .paper_code_offset2 = -1,
	  .error_detect = cp10_error_detect,
	  .pgcode_names = cp10_pgcode_names,
	},
	{ .type = -1 },
};

#define MAX_HEADER 28

static const uint32_t es40_cp790_plane_lengths[4] = { 2227456, 1601600, 698880, 2976512 };

static void setup_paper_codes(void)
{
	int i, j;
	for (i = 0 ; ; i++) {
		if (selphy_printers[i].type == -1)
			break;
		/* Default all to IGNORE */
		for (j = 0 ; j < 256 ; j++)
			selphy_printers[i].paper_codes[j] = -1;

		/* Set up specifics */
		switch (selphy_printers[i].type) {
		case P_ES1:
			selphy_printers[i].paper_codes[0x11] = 0x01;
			selphy_printers[i].paper_codes[0x12] = 0x02;
			selphy_printers[i].paper_codes[0x13] = 0x03;
			break;
		case P_ES2_20:
			selphy_printers[i].paper_codes[0x01] = 0x01;
			selphy_printers[i].paper_codes[0x02] = 0x02;
			selphy_printers[i].paper_codes[0x03] = 0x03;
			break;
		case P_ES40:
			selphy_printers[i].paper_codes[0x00] = 0x11;
			selphy_printers[i].paper_codes[0x01] = 0x22;
			selphy_printers[i].paper_codes[0x02] = 0x33;
			break;
		case P_CPGENERIC:
			selphy_printers[i].paper_codes[0x01] = 0x11;
			selphy_printers[i].paper_codes[0x02] = 0x22;
			selphy_printers[i].paper_codes[0x03] = 0x33;
			selphy_printers[i].paper_codes[0x04] = 0x44;
			break;
		case P_CP790:
			selphy_printers[i].paper_codes[0x00] = 0x00;
			selphy_printers[i].paper_codes[0x01] = 0x01;
			selphy_printers[i].paper_codes[0x02] = 0x02;
			selphy_printers[i].paper_codes[0x03] = 0x03;
			break;
		case P_ES3_30:
			/* N/A, printer does not report types */
		case P_CP10:
			/* N/A, printer supports one type only */
			break;
		}
	}
}

#define INCORRECT_PAPER -999

/* Program states */
enum {
	S_IDLE = 0,
	S_PRINTER_READY,
	S_PRINTER_INIT_SENT,
	S_PRINTER_READY_Y,
	S_PRINTER_Y_SENT,
	S_PRINTER_READY_M,
	S_PRINTER_M_SENT,
	S_PRINTER_READY_C,
	S_PRINTER_C_SENT,
	S_PRINTER_CP900_FOOTER,
	S_PRINTER_DONE,
	S_FINISHED,
};

static int fancy_memcmp(const uint8_t *buf_a, const int16_t *buf_b, uint16_t len)
{
	uint16_t i;

	for (i = 0 ; i < len ; i++) {
		if (buf_b[i] == -1)
			continue;
		else if (buf_a[i] > buf_b[i])
			return 1;
		else if (buf_a[i] < buf_b[i])
			return -1;
	}
	return CUPS_BACKEND_OK;
}

static int parse_printjob(uint8_t *buffer, uint8_t *bw_mode, uint32_t *plane_len)
{
	int printer_type = -1;

	if (buffer[0] != 0x40 &&
	    buffer[1] != 0x00) {
		goto done;
	}

	if (buffer[12] == 0x40 &&
	    buffer[13] == 0x01) {
		*plane_len = *(uint32_t*)(&buffer[16]);
		*plane_len = le32_to_cpu(*plane_len);

		if (buffer[2] == 0x00) {
			if (*plane_len == 688480)
				printer_type = P_CP10;
			else
				printer_type = P_CPGENERIC;
		} else {
			printer_type = P_ES1;
			*bw_mode = (buffer[2] == 0x20);
		}
		goto done;
	}

	*plane_len = *(uint32_t*)(&buffer[12]);
	*plane_len = le32_to_cpu(*plane_len);

	if (buffer[16] == 0x40 &&
	    buffer[17] == 0x01) {

		if (buffer[4] == 0x02) {
			printer_type = P_ES2_20;
			*bw_mode = (buffer[7] == 0x01);
			goto done;
		}

		if (es40_cp790_plane_lengths[buffer[2]] == *plane_len) {
			printer_type = P_ES40_CP790;
			*bw_mode = (buffer[3] == 0x01);
			goto done;
		} else {
			printer_type = P_ES3_30;
			*bw_mode = (buffer[3] == 0x01);
			goto done;
		}
	}

done:
	return printer_type;
}

/* Private data structure */
struct canonselphy_printjob {
	struct dyesub_job_common common;

	int16_t paper_code;
	uint8_t bw_mode;

	uint32_t plane_len;

	uint8_t *header;
	uint8_t *plane_y;
	uint8_t *plane_m;
	uint8_t *plane_c;
	uint8_t *footer;
};

struct canonselphy_ctx {
	struct dyesub_connection *conn;

	struct printer_data *printer;
	struct marker marker;

	uint8_t cp900;
};

static int canonselphy_get_status(struct canonselphy_ctx *ctx)
{
	uint8_t rdbuf[READBACK_LEN];
	int ret, num;

	/* Read in the printer status, twice. */
	ret = read_data(ctx->conn,
			 (uint8_t*) rdbuf, READBACK_LEN, &num);
	if (ret < 0)
		return CUPS_BACKEND_FAILED;

	ret = read_data(ctx->conn,
			 (uint8_t*) rdbuf, READBACK_LEN, &num);

	if (ret < 0)
		return CUPS_BACKEND_FAILED;

	INFO("Media type: %s\n", ctx->printer->pgcode_names? ctx->printer->pgcode_names(rdbuf, ctx->printer, &ret) : "Unknown");
	ctx->printer->error_detect(rdbuf);

	return CUPS_BACKEND_OK;
}

static int canonselphy_send_reset(struct canonselphy_ctx *ctx)
{
	uint8_t rstcmd[12] = { 0x40, 0x10, 0x00, 0x00,
			       0x00, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x00, 0x00 };
	int ret;

	if ((ret = send_data(ctx->conn,
			      rstcmd, sizeof(rstcmd))))
		return CUPS_BACKEND_FAILED;

	return CUPS_BACKEND_OK;
}

static void *canonselphy_init(void)
{
	struct canonselphy_ctx *ctx = malloc(sizeof(struct canonselphy_ctx));
	if (!ctx) {
		ERROR("Memory Allocation Failure!\n");
		return NULL;
	}
	memset(ctx, 0, sizeof(struct canonselphy_ctx));

	/* Static initialization */
	setup_paper_codes();

	return ctx;
}

static int canonselphy_attach(void *vctx, struct dyesub_connection *conn, uint8_t jobid)
{
	struct canonselphy_ctx *ctx = vctx;
	int i, num;
	uint8_t rdbuf[READBACK_LEN];
	UNUSED(jobid);

	ctx->conn = conn;

	if (ctx->conn->type == P_CP900) {
		ctx->conn->type = P_CPGENERIC;
		ctx->cp900 = 1;
	}
	for (i = 0 ; selphy_printers[i].type != -1; i++) {
		if (selphy_printers[i].type == ctx->conn->type) {
			ctx->printer = &selphy_printers[i];
		}
	}
	if (!ctx->printer) {
		ERROR("Error looking up printer type!\n");
		return CUPS_BACKEND_FAILED;
	}

	/* Fill out marker structure */
	ctx->marker.color = "#00FFFF#FF00FF#FFFF00";
	ctx->marker.levelmax = CUPS_MARKER_UNAVAILABLE;

	if (test_mode < TEST_MODE_NOATTACH) {
		/* Read printer status. Twice. */
		i = read_data(ctx->conn,
			      rdbuf, READBACK_LEN, &num);
		if (i < 0)
			return CUPS_BACKEND_FAILED;

		i = read_data(ctx->conn,
			      rdbuf, READBACK_LEN, &num);
		if (i < 0)
			return CUPS_BACKEND_FAILED;

		if (ctx->printer->error_detect(rdbuf))
			ctx->marker.levelnow = 0;
		else
			ctx->marker.levelnow = CUPS_MARKER_UNKNOWN_OK;

		ctx->marker.name = ctx->printer->pgcode_names? ctx->printer->pgcode_names(rdbuf, ctx->printer, &ctx->marker.numtype) : "Unknown";
	} else {
		// XXX handle MEDIA_CODE at some point.
		// we don't do any error checking here.
		ctx->marker.name = "Unknown";
		ctx->marker.numtype = -1;
	}

	return CUPS_BACKEND_OK;
}

static void canonselphy_cleanup_job(const void *vjob) {
	const struct canonselphy_printjob *job = vjob;

	if (job->header)
		free(job->header);
	if (job->plane_y)
		free(job->plane_y);
	if (job->plane_m)
		free(job->plane_m);
	if (job->plane_c)
		free(job->plane_c);
	if (job->footer)
		free(job->footer);

	free((void*)vjob);
}

static int canonselphy_read_parse(void *vctx, const void **vjob, int data_fd, int copies)
{
	struct canonselphy_ctx *ctx = vctx;
	int i, remain;
	int printer_type;
	int offset = 0;
	uint8_t rdbuf[MAX_HEADER];

	struct canonselphy_printjob *job = NULL;

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

	/* The CP900 job *may* have a 4-byte null footer after the
	   job contents.  Ignore it if it comes through here.. */
	i = read(data_fd, rdbuf, 4);
	if (i != 4) {
		if (i == 0) {
			canonselphy_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		ERROR("Read failed (%d/%d)\n", i, 4);
		perror("ERROR: Read failed");
		canonselphy_cleanup_job(job);
		return CUPS_BACKEND_FAILED;
	}
	/* if it's not the null header.. don't ignore! */
	if (rdbuf[0] != 0 ||
	    rdbuf[1] != 0 ||
	    rdbuf[2] != 0 ||
	    rdbuf[3] != 0) {
		offset = 4;
	}

	/* Read the rest of the header.. */
	i = read(data_fd, rdbuf + offset, MAX_HEADER - offset);
	if (i != MAX_HEADER - offset) {
		if (i == 0) {
			canonselphy_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		ERROR("Read failed (%d/%d)\n",
		      i, MAX_HEADER - offset);
		perror("ERROR: Read failed");
		canonselphy_cleanup_job(job);
		return CUPS_BACKEND_FAILED;
	}

	/* Figure out printer this file is intended for */
	printer_type = parse_printjob(rdbuf, &job->bw_mode, &job->plane_len);
	/* Special cases for some models */
	if (printer_type == P_ES40_CP790) {
		if (ctx->conn->type == P_CP790 ||
		    ctx->conn->type == P_ES40)
			printer_type = ctx->conn->type;
	}

	if (printer_type != ctx->conn->type) {
		ERROR("Printer/Job mismatch (%d/%d/%d)\n", ctx->conn->type, ctx->printer->type, printer_type);
		canonselphy_cleanup_job(job);
		return CUPS_BACKEND_CANCEL;
	}

	INFO("%sFile intended for a '%s' printer\n",  job->bw_mode? "B/W " : "", ctx->printer->model);

	/* Paper code setup */
	if (ctx->printer->pgcode_offset != -1)
		job->paper_code = ctx->printer->paper_codes[rdbuf[ctx->printer->pgcode_offset]];
	else
		job->paper_code = -1;

	/* Add in plane header length! */
	job->plane_len += 12;

	/* Set up buffers */
	job->plane_y = malloc(job->plane_len);
	job->plane_m = malloc(job->plane_len);
	job->plane_c = malloc(job->plane_len);
	job->header = malloc(ctx->printer->init_length);
	job->footer = malloc(ctx->printer->foot_length);
	if (!job->plane_y || !job->plane_m || !job->plane_c || !job->header ||
	    (ctx->printer->foot_length && !job->footer)) {
		ERROR("Memory allocation failure!\n");
		canonselphy_cleanup_job(job);
		return CUPS_BACKEND_RETRY_CURRENT;
	}

	/* Move over chunks already read in */
	memcpy(job->header, rdbuf, ctx->printer->init_length);
	memcpy(job->plane_y, rdbuf+ctx->printer->init_length,
	       MAX_HEADER-ctx->printer->init_length);

	/* Read in YELLOW plane */
	remain = job->plane_len - (MAX_HEADER-ctx->printer->init_length);
	while (remain > 0) {
		i = read(data_fd, job->plane_y + (job->plane_len - remain), remain);
		if (i < 0) {
			canonselphy_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		remain -= i;
	}

	/* Read in MAGENTA plane */
	remain = job->plane_len;
	while (remain > 0) {
		i = read(data_fd, job->plane_m + (job->plane_len - remain), remain);
		if (i < 0) {
			canonselphy_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		remain -= i;
	}

	/* Read in CYAN plane */
	remain = job->plane_len;
	while (remain > 0) {
		i = read(data_fd, job->plane_c + (job->plane_len - remain), remain);
		if (i < 0) {
			canonselphy_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		remain -= i;
	}

	/* Read in footer */
	if (ctx->printer->foot_length) {
		remain = ctx->printer->foot_length;
		while (remain > 0) {
			i = read(data_fd, job->footer + (ctx->printer->foot_length - remain), remain);
			if (i < 0) {
				canonselphy_cleanup_job(job);
				return CUPS_BACKEND_CANCEL;
			}
			remain -= i;
		}
	}

	*vjob = job;

	return CUPS_BACKEND_OK;
}

static int canonselphy_main_loop(void *vctx, const void *vjob, int wait_for_return) {
	struct canonselphy_ctx *ctx = vctx;

	uint8_t rdbuf[READBACK_LEN], rdbuf2[READBACK_LEN];
	int last_state = -1, state = S_IDLE;
	int ret, num;
	int copies;
	(void)wait_for_return;

	const struct canonselphy_printjob *job = vjob;

	if (!ctx)
		return CUPS_BACKEND_FAILED;
	if (!job)
		return CUPS_BACKEND_FAILED;

	copies = job->common.copies;

	/* Read in the printer status to clear last state */
	ret = read_data(ctx->conn,
			rdbuf, READBACK_LEN, &num);

	if (ret < 0)
		return CUPS_BACKEND_FAILED;
top:

	if (state != last_state) {
		if (dyesub_debug)
			DEBUG("last_state %d new %d\n", last_state, state);
	}

	/* Read in the printer status */
	ret = read_data(ctx->conn,
			rdbuf, READBACK_LEN, &num);
	if (ret < 0)
		return CUPS_BACKEND_FAILED;

	if (num != READBACK_LEN) {
		ERROR("Short read! (%d/%d)\n", num, READBACK_LEN);
		return CUPS_BACKEND_FAILED;
	}

	/* Error detection */
	if (ctx->printer->error_detect(rdbuf)) {
		dump_markers(&ctx->marker, 1, 0);
		if (ctx->printer->clear_error_len)
			/* Try to clear error state */
			if ((ret = send_data(ctx->conn, ctx->printer->clear_error, ctx->printer->clear_error_len)))
				return CUPS_BACKEND_FAILED;
		return CUPS_BACKEND_STOP;
	}

	if (memcmp(rdbuf, rdbuf2, READBACK_LEN)) {
		memcpy(rdbuf2, rdbuf, READBACK_LEN);
	} else if (state == last_state) {
		sleep(1);
	}
	last_state = state;

	fflush(logger);

	switch(state) {
	case S_IDLE:
		INFO("Waiting for printer idle\n");
		if (fancy_memcmp(rdbuf, ctx->printer->init_readback, READBACK_LEN))
			break;

		/* Make sure paper/ribbon is correct */
		if (job->paper_code != -1) {
			uint8_t ribbon, paper;
			uint8_t job_ribbon = job->paper_code & 0x0f;
			uint8_t job_paper = (job->paper_code >> 4) & 0xf;

			switch(ctx->conn->type) {
			case P_CPGENERIC:
				ribbon = rdbuf[ctx->printer->paper_code_offset] & 0xf;
				paper = rdbuf[ctx->printer->paper_code_offset] >> 4;
				if (paper == 0x0) {
					ERROR("No paper tray loaded, aborting!\n");
					return CUPS_BACKEND_STOP;
				}
				if (ribbon == 0x0) {
					ERROR("No ribbon loaded, aborting!\n");
					return CUPS_BACKEND_STOP;
				}
				if (paper != job_paper) {
					ERROR("Incorrect paper loaded (%02x vs %02x), aborting job!\n", paper, job_paper);
					return CUPS_BACKEND_HOLD;
				}
				if (ribbon != job_ribbon) {
					ERROR("Incorrect ribbon loaded (%02x vs %02x), aborting job!\n", ribbon, job_ribbon);
					return CUPS_BACKEND_HOLD;
				}
				break;
			case P_CP790:
				ribbon = rdbuf[4] >> 4;
				paper = rdbuf[5] & 0xf;
				if (paper == 0xf) {
					ERROR("No paper tray loaded, aborting!\n");
					return CUPS_BACKEND_STOP;
				}
				if (ribbon == 0xf) {
					ERROR("No ribbon loaded, aborting!\n");
					return CUPS_BACKEND_STOP;
				}
				if (paper != job_paper) {
					ERROR("Incorrect paper loaded (%02x vs %02x), aborting job!\n", paper, job_paper);
					return CUPS_BACKEND_HOLD;
				}
				if (ribbon != job_ribbon) {
					ERROR("Incorrect ribbon loaded (%02x vs %02x), aborting job!\n", ribbon, job_ribbon);
					return CUPS_BACKEND_HOLD;
				}
				break;
			default:
				if (ctx->printer->paper_code_offset != -1 &&
				    rdbuf[ctx->printer->paper_code_offset] != job->paper_code) {
					ERROR("Incorrect media/ribbon loaded (%02x vs %02x), aborting job!\n",
					      job->paper_code,
					      rdbuf[ctx->printer->paper_code_offset]);
					return CUPS_BACKEND_HOLD;  /* Hold this job, don't stop queue */
				}
				break;
			}
		}

		state = S_PRINTER_READY;
		break;
	case S_PRINTER_READY:
		INFO("Printing started; Sending init sequence\n");
		/* Send printer init */
		if ((ret = send_data(ctx->conn, job->header, ctx->printer->init_length)))
			return CUPS_BACKEND_FAILED;

		state = S_PRINTER_INIT_SENT;
		break;
	case S_PRINTER_INIT_SENT:
		if (!fancy_memcmp(rdbuf, ctx->printer->ready_y_readback, READBACK_LEN)) {
			state = S_PRINTER_READY_Y;
		}
		break;
	case S_PRINTER_READY_Y:
		if (job->bw_mode)
			INFO("Sending BLACK plane\n");
		else
			INFO("Sending YELLOW plane\n");

		if ((ret = send_data(ctx->conn, job->plane_y, job->plane_len)))
			return CUPS_BACKEND_FAILED;

		state = S_PRINTER_Y_SENT;
		break;
	case S_PRINTER_Y_SENT:
		if (!fancy_memcmp(rdbuf, ctx->printer->ready_m_readback, READBACK_LEN)) {
			if (job->bw_mode)
				state = S_PRINTER_DONE;
			else
				state = S_PRINTER_READY_M;
		}
		break;
	case S_PRINTER_READY_M:
		INFO("Sending MAGENTA plane\n");

		if ((ret = send_data(ctx->conn, job->plane_m, job->plane_len)))
			return CUPS_BACKEND_FAILED;

		state = S_PRINTER_M_SENT;
		break;
	case S_PRINTER_M_SENT:
		if (!fancy_memcmp(rdbuf, ctx->printer->ready_c_readback, READBACK_LEN)) {
			state = S_PRINTER_READY_C;
		}
		break;
	case S_PRINTER_READY_C:
		INFO("Sending CYAN plane\n");

		if ((ret = send_data(ctx->conn, job->plane_c, job->plane_len)))
			return CUPS_BACKEND_FAILED;

		state = S_PRINTER_C_SENT;
		break;
	case S_PRINTER_C_SENT:
		if (!fancy_memcmp(rdbuf, ctx->printer->done_c_readback, READBACK_LEN)) {
			if (ctx->cp900)
				state = S_PRINTER_CP900_FOOTER;
			else
				state = S_PRINTER_DONE;
		}
		break;
	case S_PRINTER_CP900_FOOTER: {
		uint32_t empty = 0;

		INFO("Sending CP900 Footer\n");
		if ((ret = send_data(ctx->conn,
				     (uint8_t*)&empty, sizeof(empty))))
			return CUPS_BACKEND_FAILED;

		state = S_PRINTER_DONE;
		break;
	}
	case S_PRINTER_DONE:
		if (ctx->printer->foot_length) {
			INFO("Cleaning up\n");

			if ((ret = send_data(ctx->conn, job->footer, ctx->printer->foot_length)))
				return CUPS_BACKEND_FAILED;
		}
		state = S_FINISHED;
		/* Intentional Fallthrough */
	case S_FINISHED:
		INFO("All data sent to printer!\n");
		break;
	}
	if (state != S_FINISHED)
		goto top;

	/* Clean up */
	if (terminate)
		copies = 1;

	INFO("Print complete (%d copies remaining)\n", copies - 1);

	if (copies && --copies) {
		state = S_IDLE;
		goto top;
	}

	return CUPS_BACKEND_OK;
}

static int canonselphy_cmdline_arg(void *vctx, int argc, char **argv)
{
	struct canonselphy_ctx *ctx = vctx;
	int i, j = 0;

	if (!ctx)
		return -1;

	while ((i = getopt(argc, argv, GETOPT_LIST_GLOBAL "Rs")) >= 0) {
		switch(i) {
		GETOPT_PROCESS_GLOBAL
		case 'R':
			canonselphy_send_reset(ctx);
			break;
		case 's':
			canonselphy_get_status(ctx);
			break;
		}

		if (j) return j;
	}

	return CUPS_BACKEND_OK;
}

static void canonselphy_cmdline(void)
{
	DEBUG("\t\t[ -R ]           # Reset printer\n");
	DEBUG("\t\t[ -s ]           # Query printer status\n");
}

static int canonselphy_query_markers(void *vctx, struct marker **markers, int *count)
{
	struct canonselphy_ctx *ctx = vctx;
	uint8_t rdbuf[READBACK_LEN];
	int ret, num;

	/* Read in the printer status, twice. */
	ret = read_data(ctx->conn,
			(uint8_t*) rdbuf, READBACK_LEN, &num);
	if (ret < 0)
		return CUPS_BACKEND_FAILED;

	ret = read_data(ctx->conn,
			(uint8_t*) rdbuf, READBACK_LEN, &num);

	if (ret < 0)
		return CUPS_BACKEND_FAILED;

	if (ctx->printer->error_detect(rdbuf))
		ctx->marker.levelnow = 0;
	else
		ctx->marker.levelnow = CUPS_MARKER_UNKNOWN_OK;

	*markers = &ctx->marker;
	*count = 1;

	return CUPS_BACKEND_OK;
}

static const char *canonselphy_prefixes[] = {
	"canonselphy", // Family name
	// backwards compatibility
	"selphycp10", "selphycp100", "selphycp200", "selphycp220",
	"selphycp300", "selphycp330", "selphycp400", "selphycp500",
	"selphycp510", "selphycp520", "selphycp530", "selphycp600",
	"selphycp710", "selphycp720", "selphycp730", "selphycp740",
	"selphycp750", "selphycp760", "selphycp770", "selphycp780",
	"selphycp790", "selphycp800", "selphycp810", "selphycp900",
	"selphyes1", "selphyes2", "selphyes20", "selphyes3",
	"selphyes30", "selphyes40",
	NULL
};
static const struct device_id canonselphy_devices[] = {
	{ 0x04a9, 0x304a, P_CP10, NULL, "canon-cp10"},
	{ 0x04a9, 0x3063, P_CPGENERIC, NULL, "canon-cp100"},
	{ 0x04a9, 0x307c, P_CPGENERIC, NULL, "canon-cp200"},
	{ 0x04a9, 0x30bd, P_CPGENERIC, NULL, "canon-cp220"},
	{ 0x04a9, 0x307d, P_CPGENERIC, NULL, "canon-cp300"},
	{ 0x04a9, 0x30be, P_CPGENERIC, NULL, "canon-cp330"},
	{ 0x04a9, 0x30f6, P_CPGENERIC, NULL, "canon-cp400"},
	{ 0x04a9, 0x30f5, P_CPGENERIC, NULL, "canon-cp500"},
	{ 0x04a9, 0x3128, P_CPGENERIC, NULL, "canon-cp510"},
	{ 0x04a9, 0x3172, P_CPGENERIC, NULL, "canon-cp520"},
	{ 0x04a9, 0x31b1, P_CPGENERIC, NULL, "canon-cp530"},
	{ 0x04a9, 0x310b, P_CPGENERIC, NULL, "canon-cp600"},
	{ 0x04a9, 0x3127, P_CPGENERIC, NULL, "canon-cp710"},
	{ 0x04a9, 0x3143, P_CPGENERIC, NULL, "canon-cp720"},
	{ 0x04a9, 0x3142, P_CPGENERIC, NULL, "canon-cp730"},
	{ 0x04a9, 0x3171, P_CPGENERIC, NULL, "canon-cp740"},
	{ 0x04a9, 0x3170, P_CPGENERIC, NULL, "canon-cp750"},
	{ 0x04a9, 0x31ab, P_CPGENERIC, NULL, "canon-cp760"},
	{ 0x04a9, 0x31aa, P_CPGENERIC, NULL, "canon-cp770"},
	{ 0x04a9, 0x31dd, P_CPGENERIC, NULL, "canon-cp780"},
	{ 0x04a9, 0x31e7, P_CP790, NULL, "canon-cp790"},
	{ 0x04a9, 0x3214, P_CPGENERIC, NULL, "canon-cp800"},
	{ 0x04a9, 0x3256, P_CPGENERIC, NULL, "canon-cp810"},
	{ 0x04a9, 0x3255, P_CPGENERIC, NULL, "canon-cp900"},
	{ 0x04a9, 0x3141, P_ES1, NULL, "canon-es1"},
	{ 0x04a9, 0x3185, P_ES2_20, NULL, "canon-es2"},
	{ 0x04a9, 0x3186, P_ES2_20, NULL, "canon-es20"},
	{ 0x04a9, 0x31af, P_ES3_30, NULL, "canon-es3"},
	{ 0x04a9, 0x31b0, P_ES3_30, NULL, "canon-es30"},
	{ 0x04a9, 0x31ee, P_ES40, NULL, "canon-es40"},
	{ 0, 0, 0, NULL, NULL}
};

const struct dyesub_backend canonselphy_backend = {
	.name = "Canon SELPHY CP/ES (legacy)",
	.version = "0.113",
	.uri_prefixes = canonselphy_prefixes,
	.devices = canonselphy_devices,
	.cmdline_usage = canonselphy_cmdline,
	.cmdline_arg = canonselphy_cmdline_arg,
	.init = canonselphy_init,
	.attach = canonselphy_attach,
	.read_parse = canonselphy_read_parse,
	.cleanup_job = canonselphy_cleanup_job,
	.main_loop = canonselphy_main_loop,
	.query_markers = canonselphy_query_markers,
};
/*

 ***************************************************************************

	Stream formats and readback codes for supported printers

 ***************************************************************************
 Selphy ES1:

   Init func:   40 00 [typeA] [pgcode]  00 00 00 00  00 00 00 00
   Plane func:  40 01 [typeB] [plane]  [length, 32-bit LE]  00 00 00 00

   TypeA codes are 0x10 for Color papers, 0x20 for B&W papers.
   TypeB codes are 0x01 for Color papers, 0x02 for B&W papers.

   Plane codes are 0x01, 0x03, 0x07 for Y, M, and C, respectively.
   B&W Jobs have a single plane code of 0x01.

   'P' papers pgcode of 0x11 and a plane length of 2227456 bytes
   'L'        pgcode of 0x12 and a plane length of 1601600 bytes.
   'C'        pgcode of 0x13 and a plane length of  698880 bytes.

   Readback values seen:

   02 00 00 00  02 01 [pg] 01  00 00 00 00   [idle, waiting for init seq]
   04 00 00 00  02 01 [pg] 01  00 00 00 00   [init received, not ready..]
   04 00 01 00  02 01 [pg] 01  00 00 00 00   [waiting for Y data]
   04 00 03 00  02 01 [pg] 01  00 00 00 00   [waiting for M data]
   04 00 07 00  02 01 [pg] 01  00 00 00 00   [waiting for C data]
   04 00 00 00  02 01 [pg] 01  00 00 00 00   [all data sent; not ready..]
   05 00 00 00  02 01 [pg] 01  00 00 00 00   [?? transitions to this]
   06 00 00 00  02 01 [pg] 01  00 00 00 00   [?? transitions to this]
   02 00 00 00  02 01 [pg] 01  00 00 00 00   [..transitions back to idle]

   02 01 00 00  01 ff ff ff  00 80 00 00     [error, no media]
   02 00 00 00  01 ff ff ff  00 00 00 00     [error, cover open]
   0f 00 00 00  02 01 01 01  00 00 00 00     [error, out of media]

   Known paper types for all ES printers:  P, Pbw, L, C, Cl
   Additional types for ES3/30/40:         Pg, Ps

   [pg] is:  0x01 for P-papers
   	     0x02 for L-papers
             0x03 for C-papers

 ***************************************************************************
 Selphy ES2/20:

   Init func:   40 00 [pgcode] 00  02 00 00 [type]  00 00 00 [pg2] [length, 32-bit LE]
   Plane func:  40 01 [plane] 00  00 00 00 00  00 00 00 00

   Type codes are 0x00 for Color papers, 0x01 for B&W papers.

   Plane codes are 0x01, 0x02, 0x03 for Y, M, and C, respectively.
   B&W Jobs have a single plane code of 0x01.

   'P' papers pgcode of 0x01 and a plane length of 2227456 bytes
   'L' 	      pgcode of 0x02 and a plane length of 1601600 bytes.
   'C'	      pgcode of 0x03 and a plane length of  698880 bytes.

   pg2 is 0x00 for all media types except for 'C', which is 0x01.

   Readback values seen on an ES2:

   02 00 00 00  [pg] 00 [pg2] [xx]  00 00 00 00   [idle, waiting for init seq]
   03 00 01 00  [pg] 00 [pg2] [xx]  00 00 00 00   [init complete, ready for Y]
   04 00 01 00  [pg] 00 [pg2] [xx]  00 00 00 00   [? paper loaded]
   05 00 01 00  [pg] 00 [pg2] [xx]  00 00 00 00   [? transitions to this]
   06 00 03 00  [pg] 00 [pg2] [xx]  00 00 00 00   [ready for M]
   08 00 03 00  [pg] 00 [pg2] [xx]  00 00 00 00   [? transitions to this]
   09 00 07 00  [pg] 00 [pg2] [xx]  00 00 00 00   [ready for C]
   09 00 00 00  [pg] 00 [pg2] 00  00 00 00 00   [? transitions to this]
   0b 00 00 00  [pg] 00 [pg2] 00  00 00 00 00   [? transitions to this]
   0c 00 00 00  [pg] 00 [pg2] 00  00 00 00 00   [? transitions to this]
   0f 00 00 00  [pg] 00 [pg2] 00  00 00 00 00   [? transitions to this]
   13 00 00 00  [pg] 00 [pg2] 00  00 00 00 00   [? transitions to this]

   14 00 00 00  [pg] 00 [pg2] 00  00 00 00 00   [out of paper/ink]
   14 00 01 00  [pg] 00 [pg2] 00  01 00 00 00   [out of paper/ink]

   16 01 00 00  [pg] 00 [pg2] 00  00 00 00 00   [error, cover open]
   02 00 00 00  05 05 02 00  00 00 00 00        [error, no media]

   [xx] can be 0x00 or 0xff, depending on if a previous print job has
	completed or not.

   [pg] is:  0x01 for P-papers
   	     0x02 for L-papers
             0x03 for C-papers

   [pg2] is: 0x00 for Normal papers
             0x01 for Label papers

 ***************************************************************************
 Selphy ES3/30:

   Init func:   40 00 [pgcode] [type]  00 00 00 00  00 00 00 00 [length, 32-bit LE]
   Plane func:  40 01 [plane] 00  00 00 00 00  00 00 00 00

   End func:    40 20 00 00  00 00 00 00  00 00 00 00

   Type codes are 0x00 for Color papers, 0x01 for B&W papers.

   Plane codes are 0x01, 0x02, 0x03 for Y, M, and C, respectively.
   B&W Jobs have a single plane code of 0x01.

   'P' papers pgcode of 0x01 and a plane length of 2227456 bytes.
   'L' 	      pgcode of 0x02 and a plane length of 1601600 bytes.
   'C' 	      pgcode of 0x03 and a plane length of  698880 bytes.

   Readback values seen on an ES3 & ES30:

   00 ff 00 00  ff ff ff ff  00 00 00 00   [idle, waiting for init seq]
   01 ff 01 00  ff ff ff ff  00 00 00 00   [init complete, ready for Y]
   03 ff 01 00  ff ff ff ff  00 00 00 00   [?]
   03 ff 02 00  ff ff ff ff  00 00 00 00   [ready for M]
   05 ff 02 00  ff ff ff ff  00 00 00 00   [?]
   05 ff 03 00  ff ff ff ff  00 00 00 00   [ready for C]
   07 ff 03 00  ff ff ff ff  00 00 00 00   [?]
   0b ff 03 00  ff ff ff ff  00 00 00 00   [?]
   13 ff 03 00  ff ff ff ff  00 00 00 00   [?]
   00 ff 10 00  ff ff ff ff  00 00 00 00   [ready for footer]

   01 ff 10 00  ff ff ff ff  01 00 0f 00   [communication error]
   00 ff 01 00  ff ff ff ff  01 00 01 00   [error, no media/ink]
   00 ff 01 00  ff ff ff ff  05 00 01 00   [error, incorrect media]
   00 ff 01 00  ff ff ff ff  03 00 02 00   [attempt to print with no media]
   00 ff 01 00  ff ff ff ff  08 00 04 00   [attempt to print with cover open]

   There appears to be no paper code in the readback; codes were identical for
   the standard 'P-Color' and 'Cl' cartridges:

 ***************************************************************************
 Selphy ES40:

   Init func:   40 00 [pgcode] [type]  00 00 00 00  00 00 00 00 [length, 32-bit LE]
   Plane func:  40 01 [plane] 00  00 00 00 00  00 00 00 00

   End func:    40 20 00 00  00 00 00 00  00 00 00 00

   Type codes are 0x00 for Color papers, 0x01 for B&W papers.

   Plane codes are 0x01, 0x02, 0x03 for Y, M, and C, respectively.
   B&W Jobs have a single plane code of 0x01.

   'P' papers pgcode of 0x00 and a plane length of 2227456 bytes.
   'L' 	      pgcode of 0x01 and a plane length of 1601600 bytes.
   'C'	      pgcode of 0x02 and a plane length of  698880 bytes.

   Readback values seen on an ES40:

   00 00 ff 00  00 00 00 00  00 00 00 [pg]
   00 00 00 00  00 00 00 00  00 00 00 [pg]   [idle, ready for header]
   00 01 01 00  00 00 00 00  00 00 00 [pg]   [ready for Y data]
   00 03 01 00  00 00 00 00  00 00 00 [pg]   [transitions to this]
   00 03 02 00  00 00 00 00  00 00 00 [pg]   [ready for M data]
   00 05 02 00  00 00 00 00  00 00 00 [pg]   [transitions to this]
   00 05 03 00  00 00 00 00  00 00 00 [pg]   [ready for C data]
   00 07 03 00  00 00 00 00  00 00 00 [pg]   [transitions to this]
   00 0b ff 00  00 00 00 00  00 00 00 [pg]   [transitions to this]
   00 0e ff 00  00 00 00 00  00 00 00 [pg]   [transitions to this]
   00 00 10 00  00 00 00 00  00 00 00 [pg]   [ready for footer]

   00 ** ** [xx]  00 00 00 00  00 00 00 [pg] [error]

   [xx]:
	01:  Generic communication error
	32:  Cover open / media empty

   [pg] is as follows:

      'P' paper 0x11
      'L' paper 0x22
      'C' paper 0x33

 ***************************************************************************
 Selphy CP790:

   Init func:   40 00 [pgcode] 00  00 00 00 00  00 00 00 00 [length, 32-bit LE]
   Plane func:  40 01 [plane] 00  00 00 00 00  00 00 00 00

   End func:    40 20 00 00  00 00 00 00  00 00 00 00

   Reset func:  40 10 00 00  00 00 00 00  00 00 00 00

   Plane codes are 0x01, 0x02, 0x03 for Y, M, and C, respectively.

   'P' papers pgcode of 0x00 and a plane length of 2227456 bytes.
   'L' 	      pgcode of 0x01 and a plane length of 1601600 bytes.
   'C'	      pgcode of 0x02 and a plane length of  698880 bytes.
   'W' 	      pgcode of 0x03 and a plane length of 2976512 bytes.

   Readback values seen on an CP790:

   00 00 ff 00  [pg1] [pg2] 00 00  00 00 00 02
   00 00 00 00  [pg1] [pg2] 00 00  00 00 00 02   [idle, ready for header]
   00 00 01 00  [pg1] [pg2] 00 00  00 00 00 02
   00 01 01 00  [pg1] [pg2] 00 00  00 00 00 02   [ready for Y data]
   00 03 01 00  [pg1] [pg2] 00 00  00 00 00 02   [transitions to this]
   00 03 02 00  [pg1] [pg2] 00 00  00 00 00 02   [ready for M data]
   00 05 02 00  [pg1] [pg2] 00 00  00 00 00 02   [transitions to this]
   00 05 03 00  [pg1] [pg2] 00 00  00 00 00 02   [ready for C data]
   00 0b ff 00  [pg1] [pg2] 00 00  00 00 00 02   [transitions to this]
   00 0e ff 00  [pg1] [pg2] 00 00  00 00 00 02   [transitions to this]
   00 00 10 00  [pg1] [pg2] 00 00  00 00 00 02   [ready for footer]

   [pg1] is:                  [pg2] is:

      0x00  'P' ribbon         0x00 'P' paper
      0x10  'L' ribbon         0x01 'L' paper
      0x20  'C' ribbon         0x02 'C' paper
      0x30  'W' ribbon         0x03 'W' paper
      0xff  NO RIBBON          0xff  NO PAPER TRAY

   Other readbacks seen:

   00 00 01 11  [pg1] [pg2] 00 00  00 00 00 02   [emptytray, ink match job ]
   00 00 01 12  [pg1] [pg2] 00 00  00 00 00 02   [ notray, ink match job ]
   00 00 01 13  [pg1] [pg2] 00 00  00 00 00 02   [ empty tray + mismatch ink ]
   00 00 01 21  [pg1] [pg2] 00 00  00 00 00 02   [ depleted ribbon, match ink ]
   00 00 01 22  [pg1] [pg2] 00 00  00 00 00 02   [ no paper tray ]
   00 00 01 23  [pg1] [pg2] 00 00  00 00 00 02   [ empty tray, ink mismatch ]

    Note : These error conditions are confusing.

 ***************************************************************************
 Selphy CP-10:

   Init func:   40 00 00 00  00 00 00 00  00 00 00 00
   Plane func:  40 01 00 [plane]  [length, 32-bit LE]  00 00 00 00

   plane codes are 0x00, 0x01, 0x02 for Y, M, and C, respectively.

   length is always '00 60 81 0a' which is 688480 bytes.

   Error clear: 40 10 00 00  00 00 00 00  00 00 00 00

   Known readback values:

   01 00 00 00  00 00 00 00  00 00 00 00   [idle, waiting for init]
   02 00 00 00  00 00 00 00  00 00 00 00   [init sent, paper feeding]
   02 00 00 00  00 00 00 00  00 00 00 00   [init sent, paper feeding]
   02 00 00 00  00 00 00 00  00 00 00 00   [waiting for Y data]
   04 00 00 00  00 00 00 00  00 00 00 00   [waiting for M data]
   08 00 00 00  00 00 00 00  00 00 00 00   [waiting for C data]
   10 00 00 00  00 00 00 00  00 00 00 00   [C done, waiting]
   20 00 00 00  00 00 00 00  00 00 00 00   [All done]

   02 00 80 00  00 00 00 00  00 00 00 00   [No ribbon]
   02 00 80 00  00 00 00 00  00 00 00 00   [Ribbon depleted]
   02 00 01 00  00 00 00 00  00 00 00 00   [No paper]

  There are no media type codes; the printer only supports one type.

 ***************************************************************************
 Selphy CP-series (except for CP790 & CP-10):

    This is known to apply to:
	CP-100, CP-200, CP-300, CP-330, CP400, CP500, CP510, CP710,
	CP720, CP730, CP740, CP750, CP760, CP770, CP780, CP800, CP900

   Init func:   40 00 00 [pgcode]  00 00 00 00  00 00 00 00
   Plane func:  40 01 00 [plane]  [length, 32-bit LE]  00 00 00 00
   End func:    00 00 00 00       # NOTE: Present (and necessary) on CP900 only.

   Error clear: 40 10 00 00  00 00 00 00  00 00 00 00

   plane codes are 0x00, 0x01, 0x02 for Y, M, and C, respectively.

   'P' papers pgcode 0x01   plane length 2227456 bytes.
   'L' 	      pgcode 0x02   plane length 1601600 bytes.
   'C' 	      pgcode 0x03   plane length  698880 bytes.
   'W'	      pgcode 0x04   plane length 2976512 bytes.

   Known readback values:

   01 00 00 00  [ss] 00 [pg] [zz]  00 00 00 [xx]   [idle, waiting for init]
   02 00 [rr] 00  00 00 [pg] [zz]  00 00 00 [xx]   [init sent, paper feeding]
   02 00 [rr] 00  10 00 [pg] [zz]  00 00 00 [xx]   [init sent, paper feeding]
   02 00 [rr] 00  70 00 [pg] [zz]  00 00 00 [xx]   [waiting for Y data]
   04 00 00 00  00 00 [pg] [zz]  00 00 00 [xx]   [waiting for M data]
   08 00 00 00  00 00 [pg] [zz]  00 00 00 [xx]   [waiting for C data]
   10 00 00 00  00 00 [pg] [zz]  00 00 00 [xx]   [C done, waiting]
   20 00 00 00  00 00 [pg] [zz]  00 00 00 [xx]   [All done]

   [xx] is 0x01 on the CP780/CP800/CP900, 0x00 on all others.

   [rr] is error code:
   	0x00 no error
	0x01 paper out
	0x04 ribbon problem
	0x08 ribbon depleted

   [ss] is either 0x00 or 0x70.  Unsure as to its significance; perhaps it
	means paper or ribbon is already set to go?

   [pg] is as follows:

      'P' paper 0x11
      'L' paper 0x22
      'C' paper 0x33
      'W' paper 0x44

      First four bits are paper, second four bits are the ribbon.  They aren't
      necessarily identical.  So it's possible to have a code of, say,
      0x41 if the 'Wide' paper tray is loaded with a 'P' ribbon. A '0' is used
      to signify nothing being loaded.

   [zz] is 0x01 when on battery power, 0x00 otherwise.

*/
