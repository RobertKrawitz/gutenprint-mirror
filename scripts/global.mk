## Global rules and macros to be included in all Makefiles.


# Variables

#export STP_MODULE_PATH = $(top_builddir)/src/main/.libs:$(top_builddir)/src/main
#export STP_DATA_PATH = $(top_srcdir)/src/xml

AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_builddir)/include $(LOCAL_CPPFLAGS) $(GNUCFLAGS) -DPACKAGE_LOCALE_DIR=\"$(PACKAGE_LOCALE_DIR)\" -DPACKAGE_DATA_DIR=\"$(PACKAGE_DATA_DIR)\" -DPACKAGE_LIB_DIR=\"$(PACKAGE_LIB_DIR)\" -DPACKAGE_BIN_DIR=\"$(PACKAGE_BIN_DIR)\" -DPKGMODULEDIR=\"$(PKGMODULEDIR)\" -DPKGXMLDATADIR=\"$(PKGXMLDATADIR)\"

LIBS = $(INTLLIBS) @LIBS@

# Libraries

GUTENPRINT_LIBS = $(top_builddir)/src/main/libgutenprint.la
GUTENPRINTUI2_LIBS = $(top_builddir)/src/gutenprintui2/libgutenprintui2.la

# Rules

$(top_builddir)/src/main/libgutenprint.la:
	cd $(top_builddir)/src/main; \
	$(MAKE)

$(top_builddir)/src/gutenprintui2/libgutenprintui2.la:
	cd $(top_builddir)/src/gutenprintui2; \
	$(MAKE)
