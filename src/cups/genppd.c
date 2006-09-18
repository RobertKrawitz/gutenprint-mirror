/*
 * "$Id$"
 *
 *   PPD file generation program for the CUPS drivers.
 *
 *   Copyright 1993-2006 by Easy Software Products and Robert Krawitz.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License,
 *   version 2, as published by the Free Software Foundation.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, please contact Easy Software
 *   Products at:
 *
 *       Attn: CUPS Licensing Information
 *       Easy Software Products
 *       44141 Airport View Drive, Suite 204
 *       Hollywood, Maryland 20636-3111 USA
 *
 *       Voice: (301) 373-9603
 *       EMail: cups-info@cups.org
 *         WWW: http://www.cups.org
 *
 * Contents:
 *
 *   main()        - Process files on the command-line...
 *   usage()       - Show program usage.
 *   help()        - Show detailed program usage.
 *   getlangs()    - Get available translations.
 *   printlangs()  - Show available translations.
 *   printmodels() - Show available printer models.
 *   checkcat()    - Check message catalogue exists.
 *   write_ppd()   - Write a PPD file.
 */

/*
 * Include necessary headers...
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#ifdef CUPS_DRIVER_INTERFACE
#  ifdef HAVE_LIBZ
#    undef HAVE_LIBZ
#  endif /* HAVE_LIBZ */
#endif /* CUPS_DRIVER_INTERFACE */

#ifdef HAVE_LIBZ
#include <zlib.h>
#endif

#include <cups/cups.h>
#include <cups/raster.h>

#include <gutenprint/gutenprint.h>
#include <gutenprint/gutenprint-intl.h>

/*
 * Note:
 *
 * The current release of ESP Ghostscript is fully Level 3 compliant,
 * so we can report Level 3 support by default...
 */

int cups_ppd_ps_level = CUPS_PPD_PS_LEVEL;

static const char *cups_modeldir = CUPS_MODELDIR;

/*
 * File handling stuff...
 */

static const char *ppdext = ".ppd";
#ifdef HAVE_LIBZ
static const char *gzext = ".gz";
#else
static const char *gzext = "";
#  define gzFile FILE *
#  define gzopen fopen
#  define gzclose fclose
#  define gzprintf fprintf
#  define gzputs(f,s) fputs((s),(f))
#  define gzputc(f,c) putc((c),(f))
#endif /* HAVE_LIBZ */


/*
 * Size data...
 */

#define DEFAULT_SIZE	"Letter"
/*#define DEFAULT_SIZE	"A4"*/
#define CATALOG       "LC_MESSAGES/gutenprint.mo"

typedef struct				/**** Media size values ****/
{
  const char	*name,			/* Media size name */
		*text;			/* Media size text */
  int		width,			/* Media width */
		height,			/* Media height */
		left,			/* Media left margin */
		right,			/* Media right margin */
		bottom,			/* Media bottom margin */
		top;			/* Media top margin */
} paper_t;

const char *special_options[] =
{
  "PageSize",
  "MediaType",
  "InputSlot",
  "Resolution",
  "OutputOrder",
  "Quality",
  "Duplex",
  NULL
};

const char *parameter_class_names[] =
{
  N_("Printer Features"),
  N_("Output Control")
};

const char *parameter_level_names[] =
{
  N_("Common"),
  N_("Extra 1"),
  N_("Extra 2"),
  N_("Extra 3"),
  N_("Extra 4"),
  N_("Extra 5")
};


/*
 * Local functions...
 */

#ifdef CUPS_DRIVER_INTERFACE
static int	cat_ppd(const char *uri);
#endif /* CUPS_DRIVER_INTERFACE */
static int	checkcat(const struct dirent *localedir);
#ifndef CUPS_DRIVER_INTERFACE
static int	generate_ppd(const char *prefix, int verbose,
		             const stp_printer_t *p, const char *language,
			     int simplified);
#endif /* !CUPS_DRIVER_INTERFACE */
static char	**getlangs(void);
static void	help(void);
static int	is_special_option(const char *name);
#ifdef CUPS_DRIVER_INTERFACE
static int	list_ppds(const char *argv0);
#endif /* CUPS_DRIVER_INTERFACE */
static void	print_group_close(gzFile fp, stp_parameter_class_t p_class,
				  stp_parameter_level_t p_level);
static void	print_group_open(gzFile fp, stp_parameter_class_t p_class,
				 stp_parameter_level_t p_level);
static void	printlangs(char** langs);
static void	printmodels(int verbose);
static int	stpi_scandir(const char *dir,
			     struct dirent ***namelist,
			     int (*sel)(const struct dirent *),
			     int (*cmp)(const void *, const void *));
static void	usage(void);
static int	write_ppd(gzFile fp, const stp_printer_t *p,
		          const char *language, int simplified);


/*
 * Global variables...
 */

const char *baselocaledir = PACKAGE_LOCALE_DIR;


/*
 * 'main()' - Process files on the command-line...
 */

int				    /* O - Exit status */
main(int  argc,			    /* I - Number of command-line arguments */
     char *argv[])		    /* I - Command-line arguments */
{
#ifdef CUPS_DRIVER_INTERFACE
 /*
  * Initialise libgutenprint
  */

  stp_init();

 /*
  * Process command-line...
  */

  if (argc == 2 && !strcmp(argv[1], "list"))
    return (list_ppds(argv[0]));
  else if (argc == 3 && !strcmp(argv[1], "cat"))
    return (cat_ppd(argv[2]));
  else
  {
    fprintf(stderr, "Usage: %s list\n", argv[0]);
    fprintf(stderr, "       %s cat URI\n", argv[0]);
    return (1);
  }


#else /* !CUPS_DRIVER_INTERFACE */
  int		i;		    /* Looping var */
  const char	*prefix;	    /* Directory prefix for output */
  const char	*language = NULL;   /* Language */
  const stp_printer_t *printer;	    /* Pointer to printer driver */
  int           verbose = 0;        /* Verbose messages */
  char          **langs = NULL;     /* Available translations */
  char          **models = NULL;    /* Models to output, all if NULL */
  int           opt_printlangs = 0; /* Print available translations */
  int           opt_printmodels = 0;/* Print available models */
  int           which_ppds = 2;	/* Simplified PPD's = 1, full = 2 */

 /*
  * Parse command-line args...
  */

  prefix   = CUPS_MODELDIR;

  for (;;)
  {
    if ((i = getopt(argc, argv, "23hvqc:p:l:LMVd:sa")) == -1)
      break;

    switch (i)
    {
    case '2':
      cups_ppd_ps_level = 2;
      break;
    case '3':
      cups_ppd_ps_level = 3;
      break;
    case 'h':
      help();
      break;
    case 'v':
      verbose = 1;
      break;
    case 'q':
      verbose = 0;
      break;
    case 'c':
      baselocaledir = optarg;
#  ifdef DEBUG
      fprintf(stderr, "DEBUG: baselocaledir: %s\n", baselocaledir);
#  endif
      break;
    case 'p':
      prefix = optarg;
#  ifdef DEBUG
      fprintf(stderr, "DEBUG: prefix: %s\n", prefix);
#  endif
      break;
    case 'l':
      language = optarg;
      break;
    case 'L':
      opt_printlangs = 1;
      break;
    case 'M':
      opt_printmodels = 1;
      break;
    case 'd':
      cups_modeldir = optarg;
      break;
    case 's':
      which_ppds = 1;
      break;
    case 'a':
      which_ppds = 3;
      break;
    case 'V':
      printf("cups-genppd version %s, "
	     "Copyright 1993-2006 by Easy Software Products and Robert Krawitz.\n\n",
	     VERSION);
      printf("Default CUPS PPD PostScript Level: %d\n", cups_ppd_ps_level);
      printf("Default PPD location (prefix):     %s\n", CUPS_MODELDIR);
      printf("Default base locale directory:     %s\n\n", PACKAGE_LOCALE_DIR);
      puts("This program is free software; you can redistribute it and/or\n"
	   "modify it under the terms of the GNU General Public License,\n"
	   "version 2, as published by the Free Software Foundation.\n"
	   "\n"
	   "This program is distributed in the hope that it will be useful,\n"
	   "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
	   "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
	   "GNU General Public License for more details.\n"
	   "\n"
	   "You should have received a copy of the GNU General Public License\n"
	   "along with this program; if not, please contact Easy Software\n"
	   "Products at:\n"
	   "\n"
	   "    Attn: CUPS Licensing Information\n"
	   "    Easy Software Products\n"
	   "    44141 Airport View Drive, Suite 204\n"
	   "    Hollywood, Maryland 20636-3111 USA\n"
	   "\n"
	   "    Voice: (301) 373-9603\n"
	   "    EMail: cups-info@cups.org\n"
	   "      WWW: http://www.cups.org\n");
      exit(EXIT_SUCCESS);
      break;
    default:
      usage();
      exit(EXIT_FAILURE);
      break;
    }
  }
  if (optind < argc) {
    int n, numargs;
    numargs = argc-optind;
    models = stp_malloc((numargs+1) * sizeof(char*));
    for (n=0; n<numargs; n++)
      {
	models[n] = argv[optind+n];
      }
    models[numargs] = (char*)NULL;

    n=0;
  }

#  ifdef ENABLE_NLS
  langs = getlangs();
#  endif

/*
 * Initialise libgutenprint
 */

  stp_init();

 /*
  * Set the language...
  */

  setlocale(LC_ALL, language ? language : "");
#  ifdef LC_CTYPE
  setlocale(LC_CTYPE, language ? language : "");
#  endif /* LC_CTYPE */
#  ifdef LC_NUMERIC
  setlocale(LC_NUMERIC, "C");
#  endif /* LC_NUMERIC */

 /*
  * Set up the catalog
  */

#  ifdef ENABLE_NLS
  if (baselocaledir)
  {
    if ((bindtextdomain(PACKAGE, baselocaledir)) == NULL)
    {
      fprintf(stderr, "cups-genppd: cannot load message catalog %s under %s: %s\n",
	      PACKAGE, baselocaledir, strerror(errno));
      exit(EXIT_FAILURE);
    }
#    ifdef DEBUG
    fprintf(stderr, "DEBUG: bound textdomain: %s under %s\n",
	     PACKAGE, baselocaledir);
#    endif
    if ((textdomain(PACKAGE)) == NULL)
    {
      fprintf(stderr, "cups-genppd: cannot select message catalog %s under %s: %s\n",
              PACKAGE, baselocaledir, strerror(errno));
      exit(EXIT_FAILURE);
    }
#    ifdef DEBUG
    fprintf(stderr, "DEBUG: textdomain set: %s\n", PACKAGE);
#    endif
  }
#  endif

  /*
   * Print lists
   */

  if (opt_printlangs)
    {
      printlangs(langs);
      exit(EXIT_SUCCESS);
    }

  if (opt_printmodels)
    {
      printmodels(verbose);
      exit(EXIT_SUCCESS);
    }

 /*
  * Write PPD files...
  */

  if (models)
    {
      int n;
      for (n=0; models[n]; n++)
	{
	  printer = stp_get_printer_by_driver(models[n]);
	  if (!printer)
	    printer = stp_get_printer_by_long_name(models[n]);

	  if (printer)
	    {
	      if ((which_ppds & 1) &&
		  generate_ppd(prefix, verbose, printer, language, 1))
		return (1);
	      if ((which_ppds & 2) &&
		  generate_ppd(prefix, verbose, printer, language, 0))
		return (1);
	    }
	  else
	    {
	      printf("Driver not found: %s\n", models[n]);
	      return (1);
	    }
	}
      stp_free(models);
    }
  else
    {
      for (i = 0; i < stp_printer_model_count(); i++)
	{
	  printer = stp_get_printer_by_index(i);

	  if (printer)
	    {
	      if ((which_ppds & 1) &&
		  generate_ppd(prefix, verbose, printer, language, 1))
		return (1);
	      if ((which_ppds & 2) &&
		  generate_ppd(prefix, verbose, printer, language, 0))
		return (1);
	    }
	}
    }
  if (!verbose)
    fprintf(stderr, " done.\n");
  if (langs)
    {
      char **langs_tmp = langs;
      while (*langs_tmp)
	{
	  stp_free(*langs_tmp);
	  langs_tmp++;
	}
      stp_free(langs);
    }

  return (0);
#endif /* CUPS_DRIVER_INTERFACE */
}


/*
 * 'cat_ppd()' - Copy the named PPD to stdout.
 */

#ifdef CUPS_DRIVER_INTERFACE
static int				/* O - Exit status */
cat_ppd(const char *uri)		/* I - Driver URI */
{
  char			scheme[64],	/* URI scheme */
			userpass[32],	/* URI user/pass (unused) */
			hostname[32],	/* URI hostname */
			resource[1024];	/* URI resource */
  int			port;		/* URI port (unused) */
  http_uri_status_t	status;		/* URI decode status */
  const stp_printer_t	*p;		/* Printer driver */


  if ((status = httpSeparateURI(HTTP_URI_CODING_ALL, uri,
                                scheme, sizeof(scheme),
                                userpass, sizeof(userpass),
				hostname, sizeof(hostname),
		                &port, resource, sizeof(resource)))
				    < HTTP_URI_OK)
  {
    fprintf(stderr, "ERROR: Bad ppd-name \"%s\" (%d)!\n", uri, status);
    return (1);
  }

  if ((p = stp_get_printer_by_driver(resource + 1)) == NULL)
  {
    fprintf(stderr, "ERROR: Unable to find driver \"%s\"!\n", resource + 1);
    return (1);
  }

  return (write_ppd(stdout, p, "en", !strcmp(hostname, "simple")));
}
#endif /* CUPS_DRIVER_INTERFACE */


/*
 * 'checkcat()' - A callback for stpi_scandir() to check
 *                if a message catalogue exists
 */

int
checkcat(const struct dirent *localedir)
{
  char* catpath;
  int catlen, status = 0, savederr;
  struct stat catstat;

  savederr = errno; /* since we are a callback, preserve stpi_scandir() state */

  /* LOCALEDIR / LANG / LC_MESSAGES/CATALOG */
  /* Add 3, for two '/' separators and '\0'   */
  catlen = strlen(baselocaledir) + strlen(localedir->d_name) + strlen(CATALOG) + 3;
  catpath = (char*)stp_malloc(catlen * sizeof(char));

  strncpy(catpath, baselocaledir, strlen(baselocaledir));
  catlen = strlen(baselocaledir);
  *(catpath+catlen) = '/';
  catlen++;
  strncpy(catpath+catlen, localedir->d_name, strlen(localedir->d_name));
  catlen += strlen(localedir->d_name);
  *(catpath+catlen) = '/';
  catlen++;
  strncpy(catpath+catlen, CATALOG, strlen(CATALOG));
  catlen += strlen(CATALOG);
  *(catpath+catlen) = '\0';

  if (!stat(catpath, &catstat))
    {
      if (S_ISREG(catstat.st_mode))
	{
	  status = 1;
	}
     }

  stp_free(catpath);

  errno = savederr;
  return status;
}


/*
 * 'dirent_sort()' - Sort directory entries.
 */

static int				/* O - Result of comparison */
dirent_sort(const void *a,		/* I - First entry */
	    const void *b)		/* I - Second entry */
{
  return strcoll((*(const struct dirent *const *)a)->d_name,
		 (*(const struct dirent *const *)b)->d_name);
}


/*
 * 'generate_ppd()' - Generate a PPD file.
 */

#ifndef CUPS_DRIVER_INTERFACE
static int				/* O - Exit status */
generate_ppd(
    const char          *prefix,	/* I - PPD directory prefix */
    int                 verbose,	/* I - Verbosity level */
    const stp_printer_t *p,		/* I - Driver */
    const char          *language,	/* I - Primary language */
    int                 simplified)	/* I - 1 = simplified options */
{
  int		status;			/* Exit status */
  gzFile	fp;			/* File to write to */
  char		filename[1024];		/* Filename */
  struct stat   dir;                    /* Prefix dir status */


 /*
  * Skip the PostScript drivers...
  */

  if (!strcmp(stp_printer_get_family(p), "ps") ||
      !strcmp(stp_printer_get_family(p), "raw"))
    return (0);

 /*
  * Make sure the destination directory exists...
  */

  if (stat(prefix, &dir) && !S_ISDIR(dir.st_mode))
  {
    if (mkdir(prefix, 0777))
    {
      printf("cups-genppd: Cannot create directory %s: %s\n",
	     prefix, strerror(errno));
      exit(EXIT_FAILURE);
    }
  }

 /*
  * The files will be named stp-<driver>.<major>.<minor>.ppd, for
  * example:
  * 
  * stp-escp2-ex.5.0.ppd
  * 
  * or
  * 
  * stp-escp2-ex.5.0.ppd.gz
  */

  snprintf(filename, sizeof(filename) - 1, "%s/stp-%s.%s%s%s%s",
	   prefix, stp_printer_get_driver(p), GUTENPRINT_RELEASE_VERSION,
	   simplified ? ".sim" : "", ppdext, gzext);

 /*
  * Open the PPD file...
  */

  if ((fp = gzopen(filename, "wb")) == NULL)
  {
    fprintf(stderr, "cups-genppd: Unable to create file \"%s\" - %s.\n",
            filename, strerror(errno));
    return (2);
  }

  if (verbose)
    fprintf(stderr, "Writing %s...\n", filename);
  else
    fprintf(stderr, ".");

  status = write_ppd(fp, p, language, simplified);

  gzclose(fp);

  return (status);
}
#endif /* !CUPS_DRIVER_INTERFACE */


/*
 * 'getlangs()' - Get a list of available translations
 */

char **
getlangs(void)
{
  struct dirent** langdirs = NULL;
  int n;
  char **langs;

  n = stpi_scandir(baselocaledir, &langdirs, checkcat, dirent_sort);
  if (n >= 0)
    {
      int idx;
      langs = stp_malloc((n+1) * sizeof(char*));
      for (idx = 0; idx < n; ++idx)
	{
	  langs[idx] = (char*)stp_malloc((strlen(langdirs[idx]->d_name)+1) * sizeof(char));
	  strcpy(langs[idx], langdirs[idx]->d_name);
	  free(langdirs[idx]); /* Must use plain free() */
	}
      langs[n] = NULL;
      free(langdirs); /* Must use plain free() */
    }
  else
    return NULL;

  return langs;
}


/*
 * 'help()' - Show detailed help.
 */

void
help(void)
{
  puts("Generate Gutenprint PPD files for use with CUPS\n\n");
  usage();
  puts("\nExamples: LANG=de_DE cups-genppd -p ppd -c /usr/share/locale\n"
       "          cups-genppd -L -c /usr/share/locale\n"
       "          cups-genppd -M -v\n\n"
       "Commands:\n"
       "  -h            Show this help message.\n"
       "  -L            List available translations (message catalogs).\n"
       "  -M            List available printer models.\n"
       "  -V            Show version information and defaults.\n"
       "  The default is to output PPDs.\n"
       "Options:\n"
       "  -c localedir  Use localedir as the base directory for locale data.\n"
       "  -l locale     Output PPDs translated with messages for locale.\n"
       "  -p prefix     Output PPDs in directory prefix.\n"
       "  -d prefix     Embed directory prefix in PPD file.\n"
       "  -s            Generate simplified PPD files.\n"
       "  -a            Generate all (simplified and full) PPD files.\n"
       "  -q            Quiet mode.\n"
       "  -v            Verbose mode.\n"
       "models:\n"
       "  A list of printer models, either the driver or quoted full name.\n");
}


/*
 * 'is_special_option()' - Determine if an option should be grouped.
 */

static int				/* O - 1 if non-grouped, 0 otherwise */
is_special_option(const char *name)	/* I - Option name */
{
  int i = 0;
  while (special_options[i])
    {
      if (strcmp(name, special_options[i]) == 0)
	return 1;
      i++;
    }
  return 0;
}


/*
 * 'list_ppds()' - List the available drivers.
 */

#ifdef CUPS_DRIVER_INTERFACE
static int				/* O - Exit status */
list_ppds(const char *argv0)		/* I - Name of program */
{
  const char		*scheme;	/* URI scheme */
  int			i;		/* Looping var */
  const stp_printer_t	*printer;	/* Pointer to printer driver */


  if ((scheme = strrchr(argv0, '/')) != NULL)
    scheme ++;
  else
    scheme = argv0;

  for (i = 0; i < stp_printer_model_count(); i++)
    if ((printer = stp_get_printer_by_index(i)) != NULL)
    {
      if (!strcmp(stp_printer_get_family(printer), "ps") ||
	  !strcmp(stp_printer_get_family(printer), "raw"))
        continue;

      printf("\"%s://expert/%s\" "
             "en "
	     "\"%s\" "
             "\"%s, Gutenprint" GUTENPRINT_RELEASE_VERSION " Expert\" "
	     "\"\"\n",			/* No IEEE-1284 Device ID yet */
             scheme, stp_printer_get_driver(printer),
	     stp_printer_get_manufacturer(printer),
	     stp_printer_get_long_name(printer));

      printf("\"%s://simple/%s\" "
             "en "
	     "\"%s\" "
             "\"%s, Gutenprint" GUTENPRINT_RELEASE_VERSION " Simplified\" "
	     "\"\"\n",			/* No IEEE-1284 Device ID yet */
             scheme, stp_printer_get_driver(printer),
	     stp_printer_get_manufacturer(printer),
	     stp_printer_get_long_name(printer));
    }

  return (0);
}
#endif /* CUPS_DRIVER_INTERFACE */


/*
 * 'print_group_close()' - Close a UI group.
 */

static void
print_group_close(
    gzFile                fp,		/* I - File to write to */
    stp_parameter_class_t p_class,	/* I - Option class */
    stp_parameter_level_t p_level)	/* I - Option level */
{
  gzprintf(fp, "*CloseGroup: %s %s\n\n",
	   gettext(parameter_class_names[p_class]),
	   gettext(parameter_level_names[p_level]));
}


/*
 * 'print_group_open()' - Open a new UI group.
 */

static void
print_group_open(
    gzFile                fp,		/* I - File to write to */
    stp_parameter_class_t p_class,	/* I - Option class */
    stp_parameter_level_t p_level)	/* I - Option level */
{
  gzprintf(fp, "*OpenGroup: %s %s\n\n",
	   gettext(parameter_class_names[p_class]),
	   gettext(parameter_level_names[p_level]));
}


/*
 * 'printlangs()' - Print list of available translations.
 */

void
printlangs(char **langs)		/* I - Languages */
{
  if (langs)
    {
      int n = 0;
      while (langs && langs[n])
	{
	  printf("%s\n", langs[n]);
	  n++;
	}
    }
  exit(EXIT_SUCCESS);
}


/*
 * 'printmodels()' - Print a list of available models.
 */

void
printmodels(int verbose)		/* I - Verbosity level */
{
  const stp_printer_t *p;
  int i;

  for (i = 0; i < stp_printer_model_count(); i++)
    {
      p = stp_get_printer_by_index(i);
      if (p &&
	  strcmp(stp_printer_get_family(p), "ps") != 0 &&
	  strcmp(stp_printer_get_family(p), "raw") != 0)
	{
	  if(verbose)
	    printf("%-20s%s\n", stp_printer_get_driver(p),
		   stp_printer_get_long_name(p));
	  else
	    printf("%s\n", stp_printer_get_driver(p));
	}
    }
  exit(EXIT_SUCCESS);
}


/* Adapted from GNU libc <dirent.h>
   These macros extract size information from a `struct dirent *'.
   They may evaluate their argument multiple times, so it must not
   have side effects.  Each of these may involve a relatively costly
   call to `strlen' on some systems, so these values should be cached.

   _D_EXACT_NAMLEN(DP) returns the length of DP->d_name, not including
   its terminating null character.

   _D_ALLOC_NAMLEN(DP) returns a size at least (_D_EXACT_NAMLEN (DP) + 1);
   that is, the allocation size needed to hold the DP->d_name string.
   Use this macro when you don't need the exact length, just an upper bound.
   This macro is less likely to require calling `strlen' than _D_EXACT_NAMLEN.
   */

#ifdef _DIRENT_HAVE_D_NAMLEN
# ifndef _D_EXACT_NAMLEN
#  define _D_EXACT_NAMLEN(d) ((d)->d_namlen)
# endif
# ifndef _D_ALLOC_NAMLEN
#  define _D_ALLOC_NAMLEN(d) (_D_EXACT_NAMLEN(d) + 1)
# endif
#else
# ifndef _D_EXACT_NAMLEN
#  define _D_EXACT_NAMLEN(d) (strlen((d)->d_name))
# endif
# ifndef _D_ALLOC_NAMLEN
#  ifdef _DIRENT_HAVE_D_RECLEN
#   define _D_ALLOC_NAMLEN(d) (((char *)(d) + (d)->d_reclen) - &(d)->d_name[0])
#  else
#   define _D_ALLOC_NAMLEN(d) (sizeof(d)->d_name > 1 ? sizeof(d)->d_name : \
                               _D_EXACT_NAMLEN(d) + 1)
#  endif
# endif
#endif

/*
 * 'stpi_scandir()' - BSD scandir() replacement.
 */

static int				/* O - Number of entries or -1 */
stpi_scandir(
    const char    *dir,			/* I - Directory to scan */
    struct dirent ***namelist,		/* O - Entries found */
    int           (*sel)(const struct dirent *),
					/* I - Selection function */
    int           (*cmp)(const void *, const void *))
					/* I - Sorting function */
{
  DIR *dp = opendir(dir);
  struct dirent **v = NULL;
  size_t vsize = 0, i;
  struct dirent *d;
  int save;

  if (dp == NULL)
    return -1;

  save = errno;
  errno = 0;

  i = 0;
  while ((d = readdir(dp)) != NULL)
    if (sel == NULL || (*sel)(d))
      {
	struct dirent *vnew;
	size_t dsize;

	/* Ignore errors from sel or readdir */
        errno = 0;

	if (i == vsize)
	  {
	    struct dirent **new;
	    if (vsize == 0)
	      vsize = 10;
	    else
	      vsize *= 2;
	    new = (struct dirent **)realloc(v, vsize * sizeof(*v));
	    if (new == NULL)
	      break;
	    v = new;
	  }

	dsize = &d->d_name[_D_ALLOC_NAMLEN(d)] - (char *)d;
	vnew = (struct dirent *)malloc(dsize);
	if (vnew == NULL)
	  break;

	v[i++] = (struct dirent *)memcpy(vnew, d, dsize);
      }

  if (errno != 0)
    {
      save = errno;

      while (i > 0)
	free(v[--i]);
      free(v);

      i = -1;
    }
  else
    {
      /* Sort the list if we have a comparison function to sort with.  */
      if (cmp != NULL)
	qsort(v, i, sizeof(*v), cmp);

      *namelist = v;
    }

  (void)closedir(dp);
  errno = save;

  return i;
}


/*
 * 'usage()' - Show program usage.
 */

void
usage(void)
{
  puts("Usage: cups-genppd [-c localedir] "
        "[-l locale] [-p prefix] [-s | -a] [-q] [-v] models...\n"
        "       cups-genppd -L [-c localedir]\n"
	"       cups-genppd -M [-v]\n"
	"       cups-genppd -h\n"
	"       cups-genppd -V\n");
}


/*
 * 'write_ppd()' - Write a PPD file.
 */

int					/* O - Exit status */
write_ppd(
    gzFile              fp,		/* I - File to write to */
    const stp_printer_t *p,		/* I - Printer driver */
    const char          *language,	/* I - Primary language */
    int                 simplified)	/* I - 1 = simplified options */
{
  int		i, j, k, l;		/* Looping vars */
  int		num_opts;		/* Number of printer options */
  int		xdpi, ydpi;		/* Resolution info */
  stp_vars_t	*v;			/* Variable info */
  int		width, height,		/* Page information */
		bottom, left,
		top, right;
  const char	*driver;		/* Driver name */
  const char	*long_name;		/* Driver long name */
  const char	*manufacturer;		/* Manufacturer of printer */
  const stp_vars_t *printvars;		/* Printer option names */
  paper_t	*the_papers;		/* Media sizes */
  int		cur_opt;		/* Current option */
  int		variable_sizes;		/* Does the driver support variable sizes? */
  int		min_width,		/* Min/max custom size */
		min_height,
		max_width,
		max_height;
  stp_parameter_t desc;
  stp_parameter_list_t param_list;
  const stp_param_string_t *opt;
  int has_quality_parameter = 0;
  int printer_is_color = 0;
  int maximum_level = simplified ?
    STP_PARAMETER_LEVEL_BASIC : STP_PARAMETER_LEVEL_ADVANCED4;


 /*
  * Initialize driver-specific variables...
  */

  driver     = stp_printer_get_driver(p);
  long_name  = stp_printer_get_long_name(p);
  manufacturer = stp_printer_get_manufacturer(p);
  printvars  = stp_printer_get_defaults(p);
  the_papers = NULL;
  cur_opt    = 0;

 /*
  * Write a standard header...
  */

  gzputs(fp, "*PPD-Adobe: \"4.3\"\n");
  gzputs(fp, "*%PPD file for CUPS/Gutenprint.\n");
  gzputs(fp, "*%Copyright 1993-2006 by Easy Software Products and Robert Krawitz.\n");
  gzputs(fp, "*%This program is free software; you can redistribute it and/or\n");
  gzputs(fp, "*%modify it under the terms of the GNU General Public License,\n");
  gzputs(fp, "*%version 2, as published by the Free Software Foundation.\n");
  gzputs(fp, "*%\n");
  gzputs(fp, "*%This program is distributed in the hope that it will be useful, but\n");
  gzputs(fp, "*%WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY\n");
  gzputs(fp, "*%or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License\n");
  gzputs(fp, "*%for more details.\n");
  gzputs(fp, "*%\n");
  gzputs(fp, "*%You should have received a copy of the GNU General Public License\n");
  gzputs(fp, "*%along with this program; if not, write to the Free Software\n");
  gzputs(fp, "*%Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.\n");
  gzputs(fp, "*%\n");
  gzputs(fp, "*FormatVersion:	\"4.3\"\n");
  gzputs(fp, "*FileVersion:	\"" VERSION "\"\n");
  /* Specify language of PPD translation */
  /* TRANSLATORS: Specify the language of the PPD translation.
   * Use the English name of your language here, e.g. "Swedish" instead of
   * "Svenska". */
  gzprintf(fp, "*LanguageVersion: %s\n", _("English"));
  /* TRANSLATORS: Specify PPD translation encoding e.g. ISOLatin1 */
  gzprintf(fp, "*LanguageEncoding: %s\n", _("ISOLatin1"));

 /*
  * Strictly speaking, the PCFileName attribute should be a 12 character
  * max (12345678.ppd) filename, as a requirement of the old PPD spec.
  * The following code generates a (hopefully unique) 8.3 filename from
  * the driver name, and makes the filename all UPPERCASE as well...
  */

  gzprintf(fp, "*PCFileName:	\"STP%05d.PPD\"\n",
	   stp_get_printer_index_by_driver(driver) +
	   simplified ? stp_printer_model_count() : 0);
  gzprintf(fp, "*Manufacturer:	\"%s\"\n", manufacturer);

 /*
  * The Product attribute specifies the string returned by the PostScript
  * interpreter.  The last one will appear in the CUPS "product" field,
  * while all instances are available as attributes.
  */

  gzputs(fp, "*Product:	\"(AFPL Ghostscript)\"\n");
  gzputs(fp, "*Product:	\"(GNU Ghostscript)\"\n");
  gzputs(fp, "*Product:	\"(ESP Ghostscript)\"\n");
  gzputs(fp, "*Product:	\"(GPL Ghostscript)\"\n");

 /*
  * The ModelName attribute now provides the long name rather than the
  * short driver name...  The rastertoprinter driver looks up both...
  */

  gzprintf(fp, "*ModelName:     \"%s\"\n", long_name);
  gzprintf(fp, "*ShortNickName: \"%s\"\n", long_name);

 /*
  * The Windows driver download stuff has problems with NickName fields
  * with commas.  Now use a dash instead...
  */

  /*
   * NOTE - code in rastertoprinter looks for this version string.
   * If this is changed, the corresponding change must be made in
   * rastertoprinter.c.  Look for "ppd->nickname"
   */
  gzprintf(fp, "*NickName:      \"%s%s%s%s\"\n",
	   long_name, CUPS_PPD_NICKNAME_STRING, VERSION,
	   simplified ? " Simplified" : "");
  if (cups_ppd_ps_level == 2)
    gzputs(fp, "*PSVersion:	\"(2017.000) 550\"\n");
  else
    {
      gzputs(fp, "*PSVersion:	\"(3010.000) 550\"\n");
      gzputs(fp, "*PSVersion:	\"(3010.000) 651\"\n");
      gzputs(fp, "*PSVersion:	\"(3010.000) 652\"\n");
      gzputs(fp, "*PSVersion:	\"(3010.000) 653\"\n");
      gzputs(fp, "*PSVersion:	\"(3010.000) 704\"\n");
      gzputs(fp, "*PSVersion:	\"(3010.000) 705\"\n");
      gzputs(fp, "*PSVersion:	\"(3010.000) 707\"\n");
      gzputs(fp, "*PSVersion:	\"(3010.000) 800\"\n");
      gzputs(fp, "*PSVersion:	\"(3010.000) 815\"\n");
      gzputs(fp, "*PSVersion:	\"(3010.000) 850\"\n");
      gzputs(fp, "*PSVersion:	\"(3010.000) 81501\"\n");
    }
  gzprintf(fp, "*LanguageLevel:	\"%d\"\n", cups_ppd_ps_level);

  /* Set Job Mode to "Job" as this enables the Duplex option */
  v = stp_vars_create_copy(printvars);
  stp_set_string_parameter(v, "JobMode", "Job");

  /* Assume that color printers are inkjets and should have pages reversed */
  stp_describe_parameter(v, "PrintingMode", &desc);
  if (desc.p_type == STP_PARAMETER_TYPE_STRING_LIST)
    {
      if (stp_string_list_is_present(desc.bounds.str, "Color"))
	{
	  printer_is_color = 1;
	  gzputs(fp, "*ColorDevice:	True\n");
	}
      else
	{
	  printer_is_color = 0;
	  gzputs(fp, "*ColorDevice:	False\n");
	}
      if (strcmp(desc.deflt.str, "Color") == 0)
	gzputs(fp, "*DefaultColorSpace:	RGB\n");
      else
	gzputs(fp, "*DefaultColorSpace:	Gray\n");
    }
  stp_parameter_description_destroy(&desc);
  gzputs(fp, "*FileSystem:	False\n");
  gzputs(fp, "*LandscapeOrientation: Plus90\n");
  gzputs(fp, "*TTRasterizer:	Type42\n");

  gzputs(fp, "*cupsVersion:	1.1\n");
  gzprintf(fp, "*cupsModelNumber: \"0\"\n");
  gzputs(fp, "*cupsManualCopies: True\n");
  gzprintf(fp, "*cupsFilter:	\"application/vnd.cups-raster 100 rastertogutenprint.%s\"\n", GUTENPRINT_RELEASE_VERSION);
  if (strcasecmp(manufacturer, "EPSON") == 0)
    gzputs(fp, "*cupsFilter:	\"application/vnd.cups-command 33 commandtoepson\"\n");
  gzputs(fp, "\n");
  gzprintf(fp, "*StpDriverName:	\"%s\"\n", driver);
  gzprintf(fp, "*StpPPDLocation:	\"%s%s%s/stp-%s.%s%s%s%s\"\n",
	   cups_modeldir,
	   cups_modeldir[strlen(cups_modeldir) - 1] == '/' ? "" : "/",
	   language ? language : "C",
	   driver,
	   GUTENPRINT_RELEASE_VERSION,
	   simplified ? ".sim" : "",
	   ppdext,
	   gzext);
  gzprintf(fp, "*StpLocale:	\"%s\"\n", language ? language : "C");	

 /*
  * Get the page sizes from the driver...
  */

  if (printer_is_color)
    stp_set_string_parameter(v, "PrintingMode", "Color");
  else
    stp_set_string_parameter(v, "PrintingMode", "BW");
  stp_set_string_parameter(v, "ChannelBitDepth", "8");
  variable_sizes = 0;
  stp_describe_parameter(v, "PageSize", &desc);
  num_opts = stp_string_list_count(desc.bounds.str);
  the_papers = stp_malloc(sizeof(paper_t) * num_opts);

  for (i = 0; i < num_opts; i++)
  {
    const stp_papersize_t *papersize;
    opt = stp_string_list_param(desc.bounds.str, i);
    papersize = stp_get_papersize_by_name(opt->name);

    if (!papersize)
    {
      printf("Unable to lookup size %s!\n", opt->name);
      continue;
    }

    if (strcmp(opt->name, "Custom") == 0)
    {
      variable_sizes = 1;
      continue;
    }
    if (simplified && num_opts >= 10 &&
	(papersize->paper_unit == PAPERSIZE_ENGLISH_EXTENDED ||
	 papersize->paper_unit == PAPERSIZE_METRIC_EXTENDED))
      continue;

    width  = papersize->width;
    height = papersize->height;

    if (width <= 0 || height <= 0)
      continue;

    stp_set_string_parameter(v, "PageSize", opt->name);

    stp_get_media_size(v, &width, &height);
    stp_get_maximum_imageable_area(v, &left, &right, &bottom, &top);

    if (left < 0)
      left = 0;
    if (right > width)
      right = width;
    if (bottom > height)
      bottom = height;
    if (top < 0)
      top = 0;

    the_papers[cur_opt].name   = opt->name;
    the_papers[cur_opt].text   = opt->text;
    the_papers[cur_opt].width  = width;
    the_papers[cur_opt].height = height;
    the_papers[cur_opt].left   = left;
    the_papers[cur_opt].right  = right;
    the_papers[cur_opt].bottom = height - bottom;
    the_papers[cur_opt].top    = height - top;

    cur_opt++;
  }

 /*
  * The VariablePaperSize attribute is obsolete, however some popular
  * applications still look for it to provide custom page size support.
  */

  gzprintf(fp, "*VariablePaperSize: %s\n\n", variable_sizes ? "true" : "false");

  gzputs(fp, "*OpenUI *PageSize: PickOne\n");
  gzputs(fp, "*OrderDependency: 10 AnySetup *PageSize\n");
  gzprintf(fp, "*DefaultPageSize: %s\n", desc.deflt.str);
  for (i = 0; i < cur_opt; i ++)
  {
    gzprintf(fp,  "*PageSize %s", the_papers[i].name);
    gzprintf(fp, "/%s:\t\"<</PageSize[%d %d]/ImagingBBox null>>setpagedevice\"\n",
             the_papers[i].text, the_papers[i].width, the_papers[i].height);
  }
  gzputs(fp, "*CloseUI: *PageSize\n\n");

  gzputs(fp, "*OpenUI *PageRegion: PickOne\n");
  gzputs(fp, "*OrderDependency: 10 AnySetup *PageRegion\n");
  gzprintf(fp, "*DefaultPageRegion: %s\n", desc.deflt.str);
  for (i = 0; i < cur_opt; i ++)
  {
    gzprintf(fp,  "*PageRegion %s", the_papers[i].name);
    gzprintf(fp, "/%s:\t\"<</PageSize[%d %d]/ImagingBBox null>>setpagedevice\"\n",
	     the_papers[i].text, the_papers[i].width, the_papers[i].height);
  }
  gzputs(fp, "*CloseUI: *PageRegion\n\n");

  gzprintf(fp, "*DefaultImageableArea: %s\n", desc.deflt.str);
  for (i = 0; i < cur_opt; i ++)
  {
    gzprintf(fp,  "*ImageableArea %s", the_papers[i].name);
    gzprintf(fp, "/%s:\t\"%d %d %d %d\"\n", the_papers[i].text,
             the_papers[i].left, the_papers[i].bottom,
	     the_papers[i].right, the_papers[i].top);
  }
  gzputs(fp, "\n");

  gzprintf(fp, "*DefaultPaperDimension: %s\n", desc.deflt.str);

  for (i = 0; i < cur_opt; i ++)
  {
    gzprintf(fp, "*PaperDimension %s", the_papers[i].name);
    gzprintf(fp, "/%s:\t\"%d %d\"\n",
	     the_papers[i].text, the_papers[i].width, the_papers[i].height);
  }
  gzputs(fp, "\n");

  if (variable_sizes)
  {
    stp_get_size_limit(v, &max_width, &max_height,
			       &min_width, &min_height);
    stp_set_string_parameter(v, "PageSize", "Custom");
    stp_get_media_size(v, &width, &height);
    stp_get_maximum_imageable_area(v, &left, &right, &bottom, &top);
    if (left < 0)
      left = 0;
    if (top < 0)
      top = 0;
    if (bottom > height)
      bottom = height;
    if (right > width)
      width = right;

    gzprintf(fp, "*MaxMediaWidth:  \"%d\"\n", max_width);
    gzprintf(fp, "*MaxMediaHeight: \"%d\"\n", max_height);
    gzprintf(fp, "*HWMargins:      %d %d %d %d\n",
	     left, height - bottom, width - right, top);
    gzputs(fp, "*CustomPageSize True: \"pop pop pop <</PageSize[5 -2 roll]/ImagingBBox null>>setpagedevice\"\n");
    gzprintf(fp, "*ParamCustomPageSize Width:        1 points %d %d\n",
             min_width, max_width);
    gzprintf(fp, "*ParamCustomPageSize Height:       2 points %d %d\n",
             min_height, max_height);
    gzputs(fp, "*ParamCustomPageSize WidthOffset:  3 points 0 0\n");
    gzputs(fp, "*ParamCustomPageSize HeightOffset: 4 points 0 0\n");
    gzputs(fp, "*ParamCustomPageSize Orientation:  5 int 0 0\n\n");
  }

  stp_parameter_description_destroy(&desc);
  if (the_papers)
    stp_free(the_papers);

 /*
  * Do we support color?
  */

  gzputs(fp, "*OpenUI *ColorModel/Color Model: PickOne\n");
  gzputs(fp, "*OrderDependency: 10 AnySetup *ColorModel\n");

  if (printer_is_color)
    gzputs(fp, "*DefaultColorModel: RGB\n");
  else
    gzputs(fp, "*DefaultColorModel: Gray\n");

  gzprintf(fp, "*ColorModel Gray/Grayscale:\t\"<<"
               "/cupsColorSpace %d"
	       "/cupsColorOrder %d"
	       "%s"
	       ">>setpagedevice\"\n",
           CUPS_CSPACE_W, CUPS_ORDER_CHUNKED,
	   simplified ? "/cupsBitsPerColor 8" : "");
  gzprintf(fp, "*ColorModel Black/Inverted Grayscale:\t\"<<"
               "/cupsColorSpace %d"
	       "/cupsColorOrder %d"
	       "%s"
	       ">>setpagedevice\"\n",
           CUPS_CSPACE_K, CUPS_ORDER_CHUNKED,
	   simplified ? "/cupsBitsPerColor 8" : "");

  if (printer_is_color)
  {
    gzprintf(fp, "*ColorModel RGB/RGB Color:\t\"<<"
                 "/cupsColorSpace %d"
		 "/cupsColorOrder %d"
	         "%s"
		 ">>setpagedevice\"\n",
             CUPS_CSPACE_RGB, CUPS_ORDER_CHUNKED,
	     simplified ? "/cupsBitsPerColor 8" : "");
    gzprintf(fp, "*ColorModel CMY/CMY Color:\t\"<<"
                 "/cupsColorSpace %d"
		 "/cupsColorOrder %d"
	         "%s"
		 ">>setpagedevice\"\n",
             CUPS_CSPACE_CMY, CUPS_ORDER_CHUNKED,
	     simplified ? "/cupsBitsPerColor 8" : "");
    gzprintf(fp, "*ColorModel CMYK/CMYK:\t\"<<"
                 "/cupsColorSpace %d"
		 "/cupsColorOrder %d"
	         "%s"
		 ">>setpagedevice\"\n",
             CUPS_CSPACE_CMYK, CUPS_ORDER_CHUNKED,
	     simplified ? "/cupsBitsPerColor 8" : "");
    gzprintf(fp, "*ColorModel KCMY/KCMY:\t\"<<"
                 "/cupsColorSpace %d"
		 "/cupsColorOrder %d"
	         "%s"
		 ">>setpagedevice\"\n",
             CUPS_CSPACE_KCMY, CUPS_ORDER_CHUNKED,
	     simplified ? "/cupsBitsPerColor 8" : "");
  }

  gzputs(fp, "*CloseUI: *ColorModel\n\n");

  if (!simplified)
    {
      /*
       * 8 or 16 bit color (16 bit is slower)
       */
      gzputs(fp, "*OpenUI *StpColorPrecision/Color Precision: PickOne\n");
      gzputs(fp, "*OrderDependency: 10 AnySetup *StpColorPrecision\n");
      gzputs(fp, "*DefaultStpColorPrecision: Normal\n");
      gzputs(fp, "*StpColorPrecision Normal/Normal:\t\"<<"
	     "/cupsBitsPerColor 8>>setpagedevice\"\n");
      gzputs(fp, "*StpColorPrecision Best/Best:\t\"<<"
	     "/cupsBitsPerColor 8"
	     "/cupsPreferredBitsPerColor 16>>setpagedevice\"\n");
      gzputs(fp, "*CloseUI: *StpColorPrecision\n\n");
    }

 /*
  * Media types...
  */

  stp_describe_parameter(v, "MediaType", &desc);
  num_opts = stp_string_list_count(desc.bounds.str);

  if (num_opts > 0)
  {
    gzprintf(fp, "*OpenUI *MediaType/%s: PickOne\n", _("Media Type"));
    gzputs(fp, "*OrderDependency: 10 AnySetup *MediaType\n");
    gzprintf(fp, "*DefaultMediaType: %s\n", desc.deflt.str);

    for (i = 0; i < num_opts; i ++)
    {
      opt = stp_string_list_param(desc.bounds.str, i);
      gzprintf(fp, "*MediaType %s/%s:\t\"<</MediaType(%s)>>setpagedevice\"\n",
               opt->name, opt->text, opt->name);
    }

    gzputs(fp, "*CloseUI: *MediaType\n\n");
  }
  stp_parameter_description_destroy(&desc);

 /*
  * Input slots...
  */

  stp_describe_parameter(v, "InputSlot", &desc);
  num_opts = stp_string_list_count(desc.bounds.str);

  if (num_opts > 0)
  {
    gzprintf(fp, "*OpenUI *InputSlot/%s: PickOne\n", _("Media Source"));
    gzputs(fp, "*OrderDependency: 10 AnySetup *InputSlot\n");
    gzprintf(fp, "*DefaultInputSlot: %s\n", desc.deflt.str);

    for (i = 0; i < num_opts; i ++)
    {
      opt = stp_string_list_param(desc.bounds.str, i);
      gzprintf(fp, "*InputSlot %s/%s:\t\"<</MediaClass(%s)>>setpagedevice\"\n",
               opt->name, opt->text, opt->name);
    }

    gzputs(fp, "*CloseUI: *InputSlot\n\n");
  }
  stp_parameter_description_destroy(&desc);

 /*
  * Quality settings
  */

  stp_describe_parameter(v, "Quality", &desc);
  if (desc.p_type == STP_PARAMETER_TYPE_STRING_LIST && desc.is_active)
    {
      stp_clear_string_parameter(v, "Resolution");
      has_quality_parameter = 1;
      gzprintf(fp, "*OpenUI *StpQuality/%s: PickOne\n", gettext(desc.text));
      gzputs(fp, "*OrderDependency: 5 AnySetup *StpQuality\n");
      gzprintf(fp, "*DefaultStpQuality: %s\n", desc.deflt.str);
      num_opts = stp_string_list_count(desc.bounds.str);
      for (i = 0; i < num_opts; i++)
	{
	  opt = stp_string_list_param(desc.bounds.str, i);
	  stp_set_string_parameter(v, "Quality", opt->name);
	  stp_describe_resolution(v, &xdpi, &ydpi);
	  if (xdpi == -1 || ydpi == -1)
	    {
	      stp_parameter_t res_desc;
	      stp_clear_string_parameter(v, "Quality");
	      stp_describe_parameter(v, "Resolution", &res_desc);
	      stp_set_string_parameter(v, "Resolution", res_desc.deflt.str);
	      stp_describe_resolution(v, &xdpi, &ydpi);
	      stp_clear_string_parameter(v, "Resolution");
	      stp_parameter_description_destroy(&res_desc);
	    }
	  gzprintf(fp, "*StpQuality %s/%s:\t\"<</HWResolution[%d %d]/cupsRowFeed %d>>setpagedevice\"\n",
		   opt->name, opt->text, xdpi, ydpi, i + 1);
	}
      gzputs(fp, "*CloseUI: *StpQuality\n\n");
    }
  stp_parameter_description_destroy(&desc);
  stp_clear_string_parameter(v, "Quality");

 /*
  * Resolutions...
  */

  stp_describe_parameter(v, "Resolution", &desc);
  num_opts = stp_string_list_count(desc.bounds.str);

  if (!simplified || desc.p_level == STP_PARAMETER_LEVEL_BASIC)
    {
      gzprintf(fp, "*OpenUI *Resolution/%s: PickOne\n", _("Resolution"));
      gzputs(fp, "*OrderDependency: 20 AnySetup *Resolution\n");
      if (has_quality_parameter)
	gzprintf(fp, "*DefaultResolution: None\n");
      else
	gzprintf(fp, "*DefaultResolution: %s\n", desc.deflt.str);

      stp_clear_string_parameter(v, "Quality");
      if (has_quality_parameter)
	gzprintf(fp, "*Resolution None/%s: \"\"\n", _("Automatic"));
      for (i = 0; i < num_opts; i ++)
	{
	  /*
	   * Strip resolution name to its essentials...
	   */
	  opt = stp_string_list_param(desc.bounds.str, i);
	  stp_set_string_parameter(v, "Resolution", opt->name);
	  stp_describe_resolution(v, &xdpi, &ydpi);

	  /* This should not happen! */
	  if (xdpi == -1 || ydpi == -1)
	    continue;

	  /*
	   * Write the resolution option...
	   */

	  gzprintf(fp, "*Resolution %s/%s:\t\"<</HWResolution[%d %d]/cupsCompression %d>>setpagedevice\"\n",
		   opt->name, opt->text, xdpi, ydpi, i + 1);
	}

      gzputs(fp, "*CloseUI: *Resolution\n\n");
    }

  stp_parameter_description_destroy(&desc);

  stp_describe_parameter(v, "OutputOrder", &desc);
  if (desc.p_type == STP_PARAMETER_TYPE_STRING_LIST)
    {
      gzputs(fp, "*OpenUI *OutputOrder: PickOne\n");
      gzputs(fp, "*OrderDependency: 10 AnySetup *OutputOrder\n");
      gzprintf(fp, "*DefaultOutputOrder: %s\n", desc.deflt.str);
      gzputs(fp, "*OutputOrder Normal/Normal: \"\"\n");
      gzputs(fp, "*OutputOrder Reverse/Reverse: \"\"\n");
      gzputs(fp, "*CloseUI: *OutputOrder\n\n");
    }
  stp_parameter_description_destroy(&desc);

 /* 
  * Duplex
  * Note that the opt->name strings MUST match those in the printer driver(s)
  * else the PPD files will not be generated correctly
  */

  stp_describe_parameter(v, "Duplex", &desc);
  if (desc.p_type == STP_PARAMETER_TYPE_STRING_LIST)
    {
      num_opts = stp_string_list_count(desc.bounds.str);
      if (num_opts > 0)
      {
        gzputs(fp, "*OpenUI *Duplex/Double-Sided Printing: PickOne\n");
        gzputs(fp, "*OrderDependency: 20 AnySetup *Duplex\n");
        gzprintf(fp, "*DefaultDuplex: %s\n", desc.deflt.str);

        for (i = 0; i < num_opts; i++)
          {
            opt = stp_string_list_param(desc.bounds.str, i);
            if (strcmp(opt->name, "None") == 0)
              gzprintf(fp, "*Duplex %s/%s: \"<</Duplex false>>setpagedevice\"\n", opt->name, opt->text);
            else if (strcmp(opt->name, "DuplexNoTumble") == 0)
              gzprintf(fp, "*Duplex %s/%s: \"<</Duplex true/Tumble false>>setpagedevice\"\n", opt->name, opt->text);
            else if (strcmp(opt->name, "DuplexTumble") == 0)
              gzprintf(fp, "*Duplex %s/%s: \"<</Duplex true/Tumble true>>setpagedevice\"\n", opt->name, opt->text);
           }
        gzputs(fp, "*CloseUI: *Duplex\n\n");
      }
    }
  stp_parameter_description_destroy(&desc);

  param_list = stp_get_parameter_list(v);

  for (j = 0; j <= STP_PARAMETER_CLASS_OUTPUT; j++)
    {
      for (k = 0; k <= maximum_level; k++)
	{
	  int printed_open_group = 0;
	  size_t param_count = stp_parameter_list_count(param_list);
	  for (l = 0; l < param_count; l++)
	    {
	      int print_close_ui = 1;
	      const stp_parameter_t *lparam =
		stp_parameter_list_param(param_list, l);
	      if (lparam->p_class != j || lparam->p_level != k ||
		  is_special_option(lparam->name) || lparam->read_only ||
		  (lparam->p_type != STP_PARAMETER_TYPE_STRING_LIST &&
		   lparam->p_type != STP_PARAMETER_TYPE_BOOLEAN &&
		   lparam->p_type != STP_PARAMETER_TYPE_DIMENSION &&
		   lparam->p_type != STP_PARAMETER_TYPE_DOUBLE))
		  continue;
	      stp_describe_parameter(v, lparam->name, &desc);
	      if (desc.is_active)
		{
		  int printed_default_value = 0;
		  if (!printed_open_group)
		    {
		      print_group_open(fp, j, k);
		      printed_open_group = 1;
		    }
		  gzprintf(fp, "*OpenUI *Stp%s/%s: PickOne\n",
			   desc.name, gettext(desc.text));
#if 0
		  gzprintf(fp, "*OrderDependency: %d AnySetup *Stp%s\n",
			   (100 + l + (j * param_count) +
			    (k * STP_PARAMETER_LEVEL_INTERNAL * param_count)),
			   desc.name);
#endif
		  if (!desc.is_mandatory)
		    gzprintf(fp, "*DefaultStp%s: None\n", desc.name);
		  switch (desc.p_type)
		    {
		    case STP_PARAMETER_TYPE_STRING_LIST:
		      if (desc.is_mandatory)
			gzprintf(fp, "*DefaultStp%s: %s\n",
				 desc.name, desc.deflt.str);
		      else
			gzprintf(fp, "*Stp%s %s/%s: \"\"\n", desc.name,
				 "None", _("None"));
		      num_opts = stp_string_list_count(desc.bounds.str);
		      for (i = 0; i < num_opts; i++)
			{
			  opt = stp_string_list_param(desc.bounds.str, i);
			  gzprintf(fp, "*Stp%s %s/%s: \"\"\n",
				   desc.name, opt->name, opt->text);
			}
		      break;
		    case STP_PARAMETER_TYPE_BOOLEAN:
		      if (desc.is_mandatory)
			gzprintf(fp, "*DefaultStp%s: %s\n", desc.name,
				 desc.deflt.boolean ? "True" : "False");
		      else
			gzprintf(fp, "*Stp%s %s/%s: \"\"\n", desc.name,
				 "None", _("None"));
		      gzprintf(fp, "*Stp%s %s/%s: \"\"\n",
			       desc.name, "False", _("No"));
		      gzprintf(fp, "*Stp%s %s/%s: \"\"\n",
			       desc.name, "True", _("Yes"));
		      break;
		    case STP_PARAMETER_TYPE_DOUBLE:
		      if (desc.is_mandatory)
			{
			  gzprintf(fp, "*DefaultStp%s: None\n", desc.name);
			}
		      for (i = desc.bounds.dbl.lower * 1000;
			   i <= desc.bounds.dbl.upper * 1000 ; i += 100)
			{
			  if (desc.deflt.dbl * 1000 == i && desc.is_mandatory)
			    {
			      gzprintf(fp, "*Stp%s None/%.3f: \"\"\n",
				       desc.name, ((double) i) * .001);
			      printed_default_value = 1;
			    }
			  else
			    gzprintf(fp, "*Stp%s %d/%.3f: \"\"\n",
				     desc.name, i, ((double) i) * .001);
			}
		      if (!desc.is_mandatory)
			gzprintf(fp, "*Stp%s None/None: \"\"\n",
				 desc.name);
		      else if (! printed_default_value)
			gzprintf(fp, "*Stp%s None/%.3f: \"\"\n",
				 desc.name, desc.deflt.dbl);
		      gzprintf(fp, "*CloseUI: *Stp%s\n\n", desc.name);
		      if (!simplified)
			{
			  gzprintf(fp, "*OpenUI *StpFine%s/%s %s: PickOne\n",
				   desc.name, gettext(desc.text), _("Fine Adjustment"));
			  gzprintf(fp, "*DefaultStpFine%s:None\n", desc.name);
			  gzprintf(fp, "*StpFine%s None/0.000: \"\"\n", desc.name);
			  for (i = 0; i < 100; i += 5)
			    gzprintf(fp, "*StpFine%s %d/%.3f: \"\"\n",
				     desc.name, i, ((double) i) * .001);
			  gzprintf(fp, "*CloseUI: *StpFine%s\n\n", desc.name);
			}
		      print_close_ui = 0;
		      
		      break;
		    case STP_PARAMETER_TYPE_DIMENSION:
		      if (desc.is_mandatory)
			gzprintf(fp, "*DefaultStp%s: %d\n",
				 desc.name, desc.deflt.dimension);
		      else
			gzprintf(fp, "*Stp%s %s/%s: \"\"\n", desc.name,
				 "None", _("None"));
		      for (i = desc.bounds.dimension.lower;
			   i <= desc.bounds.dimension.upper; i++)
			{
			  /* FIXME
			   * For now, just use mm; we'll fix it later
			   * for the locale-appropriate setting.
			   * --rlk 20040818
			   */
			  gzprintf(fp, "*Stp%s %d/%.1f mm: \"\"\n",
				   desc.name, i, ((double) i) * 25.4 / 72);
			}
		      break;
		    default:
		      break;
		    }
		  if (print_close_ui)
		    gzprintf(fp, "*CloseUI: *Stp%s\n\n", desc.name);
		}
	      stp_parameter_description_destroy(&desc);
	    }
	  if (printed_open_group)
	    print_group_close(fp, j, k);
	}
    }
  stp_parameter_list_destroy(param_list);

 /*
  * Fonts...
  */

  gzputs(fp, "*DefaultFont: Courier\n");
  gzputs(fp, "*Font AvantGarde-Book: Standard \"(001.006S)\" Standard ROM\n");
  gzputs(fp, "*Font AvantGarde-BookOblique: Standard \"(001.006S)\" Standard ROM\n");
  gzputs(fp, "*Font AvantGarde-Demi: Standard \"(001.007S)\" Standard ROM\n");
  gzputs(fp, "*Font AvantGarde-DemiOblique: Standard \"(001.007S)\" Standard ROM\n");
  gzputs(fp, "*Font Bookman-Demi: Standard \"(001.004S)\" Standard ROM\n");
  gzputs(fp, "*Font Bookman-DemiItalic: Standard \"(001.004S)\" Standard ROM\n");
  gzputs(fp, "*Font Bookman-Light: Standard \"(001.004S)\" Standard ROM\n");
  gzputs(fp, "*Font Bookman-LightItalic: Standard \"(001.004S)\" Standard ROM\n");
  gzputs(fp, "*Font Courier: Standard \"(002.004S)\" Standard ROM\n");
  gzputs(fp, "*Font Courier-Bold: Standard \"(002.004S)\" Standard ROM\n");
  gzputs(fp, "*Font Courier-BoldOblique: Standard \"(002.004S)\" Standard ROM\n");
  gzputs(fp, "*Font Courier-Oblique: Standard \"(002.004S)\" Standard ROM\n");
  gzputs(fp, "*Font Helvetica: Standard \"(001.006S)\" Standard ROM\n");
  gzputs(fp, "*Font Helvetica-Bold: Standard \"(001.007S)\" Standard ROM\n");
  gzputs(fp, "*Font Helvetica-BoldOblique: Standard \"(001.007S)\" Standard ROM\n");
  gzputs(fp, "*Font Helvetica-Narrow: Standard \"(001.006S)\" Standard ROM\n");
  gzputs(fp, "*Font Helvetica-Narrow-Bold: Standard \"(001.007S)\" Standard ROM\n");
  gzputs(fp, "*Font Helvetica-Narrow-BoldOblique: Standard \"(001.007S)\" Standard ROM\n");
  gzputs(fp, "*Font Helvetica-Narrow-Oblique: Standard \"(001.006S)\" Standard ROM\n");
  gzputs(fp, "*Font Helvetica-Oblique: Standard \"(001.006S)\" Standard ROM\n");
  gzputs(fp, "*Font NewCenturySchlbk-Bold: Standard \"(001.009S)\" Standard ROM\n");
  gzputs(fp, "*Font NewCenturySchlbk-BoldItalic: Standard \"(001.007S)\" Standard ROM\n");
  gzputs(fp, "*Font NewCenturySchlbk-Italic: Standard \"(001.006S)\" Standard ROM\n");
  gzputs(fp, "*Font NewCenturySchlbk-Roman: Standard \"(001.007S)\" Standard ROM\n");
  gzputs(fp, "*Font Palatino-Bold: Standard \"(001.005S)\" Standard ROM\n");
  gzputs(fp, "*Font Palatino-BoldItalic: Standard \"(001.005S)\" Standard ROM\n");
  gzputs(fp, "*Font Palatino-Italic: Standard \"(001.005S)\" Standard ROM\n");
  gzputs(fp, "*Font Palatino-Roman: Standard \"(001.005S)\" Standard ROM\n");
  gzputs(fp, "*Font Symbol: Special \"(001.007S)\" Special ROM\n");
  gzputs(fp, "*Font Times-Bold: Standard \"(001.007S)\" Standard ROM\n");
  gzputs(fp, "*Font Times-BoldItalic: Standard \"(001.009S)\" Standard ROM\n");
  gzputs(fp, "*Font Times-Italic: Standard \"(001.007S)\" Standard ROM\n");
  gzputs(fp, "*Font Times-Roman: Standard \"(001.007S)\" Standard ROM\n");
  gzputs(fp, "*Font ZapfChancery-MediumItalic: Standard \"(001.007S)\" Standard ROM\n");
  gzputs(fp, "*Font ZapfDingbats: Special \"(001.004S)\" Standard ROM\n");

  gzprintf(fp, "\n*%%End of stp-%s.%s%s\n",
           driver,
           GUTENPRINT_RELEASE_VERSION,
           ppdext);

  stp_vars_destroy(v);
  return (0);
}


/*
 * End of "$Id$".
 */
