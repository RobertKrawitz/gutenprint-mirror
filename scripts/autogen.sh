#!/bin/sh
# Run this to generate all the initial makefiles, etc.
# Shamelessly copied from Glade

DIE=0

(autoconf --version) < /dev/null > /dev/null 2>&1 || {
  echo
  echo "**Error**: You must have \`autoconf' installed to compile gimp-print."
  echo "Download the appropriate package for your distribution,"
  echo "or get the source tarball at ftp://ftp.gnu.org/pub/gnu/"
  DIE=1
}

test -f $srcdir/configure.in.in && sed "s/XXXRELEASE_DATE=XXX/RELEASE_DATE=\"`date '+%d %b %Y'`\"/" $srcdir/configure.in.in > $srcdir/configure.in

test -f $srcdir/ChangeLog || echo > $srcdir/ChangeLog

(grep "^AM_PROG_LIBTOOL" $srcdir/configure.in >/dev/null) && {
  (libtool --version) < /dev/null > /dev/null 2>&1 || {
    echo
    echo "**Error**: You must have \`libtool' installed to compile gimp-print."
    echo "Get ftp://ftp.gnu.org/pub/gnu/libtool-1.2d.tar.gz"
    echo "(or a newer version if it is available)"
    DIE=1
  }
}

grep "^AM_GNU_GETTEXT" $srcdir/configure.in >/dev/null && {
  grep "sed.*POTFILES" $srcdir/configure.in >/dev/null || \
  (gettext --version) < /dev/null > /dev/null 2>&1 || {
    echo
    echo "**Error**: You must have \`gettext' installed to compile gimp-print."
    echo "Get ftp://alpha.gnu.org/gnu/gettext-0.10.38.tar.gz"
    echo "(or a newer version if it is available)"
    DIE=1
  }
}

gettextv=`gettext --version | head -1 | awk '{print $NF}'`
gettext_major=`echo $gettextv | awk -F. '{print $1}'`
gettext_minor=`echo $gettextv | awk -F. '{print $2}'`
gettext_point=`echo $gettextv | awk -F. '{print $3}'`

test "$gettext_major" -eq 0 && {
  test "$gettext_minor" -lt 10 || {
    test "$gettext_minor" -eq 10 -a "$gettext_point" -lt 38
  }
} && {
  echo
  echo "**Error**: You must have \`gettext' 0.10.38 or newer installed to"
  echo "compile gimp-print."
  echo "Get ftp://alpha.gnu.org/gnu/gettext-0.10.38.tar.gz"
  echo "(or a newer version if it is available)"
  DIE=1
}

(automake --version) < /dev/null > /dev/null 2>&1 || {
  echo
  echo "**Error**: You must have \`automake' installed to compile gimp-print."
  echo "Get ftp://ftp.gnu.org/pub/gnu/automake-1.3.tar.gz"
  echo "(or a newer version if it is available)"
  DIE=1
  NO_AUTOMAKE=yes
}


# if no automake, don't bother testing for aclocal
test -n "$NO_AUTOMAKE" || (aclocal --version) < /dev/null > /dev/null 2>&1 || {
  echo
  echo "**Error**: Missing \`aclocal'.  The version of \`automake'"
  echo "installed doesn't appear recent enough."
  echo "Get ftp://ftp.gnu.org/pub/gnu/automake-1.3.tar.gz"
  echo "(or a newer version if it is available)"
  DIE=1
}

# Check first for existence and then for proper version of Jade >= 1.2.1

jade_err=0

# Exists?
test -z "$(type -p jade)" && jade_err=1

# Proper rev?
test "$jade_err" -eq 0 && {
#  echo "Checking for proper revision of jade..."
  tmp_file=$(mktemp /tmp/jade_conf.XXXXXX)
  jade -v < /dev/null > $tmp_file 2>&1
  jade_version=`grep -i "jade version" $tmp_file | awk -F\" '{print $2}'`
  rm $tmp_file

  jade_version_major=`echo $jade_version | awk -F. '{print $1}'`
  jade_version_minor=`echo $jade_version | awk -F. '{print $2}'`
  jade_version_point=`echo $jade_version | awk -F. '{print $3}'`

  test "$jade_version_major" -ge 1 || jade_err=1

  test "$jade_version_minor" -lt 2 || {
      test "$jade_version_minor" -eq 2 -a "$jade_version_point" -lt 1
    } && jade_err=1

  test "$jade_err" -eq 1 && {
    echo " "
    echo "***Warning***: You must have \"Jade\" version 1.2.1 or newer installed to"
    echo "build the Gimp-Print user's guide."
    echo "Get ftp://ftp.jclark.com/pub/jade/jade-1.2.1.tar.gz"
    echo "(or a newer version if available)"
    echo " "
  }
}

# Check for existence of dvips

test -z "$(type -p dvips)" && {
  echo " "
  echo "***Warning***: You must have \"dvips\" installed to"
  echo "build the Gimp-Print user's guide."
  echo " "
}

# Check for existence of jadetex

test -z "$(type -p jadetex)" && {
  echo " "
  echo "***Warning***: You must have \"jadetex\" version 3.5 or newer installed to"
  echo "build the Gimp-Print user's guide."
  echo "Get ftp://prdownloads.sourceforge.net/jadetex/jadetex-3.5.tar.gz"
  echo "(or a newer version if available)"
  echo " "
}

# Check for OpenJade >= 1.3

openjade_err=0

# Exists?
test -z "$(type -p openjade)" && openjade_err=1

# Proper rev?
test "$openjade_err" -eq 0 && {
#  echo "Checking for proper revision of openjade..."
  tmp_file=$(mktemp /tmp/open_jade.XXXXXX)
  openjade -v < /dev/null > $tmp_file 2>&1
  openjade_version=`grep -i "openjade version" $tmp_file | awk -F\" '{print $2}'`
  rm $tmp_file

  openjade_version_major=`echo $openjade_version | awk -F. '{print $1}'`
  openjade_version_minor=`echo $openjade_version | awk -F. '{print $2}'`
  openjade_version_minor=`echo $openjade_version_minor | awk -F- '{print $1}'`

#  echo $openjade_version
#  echo $openjade_version_major
#  echo $openjade_version_minor

  test "$openjade_version_major" -ge 1 || openjade_err=1
  test "$openjade_version_minor" -ge 3 || openjade_err=1

  test "$openjade_err" -eq 1 && {
    echo " "
    echo "***Warning***: You must have \"OpenJade\" version 1.3 or newer installed to"
    echo "build the Gimp-Print user's guide."
    echo "Get http://download.sourceforge.net/openjade/openjade-1.3.tar.gz"
    echo "(or a newer version if available)"
    echo " "
  }
}

# Check for ps2pdf

test -z "$(type -p ps2pdf)" && {
  echo " "
  echo "***Warning***: You must have \"ps2pdf\" installed to"
  echo "build the Gimp-Print user's guide."
  echo "ps2pdf comes from the GNU Ghostscript software package."
  echo "Get ftp://ftp.gnu.org/gnu/ghostscript/ghostscript-6.5.1.tar.gz"
  echo "(or a newer version if available)"
  echo " "
}

# Check for docbook-toys (seems to be SuSE specific?)
# I will ultimately extract the essence of this and code it into the
# Makefile.am in the doc/users_guide directory, but for now this will
# have to do.  AMS 3-oct-2001

test -z "$(type -p dvips)" && {
  echo " "
  echo "***Warning***: You must have \"docbook-toys\" installed to"
  echo "build the Gimp-Print user's guide."
  echo "Get http://www.suse.de/~ke/docbook-toys-0.15.2.tar.bz2"
  echo "(or a newer version if available)"
  echo " "
}

# Check first for existence and then for proper version of sgmltools-lite >=3.0.2

sgmltools_err=0

# Exists?
test -z "$(type -p sgmltools)" && sgmltools_err=1

# Proper rev?
test "$sgmltools_err" -eq 0 && {
#  echo "Checking for proper revision of sgmltools..."
  sgmltools_version="$(sgmltools --version | awk '{print $3}')"

  sgmltools_version_major=`echo $sgmltools_version | awk -F. '{print $1}'`
  sgmltools_version_minor=`echo $sgmltools_version | awk -F. '{print $2}'`
  sgmltools_version_point=`echo $sgmltools_version | awk -F. '{print $3}'`

  test "$sgmltools_version_major" -ge 3 || sgmltools_err=1
  test "$sgmltools_version_minor" -gt 0 ||
    (test "$sgmltools_version_minor" -eq 0 -a "$sgmltools_version_point" -ge 2) ||
    sgmltools_err=1

  test "$sgmltools_err" -eq 1 && {
    echo " "
    echo "***Warning***: You must have \"sgmltools-lite\" version 3.0.2 or newer installed to"
    echo "build the Gimp-Print user's guide."
    echo "Get http://prdownloads.sourceforge.net/projects/sgmltools-lite/sgmltools-lite-3.0.2.tar.gz"
    echo "(or a newer version if available)"
    echo " "
  }
}

if test "$DIE" -eq 1; then
  exit 1
fi

if test -z "$*"; then
  echo "**Warning**: I am going to run \`configure' with no arguments."
  echo "If you wish to pass any to it, please specify them on the"
  echo \`$0\'" command line."
  echo
fi

case $CC in
xlc )
  am_opt=--include-deps;;
esac

for coin in `find $srcdir -name configure.in -print`
do 
  dr=`dirname $coin`
  if test -f $dr/NO-AUTO-GEN; then
    echo skipping $dr -- flagged as no auto-gen
  else
    echo processing $dr
    macrodirs=`sed -n -e 's,^dnl AM_ACLOCAL_INCLUDE(\(.*\)),\1,gp' < $coin`
    ( cd $dr
      aclocalinclude="$ACLOCAL_FLAGS"
      for k in $macrodirs; do
  	if test -d $k; then
          aclocalinclude="$aclocalinclude -I $k"
  	##else 
	##  echo "**Warning**: No such directory \`$k'.  Ignored."
        fi
      done
      if grep "^AM_GNU_GETTEXT" configure.in >/dev/null; then
	if grep "sed.*POTFILES" configure.in >/dev/null; then
	  : do nothing -- we still have an old unmodified configure.in
	else
	  echo "Creating $dr/aclocal.m4 ..."
	  test -r $dr/aclocal.m4 || touch $dr/aclocal.m4
	  echo "Running gettextize...  Ignore non-fatal messages."
	  echo "no" | gettextize --force --copy
	  echo "Making $dr/aclocal.m4 writable ..."
	  test -r $dr/aclocal.m4 && chmod u+w $dr/aclocal.m4
        fi
      fi
      if grep "^AM_GNOME_GETTEXT" configure.in >/dev/null; then
	echo "Creating $dr/aclocal.m4 ..."
	test -r $dr/aclocal.m4 || touch $dr/aclocal.m4
	echo "Running gettextize...  Ignore non-fatal messages."
	echo "no" | gettextize --force --copy
	echo "Making $dr/aclocal.m4 writable ..."
	test -r $dr/aclocal.m4 && chmod u+w $dr/aclocal.m4
      fi
      if grep "^AM_PROG_LIBTOOL" configure.in >/dev/null; then
	echo "Running libtoolize..."
	libtoolize --force --copy
      fi
      echo "Running aclocal $aclocalinclude ..."
      if aclocal $aclocalinclude -I src/main ; then
        echo "added local version of AM_PATH_GIMPPRINT"
      else
        echo "aclocal returned error status; running again without '-I src/main' ..."
        aclocal $aclocalinclude
	echo "using installed version of AM_PATH_GIMPPRINT"
      fi
      if grep "^AM_CONFIG_HEADER" configure.in >/dev/null; then
	echo "Running autoheader..."
	autoheader
      fi
      echo "Running automake --gnu $am_opt ..."
      automake --add-missing --gnu $am_opt
      echo "Running autoconf ..."
      autoconf
    )
  fi
done

conf_flags="--enable-maintainer-mode --enable-compile-warnings" #--enable-iso-c

if test x$NOCONFIGURE = x; then
  echo Running $srcdir/configure $conf_flags "$@" ...
  $srcdir/configure $conf_flags "$@" \
  && echo Now type \`make\' to compile $PKG_NAME || exit 1
else
  echo Skipping configure process.
fi
