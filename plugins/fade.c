/*
 * Copyright © 2005 Novell, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of
 * Novell, Inc. not be used in advertising or publicity pertaining to
 * distribution of the software without specific, written prior permission.
 * Novell, Inc. makes no representations about the suitability of this
 * software for any purpose. It is provided "as is" without express or
 * implied warranty.
 *
 * NOVELL, INC. DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
 * NO EVENT SHALL NOVELL, INC. BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author: David Reveman <davidr@novell.com>
 */

#include <stdlib.h>

#include <comp.h>

#define FADE_SPEED_DEFAULT    4.0f
#define FADE_SPEED_MIN        0.1f
#define FADE_SPEED_MAX       10.0f
#define FADE_SPEED_PRECISION  0.1f

static int displayPrivateIndex;

typedef struct _FadeDisplay {
    int		    screenPrivateIndex;
    HandleEventProc handleEvent;
} FadeDisplay;

#define FADE_SCREEN_OPTION_FADE_SPEED 0
#define FADE_SCREEN_OPTION_NUM        1

typedef struct _FadeScreen {
    int			   windowPrivateIndex;
    int			   fadeTime;
    int			   steps;

    CompOption opt[FADE_SCREEN_OPTION_NUM];

    PreparePaintScreenProc preparePaintScreen;
    PaintWindowProc	   paintWindow;
    DamageWindowRectProc   damageWindowRect;
} FadeScreen;

typedef struct _FadeWindow {
    int	opacity;
    int	direction;
    int destroyed;
} FadeWindow;

#define GET_FADE_DISPLAY(d)				     \
    ((FadeDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define FADE_DISPLAY(d)			   \
    FadeDisplay *fd = GET_FADE_DISPLAY (d)

#define GET_FADE_SCREEN(s, fd)					 \
    ((FadeScreen *) (s)->privates[(fd)->screenPrivateIndex].ptr)

#define FADE_SCREEN(s)							\
    FadeScreen *fs = GET_FADE_SCREEN (s, GET_FADE_DISPLAY (s->display))

#define GET_FADE_WINDOW(w, fs)				         \
    ((FadeWindow *) (w)->privates[(fs)->windowPrivateIndex].ptr)

#define FADE_WINDOW(w)					     \
    FadeWindow *fw = GET_FADE_WINDOW  (w,		     \
		     GET_FADE_SCREEN  (w->screen,	     \
		     GET_FADE_DISPLAY (w->screen->display)))

#define NUM_OPTIONS(s) (sizeof ((s)->opt) / sizeof (CompOption))

static CompOption *
fadeGetScreenOptions (CompScreen *screen,
		      int	 *count)
{
    FADE_SCREEN (screen);

    *count = NUM_OPTIONS (fs);
    return fs->opt;
}

static Bool
fadeSetScreenOption (CompScreen      *screen,
		     char	     *name,
		     CompOptionValue *value)
{
    CompOption *o;
    int	       index;

    FADE_SCREEN (screen);

    o = compFindOption (fs->opt, NUM_OPTIONS (fs), name, &index);
    if (!o)
	return FALSE;

    switch (index) {
    case FADE_SCREEN_OPTION_FADE_SPEED:
	if (compSetFloatOption (o, value))
	{
	    fs->fadeTime = 1000.0f / o->value.f;
	    return TRUE;
	}
    default:
	break;
    }

    return FALSE;
}

static void
fadeScreenInitOptions (FadeScreen *fs)
{
    CompOption *o;

    o = &fs->opt[FADE_SCREEN_OPTION_FADE_SPEED];
    o->name		= "fade_speed";
    o->shortDesc	= "Fade Speed";
    o->longDesc		= "Window fade speed";
    o->type		= CompOptionTypeFloat;
    o->value.f		= FADE_SPEED_DEFAULT;
    o->rest.f.min	= FADE_SPEED_MIN;
    o->rest.f.max	= FADE_SPEED_MAX;
    o->rest.f.precision = FADE_SPEED_PRECISION;
}

static void
fadePreparePaintScreen (CompScreen *s,
			int	   msSinceLastPaint)
{
    FADE_SCREEN (s);

    fs->steps = (msSinceLastPaint * OPAQUE) / fs->fadeTime;
    if (fs->steps < 256)
	fs->steps = 256;

    UNWRAP (fs, s, preparePaintScreen);
    (*s->preparePaintScreen) (s, msSinceLastPaint);
    WRAP (fs, s, preparePaintScreen, fadePreparePaintScreen);
}

static Bool
fadePaintWindow (CompWindow		 *w,
		 const WindowPaintAttrib *attrib,
		 Region			 region,
		 unsigned int		 mask)
{
    CompScreen *s = w->screen;
    Bool       status;

    FADE_SCREEN (s);
    FADE_WINDOW (w);

    if (fw->opacity < OPAQUE)
    {
	GLint opacity;

	opacity = fw->opacity + fs->steps * fw->direction;
	if (opacity > 0)
	{
	    if (opacity < OPAQUE)
	    {
		WindowPaintAttrib fAttrib = *attrib;

		fAttrib.opacity = MULTIPLY_USHORT (opacity, attrib->opacity);

		UNWRAP (fs, s, paintWindow);
		status = (*s->paintWindow) (w, &fAttrib, region, mask);
		WRAP (fs, s, paintWindow, fadePaintWindow);

		if (status)
		{
		    fw->opacity = opacity;

		    addWindowDamage (w);
		}
	    }
	    else
	    {
		UNWRAP (fs, s, paintWindow);
		status = (*s->paintWindow) (w, attrib, region, mask);
		WRAP (fs, s, paintWindow, fadePaintWindow);

		if (status)
		{
		    fw->opacity   = OPAQUE;
		    fw->direction = 0;
		}
	    }
	}
	else
	{
	    fw->opacity   = 0;
	    fw->direction = 0;

	    if (fw->destroyed)
		destroyWindow (w);
	    else
		unmapWindow (w);

	    return (mask & PAINT_WINDOW_SOLID_MASK) ? FALSE : TRUE;
	}
    }
    else
    {
	UNWRAP (fs, s, paintWindow);
	status = (*s->paintWindow) (w, attrib, region, mask);
	WRAP (fs, s, paintWindow, fadePaintWindow);
    }

    return status;
}

static void
fadeHandleEvent (CompDisplay *d,
		 XEvent      *event)
{
    CompWindow *w;

    FADE_DISPLAY (d);

    switch (event->type) {
    case DestroyNotify:
	w = findWindowAtDisplay (d, event->xdestroywindow.window);
	if (w)
	{
	    FADE_WINDOW (w);

	    if (!fw->direction)
		fw->opacity = OPAQUE - 1;

	    fw->direction = -1;
	    fw->destroyed = 1;

	    addWindowDamage (w);
	    return;
	}
	break;
    case UnmapNotify:
	w = findWindowAtDisplay (d, event->xunmap.window);
	if (w)
	{
	    FADE_WINDOW (w);

	    if (!fw->direction)
		fw->opacity = OPAQUE - 1;

	    fw->direction = -1;

	    addWindowDamage (w);
	    return;
	}
	break;
    case MapNotify:
	w = findWindowAtDisplay (d, event->xunmap.window);
	if (w)
	{
	    FADE_WINDOW (w);

	    /* make sure any pending unmap are processed */
	    if (fw->direction < 0)
		unmapWindow (w);
	}
    default:
	break;
    }

    UNWRAP (fd, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (fd, d, handleEvent, fadeHandleEvent);
}

static Bool
fadeDamageWindowRect (CompWindow *w,
		      Bool	 initial,
		      BoxPtr     rect)
{
    Bool status;

    FADE_SCREEN (w->screen);

    if (initial)
    {
	FADE_WINDOW (w);

	if (!fw->direction)
	    fw->opacity = 1;

	fw->direction = 1;
    }

    UNWRAP (fs, w->screen, damageWindowRect);
    status = (*w->screen->damageWindowRect) (w, initial, rect);
    WRAP (fs, w->screen, damageWindowRect, fadeDamageWindowRect);

    return status;
}

static Bool
fadeInitDisplay (CompPlugin  *p,
		 CompDisplay *d)
{
    FadeDisplay *fd;

    fd = malloc (sizeof (FadeDisplay));
    if (!fd)
	return FALSE;

    fd->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (fd->screenPrivateIndex < 0)
    {
	free (fd);
	return FALSE;
    }

    WRAP (fd, d, handleEvent, fadeHandleEvent);

    d->privates[displayPrivateIndex].ptr = fd;

    return TRUE;
}

static void
fadeFiniDisplay (CompPlugin *p,
		 CompDisplay *d)
{
    FADE_DISPLAY (d);

    freeScreenPrivateIndex (d, fd->screenPrivateIndex);

    UNWRAP (fd, d, handleEvent);

    free (fd);
}

static Bool
fadeInitScreen (CompPlugin *p,
		CompScreen *s)
{
    FadeScreen *fs;

    FADE_DISPLAY (s->display);

    fs = malloc (sizeof (FadeScreen));
    if (!fs)
	return FALSE;

    fs->windowPrivateIndex = allocateWindowPrivateIndex (s);
    if (fs->windowPrivateIndex < 0)
    {
	free (fs);
	return FALSE;
    }

    fs->steps    = 0;
    fs->fadeTime = 1000.0f / FADE_SPEED_DEFAULT;

    fadeScreenInitOptions (fs);

    WRAP (fs, s, preparePaintScreen, fadePreparePaintScreen);
    WRAP (fs, s, paintWindow, fadePaintWindow);
    WRAP (fs, s, damageWindowRect, fadeDamageWindowRect);

    s->privates[fd->screenPrivateIndex].ptr = fs;

    return TRUE;
}

static void
fadeFiniScreen (CompPlugin *p,
		CompScreen *s)
{
    FADE_SCREEN (s);

    freeWindowPrivateIndex (s, fs->windowPrivateIndex);

    UNWRAP (fs, s, preparePaintScreen);
    UNWRAP (fs, s, paintWindow);
    UNWRAP (fs, s, damageWindowRect);

    free (fs);
}

static Bool
fadeInitWindow (CompPlugin *p,
		CompWindow *w)
{
    FadeWindow *fw;

    FADE_SCREEN (w->screen);

    fw = malloc (sizeof (FadeWindow));
    if (!fw)
	return FALSE;

    fw->opacity   = OPAQUE;
    fw->direction = 0;
    fw->destroyed = 0;

    w->privates[fs->windowPrivateIndex].ptr = fw;

    return TRUE;
}

static void
fadeFiniWindow (CompPlugin *p,
		CompWindow *w)
{
    FADE_WINDOW (w);

    free (fw);
}

static Bool
fadeInit (CompPlugin *p)
{
    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
	return FALSE;

    return TRUE;
}

static void
fadeFini (CompPlugin *p)
{
    if (displayPrivateIndex >= 0)
	freeDisplayPrivateIndex (displayPrivateIndex);
}

static CompPluginVTable fadeVTable = {
    "fade",
    "Fading Windows",
    "Fade in windows when mapped and fade out windows when unmapped",
    fadeInit,
    fadeFini,
    fadeInitDisplay,
    fadeFiniDisplay,
    fadeInitScreen,
    fadeFiniScreen,
    fadeInitWindow,
    fadeFiniWindow,
    0, /* GetDisplayOptions */
    0, /* SetDisplayOption */
    fadeGetScreenOptions,
    fadeSetScreenOption
};

CompPluginVTable *
getCompPluginInfo (void)
{
    return &fadeVTable;
}
