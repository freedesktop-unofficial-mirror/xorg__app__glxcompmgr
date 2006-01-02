libfade_la_LDFLAGS = -avoid-version
libfade_la_LIBADD = @GLXCOMP_LIBS@
libfade_la_SOURCES = fade.c

libcube_la_LDFLAGS = -avoid-version
libcube_la_LIBADD = @GLXCOMP_LIBS@ @LIBSVG_CAIRO_LIBS@
libcube_la_SOURCES = cube.c

librotate_la_LDFLAGS = -avoid-version
librotate_la_LIBADD = @GLXCOMP_LIBS@
librotate_la_SOURCES = rotate.c

libzoom_la_LDFLAGS = -avoid-version
libzoom_la_LIBADD = @GLXCOMP_LIBS@
libzoom_la_SOURCES = zoom.c

libexpose_la_LDFLAGS = -avoid-version
libexpose_la_LIBADD = @GLXCOMP_LIBS@
libexpose_la_SOURCES = expose.c

libwobbly_la_LDFLAGS = -avoid-version
libwobbly_la_LIBADD = @GLXCOMP_LIBS@
libwobbly_la_SOURCES = wobbly.c

libshadow_la_LDFLAGS = -avoid-version
libshadow_la_LIBADD = @GLXCOMP_LIBS@
libshadow_la_SOURCES = shadow.c

if GCONF_PLUGIN
libgconf_la_LDFLAGS = -avoid-version
libgconf_la_LIBADD = @GLXCOMP_LIBS@ @GCONF_LIBS@
libgconf_la_SOURCES = gconf.c
libgconf_library = libgconf.la
libgconf_includes = @GCONF_CFLAGS@
endif

INCLUDES =			\
	@GLXCOMP_CFLAGS@	\
	@LIBSVG_CAIRO_CFLAGS@	\
	@GCONF_CFLAGS@		\
	-I$(top_srcdir)/include

moduledir = $(plugindir)

module_LTLIBRARIES =	    \
	libgconf.la	    \
	libfade.la	    \
	libcube.la	    \
	librotate.la	    \
	libzoom.la	    \
	libexpose.la	    \
	libwobbly.la	    \
	libshadow.la	    \
	$(libgconf_library)
