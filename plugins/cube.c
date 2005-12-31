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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>

#ifdef USE_LIBSVG_CAIRO
#include <cairo-xlib.h>
#include <svg-cairo.h>
#endif

#include <comp.h>

#define CUBE_COLOR_RED_DEFAULT   0xffff
#define CUBE_COLOR_GREEN_DEFAULT 0xffff
#define CUBE_COLOR_BLUE_DEFAULT  0xffff

#define CUBE_NEXT_KEY_DEFAULT       "space"
#define CUBE_NEXT_MODIFIERS_DEFAULT CompPressMask

#define CUBE_PREV_KEY_DEFAULT       "BackSpace"
#define CUBE_PREV_MODIFIERS_DEFAULT CompPressMask

static int displayPrivateIndex;

typedef struct _CubeDisplay {
    int		    screenPrivateIndex;
    HandleEventProc handleEvent;
} CubeDisplay;

#define CUBE_SCREEN_OPTION_COLOR 0
#define CUBE_SCREEN_OPTION_SVGS  1
#define CUBE_SCREEN_OPTION_NEXT  2
#define CUBE_SCREEN_OPTION_PREV  3
#define CUBE_SCREEN_OPTION_NUM   4

typedef struct _CubeScreen {
    PaintTransformedScreenProc paintTransformedScreen;
    PaintBackgroundProc	       paintBackground;

    CompOption opt[CUBE_SCREEN_OPTION_NUM];

    int      xrotations;
    Bool     paintTopBottom;
    GLushort color[3];
    GLfloat  tc[8];

    Pixmap	    pixmap;
    CompTexture     texture;

#ifdef USE_LIBSVG_CAIRO
    cairo_t	    *cr;
    svg_cairo_t	    *svgc;
    int		    svgNFile;
    int		    svgCurFile;
    CompOptionValue *svgFiles;
#endif

} CubeScreen;

#define GET_CUBE_DISPLAY(d)				     \
    ((CubeDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define CUBE_DISPLAY(d)			   \
    CubeDisplay *cd = GET_CUBE_DISPLAY (d)

#define GET_CUBE_SCREEN(s, cd)					 \
    ((CubeScreen *) (s)->privates[(cd)->screenPrivateIndex].ptr)

#define CUBE_SCREEN(s)							\
    CubeScreen *cs = GET_CUBE_SCREEN (s, GET_CUBE_DISPLAY (s->display))

#define NUM_OPTIONS(s) (sizeof ((s)->opt) / sizeof (CompOption))

#ifdef USE_LIBSVG_CAIRO
static void
cubeInitSvg (CompScreen *s)

{
    CUBE_SCREEN (s);

    cs->pixmap = None;
    cs->cr = 0;
    cs->svgc = 0;
}

static void
cubeFiniSvg (CompScreen *s)

{
    CUBE_SCREEN (s);

    if (cs->svgc)
	svg_cairo_destroy (cs->svgc);

    if (cs->cr)
	cairo_destroy (cs->cr);

    if (cs->pixmap)
	XFreePixmap (s->display->display, cs->pixmap);
}

static void
cubeLoadSvg (CompScreen *s,
	     int	n)
{
    int width, height;

    CUBE_SCREEN (s);

    if (!cs->svgNFile)
    {
	finiTexture (s, &cs->texture);
	initTexture (s, &cs->texture);
	cubeFiniSvg (s);
	cubeInitSvg (s);
	return;
    }

    if (!cs->pixmap)
    {
	cairo_surface_t *surface;
	Visual		*visual;
	int		depth;

	depth = DefaultDepth (s->display->display, s->screenNum);
	cs->pixmap = XCreatePixmap (s->display->display, s->root,
				    s->width, s->height,
				    depth);

	if (!bindPixmapToTexture (s, &cs->texture, cs->pixmap,
				  s->width, s->height, depth))
	{
	    fprintf (stderr, "%s: Couldn't bind slide pixmap 0x%x to "
		     "texture\n", programName, (int) cs->pixmap);
	}

	switch (cs->texture.target) {
	case GL_TEXTURE_RECTANGLE_ARB:
	    cs->tc[2] = cs->tc[4] = s->width;
	    cs->tc[5] = cs->tc[7] = s->height;
	    break;
	case GL_TEXTURE_2D:
	default:
	    cs->tc[2] = cs->tc[4] = 1.0f;
	    cs->tc[5] = cs->tc[7] = 1.0f;
	    break;
	}

	visual = DefaultVisual (s->display->display, s->screenNum);
	surface = cairo_xlib_surface_create (s->display->display,
					     cs->pixmap, visual,
					     s->width, s->height);
	cs->cr = cairo_create (surface);
	cairo_surface_destroy (surface);
    }

    cs->svgCurFile = n % cs->svgNFile;

    if (cs->svgc)
	svg_cairo_destroy (cs->svgc);

    if (svg_cairo_create (&cs->svgc))
    {
	fprintf (stderr, "%s: Failed to create svg_cairo_t.\n",
		 programName);
	return;
    }

    svg_cairo_set_viewport_dimension (cs->svgc, s->width, s->height);

    if (svg_cairo_parse (cs->svgc, cs->svgFiles[cs->svgCurFile].s))
    {
	fprintf (stderr, "%s: Failed to load svg: %s.\n",
		 programName, cs->svgFiles[cs->svgCurFile].s);
	return;
    }

    svg_cairo_get_size (cs->svgc, &width, &height);

    cairo_save (cs->cr);
    cairo_set_source_rgb (cs->cr,
			  (double) cs->color[0] / 0xffff,
			  (double) cs->color[1] / 0xffff,
			  (double) cs->color[2] / 0xffff);
    cairo_rectangle (cs->cr, 0, 0, s->width, s->height);
    cairo_fill (cs->cr);

    cairo_scale (cs->cr,
		 (double) s->width / width,
		 (double) s->height / height);

    svg_cairo_render (cs->svgc, cs->cr);
    cairo_restore (cs->cr);
}
#endif

static CompOption *
cubeGetScreenOptions (CompScreen *screen,
		      int	 *count)
{
    CUBE_SCREEN (screen);

    *count = NUM_OPTIONS (cs);
    return cs->opt;
}

static Bool
cubeSetScreenOption (CompScreen      *screen,
		     char	     *name,
		     CompOptionValue *value)
{
    CompOption *o;
    int	       index;

    CUBE_SCREEN (screen);

    o = compFindOption (cs->opt, NUM_OPTIONS (cs), name, &index);
    if (!o)
	return FALSE;

    switch (index) {
    case CUBE_SCREEN_OPTION_COLOR:
	if (compSetColorOption (o, value))
	{
	    memcpy (cs->color, o->value.c, sizeof (cs->color));
	    damageScreen (screen);
	    return TRUE;
	}
	break;
    case CUBE_SCREEN_OPTION_SVGS:
	if (compSetOptionList (o, value))
	{

#ifdef USE_LIBSVG_CAIRO
	    cs->svgFiles = cs->opt[CUBE_SCREEN_OPTION_SVGS].value.list.value;
	    cs->svgNFile = cs->opt[CUBE_SCREEN_OPTION_SVGS].value.list.nValue;

	    cubeLoadSvg (screen, cs->svgCurFile);
	    damageScreen (screen);
#endif

	    return TRUE;
	}
	break;
    case CUBE_SCREEN_OPTION_NEXT:
    case CUBE_SCREEN_OPTION_PREV:
	if (compSetBindingOption (o, value))
	    return TRUE;
    default:
	break;
    }

    return FALSE;
}

static void
cubeScreenInitOptions (CubeScreen *cs,
		       Display    *display)
{
    CompOption *o;

    o = &cs->opt[CUBE_SCREEN_OPTION_COLOR];
    o->name	  = "color";
    o->shortDesc  = "Cube Color";
    o->longDesc	  = "Color of top and bottom sides of the cube";
    o->type	  = CompOptionTypeColor;
    o->value.c[0] = CUBE_COLOR_RED_DEFAULT;
    o->value.c[1] = CUBE_COLOR_GREEN_DEFAULT;
    o->value.c[2] = CUBE_COLOR_BLUE_DEFAULT;
    o->value.c[3] = 0xffff;

    o = &cs->opt[CUBE_SCREEN_OPTION_SVGS];
    o->name	         = "svgs";
    o->shortDesc         = "SVG files";
    o->longDesc	         = "List of SVG files rendered on top face of cube";
    o->type	         = CompOptionTypeList;
    o->value.list.type   = CompOptionTypeString;
    o->value.list.nValue = 0;
    o->value.list.value  = 0;
    o->rest.s.string     = 0;
    o->rest.s.nString    = 0;

    o = &cs->opt[CUBE_SCREEN_OPTION_NEXT];
    o->name			  = "next_slide";
    o->shortDesc		  = "Next Slide";
    o->longDesc			  = "Adavence to next slide";
    o->type			  = CompOptionTypeBinding;
    o->value.bind.type		  = CompBindingTypeKey;
    o->value.bind.u.key.modifiers = CUBE_NEXT_MODIFIERS_DEFAULT;
    o->value.bind.u.key.keycode   =
	XKeysymToKeycode (display, XStringToKeysym (CUBE_NEXT_KEY_DEFAULT));

    o = &cs->opt[CUBE_SCREEN_OPTION_PREV];
    o->name			  = "prev_slide";
    o->shortDesc		  = "Previous Slide";
    o->longDesc			  = "Go back to previous slide";
    o->type			  = CompOptionTypeBinding;
    o->value.bind.type		  = CompBindingTypeKey;
    o->value.bind.u.key.modifiers = CUBE_PREV_MODIFIERS_DEFAULT;
    o->value.bind.u.key.keycode   =
	XKeysymToKeycode (display, XStringToKeysym (CUBE_PREV_KEY_DEFAULT));
}

static void
cubeTranslateWindows (CompScreen *s,
		      int	 tx)
{
    CompWindow *w;

    if (tx == 0)
	return;

    for (w = s->windows; w; w = w->next)
    {
	if (w->attrib.map_state != IsViewable)
	    continue;

	if (w->type == s->display->winDesktopAtom ||
	    w->type == s->display->winDockAtom)
	    continue;

	(*s->invisibleWindowMove) (w, tx, 0);
    }
}

static void
cubePaintTransformedScreen (CompScreen		    *s,
			    const ScreenPaintAttrib *sAttrib,
			    const WindowPaintAttrib *wAttrib,
			    unsigned int	    mask)
{
    ScreenPaintAttrib sa = defaultScreenPaintAttrib;
    int		      xMove = 0;

    CUBE_SCREEN (s);

    sa.xTranslate = sAttrib->xTranslate;
    sa.yTranslate = sAttrib->yTranslate;
    sa.zTranslate = sAttrib->zTranslate;

    if (sAttrib->xRotate > 0.0f)
    {
	cs->xrotations = (int) sAttrib->xRotate / 90;
	sa.xRotate = sAttrib->xRotate - (cs->xrotations * 90.0f);
    }
    else
    {
	cs->xrotations = (int) sAttrib->xRotate / 90;
	sa.xRotate = (sAttrib->xRotate - cs->xrotations * 90.0f) + 90.0f;
	cs->xrotations--;
    }

    if (sa.vRotate > 100.0f)
	sa.vRotate = 100.0f;
    else if (sAttrib->vRotate < -100.0f)
	sa.vRotate = -100.0f;
    else
	sa.vRotate = sAttrib->vRotate;

    if (mask & PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS_MASK)
	glClear (GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    else
	glClear (GL_COLOR_BUFFER_BIT);

    UNWRAP (cs, s, paintTransformedScreen);

    cs->paintTopBottom = TRUE;

    if (sAttrib->xRotate != 0.0f)
    {
	xMove = cs->xrotations * s->width;
	cubeTranslateWindows (s, xMove);

	(*s->paintTransformedScreen) (s, &sa, wAttrib, mask);

	xMove += s->width;
	cubeTranslateWindows (s, s->width);

	cs->paintTopBottom = FALSE;
    }

    sa.yRotate -= 90.0f;

    (*s->paintTransformedScreen) (s, &sa, wAttrib, mask);

    cubeTranslateWindows (s, -xMove);

    WRAP (cs, s, paintTransformedScreen, cubePaintTransformedScreen);
}

static void
cubePaintBackground (CompScreen   *s,
		     Region	  region,
		     unsigned int mask)
{
    GLint stencilRef = s->stencilRef;

    CUBE_SCREEN (s);

    if (cs->paintTopBottom)
    {
	int     first, count, rot;
	GLfloat data[] = {
	    /* top */
	    0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
	    0.0f, 0.0f, s->width, 0.0f, 0.0f,

	    0.0f, 0.0f, s->width, 0.0f, -1.0f,
	    0.0f, 0.0f, 0.0f, 0.0f, -1.0f,

	    /* bottom */
	    0.0f, 0.0f, 0.0f, s->height, -1.0f,
	    0.0f, 0.0f, s->width, s->height, -1.0f,

	    0.0f, 0.0f, s->width, s->height, 0.0f,
	    0.0f, 0.0f, 0.0f, s->height, 0.0f
	};

	rot = 2 * cs->xrotations;

#define MOD(a,b) ((a) < 0 ? ((b) - ((-(a) - 1) % (b))) - 1 : (a) % (b))

	data[0] = cs->tc[MOD (0 - rot, 8)];
	data[1] = cs->tc[MOD (1 - rot, 8)];
	data[5] = cs->tc[MOD (2 - rot, 8)];
	data[6] = cs->tc[MOD (3 - rot, 8)];

	data[10] = cs->tc[MOD (4 - rot, 8)];
	data[11] = cs->tc[MOD (5 - rot, 8)];
	data[15] = cs->tc[MOD (6 - rot, 8)];
	data[16] = cs->tc[MOD (7 - rot, 8)];

#undef MOD

	first = 0;

	glVertexPointer (3, GL_FLOAT, sizeof (GLfloat) * 5, data + 2);
	glTexCoordPointer (2, GL_FLOAT, sizeof (GLfloat) * 5, data);

	if (cs->texture.name)
	{
	    if (mask & PAINT_BACKGROUND_ON_TRANSFORMED_SCREEN_MASK)
		enableTexture (s, &cs->texture, COMP_TEXTURE_FILTER_GOOD);
	    else
		enableTexture (s, &cs->texture, COMP_TEXTURE_FILTER_FAST);

	    glDrawArrays (GL_QUADS, first, 4);

	    disableTexture (&cs->texture);

	    first += 4;
	    count = 4;
	}
	else
	    count = 8;

	glColor3usv (cs->color);
	glDrawArrays (GL_QUADS, first, count);
	glColor4usv (defaultColor);
    }
    else
	s->stencilRef++;

    UNWRAP (cs, s, paintBackground);
    (*s->paintBackground) (s, region, mask);
    WRAP (cs, s, paintBackground, cubePaintBackground);

    s->stencilRef = stencilRef;
}

#ifdef USE_LIBSVG_CAIRO
static void
cubeHandleEvent (CompDisplay *d,
		 XEvent      *event)
{
    CompScreen *s;

    CUBE_DISPLAY (d);

    switch (event->type) {
    case KeyPress:
    case KeyRelease:
	s = findScreenAtDisplay (d, event->xkey.root);
	if (s)
	{
	    CUBE_SCREEN (s);

	    if (EV_KEY (&cs->opt[CUBE_SCREEN_OPTION_NEXT], event))
	    {
		cubeLoadSvg (s, (cs->svgCurFile + 1) % cs->svgNFile);
		damageScreen (s);
	    }

	    if (EV_KEY (&cs->opt[CUBE_SCREEN_OPTION_PREV], event))
	    {
		cubeLoadSvg (s, (cs->svgCurFile - 1 + cs->svgNFile) %
			     cs->svgNFile);
		damageScreen (s);
	    }
	}
	break;
    case ButtonPress:
    case ButtonRelease:
	s = findScreenAtDisplay (d, event->xbutton.root);
	if (s)
	{
	    CUBE_SCREEN (s);

	    if (EV_BUTTON (&cs->opt[CUBE_SCREEN_OPTION_NEXT], event))
	    {
		cubeLoadSvg (s, (cs->svgCurFile + 1) % cs->svgNFile);
		damageScreen (s);
	    }

	    if (EV_BUTTON (&cs->opt[CUBE_SCREEN_OPTION_PREV], event))
	    {
		cubeLoadSvg (s, (cs->svgCurFile - 1 + cs->svgNFile) %
			     cs->svgNFile);
		damageScreen (s);
	    }
	}
    default:
	break;
    }

    UNWRAP (cd, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (cd, d, handleEvent, cubeHandleEvent);
}
#endif

static Bool
cubeInitDisplay (CompPlugin  *p,
		 CompDisplay *d)
{
    CubeDisplay *cd;

    cd = malloc (sizeof (CubeDisplay));
    if (!cd)
	return FALSE;

    cd->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (cd->screenPrivateIndex < 0)
    {
	free (cd);
	return FALSE;
    }

#ifdef USE_LIBSVG_CAIRO
    WRAP (cd, d, handleEvent, cubeHandleEvent);
#endif

    d->privates[displayPrivateIndex].ptr = cd;

    return TRUE;
}

static void
cubeFiniDisplay (CompPlugin  *p,
		 CompDisplay *d)
{
    CUBE_DISPLAY (d);

    freeScreenPrivateIndex (d, cd->screenPrivateIndex);

#ifdef USE_LIBSVG_CAIRO
    UNWRAP (cd, d, handleEvent);
#endif

    free (cd);
}

static Bool
cubeInitScreen (CompPlugin *p,
		CompScreen *s)
{
    CubeScreen *cs;

    CUBE_DISPLAY (s->display);

    cs = malloc (sizeof (CubeScreen));
    if (!cs)
	return FALSE;

    cs->tc[0] = cs->tc[1] = cs->tc[2] = cs->tc[3] = 0.0f;
    cs->tc[4] = cs->tc[5] = cs->tc[6] = cs->tc[7] = 0.0f;

    cs->color[0] = CUBE_COLOR_RED_DEFAULT;
    cs->color[1] = CUBE_COLOR_GREEN_DEFAULT;
    cs->color[2] = CUBE_COLOR_BLUE_DEFAULT;

    s->privates[cd->screenPrivateIndex].ptr = cs;

    cs->paintTopBottom = FALSE;

    initTexture (s, &cs->texture);

#ifdef USE_LIBSVG_CAIRO
    cubeInitSvg (s);

    cs->svgFiles   = 0;
    cs->svgNFile   = 0;
    cs->svgCurFile = 0;
#endif

    cubeScreenInitOptions (cs, s->display->display);

    WRAP (cs, s, paintTransformedScreen, cubePaintTransformedScreen);
    WRAP (cs, s, paintBackground, cubePaintBackground);

    return TRUE;
}

static void
cubeFiniScreen (CompPlugin *p,
		CompScreen *s)
{
    CUBE_SCREEN (s);

    UNWRAP (cs, s, paintTransformedScreen);
    UNWRAP (cs, s, paintBackground);

    finiTexture (s, &cs->texture);

#ifdef USE_LIBSVG_CAIRO
    cubeFiniSvg (s);
#endif

    free (cs);
}

static Bool
cubeInit (CompPlugin *p)
{
    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
	return FALSE;

    return TRUE;
}

static void
cubeFini (CompPlugin *p)
{
    if (displayPrivateIndex >= 0)
	freeDisplayPrivateIndex (displayPrivateIndex);
}

CompPluginVTable cubeVTable = {
    "cube",
    "Desktop Cube",
    "Place windows on cube",
    cubeInit,
    cubeFini,
    cubeInitDisplay,
    cubeFiniDisplay,
    cubeInitScreen,
    cubeFiniScreen,
    0, /* InitWindow */
    0, /* FiniWindow */
    0, /* GetDisplayOptions */
    0, /* SetDisplayOption */
    cubeGetScreenOptions,
    cubeSetScreenOption
};

CompPluginVTable *
getCompPluginInfo (void)
{
    return &cubeVTable;
}
