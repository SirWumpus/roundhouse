dnl
dnl aclocal.m4
dnl
dnl Copyright 2002, 2006 by Anthony Howe.  All rights reserved.
dnl

dnl AC_LANG(C)
dnl ----------
dnl CFLAGS is not in ac_cpp because -g, -O, etc. are not valid cpp options.
dnl
dnl Handle Borland C++ 5.5 where it has none standard output flags that
dnl require the filename argument be abutted to the option letter.
dnl
m4_define([AC_LANG(C)],
[ac_ext=c
dnl Mac OS X gcc 3.x wants to option and argument to be separated.
dnl Don't people read standards like POSIX and SUS?
CC_E='-o '
ac_cpp='$CPP $CPPFLAGS'
ac_compile='$CC -c $CFLAGS $CPPFLAGS conftest.$ac_ext >&AS_MESSAGE_LOG_FD'
ac_link='$CC ${CC_E}conftest$ac_exeext $CFLAGS $CPPFLAGS $LDFLAGS conftest.$ac_ext $LIBS >&AS_MESSAGE_LOG_FD'
ac_compiler_gnu=$ac_cv_c_compiler_gnu
])

dnl
dnl SNERT_GCC_SETTINGS
dnl
m4_define([SNERT_GCC_SETTINGS],[
	CFLAGS="-Wall ${CFLAGS}"
	if test ${enable_debug:-yes} = 'yes'; then
		CFLAGS="-g ${CFLAGS}"
	else 
		CFLAGS="-O2 ${CFLAGS}"
	fi

	if test ${platform:-UNKNOWN} = 'CYGWIN'; then
		if test ${enable_debug:-yes} = 'no'; then
			CFLAGS="-s ${CFLAGS}"
		fi
		if test ${enable_win32:-no} = 'yes'; then
			dnl -s		strip, no symbols
			dnl -mno-cygwin	native windows console app
			dnl -mwindows	native windows gui app
			dnl -lws2_32	WinSock2 library
			CFLAGS="-mno-cygwin ${CFLAGS}"
			LIBS="-lws2_32 ${LIBS}"
		fi
	fi		
])

dnl
dnl SNERT_BCC_SETTINGS
dnl
m4_define([SNERT_BCC_SETTINGS],[
	# This assumes a Cygwin environment with Boland C++ 5.5 CLI.
	#
	# Borland C++ 5.5 CLI tools don't have all the standard options
	# and require that arguments be abutted with the option letter.

	platform='BorlandC'

	ac_libext='lib'
	ac_objext='obj'
	ac_exeext='.exe'
	LIBEXT=$ac_libext
	OBJEXT=$ac_objext
	EXEEXT=$ac_exeext

	CPP='cpp32'
	CPPFLAGS=

	CFLAGS="-d -O1 -w-8064 ${CFLAGS}"	
	if test ${enable_debug:-yes} = 'yes'; then
		CFLAGS="-v ${CFLAGS}"
	else 
		CFLAGS="-v- ${CFLAGS}"
	fi

	CC_E='-e'
	CC_E_NAME='-e$''*$E'
	CC_O='-o'
	CC_O_NAME='-o$''*$O'
	COMPILE='${CC} ${CFLAGS} ${CC_O}$''*$O -c $<'

	LD='bcc32'
	LDFLAGS='-q -lc -L"c:/Borland/Bcc55/lib" -L"c:/Borland/Bcc55/lib/PSDK"'
	LIBSNERT=`echo "$abs_tardir/com/snert/lib/libsnert.$LIBEXT" | sed -e 's,/,\\\\\\\\,g'`

	ARCHIVE='tlib /C /a ${LIBSNERT} $$obj'
	RANLIB='true'

	XARGSI='xargs -i{}'
	AC_SUBST(FAMILY, [WIN])
])

dnl
dnl SNERT_DEFAULT_CC_SETTINGS
dnl
m4_define([SNERT_DEFAULT_CC_SETTINGS],[
	dnl Tradional cc options.
	dnl NOTE SunOS as(1) _wants_ a space between -o and its argument.
	CC_E='-o'
	CC_E_NAME='-o $@'
	CC_O='-o'
	CC_O_NAME='-o $''*$O'
	LD=$CC

	# Where to find libsnert includes and library.	
dnl	CFLAGS='-I${SNERT_INCDIR} '"${CFLAGS}"
dnl	LDFLAGS='-L${SNERT_LIBDIR} '"${LDFLAGS}"
	LIBSNERT='-lsnert'

	# Makefile macro to compile C into an object file.
	COMPILE='${CC} ${CFLAGS} ${CC_O} $''*$O -c $<'		

	# Makefile macro to archive an object file.
	ARCHIVE='${AR} rc ${LIB} $$obj'

	# Assume the following traditional extensions.
	ac_libext='a'
	LIBEXT=$ac_libext

	case "$CC" in
	gcc) 
		SNERT_GCC_SETTINGS
		;;
	bcc32)
		SNERT_BCC_SETTINGS
		;;	
	*)
		if test ${enable_debug:-yes} = 'yes'; then
			CFLAGS="-g ${CFLAGS}"
		fi 
	esac

	AC_CHECK_TOOL(RANLIB, ranlib, true)
	AC_AIX

	AC_SUBST(CC_E)
	AC_SUBST(CC_E_NAME)
	AC_SUBST(CC_O)
	AC_SUBST(CC_O_NAME)
	AC_SUBST(COMPILE)
	AC_SUBST(ARCHIVE)
	AC_SUBST(LIBEXT)
	AC_SUBST(LIBSNERT)
])

dnl 
dnl SNERT_CHECK_LIB(lib, symbol)
dnl
dnl Only check AC_CHECK_LIB if $lib not already in $LIBS
dnl   
AC_DEFUN(SNERT_CHECK_LIB,[
	if echo "$LIBS" | grep -- "-l$1" >/dev/null ; then
		echo "checking for $2 in $1... (cached) yes"
	else
		AC_CHECK_LIB([$1], [$2])
	fi
])

dnl 
dnl SNERT_CHECK_DEFINE(symbol, header_file)
dnl   
AC_DEFUN(SNERT_CHECK_DEFINE,[
	AC_CACHE_CHECK([for $1],ac_cv_define_$1,[
		AC_RUN_IFELSE([
#include <$2>
int main()
{
#ifdef $1
	return 0;
#else
	return 1;
#endif
}
		],ac_cv_define_$1=yes, ac_cv_define_$1=no)
	])
])


dnl 
dnl SNERT_CHECK_PREDEFINE(symbol)
dnl   
AC_DEFUN(SNERT_CHECK_PREDEFINE,[
	AC_CACHE_CHECK([for $1],ac_cv_define_$1,[
		AC_RUN_IFELSE([
int main()
{
#ifdef $1
	return 0;
#else
	return 1;
#endif
}
		],ac_cv_define_$1=yes, ac_cv_define_$1=no)
	])
])


dnl 
dnl SNERT_GET_NUMERIC_DEFINE(header_file, symbol)
dnl   
AC_DEFUN(SNERT_GET_NUMERIC_DEFINE,[
	AS_VAR_PUSHDEF([ac_Header], [snert_define_$1_$2])dnl
	AC_CACHE_CHECK([for $2],[ac_Header],[
		AC_RUN_IFELSE([
#include <stdio.h>
#include <$1>
int main()
{
#ifdef $2
	FILE *fp;
	if ((fp = fopen("snert_output.txt", "w")) == NULL)
		return 1;		
	fprintf(fp, "%d", $2);
	fclose(fp);	
	return 0;
#else
	return 1;
#endif
}
		],[
			AS_VAR_SET([ac_Header], [[`cat snert_output.txt`]])
			rm -f snert_output.txt
		])
	])
	AS_VAR_POPDEF([ac_Header])dnl
])

dnl
dnl SNERT_BERKELEY_DB
dnl
dnl	Sets $with_db to yes or no
dnl	Sets $db_lib to the library -l option or an empty string.
dnl
AC_DEFUN(SNERT_BERKELEY_DB,[
	echo
	echo "Check for Berkeley DB support..."
	echo 
	db_lib=''

	if test "$with_db" != 'no'; then
   	    count=1
	    while test $count -gt 0 ; do
		unset ac_cv_header_db4_db_h
		unset ac_cv_header_db3_db_h
		unset ac_cv_header_db_h

		bdb_save_LIBS=$LIBS

		if test -n "$with_db" -a "$with_db" != 'yes'; then
			CFLAGS="-I$with_db/include ${CFLAGS}"
			LDFLAGS="-L$with_db/lib ${LDFLAGS}"
			if test -f $with_db/include/db.h ; then
				AC_DEFINE_UNQUOTED(HAVE_WITH_DB_DB_H)
			fi
		fi

		unset ac_cv_search_db_create_4003
		AC_SEARCH_LIBS([db_create_4003], [db-4.3 db4 db],[
			AC_CHECK_HEADERS([db4/db.h db.h],[installedBerkeleyDB='yes'],[],[
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
			])
		])
		LIBS=$bdb_save_LIBS

		unset ac_cv_search_db_create_4002
		AC_SEARCH_LIBS([db_create_4002], [db-4.2 db4 db],[
			AC_CHECK_HEADERS([db4/db.h db.h],[installedBerkeleyDB='yes'],[],[
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
			])
		])
		LIBS=$bdb_save_LIBS

		unset ac_cv_search_db_create_4001
		AC_SEARCH_LIBS([db_create_4001], [db-4.1 db4 db],[
			AC_CHECK_HEADERS([db4/db.h db.h],[installedBerkeleyDB='yes'],[],[
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
			])
		])
		LIBS=$bdb_save_LIBS

		unset ac_cv_search_db_create_3003
		AC_SEARCH_LIBS([db_create_3003], [db-3.3 db3 db],[
			AC_CHECK_HEADERS([db3/db.h db.h],[installedBerkeleyDB='yes'],[],[
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
			])
		])
		LIBS=$bdb_save_LIBS

		unset ac_cv_search_db_create_3002
		AC_SEARCH_LIBS([db_create_3002], [db-3.2 db3 db],[
			AC_CHECK_HEADERS([db3/db.h db.h],[installedBerkeleyDB='yes'],[],[
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
			])
		])
		LIBS=$bdb_save_LIBS

		unset ac_cv_search_db_create
		unset ac_cv_search_db_create_4
		AC_SEARCH_LIBS([db_create], [db-4.3 db-4.2 db-4.1 db4 db],[
			AC_CHECK_HEADERS([db4/db.h db.h],[
				ac_cv_search_db_create_4=$ac_cv_search_db_create
				installedBerkeleyDB='yes'
				break
			],[],[
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
			])
		])
		LIBS=$bdb_save_LIBS

		unset ac_cv_search_db_create
		unset ac_cv_search_db_create_3
		AC_SEARCH_LIBS([db_create], [db-3.3 db-3.2 db3 db],[
			AC_CHECK_HEADERS([db3/db.h db.h],[
				ac_cv_search_db_create_3=$ac_cv_search_db_create
				installedBerkeleyDB='yes'
				break
			],[],[
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
			])
		])
		LIBS=$bdb_save_LIBS

		AC_SEARCH_LIBS([dbopen], [db],[
			AC_CHECK_HEADERS([db.h],[installedBerkeleyDB='yes'],[],[
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
			])
		])
		LIBS=$bdb_save_LIBS

		with_db='yes'
		if test "${ac_cv_search_db_create_4003:=no}" != no; then
			db_lib=$ac_cv_search_db_create_4003
		elif test "${ac_cv_search_db_create_4002:=no}" != no; then
			db_lib=$ac_cv_search_db_create_4002
		elif test "${ac_cv_search_db_create_4001:=no}" != no; then
			db_lib=$ac_cv_search_db_create_4001
		elif test "${ac_cv_search_db_create_4:=no}" != no; then
			db_lib=$ac_cv_search_db_create_4
		elif test "${ac_cv_search_db_create_3003:=no}" != no; then
			db_lib=$ac_cv_search_db_create_3003
		elif test "${ac_cv_search_db_create_3002:=no}" != no; then
			db_lib=$ac_cv_search_db_create_3002
		elif test "${ac_cv_search_db_create_3:=no}" != no; then
			db_lib=$ac_cv_search_db_create_3
		elif test "${ac_cv_search_dbopen:=no}" != no; then
			db_lib=$ac_cv_search_dbopen
		else
			with_db='no'
		fi

		if test ${isDebian:-no} = 'yes' -a ${installedBerkeleyDB:-no} = 'no'; then
			# Fetch headers matching library.
			apt-get install -y libdb`expr $db_lib : '-ldb-\(.*\)'`-dev
			installedBerkeleyDB='yes'
			count=`expr $count + 1`
			echo "repeating Berkeley DB checks..."
		fi
		count=`expr $count - 1`
	    done

		if test "$db_lib" = 'none required'; then
			db_lib=''
		else
			LIBS="$db_lib $LIBS"
		fi

		AC_CHECK_FUNCS(db_create dbopen)
	fi
])

AC_DEFUN(SNERT_FIND_LIBC,[
	AC_CHECK_HEADER([dlfcn.h], [
		AC_DEFINE_UNQUOTED(HAVE_DLFCN_H)
		AC_CHECK_TOOL(ldd_tool, ldd)
		if test ${ldd_tool:-no} != 'no'	; then
			AC_CHECK_LIB([dl],[dlopen])
			AC_MSG_CHECKING([for libc])
			AC_RUN_IFELSE([
				AC_LANG_SOURCE([[
#include <stdio.h> 
#include <stdlib.h>
#ifdef HAVE_DLFCN_H
# include <dlfcn.h>
#endif

int
main(int argc, char **argv)
{
	void *handle;
	handle = dlopen(argv[1], RTLD_NOW);
	return dlerror() != NULL;
}
				]])
			],[	
				libc=[`$ldd_tool ./conftest$ac_exeext | sed -n -e '/libc/s/.* \([^ ]*libc[^ ]*\).*/\1/p'`]
				if ./conftest$ac_exeext $libc ; then
					AC_DEFINE_UNQUOTED(LIBC_PATH, ["$libc"])
				else
					libc='not found'
				fi				
			],[
				libc='not found'
			])
			AC_MSG_RESULT($libc)
		fi
	])
])


dnl
dnl SNERT_PLATFORM
dnl
AC_DEFUN(SNERT_PLATFORM,[
	platform=`uname -s|sed -e 's/^\([[a-zA-Z0-9]]*\)[[^a-zA-Z0-9]].*/\1/'`
	AC_SUBST(platform, $platform)
	echo "platform is... $platform"
	if test -e /etc/debian_version ; then
		echo "this Linux is a Debian" `cat /etc/debian_version`
		apt-get install -y gcc libc6-dev
		isDebian='yes'
	fi

	AC_CHECK_TOOL(MD5SUM, md5sum)
	if test ${MD5SUM:-no} = 'no' ; then
		AC_CHECK_TOOL(MD5SUM, md5)
	fi
	
dnl	case $platform in
dnl	Linux*)
dnl		kernel=`uname -r`
dnl		case "$kernel" in
dnl		1.*|2.0.*)
dnl			echo "Linux kernel... $kernel"
dnl			dnl Older Linux kernels have a broken poll() where it
dnl			dnl might block indefinitely in nanosleep().
dnl			AC_DEFINE_UNQUOTED(HAS_BROKEN_POLL)
dnl			;;
dnl		esac
dnl	esac	
])

dnl
dnl SNERT_CHECK_CONFIGURE
dnl
AC_DEFUN(SNERT_CHECK_CONFIGURE,[
	# When we have no makefile, do it ourselves...
	snert_configure_command="$[]0 $[]@"
	AC_CHECK_TOOL([autoconf_tool], [autoconf-2.59])
	if test ${autoconf_tool:-no} = 'no' ; then
		AC_CHECK_TOOL([autoconf_tool], [autoconf])
	fi
	if test ${autoconf_tool:-no} != 'no' -a \( aclocal.m4 -nt configure -o configure.in -nt configure \); then
		echo 'Rebuilding the configure script first...'
		${autoconf_tool} -f
		echo 'Restarting configure script...'
		echo $snert_configure_command
 		exec $snert_configure_command
	fi
])

dnl
dnl SNERT_LIBMILTER
dnl
dnl Depends on SNERT_PTHREAD
dnl
AC_DEFUN(SNERT_LIBMILTER,[
	echo
	echo "Check for sendmail's libmilter library & header support..."
	echo 
	dnl Always add these paths, since its common practice to override
	dnl system defaults in a /usr/local hierarchy. Why this is not the
	dnl default for gcc is beyond me.
	AC_CHECK_LIB(milter, smfi_main)
	if test "$ac_cv_lib_milter_smfi_main" = 'no'; then		
		save_ldflags=$LDFLAGS
		LDFLAGS="-L/usr/local/lib ${LDFLAGS}"
		unset ac_cv_lib_milter_smfi_main
		AC_CHECK_LIB(milter, smfi_main)
		if test "$ac_cv_lib_milter_smfi_main" = 'no'; then
			LDFLAGS=$save_ldflags
		fi
	fi
	if test ${isDebian:-no} = 'yes' -a "$ac_cv_lib_milter_smfi_main" = 'no'; then
		apt-get install -y libmilter-dev
		unset ac_cv_lib_milter_smfi_main
		AC_CHECK_LIB(milter, smfi_main)
	fi
	if test "$ac_cv_lib_milter_smfi_main" = 'no'; then
		ac_cv_lib_milter_smfi_main='required'
	fi

	AC_CHECK_HEADERS([libmilter/mfapi.h],[],[],[/* */])
	if test "$ac_cv_header_libmilter_mfapi_h" = 'no'; then
		saved_cflags=$CFLAGS
		CFLAGS="-I/usr/local/include ${CFLAGS}"		
		unset ac_cv_header_libmilter_mfapi_h
		AC_CHECK_HEADERS([libmilter/mfapi.h],[],[],[/* */])
		if test "$ac_cv_header_libmilter_mfapi_h" = 'no'; then
			CFLAGS=$saved_cflags
		fi
	fi
	if test ${isDebian:-no} = 'yes' -a "$ac_cv_header_libmilter_mfapi_h" = 'no'; then
		apt-get install -y libmilter-dev
		unset ac_cv_header_libmilter_mfapi_h
		AC_CHECK_HEADERS([libmilter/mfapi.h],[],[],[/* */])
	fi

	if test $ac_cv_header_libmilter_mfapi_h = 'yes' ; then
		AC_CHECK_FUNCS([smfi_addheader smfi_addrcpt smfi_chgheader smfi_delrcpt smfi_getpriv smfi_getsymval smfi_main smfi_register smfi_replacebody smfi_setbacklog smfi_setconn smfi_setdbg smfi_setpriv smfi_setreply smfi_settimeout smfi_stop])
		AC_CHECK_FUNCS([smfi_insheader smfi_opensocket smfi_progress smfi_quarantine smfi_setmlreply])
	fi

	AC_CHECK_MEMBERS([struct smfiDesc.xxfi_unknown, struct smfiDesc.xxfi_data],[],[],[
#ifdef HAVE_LIBMILTER_MFAPI_H
# define SMFI_VERSION	999		/* Look for all enhancements */
# include <libmilter/mfapi.h>
#endif
	])

	snert_libmilter=$ac_cv_lib_milter_smfi_main
])

dnl
dnl SNERT_LIBSNERT
dnl
AC_DEFUN(SNERT_LIBSNERT,[
	saved_cflags=$CFLAGS
	saved_ldflags=$LDFLAGS
	
	CFLAGS="-I../../include $CFLAGS"
	LDFLAGS="-L../../lib $LDFLAGS"
	
	AC_CHECK_LIB(snert, parsePath)
	if test "$ac_cv_lib_snert_parsePath" = 'no'; then
		echo
		AC_MSG_WARN([The companion library, LibSnert, is required.])
		echo
		ac_cv_lib_snert_parsePath='required'
		CFLAGS=$saved_cflags
		LDFLAGS=$save_ldflags
	fi
	snert_libsnert=$ac_cv_lib_snert_parsePath

])

dnl
dnl SNERT_OPTION_ENABLE_DEBUG
dnl
AC_DEFUN(SNERT_OPTION_ENABLE_DEBUG,[
	AC_ARG_ENABLE(debug, 
		[AC_HELP_STRING([--disable-debug],[disable compiler debug option, default is enabled])],
		[
			AC_DEFINE_UNQUOTED(NDEBUG)
		],[
			AC_DEFINE_UNQUOTED(LIBSNERT_DEBUG)
			enable_debug='yes'
		]
	)
])

dnl
dnl SNERT_OPTION_ENABLE_WIN32
dnl
AC_DEFUN(SNERT_OPTION_ENABLE_MINGW,[
	AC_ARG_ENABLE(mingw, 
		[AC_HELP_STRING([--enable-mingw],[generate native Windows application using mingw])],
		[
			enable_win32='yes'
 			CFLAGS="-mno-cygwin ${CFLAGS}"
 			LIBS="-lws2_32 ${LIBS}"
		]
	)
])

dnl
dnl SNERT_OPTION_ENABLE_BCC32
dnl
AC_DEFUN(SNERT_OPTION_ENABLE_BCC32,[
	AC_ARG_ENABLE(bcc32, 
		[AC_HELP_STRING([--enable-bcc32],[generate native Windows application using Borland C++ 5.5])],
		[
			enable_bcc32='yes'
			CC=bcc32
		]
	)
])

dnl
dnl SNERT_OPTION_ENABLE_RUN_USER(default_user_name)
dnl
AC_DEFUN(SNERT_OPTION_ENABLE_RUN_USER,[
	AC_ARG_ENABLE(run-user, 
		[AC_HELP_STRING([--enable-run-user=user],[specifiy the process user name, default is "$1"])],
		[enable_run_user="$enableval"], [enable_run_user=$1]
	)
	AC_DEFINE_UNQUOTED(RUN_AS_USER, ["$enable_run_user"])
	AC_SUBST(enable_run_user)
])

dnl
dnl SNERT_OPTION_ENABLE_RUN_GROUP(default_group_name)
dnl
AC_DEFUN(SNERT_OPTION_ENABLE_RUN_GROUP,[
	AC_ARG_ENABLE(run-group, 
		[AC_HELP_STRING([--enable-run-group=group],[specifiy the process group name, default is "$1"])],
		[enable_run_group="$enableval"], [enable_run_group=$1]
	)
	AC_DEFINE_UNQUOTED(RUN_AS_GROUP, ["$enable_run_group"])
	AC_SUBST(enable_run_group)
])

dnl
dnl SNERT_OPTION_ENABLE_CACHE_TYPE(default_cache_type)
dnl
AC_DEFUN(SNERT_OPTION_ENABLE_CACHE_TYPE,[
	AC_ARG_ENABLE(cache-type, 
		[AC_HELP_STRING([--enable-cache-type=type],[specifiy the cache type: bdb, flatfile, hash])],
		[
			# Force a specific type.
			case "$enableval" in
			bdb|flatfile|hash)
				enable_cache_type="$enableval"		
				AC_DEFINE_UNQUOTED(CACHE_TYPE, ["$enable_cache_type"])
				;;
			*)
				enable_cache_type="$1"
			esac
		], [
			# Depends on whether Berkeley DB is available.
			enable_cache_type="$1"
		]
	)
])

dnl
dnl SNERT_OPTION_ENABLE_CACHE_FILE
dnl
AC_DEFUN(SNERT_OPTION_ENABLE_CACHE_FILE,[
	AC_ARG_ENABLE(cache-file, 
		[AC_HELP_STRING([--enable-cache-file=filepath],[specifiy the cache file])],
		[
			enable_cache_file="$enableval"		
		], [
			if test -d /var/cache ; then
				# Linux http://www.pathname.com/fhs/pub/fhs-2.3.html
				enable_cache_file='${localstatedir}/cache/${PACKAGE_NAME}.db'
			elif test -d /var/db ; then
				# FreeBSD, OpenBSD, NetBSD
				enable_cache_file='${localstatedir}/db/${PACKAGE_NAME}.db'
			else
				echo "Cannot find a suitable location for the cache file."
				echo "Please specify --enable-cache-file."
				exit 1
			fi
		]
	)
	snert_cache_file=`eval echo $enable_cache_file`	
	AC_DEFINE_UNQUOTED(CACHE_FILE, ["`eval echo ${snert_cache_file}`"])
	AC_SUBST(snert_cache_file)	
	AC_SUBST(enable_cache_file)	
])

dnl
dnl SNERT_OPTION_ENABLE_PID(default)
dnl
AC_DEFUN(SNERT_OPTION_ENABLE_PID,[
	AC_ARG_ENABLE(pid, 
		[AC_HELP_STRING([--enable-pid=filepath],[specifiy an alternative pid file path])],
		[
		],[
			dnl Almost all unix machines agree on this location.
			if test -z "$1"; then
				enable_pid='${localstatedir}/run/${PACKAGE_NAME}.pid'
			else
				enable_pid=$1
			fi
		]
	)
	snert_pid_file=`eval echo $enable_pid`
	AC_DEFINE_UNQUOTED(PID_FILE, ["`eval echo ${snert_pid_file}`"])
	AC_SUBST(snert_pid_file)
	AC_SUBST(enable_pid)
])

dnl
dnl SNERT_OPTION_ENABLE_SOCKET(default)
dnl
AC_DEFUN(SNERT_OPTION_ENABLE_SOCKET,[
	AC_ARG_ENABLE(socket, 
		[AC_HELP_STRING([--enable-socket=filepath],[specifiy an alternative Unix domain socket])],
		[
		],[
			dnl Almost all unix machines agree on this location. 
			dnl Note though that if you have more than one file 
			dnl here, its recommended to create a subdirectory
			dnl with all the related files.
			if test -z "$1"; then
				enable_socket='${localstatedir}/run/${PACKAGE_NAME}.socket'
			else
				enable_socket=$1
			fi			
		]
	)
	snert_socket_file=`eval echo $enable_socket`
	AC_DEFINE_UNQUOTED(SOCKET_FILE, ["`eval echo ${snert_socket_file}`"])
	AC_SUBST(snert_socket_file)
	AC_SUBST(enable_socket)
])

dnl
dnl SNERT_OPTION_ENABLE_POPAUTH
dnl
AC_DEFUN(SNERT_OPTION_ENABLE_POPAUTH,[
	AC_ARG_ENABLE(popauth, 
		[AC_HELP_STRING([--enable-popauth],[enable POP-before-SMTP macro checking in smf API])],
		[AC_DEFINE_UNQUOTED(HAVE_POP_BEFORE_SMTP)]
	)
])

dnl
dnl SNERT_OPTION_ENABLE_STARTUP_DIR
dnl
AC_DEFUN(SNERT_OPTION_ENABLE_STARTUP_DIR,[
	AC_ARG_ENABLE(startup-dir, 
		[AC_HELP_STRING([--enable-startup-dir=dir],[specifiy the startup script directory location])],
		[
			STARTUP_DIR="$enableval"
			STARTUP_EXT='.sh'
		], [
			if test -d '/usr/local/etc/rc.d'; then
				# FreeBSD
				STARTUP_DIR='/usr/local/etc/rc.d'
				STARTUP_EXT='.sh'
			elif test -d '/etc/init.d'; then
				# SunOS, Debian Linux, System V variant
				STARTUP_DIR='/etc/init.d'
			elif test -d '/etc/rc.d/init.d'; then
				# Linux, System V variant
				STARTUP_DIR='/etc/rc.d/init.d'
			else
				# OpenBSD has no place to put startup
				# scripts that might be used by rc.local.
				STARTUP_DIR='/usr/local/sbin' 
				STARTUP_EXT='.sh'
			fi	
		]
	)
	AC_SUBST(STARTUP_DIR)
	AC_SUBST(STARTUP_EXT)
])

dnl
dnl SNERT_OPTION_WITH_DB
dnl
AC_DEFUN(SNERT_OPTION_WITH_DB,[
	AC_ARG_WITH(db, 
		[AC_HELP_STRING([[--with-db{=DIR} ]],[Include Berkeley DB support])]
	)
])

dnl
dnl SNERT_OPTION_WITH_SENDMAIL
dnl
AC_DEFUN(SNERT_OPTION_WITH_SENDMAIL,[
	AC_ARG_WITH(sendmail, 
		[AC_HELP_STRING([--with-sendmail=dir],[directory where sendmail.cf lives, default /etc/mail])],
		[with_sendmail="$withval"], [with_sendmail='/etc/mail']
	)
	AC_SUBST(with_sendmail)
])

dnl
dnl SNERT_FUNC_FLOCK
dnl
AC_DEFUN(SNERT_FUNC_FLOCK,[
	echo
	echo "Check for flock() support..."
	echo 
	AC_CHECK_HEADER(sys/file.h, [
		AC_DEFINE_UNQUOTED(HAVE_SYS_FILE_H)

		SNERT_CHECK_DEFINE(LOCK_SH, sys/file.h)
		if test $ac_cv_define_LOCK_SH = 'yes'; then
			AC_CHECK_FUNC(flock)
		fi
	])
])
	
dnl
dnl SNERT_POSIX_IO
dnl
AC_DEFUN(SNERT_POSIX_IO,[
	echo
	echo "Check for POSIX File & Directory I/O support..."
	echo 
	AC_HEADER_DIRENT
dnl autoconf says the following should be included:
dnl
dnl #if HAVE_DIRENT_H
dnl # include <dirent.h>
dnl # define NAMLEN(dirent) strlen((dirent)->d_name)
dnl #else
dnl # define dirent direct
dnl # define NAMLEN(dirent) (dirent)->d_namlen
dnl # if HAVE_SYS_NDIR_H
dnl #  include <sys/ndir.h>
dnl # endif
dnl # if HAVE_SYS_DIR_H
dnl #  include <sys/dir.h>
dnl # endif
dnl # if HAVE_NDIR_H
dnl #  include <ndir.h>
dnl # endif
dnl #endif	

	AC_CHECK_HEADERS([unistd.h fcntl.h sys/stat.h utime.h])
	AC_CHECK_FUNCS([chdir getcwd mkdir rmdir closedir opendir readdir])
	AC_CHECK_FUNCS([chmod chown fchmod stat fstat link rename unlink umask utime])
	AC_CHECK_FUNCS([close creat dup dup2 ftruncate chsize lseek open pipe read write])
	AC_FUNC_CHOWN
])

dnl
dnl SNERT_POSIX_SIGNALS
dnl
AC_DEFUN(SNERT_POSIX_SIGNALS,[
	echo
	echo "Check for POSIX signal support..."
	echo 
	AC_CHECK_HEADER([signal.h],[
		AC_DEFINE_UNQUOTED(HAVE_SIGNAL_H)
		AC_CHECK_TYPES([sigset_t],[],[],[
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_SIGNAL_H
# include <signal.h>
#endif
		])
		AC_CHECK_FUNCS([kill sigemptyset sigfillset sigaddset sigdelset sigismember])
		AC_CHECK_FUNCS([sigaction sigprocmask sigpending sigsuspend])

		dnl _POSIX_REAL_TIME_SIGNALS
		dnl AC_CHECK_FUNCS([sigwaitinfo sigtimedwait sigqueue])
	])
])

dnl
dnl SNERT_PROCESS
dnl
AC_DEFUN(SNERT_PROCESS,[
	echo
	echo "Check for process support..."
	echo 
	AC_CHECK_HEADER([unistd.h],[
		AC_DEFINE_UNQUOTED(HAVE_UNISTD_H)
		AC_CHECK_FUNCS([getuid setgid geteuid setegid getgroups setgroups initgroups])
	])
])

dnl
dnl SNERT_SETJMP
dnl
AC_DEFUN(SNERT_SETJMP,[
	echo
	echo "Check for setjmp support..."
	echo 
	AC_CHECK_HEADER([setjmp.h], [
		AC_DEFINE_UNQUOTED(HAVE_SETJMP_H)
		AC_CHECK_TYPES([jmp_buf, sigjmp_buf],[],[],[
#ifdef HAVE_SETJMP_H
# include <setjmp.h>
#endif
		])
		AC_CHECK_FUNCS([setjmp longjmp sigsetjmp siglongjmp])
	])
])

dnl
dnl SNERT_PTHREAD
dnl
AC_DEFUN(SNERT_PTHREAD,[
	echo
	echo "Check for POSIX thread & mutex support..."
	echo 
	AC_CHECK_HEADER([pthread.h],[
		AC_DEFINE_UNQUOTED(HAVE_PTHREAD_H)

		CFLAGS="-D_REENTRANT ${CFLAGS}"		

		case "$platform" in
		FreeBSD|OpenBSD|NetBSD)
			CFLAGS="-D_THREAD_SAFE -pthread ${CFLAGS}"
			;;
		esac

		dnl For SunOS. Beware of using AC_SEARCH_LIBS() on SunOS
		dnl platforms, because some functions appear as stubs in
		dnl other libraries.
		SNERT_CHECK_LIB(pthread, pthread_create)

		AC_CHECK_TYPES([pthread_t, pthread_mutex_t, sigset_t],[],[],[
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_SIGNAL_H
# include <signal.h>
#endif
#ifdef HAVE_PTHREAD_H
# include <pthread.h>
#endif
		])
		
		snert_pthread_min='yes'
		AC_CHECK_FUNCS([pthread_create pthread_cancel pthread_equal pthread_exit pthread_join pthread_kill pthread_self],[],[snert_pthread_min='no'])

		dnl OpenBSD requires -pthread in order to link sigwait.
		snert_pthread_max='yes'
		AC_CHECK_FUNCS([pthread_detach pthread_yield pthread_sigmask sigwait],[],[snert_pthread_max='no'])

		snert_pthread_mutex='yes'
		AC_CHECK_FUNCS([pthread_mutex_init pthread_mutex_destroy pthread_mutex_lock pthread_mutex_unlock],[],[snert_pthread_mutex='no'])

		snert_pthread_cond='yes'
		AC_CHECK_FUNCS([pthread_cond_broadcast pthread_cond_destroy pthread_cond_init pthread_cond_signal pthread_cond_timedwait pthread_cond_wait],[],[snert_pthread_cond='no'])	

		snert_pthread_rwlock='yes'
		AC_CHECK_FUNCS([pthread_rwlock_init pthread_rwlock_destroy pthread_rwlock_unlock pthread_rwlock_rdlock pthread_rwlock_wrlock pthread_rwlock_tryrdlock pthread_rwlock_trywrlock],[],[snert_pthread_rwlock='no'])

		snert_pthread_lock_global='yes'
		AC_CHECK_FUNCS([pthread_lock_global_np pthread_unlock_global_np],[],[snert_pthread_lock_global='no'])	
	],[
		snert_pthread_min='no'
		snert_pthread_max='no'
		snert_pthread_mutex='no'
		snert_pthread_rwlock='no'
		snert_pthread_lock_global='no'	
		snert_pthread_cond='no'
	],[/* */])
])

dnl
dnl SNERT_POSIX_SEMAPHORES
dnl
AC_DEFUN(SNERT_POSIX_SEMAPHORES,[
	echo
	echo "Check for POSIX semaphore support..."
	echo 
	AC_CHECK_HEADER([semaphore.h],[
		AC_DEFINE_UNQUOTED(HAVE_SEMAPHORE_H)
		
		dnl SunOS
		SNERT_CHECK_LIB(rt, sem_init)
		
		AC_SEARCH_LIBS([sem_init], [pthread rt],[
	
			snert_posix_semaphores='yes'
			AC_CHECK_TYPES([sem_t],[],[],[
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_SEMAPHORE_H
# include <semaphore.h>
#endif
			])
			AC_CHECK_FUNCS([sem_init sem_destroy sem_wait sem_post],[],[
				snert_posix_semaphores='no'
				break
			])
		],[
			snert_posix_semaphores='no'
		])
	],[
		snert_posix_semaphores='no'	
	],[/* */])
])

dnl
dnl SNERT_SYSTEMV_SEMAPHORES
dnl
AC_DEFUN(SNERT_SYSTEMV_SEMAPHORES,[
	echo
	echo "Check for System V semaphore support..."
	echo 
	snert_systemv_semaphores='yes'
	AC_CHECK_HEADERS([sys/ipc.h sys/sem.h],[],[snert_systemv_semaphores='no'],[
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
	])

	if test $snert_systemv_semaphores = 'yes'; then
		AC_CHECK_FUNCS([semget semctl semop],[],[
			snert_systemv_semaphores='no'
			break;
		])
	fi
])

dnl
dnl SNERT_PAM
dnl
AC_DEFUN(SNERT_PAM,[
	echo
	echo "Check for PAM support..."
	echo 
	AC_CHECK_LIB(pam, pam_start)
	AC_CHECK_HEADERS(security/pam_appl.h)
	if test ${isDebian:-no} = 'yes' -a "$ac_cv_header_security_pam_appl_h" = 'no'; then
		apt-get install -y libpam0g-dev
		unset ac_cv_header_security_pam_appl_h
		AC_CHECK_HEADERS(security/pam_appl.h)
	fi
	AC_CHECK_FUNCS(pam_start pam_authenticate pam_end)
])

dnl
dnl SNERT_ANSI_STRING
dnl
AC_DEFUN(SNERT_ANSI_STRING,[
	echo
	echo "Check for ANSI string functions..."
	echo 
	AC_CHECK_FUNCS(memchr memcmp memcpy memmove memset)
	AC_FUNC_MEMCMP	
	AC_LIBOBJ(memcmp)
	AC_CHECK_FUNCS(strcat strncat strcpy strncpy strcmp strncmp strxfrm)
	AC_CHECK_FUNCS(strchr strcspn strerror strlen strpbrk strrchr strspn strstr strtok)
	AC_FUNC_STRCOLL
	AC_FUNC_STRERROR_R
])

dnl
dnl SNERT_ANSI_TIME
dnl
AC_DEFUN(SNERT_ANSI_TIME,[
	echo
	echo "Check for ANSI & supplemental time support..."
	echo 
	
	dnl SunOS
	SNERT_CHECK_LIB(rt, clock_gettime)
	
	AC_CHECK_HEADERS(time.h sys/time.h)
	AC_HEADER_TIME
dnl autoconf says the following should be included:
dnl
dnl #if TIME_WITH_SYS_TIME
dnl # include <sys/time.h>
dnl # include <time.h>
dnl #else
dnl # if HAVE_SYS_TIME_H
dnl #  include <sys/time.h>
dnl # else
dnl #  include <time.h>
dnl # endif
dnl #endif	
	AC_CHECK_FUNCS(clock difftime mktime time asctime ctime gmtime localtime tzset)
	AC_CHECK_FUNCS(asctime_r ctime_r gmtime_r localtime_r clock_gettime gettimeofday)
	AC_CHECK_FUNCS(alarm getitimer setitimer)
	dnl These are typically macros:  timerclear timerisset timercmp timersub timeradd
	AC_FUNC_MKTIME
	AC_LIBOBJ(mktime)
	AC_FUNC_STRFTIME
dnl	AC_CHECK_TYPE(struct tm)
	AC_STRUCT_TM
	AC_STRUCT_TIMEZONE	
])

dnl
dnl SNERT_EXTRA_STRING
dnl
AC_DEFUN(SNERT_EXTRA_STRING,[
	echo
	echo "Check for supplemental string support..."
	echo 
	AC_CHECK_FUNCS(strdup strtol strlcpy strlcat strcasecmp strncasecmp)
	AC_CHECK_FUNCS(snprintf vsnprintf setproctitle)
	AC_FUNC_VPRINTF
])

dnl
dnl SNERT_REGEX
dnl
AC_DEFUN(SNERT_REGEX,[
	echo
	echo "Check for regex..."
	echo 
	AC_SEARCH_LIBS([regcomp], [regex])
	AC_CHECK_HEADERS([regex.h],[
		AC_CHECK_FUNCS(regcomp regexec regerror regfree)
	])
])

dnl
dnl SNERT_NETWORK
dnl
AC_DEFUN(SNERT_NETWORK,[
	echo
	echo "Check for Network services..."
	echo 
	AC_SEARCH_LIBS([gethostbyname], [socket nsl])
	AC_CHECK_HEADERS([netdb.h],[
		AC_CHECK_FUNCS(gethostname gethostbyname gethostbyname2 gethostbyaddr gethostbyname_r gethostbyname2_r gethostbyaddr_r)
		AC_CHECK_FUNCS(gethostent sethostent endhostent hstrerror herror)
		AC_CHECK_FUNCS(getservent getservbyport getservbyname setservent endservent)
		AC_CHECK_FUNCS(getprotoent getprotobynumber getprotobyname setprotoent endprotoent)
	])
])

dnl
dnl SNERT_SOCKET
dnl
AC_DEFUN(SNERT_SOCKET,[
	echo
	echo "Check for Socket support..."
	echo 
	SNERT_CHECK_PREDEFINE(__WIN32__)
	
	if test "$ac_cv_define___WIN32__" = 'no' ; then
		AC_SEARCH_LIBS([socket], [socket nsl])
		AC_SEARCH_LIBS([inet_aton], [socket nsl resolv])
		
		AC_CHECK_HEADERS([sys/socket.h netinet/in.h netinet/in6.h netinet6/in6.h netinet/tcp.h poll.h sys/poll.h sys/un.h arpa/inet.h])
		
dnl When using poll() use this block.
dnl
dnl #ifdef HAVE_POLL_H
dnl # include <poll.h>
dnl # ifndef INFTIM
dnl #  define INFTIM	(-1)
dnl # endif
dnl #endif
		
		AC_CHECK_FUNCS(inet_pton inet_aton inet_addr inet_ntoa inet_ntop)		
		AC_CHECK_FUNCS(accept bind connect listen poll select shutdown socket)
		AC_CHECK_FUNCS(getnameinfo getpeereid getpeername getsockname getsockopt setsockopt)
		AC_CHECK_FUNCS(recv recvfrom recvmsg send sendmsg sendto)
		AC_CHECK_FUNCS(htonl htons ntohl ntohs)

		AC_CHECK_TYPES([struct sockaddr_in6, struct in6_addr, struct sockaddr_un, socklen_t],[],[],[
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif
#ifdef HAVE_SYS_UN_H
# include <sys/un.h>
#endif
#ifdef HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
#ifdef HAVE_NETINET_IN6_H
# include <netinet/in6.h>
#endif
#ifdef HAVE_NETINET6_IN6_H
# include <netinet6/in6.h>
#endif
		])
		AC_CHECK_MEMBERS([struct sockaddr.sa_len, struct sockaddr_in.sin_len, struct sockaddr_in6.sin6_len, struct sockaddr_un.sun_len],[],[],[
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif
#ifdef HAVE_SYS_UN_H
# include <sys/un.h>
#endif
#ifdef HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
#ifdef HAVE_NETINET_IN6_H
# include <netinet/in6.h>
#endif
#ifdef HAVE_NETINET6_IN6_H
# include <netinet6/in6.h>
#endif
		])
		
	else
		AC_CHECK_HEADERS(windows.h io.h)
		AC_CHECK_HEADER(winsock2.h,[
			AC_DEFINE_UNQUOTED(AS_TR_CPP([HAVE_]winsock2.h))
		],[],[
#if defined(__WIN32__) && defined(HAVE_WINDOWS_H)
# include  <windows.h>
#endif
		])
		AC_CHECK_HEADER(ws2tcpip.h,[
			AC_DEFINE_UNQUOTED(AS_TR_CPP([HAVE_]ws2tcpip.h))
		],[],[
#if defined(__WIN32__)
# if defined(HAVE_WINDOWS_H)
#  include  <windows.h>
# endif
# if defined(HAVE_WINSOCK2_H)
#  include  <winsock2.h>
# endif
#endif
		])

		# Assume these are present in winsock2.h
		for i in \
			accept \
			bind \
			closesocket \
			connect \
			endservent \
			gethostname \
			getnameinfo \
			getpeername \
			getprotobyname \
			getprotobynumber \
			getservbyname \
			getservbyport \
			getservent \
			getsockname \
			getsockopt \
			htonl \
			htons \
			inet_addr \
			inet_ntoa \
			listen \
			ntohl \
			ntohs \
			recv \
			recvfrom \
			select \
			send \
			sendto \
			setservent \
			setsockopt \
			shutdown \
			socket
		do
			AC_MSG_CHECKING([for $i])
			AC_DEFINE_UNQUOTED(AS_TR_CPP([HAVE_]$i))
			AC_MSG_RESULT([assumed in winsock2.h])	
		done
		LIBS="-lIphlpapi ${LIBS}"
	fi
])

dnl
dnl SNERT_INIT($c_macro_prefix, $copyright, $build_id_file)
dnl
AC_DEFUN(SNERT_INIT,[
	AC_COPYRIGHT([$2])

	AC_SUBST(package_copyright, ['$2'])
	AC_SUBST(package_major, [[`echo $PACKAGE_VERSION | sed -e 's/^\([0-9]*\)\.\([0-9]*\)/\1/'`]])
	AC_SUBST(package_minor, [[`echo $PACKAGE_VERSION | sed -e 's/^\([0-9]*\)\.\([0-9]*\)/\2/'`]])

	AC_DEFINE_UNQUOTED($1_NAME, ["$PACKAGE_NAME"])
	AC_DEFINE_UNQUOTED($1_MAJOR, $package_major)
	AC_DEFINE_UNQUOTED($1_MINOR, $package_minor)
	AC_DEFINE_UNQUOTED($1_AUTHOR, ["$PACKAGE_BUGREPORT"])
	AC_DEFINE_UNQUOTED($1_VERSION, ["$PACKAGE_VERSION"])
	AC_DEFINE_UNQUOTED($1_STRING, ["$PACKAGE_STRING"])
	AC_DEFINE_UNQUOTED($1_COPYRIGHT, ["$package_copyright"])

	dnl Used for summary display. Note that the build number should be
	dnl passed on the compiler command line within the makefiles using
	dnl -D in order to make sure we have the most recent build number. 
	dnl 
	dnl Placing the build number into config.h using substitutions
	dnl means we have to rerun ./configure in order update config.status
	dnl when the build number changes. This is slow and cumbersome during
	dnl development.
	
	package_build_number="$3"
	if test -z "${package_build_number}" ; then
		package_build_number='BUILD_ID.TXT'
	fi
	AC_DEFINE_UNQUOTED($1_BUILD, [`cat $package_build_number`])
	AC_CONFIG_COMMANDS_POST([package_build=`cat $package_build_number`])

	SNERT_PLATFORM
	SNERT_CHECK_CONFIGURE
	
	dnl Define some default vaules for milters subsitutions. 
	if test X"$1" = X'MILTER' ; then
		test "$prefix" = NONE -a "$localstatedir" = '${prefix}/var' && localstatedir='/var'
		AC_DEFINE_UNQUOTED(snert_milter_t_equate, [C:5m;S=10s;R=10s;E:5m])		
	fi
])

dnl
dnl SNERT_SUMMARY
dnl
AC_DEFUN(SNERT_SUMMARY,[
	echo
	echo $PACKAGE_NAME/$package_major.$package_minor.$package_build
	echo $package_copyright
	echo
	AC_MSG_RESULT([  Platform.......: $platform $CC])
	AC_MSG_RESULT([  CFLAGS.........: $CFLAGS])
	AC_MSG_RESULT([  LDFLAGS........: $LDFLAGS])
	AC_MSG_RESULT([  LIBS...........: $LIBS])
])

dnl if ! grep -q "^milter:" /etc/group; then
dnl 	groupadd milter
dnl fi
dnl 
dnl if ! grep -q "^milter:" /etc/passwd; then
dnl 	useradd -c 'milter processes' -g milter milter
dnl fi
dnl 
dnl if grep -q "^smmsp:" /etc/group; then
dnl 	usermod -G smmsp milter
dnl fi
