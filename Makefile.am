AUTOMAKE_OPTIONS = foreign
lib_LTLIBRARIES = libhttpio.la
libhttpio_la_SOURCES =        \
    src/http-connection.c      \
    src/http-post-parameters.c \
    src/http-protocol.c        \
    src/http-util.c            \
    src/http-ssl.c             \
    src/http-websockets.c
libhttpio_la_CFLAGS = $(CFLAGS) -I$(srcdir)/include
libhttpio_la_LIBADD = $(LDFLAGS)

httpiodir = $(includedir)/httpio
httpio_HEADERS = \
    include/http-connection.h      \
    include/http-post-parameters.h \
    include/http-protocol.h        \
    include/http-ssl.h             \
    include/http-util.h            \
    include/http-websockets.h
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = httpio.pc

CPPCHECKOPTS = warning,style,performance

clang-analyze: $(libhttpio_la_SOURCES:.c=.clang-analyze)
	@true

cppcheck: $(libhttpio_la_SOURCES:.c=.cppcheck)
	@true

%.cppcheck: %.c
	@echo "-- CPPCHECK $<"
	cppcheck --language=c --enable=$(CPPCHECKOPTS) --template='{file}:{line}:4: {severity}:{message}' $<

%.clang-analyze: %.c
	@clang --analyze $(libhttpio_la_CFLAGS)  $<

