AC_INIT([httpio], [1.0.5], [iharob@gmail.com])
AM_INIT_AUTOMAKE([subdir-objects foreign])

AC_PROG_LIBTOOL
AC_PROG_CC

PKG_CHECK_MODULES([OPENSSL], [openssl >= 1.1.0])

AC_SUBST([CFLAGS], "${CFLAGS} -std=gnu99")
AC_ARG_ENABLE(
    [debug],
    AS_HELP_STRING([--enable-debug], [Habilitar depuración en los binarios]),
    [
        AC_SUBST([LDFLAGS], "${LDFLAGS} -O0 -g3")
        AC_SUBST([CFLAGS], "${CFLAGS} -O0 -g3 -D_DEBUG -Wall -Werror")
    ],
    [
        AC_SUBST([LDFLAGS], "${LDFLAGS} -O2 -g0")
        AC_SUBST([CFLAGS], "${CFLAGS} -O2 -g0")
    ]
)

AC_SUBST(CFLAGS)
AC_SUBST(LDFLAGS)

AC_CONFIG_FILES([Makefile ${PACKAGE_NAME}.pc])
AC_OUTPUT
