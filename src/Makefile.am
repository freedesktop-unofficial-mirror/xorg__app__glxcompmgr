INCLUDES =			     \
	@GLXCOMP_CFLAGS@	     \
	-I$(top_srcdir)/include	     \
	-DPLUGINDIR=\"$(plugindir)\" \
	-DIMAGEDIR=\"$(imagedir)\"

bin_PROGRAMS = glxcompmgr

glxcompmgr_LDADD = @GLXCOMP_LIBS@ @GL_LIBS@ -lm
glxcompmgr_LDFLAGS = -export-dynamic
glxcompmgr_SOURCES = \
	glxcompmgr.c \
	privates.c   \
	texture.c    \
	display.c    \
	screen.c     \
	window.c     \
	event.c      \
	paint.c	     \
	option.c     \
	plugin.c     \
	readpng.c
