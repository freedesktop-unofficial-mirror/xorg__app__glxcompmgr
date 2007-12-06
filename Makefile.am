SUBDIRS = include src plugins images

EXTRA_DIST =	      \
	COPYING	      \
	glxcomp.pc.in

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = glxcomp.pc

EXTRA_DIST += ChangeLog
MAINTAINERCLEANFILES = ChangeLog

.PHONY: ChangeLog

ChangeLog:
	(GIT_DIR=$(top_srcdir)/.git git-log > .changelog.tmp && mv .changelog.tmp ChangeLog; rm -f .changelog.tmp) || (touch ChangeLog; echo 'git directory not found: installing possibly empty changelog.' >&2)

dist-hook: ChangeLog
