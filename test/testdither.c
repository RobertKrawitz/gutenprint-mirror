/*
 * "$Id$"
 *
 *   Test/profiling program for dithering code.
 *
 *   Copyright 1997-2000 Michael Sweet (mike@easysw.com)
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
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#ifdef INCLUDE_GIMP_PRINT_H
#include INCLUDE_GIMP_PRINT_H
#else
#include <gimp-print/gimp-print.h>
#endif
#include "../lib/libprintut.h"
#include "../src/main/gimp-print-internal.h"
#include "../src/main/weave.h"
#include "../src/main/bit-ops.h"
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>

/*
 * Definitions for dither test...
 */

#define IMAGE_WIDTH	5760	/* 8in * 720dpi */
#define IMAGE_HEIGHT	720	/* 4in * 720dpi */
#define BUFFER_SIZE	IMAGE_WIDTH

#define IMAGE_MIXED	0	/* Mix of line types */
#define IMAGE_WHITE	1	/* All white image */
#define IMAGE_BLACK	2	/* All black image */
#define IMAGE_COLOR	3	/* All color image */
#define IMAGE_RANDOM	4	/* All random image */

#define DITHER_GRAY	0	/* Dither grayscale pixels */
#define DITHER_COLOR	1	/* Dither color pixels */
#define DITHER_PHOTO	2	/* Dither photo pixels */
#define DITHER_CMYK	3	/* Dither photo pixels */
#define DITHER_PHOTO_CMYK	4	/* Dither photo pixels */


/*
 * Globals...
 */

int		image_type = IMAGE_MIXED;
int		stpi_dither_type = DITHER_COLOR;
const char     *dither_name = NULL;
int		dither_bits = 1;
unsigned short	white_line[IMAGE_WIDTH * 4],
		black_line[IMAGE_WIDTH * 4],
		color_line[IMAGE_WIDTH * 4],
		random_line[IMAGE_WIDTH * 4];


stpi_dither_range_simple_t normal_1bit_ranges[] =
{
  { 1.0,  0x1, 0, 1 }
};

stpi_dither_range_simple_t normal_2bit_ranges[] =
{
  { 0.45,  0x1, 0, 1 },
  { 0.68,  0x2, 0, 2 },
  { 1.0,   0x3, 0, 3 }
};

stpi_dither_range_simple_t photo_1bit_ranges[] =
{
  { 0.33, 0x1, 1, 1 },
  { 1.0,  0x1, 0, 1 }
};

stpi_dither_range_simple_t photo_2bit_ranges[] =
{
  { 0.15,  0x1, 1, 1 },
  { 0.227, 0x2, 1, 2 },
  { 0.45,  0x1, 0, 1 },
  { 0.68,  0x2, 0, 2 },
  { 1.0,   0x3, 0, 3 }
};


double compute_interval(struct timeval *tv1, struct timeval *tv2);
void   image_init(void);
void   image_get_row(unsigned short *data, int row);
void   write_gray(FILE *fp, unsigned char *black);
void   write_color(FILE *fp, unsigned char *cyan, unsigned char *magenta,
                   unsigned char *yellow, unsigned char *black);
void   write_photo(FILE *fp, unsigned char *cyan, unsigned char *lcyan,
                   unsigned char *magenta, unsigned char *lmagenta,
                   unsigned char *yellow, unsigned char *black);


double
compute_interval(struct timeval *tv1, struct timeval *tv2)
{
  return ((double) tv2->tv_sec + (double) tv2->tv_usec / 1000000.) -
    ((double) tv1->tv_sec + (double) tv1->tv_usec / 1000000.);
}

static void
writefunc(void *file, const char *buf, size_t bytes)
{
  FILE *prn = (FILE *)file;
  fwrite(buf, 1, bytes, prn);
}

static int global_bpp = 0;

static int
image_bpp(stp_image_t *image)
{
  return global_bpp;
}

static int
image_width(stp_image_t *image)
{
  return IMAGE_WIDTH;
}

static stp_image_t theImage =
{
  NULL,
  NULL,
  image_bpp,
  image_width,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL
};

/*
 * 'main()' - Test dithering code for performance measurement.
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line arguments */
     char *argv[])			/* I - Command-line arguments */
{
  int		i, j;			/* Looping vars */
  unsigned char	black[BUFFER_SIZE],	/* Black bitmap data */
		cyan[BUFFER_SIZE],	/* Cyan bitmap data */
		magenta[BUFFER_SIZE],	/* Magenta bitmap data */
		lcyan[BUFFER_SIZE],	/* Light cyan bitmap data */
		lmagenta[BUFFER_SIZE],	/* Light magenta bitmap data */
		yellow[BUFFER_SIZE];	/* Yellow bitmap data */
  unsigned short rgb[IMAGE_WIDTH * 4],	/* RGB buffer */
		gray[IMAGE_WIDTH];	/* Grayscale buffer */
  int		write_image;		/* Write the image to disk? */
  FILE		*fp = NULL;		/* PPM/PGM output file */
  char		filename[1024];		/* Name of file */
  stp_vars_t	v; 		        /* Dither variables */
  stp_parameter_t desc;
  static const char	*stpi_dither_types[] =	/* Different dithering modes */
		{
		  "gray",
		  "color",
		  "photo",
		  "cmyk",
		  "photocmyk"
		};
  static const char	*image_types[] =	/* Different image types */
		{
		  "mixed",
		  "white",
		  "black",
		  "colorimage",
		  "random"
		};
  struct timeval tv1, tv2;
  int bpp = 0;
  int quiet = 0;

 /*
  * Initialise libgimpprint
  */

  stp_init();
  v = stp_vars_create();
  stp_set_driver(v, "escp2-ex");
  stp_describe_parameter(v, "DitherAlgorithm", &desc);

 /*
  * Get command-line args...
  */

  write_image = 1;

  for (i = 1; i < argc; i ++)
  {
    if (strcmp(argv[i], "no-image") == 0)
    {
      write_image = 0;
      continue;
    }

    if (strcmp(argv[i], "quiet") == 0)
    {
      quiet = 1;
      continue;
    }

    if (strcmp(argv[i], "1-bit") == 0)
    {
      dither_bits = 1;
      continue;
    }

    if (strcmp(argv[i], "2-bit") == 0)
    {
      dither_bits = 2;
      continue;
    }

    for (j = 0; j < 5; j ++)
      if (strcmp(argv[i], stpi_dither_types[j]) == 0)
        break;

    if (j < 5)
    {
      stpi_dither_type = j;
      continue;
    }

    for (j = 0; j < 5; j ++)
      if (strcmp(argv[i], image_types[j]) == 0)
        break;

    if (j < 5)
    {
      image_type = j;
      continue;
    }

    for (j = 0; j < stp_string_list_count(desc.bounds.str); j ++)
      if (strcmp(argv[i], stp_string_list_param(desc.bounds.str,j)->name) == 0)
	dither_name = stp_string_list_param(desc.bounds.str, j)->name;

    if (!dither_name)
      printf("Unknown option \"%s\" ignored!\n", argv[i]);
  }

 /*
  * Setup the image and color functions...
  */

  image_init();
  stp_set_outfunc(v, writefunc);
  stp_set_errfunc(v, writefunc);
  stp_set_outdata(v, stdout);
  stp_set_errdata(v, stderr);

 /*
  * Output the page...
  */

  if (dither_name)
    stp_set_string_parameter(v, "DitherAlgorithm", dither_name);

  switch (stpi_dither_type)
    {
    case DITHER_GRAY:
      stp_set_output_type(v, OUTPUT_GRAY);
      bpp = 1;
      break;
    case DITHER_COLOR:
    case DITHER_PHOTO:
      stp_set_output_type(v, OUTPUT_COLOR);
      bpp = 3;
      break;
    case DITHER_CMYK:
    case DITHER_PHOTO_CMYK:
      stp_set_output_type(v, OUTPUT_RAW_CMYK);
      bpp = 4;
      break;
    }

  stpi_dither_init(v, &theImage, IMAGE_WIDTH, 1, 1);

  if (stpi_dither_type == DITHER_PHOTO)
    stp_set_float_parameter(v, "GCRLower", 0.4 / dither_bits + 0.1);
  else
    stp_set_float_parameter(v, "GCRLower", 0.25 / dither_bits);

  stp_set_float_parameter(v, "GCRUpper", .5);

  switch (stpi_dither_type)
  {
    case DITHER_GRAY :
        switch (dither_bits)
	{
	  case 1 :
              stpi_dither_set_ranges(v, ECOLOR_K, 1, normal_1bit_ranges, 1.0);
	      break;
	  case 2 :
	      stpi_dither_set_transition(v, 0.5);
              stpi_dither_set_ranges(v, ECOLOR_K, 3, normal_2bit_ranges, 1.0);
	      break;
       }
       break;
    case DITHER_COLOR :
    case DITHER_CMYK :
        switch (dither_bits)
	{
	  case 1 :
              stpi_dither_set_ranges(v, ECOLOR_C, 1, normal_1bit_ranges, 1.0);
              stpi_dither_set_ranges(v, ECOLOR_M, 1, normal_1bit_ranges, 1.0);
              stpi_dither_set_ranges(v, ECOLOR_Y, 1, normal_1bit_ranges, 1.0);
              stpi_dither_set_ranges(v, ECOLOR_K, 1, normal_1bit_ranges, 1.0);
	      break;
	  case 2 :
	      stpi_dither_set_transition(v, 0.5);
              stpi_dither_set_ranges(v, ECOLOR_C, 3, normal_2bit_ranges, 1.0);
              stpi_dither_set_ranges(v, ECOLOR_M, 3, normal_2bit_ranges, 1.0);
              stpi_dither_set_ranges(v, ECOLOR_Y, 3, normal_2bit_ranges, 1.0);
              stpi_dither_set_ranges(v, ECOLOR_K, 3, normal_2bit_ranges, 1.0);
	      break;
       }
       break;
    case DITHER_PHOTO :
    case DITHER_PHOTO_CMYK :
        switch (dither_bits)
	{
	  case 1 :
              stpi_dither_set_ranges(v, ECOLOR_C, 2, photo_1bit_ranges, 1.0);
              stpi_dither_set_ranges(v, ECOLOR_M, 2, photo_1bit_ranges, 1.0);
              stpi_dither_set_ranges(v, ECOLOR_Y, 1, normal_1bit_ranges, 1.0);
              stpi_dither_set_ranges(v, ECOLOR_K, 1, normal_1bit_ranges, 1.0);
	      break;
	  case 2 :
	      stpi_dither_set_transition(v, 0.7);
              stpi_dither_set_ranges(v, ECOLOR_C, 5, photo_2bit_ranges, 1.0);
              stpi_dither_set_ranges(v, ECOLOR_M, 5, photo_2bit_ranges, 1.0);
              stpi_dither_set_ranges(v, ECOLOR_Y, 3, normal_2bit_ranges, 1.0);
              stpi_dither_set_ranges(v, ECOLOR_K, 3, normal_2bit_ranges, 1.0);
	      break;
       }
       break;
  }

  stpi_dither_set_ink_spread(v, 12 + dither_bits);

 /*
  * Open the PPM/PGM file...
  */


  sprintf(filename, "%s-%s-%s-%dbit.%s", image_types[image_type],
	  stpi_dither_types[stpi_dither_type],
	  dither_name ? dither_name : desc.deflt.str, dither_bits,
	  (stpi_dither_type == DITHER_GRAY) ? "pgm" : "ppm");

  if (!quiet)
    printf("%s ", filename);

  if (write_image)
    {
      if ((fp = fopen(filename, "wb")) != NULL)
	{
	  puts(filename);
	  if (stpi_dither_type == DITHER_GRAY)
	    fputs("P5\n", fp);
	  else
	    fputs("P6\n", fp);

	  fprintf(fp, "%d\n%d\n255\n", IMAGE_WIDTH, IMAGE_HEIGHT);
	}
      else
	perror("Create");
    }

 /*
  * Now dither the "page"...
  */

  switch (stpi_dither_type)
    {
    case DITHER_PHOTO:
    case DITHER_PHOTO_CMYK :
      stpi_dither_add_channel(v, lcyan, ECOLOR_C, 1);
      stpi_dither_add_channel(v, lmagenta, ECOLOR_M, 1);
      /* FALLTHROUGH */
    case DITHER_COLOR:
    case DITHER_CMYK :
      stpi_dither_add_channel(v, cyan, ECOLOR_C, 0);
      stpi_dither_add_channel(v, magenta, ECOLOR_M, 0);
      stpi_dither_add_channel(v, yellow, ECOLOR_Y, 0);
      /* FALLTHROUGH */
    case DITHER_GRAY:
      stpi_dither_add_channel(v, black, ECOLOR_K, 0);
    }

  (void) gettimeofday(&tv1, NULL);

  for (i = 0; i < IMAGE_HEIGHT; i ++)
  {
    if (!quiet && (i & 63) == 0)
    {
      printf("\rProcessing row %d...", i);
      fflush(stdout);
    }

    switch (stpi_dither_type)
    {
      case DITHER_GRAY :
          image_get_row(gray, i);
	  stpi_dither(v, i, gray, 0, 0);
	  if (fp)
	    write_gray(fp, black);
	  break;
      case DITHER_COLOR :
      case DITHER_CMYK :
          image_get_row(rgb, i);
	  stpi_dither(v, i, rgb, 0, 0);
	  if (fp)
	    write_color(fp, cyan, magenta, yellow, black);
	  break;
      case DITHER_PHOTO :
      case DITHER_PHOTO_CMYK :
          image_get_row(rgb, i);
	  stpi_dither(v, i, rgb, 0, 0);
	  if (fp)
	    write_photo(fp, cyan, lcyan, magenta, lmagenta, yellow, black);
	  break;
    }
  }

  (void) gettimeofday(&tv2, NULL);

  stp_vars_free(v);

  if (fp != NULL)
    fclose(fp);

  if (quiet)
    fputc('.', stdout);
  else
    printf("\r%-40s Total dither time for %d pixels is %.3f seconds, or %.2f pixels/sec.\n",
	   filename, IMAGE_WIDTH * IMAGE_HEIGHT, compute_interval(&tv1, &tv2),
	   (float)(IMAGE_WIDTH * IMAGE_HEIGHT) / compute_interval(&tv1, &tv2));
  return 0;
}


void
image_get_row(unsigned short *data,
              int            row)
{
  unsigned short *src;


  switch (image_type)
    {
    case IMAGE_MIXED :
      switch ((row / 100) & 3)
	{
	case 0 :
	  src = white_line;
	  break;
	case 1 :
	  src = color_line;
	  break;
	case 2 :
	  src = black_line;
	  break;
	case 3 :
	default:
	  src = random_line;
	  break;
	}
      break;
    case IMAGE_WHITE :
      src = white_line;
      break;
    case IMAGE_BLACK :
      src = black_line;
      break;
    case IMAGE_COLOR :
      src = color_line;
      break;
    case IMAGE_RANDOM :
    default:
      src = random_line;
      break;
    }

  switch (stpi_dither_type)
    {
    case DITHER_GRAY:
      memcpy(data, src, IMAGE_WIDTH * 2);
      break;
    case DITHER_COLOR:
    case DITHER_PHOTO:
      memcpy(data, src, IMAGE_WIDTH * 6);
      break;
    case DITHER_CMYK:
    case DITHER_PHOTO_CMYK:
      memcpy(data, src, IMAGE_WIDTH * 8);
      break;
    }
}


void
image_init(void)
{
  int			i, j;
  unsigned short	*cptr,
			*rptr;


 /*
  * Set the white and black line data...
  */

  memset(white_line, 0, sizeof(white_line));
  memset(black_line, 255, sizeof(black_line));

 /*
  * Fill in the color and random data...
  */

  for (i = IMAGE_WIDTH, cptr = color_line, rptr = random_line; i > 0; i --)
  {
   /*
    * Do 64 color or grayscale blocks over the line...
    */

    j = i / (IMAGE_WIDTH / 64);

    switch (stpi_dither_type)
      {
      case DITHER_GRAY:
	*cptr++ = 65535 * j / 63;
	*rptr++ = 65535 * (rand() & 255) / 255;
	break;
      case DITHER_COLOR:
      case DITHER_PHOTO:
	*cptr++ = 65535 * (j >> 4) / 3;
	*cptr++ = 65535 * ((j >> 2) & 3) / 3;
	*cptr++ = 65535 * (j & 3) / 3;
	*rptr++ = 65535 * (rand() & 255) / 255;
	*rptr++ = 65535 * (rand() & 255) / 255;
	*rptr++ = 65535 * (rand() & 255) / 255;
	break;
      case DITHER_CMYK:
      case DITHER_PHOTO_CMYK:
	*cptr++ = 65535 * (j >> 4) / 3;
	*cptr++ = 65535 * ((j >> 2) & 3) / 3;
	*cptr++ = 65535 * (j & 3) / 3;
	*cptr++ = 65535 * j / 63;
	*rptr++ = 65535 * (rand() & 255) / 255;
	*rptr++ = 65535 * (rand() & 255) / 255;
	*rptr++ = 65535 * (rand() & 255) / 255;
	*rptr++ = 65535 * (rand() & 255) / 255;
	break;
      }
  }
}


void
write_gray(FILE          *fp,
           unsigned char *black)
{
  int		count;
  unsigned char	byte,
		bit,
		shift;


  if (dither_bits == 1)
  {
    for (count = IMAGE_WIDTH, byte = *black++, bit = 128; count > 0; count --)
    {
      if (byte & bit)
        putc(0, fp);
      else
        putc(255, fp);

      if (bit > 1)
        bit >>= 1;
      else
      {
        byte = *black++;
	bit  = 128;
      }
    }
  }
  else
  {
    unsigned char kb[BUFFER_SIZE];
    unsigned char *kbuf = kb;
    stpi_fold(black, IMAGE_WIDTH / 8, kbuf);
    for (count = IMAGE_WIDTH, byte = *kbuf++, shift = 6; count > 0; count --)
    {
      putc(255 - 255 * ((byte >> shift) & 3) / 3, fp);

      if (shift > 0)
        shift -= 2;
      else
      {
        byte  = *kbuf++;
	shift = 6;
      }
    }
  }
}


void
write_color(FILE          *fp,
            unsigned char *cyan,
	    unsigned char *magenta,
            unsigned char *yellow,
	    unsigned char *black)
{
  int		count;
  unsigned char	cbyte,
                mbyte,
                ybyte,
                kbyte,
		bit,
		shift;
  int		r, g, b, k;


  if (dither_bits == 1)
  {
    for (count = IMAGE_WIDTH, cbyte = *cyan++, mbyte = *magenta++,
             ybyte = *yellow++, kbyte = *black++, bit = 128;
	 count > 0;
	 count --)
    {
      if (kbyte & bit)
      {
        putc(0, fp);
        putc(0, fp);
        putc(0, fp);
      }
      else
      {
	if (cbyte & bit)
          putc(0, fp);
	else
          putc(255, fp);

	if (mbyte & bit)
          putc(0, fp);
	else
          putc(255, fp);

	if (ybyte & bit)
          putc(0, fp);
	else
          putc(255, fp);
      }

      if (bit > 1)
        bit >>= 1;
      else
      {
        cbyte = *cyan++;
        mbyte = *magenta++;
        ybyte = *yellow++;
        kbyte = *black++;
	bit   = 128;
      }
    }
  }
  else
  {
    unsigned char kb[BUFFER_SIZE];
    unsigned char cb[BUFFER_SIZE];
    unsigned char mb[BUFFER_SIZE];
    unsigned char yb[BUFFER_SIZE];
    unsigned char *kbuf = kb;
    unsigned char *cbuf = cb;
    unsigned char *mbuf = mb;
    unsigned char *ybuf = yb;
    stpi_fold(black, IMAGE_WIDTH / 8, kbuf);
    stpi_fold(cyan, IMAGE_WIDTH / 8, cbuf);
    stpi_fold(magenta, IMAGE_WIDTH / 8, mbuf);
    stpi_fold(yellow, IMAGE_WIDTH / 8, ybuf);
    for (count = IMAGE_WIDTH, cbyte = *cbuf++, mbyte = *mbuf++,
             ybyte = *ybuf++, kbyte = *kbuf++, shift = 6;
	 count > 0;
	 count --)
    {
      k = 255 * ((kbyte >> shift) & 3) / 3;
      r = 255 - 255 * ((cbyte >> shift) & 3) / 3 - k;
      g = 255 - 255 * ((mbyte >> shift) & 3) / 3 - k;
      b = 255 - 255 * ((ybyte >> shift) & 3) / 3 - k;

      if (r < 0)
        putc(0, fp);
      else
        putc(r, fp);

      if (g < 0)
        putc(0, fp);
      else
        putc(g, fp);

      if (b < 0)
        putc(0, fp);
      else
        putc(b, fp);

      if (shift > 0)
        shift -= 2;
      else
      {
        cbyte = *cbuf++;
        mbyte = *mbuf++;
        ybyte = *ybuf++;
        kbyte = *kbuf++;
	shift = 6;
      }
    }
  }
}


void
write_photo(FILE          *fp,
            unsigned char *cyan,
            unsigned char *lcyan,
            unsigned char *magenta,
	    unsigned char *lmagenta,
            unsigned char *yellow,
            unsigned char *black)
{
  int		count;
  unsigned char	cbyte,
		lcbyte,
                mbyte,
                lmbyte,
                ybyte,
                kbyte,
		bit,
		shift;
  int		r, g, b, k;


  if (dither_bits == 1)
  {
    for (count = IMAGE_WIDTH, cbyte = *cyan++, lcbyte = *lcyan++,
             mbyte = *magenta++, lmbyte = *lmagenta++,
             ybyte = *yellow++, kbyte = *black++, bit = 128;
	 count > 0;
	 count --)
    {
      if (kbyte & bit)
      {
        putc(0, fp);
        putc(0, fp);
        putc(0, fp);
      }
      else
      {
	if (cbyte & bit)
          putc(0, fp);
	else if (lcbyte & bit)
          putc(127, fp);
	else
          putc(255, fp);

	if (mbyte & bit)
          putc(0, fp);
	else if (lmbyte & bit)
          putc(127, fp);
	else
          putc(255, fp);

	if (ybyte & bit)
          putc(0, fp);
	else
          putc(255, fp);
      }

      if (bit > 1)
        bit >>= 1;
      else
      {
        cbyte  = *cyan++;
        lcbyte = *lcyan++;
        mbyte  = *magenta++;
        lmbyte = *lmagenta++;
        ybyte  = *yellow++;
        kbyte  = *black++;
	bit    = 128;
      }
    }
  }
  else
  {
    unsigned char kb[BUFFER_SIZE];
    unsigned char cb[BUFFER_SIZE];
    unsigned char mb[BUFFER_SIZE];
    unsigned char lcb[BUFFER_SIZE];
    unsigned char lmb[BUFFER_SIZE];
    unsigned char yb[BUFFER_SIZE];
    unsigned char *kbuf = kb;
    unsigned char *cbuf = cb;
    unsigned char *mbuf = mb;
    unsigned char *lcbuf = lcb;
    unsigned char *lmbuf = lmb;
    unsigned char *ybuf = yb;
    stpi_fold(black, IMAGE_WIDTH / 8, kbuf);
    stpi_fold(cyan, IMAGE_WIDTH / 8, cbuf);
    stpi_fold(magenta, IMAGE_WIDTH / 8, mbuf);
    stpi_fold(yellow, IMAGE_WIDTH / 8, ybuf);
    stpi_fold(lcyan, IMAGE_WIDTH / 8, lcbuf);
    stpi_fold(lmagenta, IMAGE_WIDTH / 8, lmbuf);
    for (count = IMAGE_WIDTH,  cbyte = *cbuf++, mbyte = *mbuf++,
	   ybyte = *ybuf++, kbyte = *kbuf++, lmbyte = *lmbuf++,
	   lcbyte = *lcyan++, shift = 6;
	 count > 0;
	 count --)
    {
      k = 255 * ((kbyte >> shift) & 3) / 3;
      r = 255 - 255 * ((cbyte >> shift) & 3) / 3 -
          127 * ((lcbyte >> shift) & 3) / 3 - k;
      g = 255 - 255 * ((mbyte >> shift) & 3) / 3 -
          127 * ((lmbyte >> shift) & 3) / 3 - k;
      b = 255 - 255 * ((ybyte >> shift) & 3) / 3 - k;

      if (r < 0)
        putc(0, fp);
      else
        putc(r, fp);

      if (g < 0)
        putc(0, fp);
      else
        putc(g, fp);

      if (b < 0)
        putc(0, fp);
      else
        putc(b, fp);

      if (shift > 0)
        shift -= 2;
      else
      {
        cbyte = *cbuf++;
        mbyte = *mbuf++;
        ybyte = *ybuf++;
        kbyte = *kbuf++;
        lmbyte = *lmbuf++;
        lcbyte = *lcbuf++;
	shift  = 6;
      }
    }
  }
}


/*
 * End of "$Id$".
 */
