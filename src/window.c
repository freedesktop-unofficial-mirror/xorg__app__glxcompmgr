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

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xcomposite.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <comp.h>

static int
reallocWindowPrivates (int  size,
		       void *closure)
{
    CompScreen *s = (CompScreen *) closure;
    CompWindow *w;
    void       *privates;

    for (w = s->windows; w; w = w->next)
    {
	privates = realloc (w->privates, size * sizeof (CompPrivate));
	if (!privates)
	    return FALSE;

	w->privates = (CompPrivate *) privates;
    }

    return TRUE;
}

int
allocateWindowPrivateIndex (CompScreen *screen)
{
    return allocatePrivateIndex (&screen->windowPrivateLen,
				 &screen->windowPrivateIndices,
				 reallocWindowPrivates,
				 (void *) screen);
}

void
freeWindowPrivateIndex (CompScreen *screen,
			int	   index)
{
    freePrivateIndex (screen->windowPrivateLen,
		      screen->windowPrivateIndices,
		      index);
}

static Window
tryChildren (CompDisplay *display,
	     Window      win)
{
    Window	  root, parent;
    Window	  *children;
    unsigned int  nchildren;
    unsigned int  i;
    Atom	  type = None;
    int		  format;
    unsigned long nitems, after;
    unsigned char *data;
    Window	  inf = 0;

    if (!XQueryTree (display->display, win, &root, &parent,
		     &children, &nchildren))
	return 0;

    for (i = 0; !inf && (i < nchildren); i++)
    {
	data = NULL;

	XGetWindowProperty (display->display, children[i],
			    display->wmStateAtom, 0, 0,
			    False, AnyPropertyType, &type, &format, &nitems,
			    &after, &data);
	if (data)
	    XFree (data);

	if (type)
	    inf = children[i];
    }

    for (i = 0; !inf && (i < nchildren); i++)
	inf = tryChildren (display, children[i]);

    if (children)
	XFree (children);

    return inf;
}

static Window
clientWindow (CompDisplay *display,
	      Window      win)
{
    Atom	  type = None;
    int		  format;
    unsigned long nitems, after;
    unsigned char *data = NULL;
    Window	  inf;

    XGetWindowProperty (display->display, win, display->wmStateAtom, 0, 0,
			False, AnyPropertyType, &type, &format, &nitems,
			&after, &data);

    if (data)
	XFree (data);

    if (type)
	return win;

    inf = tryChildren (display, win);
    if (!inf)
	inf = win;

    return inf;
}

Window
getActiveWindow (CompDisplay *display,
		 Window      root)
{
    Atom	  actual;
    int		  result, format;
    unsigned long n, left;
    unsigned char *data;

    result = XGetWindowProperty (display->display, root,
				 display->winActiveAtom, 0L, 1L, False,
				 XA_WINDOW, &actual, &format,
				 &n, &left, &data);

    if (result == Success && n && data)
    {
	Window w;

	memcpy (&w, data, sizeof (Window));
	XFree ((void *) data);

	return w;
    }

    return None;
}

Atom
getWindowType (CompDisplay *display,
	       Window      id)
{
    Atom	  actual;
    int		  result, format;
    unsigned long n, left;
    unsigned char *data;

    result = XGetWindowProperty (display->display, id, display->winTypeAtom,
				 0L, 1L, FALSE, XA_ATOM, &actual, &format,
				 &n, &left, &data);

    if (result == Success && n && data)
    {
	Atom a;

	memcpy (&a, data, sizeof (Atom));
	XFree ((void *) data);

	return a;
    }

    return display->winNormalAtom;
}

unsigned short
getWindowOpacity (CompDisplay *display,
		  Window      id)
{
    Atom	  actual;
    int		  result, format;
    unsigned long n, left;
    unsigned char *data;

    result = XGetWindowProperty (display->display, id, display->winOpacityAtom,
				 0L, 1L, FALSE, XA_CARDINAL, &actual, &format,
				 &n, &left, &data);

    if (result == Success && n && data)
    {
	unsigned int o;

	memcpy (&o, data, sizeof (Atom));
	XFree ((void *) data);

	return (o / 0xffff);
    }

    return MAXSHORT;
}

static void
setWindowMatrix (CompWindow *w)
{
    w->matrix = w->texture.matrix;
    w->matrix.x0 -= (w->attrib.x * w->matrix.xx);
    w->matrix.y0 -= (w->attrib.y * w->matrix.yy);
}

void
bindWindow (CompWindow *w)
{
    if (testMode)
    {
	unsigned int width, height;

	if (readImageToTexture (w->screen, &w->texture,
				windowImage, &width, &height))
	{
	    XResizeWindow (w->screen->display->display, w->id, width, height);

	    w->width  = width;
	    w->height = height;
	}

	w->pixmap = 1;
    }
    else
    {
	w->pixmap = XCompositeNameWindowPixmap (w->screen->display->display,
						w->id);
	if (!w->pixmap)
	{
	    fprintf (stderr, "%s: XCompositeNameWindowPixmap failed\n",
		     programName);
	    return;
	}

	if (!bindPixmapToTexture (w->screen, &w->texture, w->pixmap,
				  w->width, w->height,
				  w->attrib.depth))
	{
	    fprintf (stderr, "%s: Couldn't bind redirected window 0x%x to "
		     "texture\n", programName, (int) w->id);
	}
    }

    setWindowMatrix (w);
}

void
releaseWindow (CompWindow *w)
{
    if (w->pixmap)
    {
	releasePixmapFromTexture (w->screen, &w->texture);

	if (!testMode)
	    XFreePixmap (w->screen->display->display, w->pixmap);

	w->pixmap = None;
    }
}

static void
freeWindow (CompWindow *w)
{
    releaseWindow (w);

    if (w->texture.name)
	finiTexture (w->screen, &w->texture);

    if (w->clip)
	XDestroyRegion (w->clip);

    if (w->region)
	XDestroyRegion (w->region);

    if (w->privates)
	free (w->privates);

    if (w->vertices)
	free (w->vertices);

    if (w->indices)
	free (w->indices);

    if (lastFoundWindow == w)
	lastFoundWindow = 0;

    if (lastDamagedWindow == w)
	lastDamagedWindow = 0;

    free (w);
}

Bool
damageWindowRegion (CompWindow *w,
		    Region     region)
{
    return FALSE;
}

Bool
damageWindowRect (CompWindow *w,
		  Bool       initial,
		  BoxPtr     rect)
{
    return FALSE;
}

void
addWindowDamage (CompWindow *w)
{
    if (w->screen->allDamaged)
	return;

    if (w->attrib.map_state == IsViewable && w->damaged)
    {
	if (!(*w->screen->damageWindowRegion) (w, w->region))
	    damageScreenRegion (w->screen, w->region);
    }
}

void
updateWindowRegion (CompWindow *w)
{
    REGION     rect;
    XRectangle r, *rects, *shapeRects = 0;
    int	       i, n = 0;

    EMPTY_REGION (w->region);

    if (w->screen->display->shapeExtension)
    {
	int order;

	shapeRects = XShapeGetRectangles (w->screen->display->display, w->id,
					  ShapeBounding, &n, &order);
    }

    if (n < 2)
    {
	r.x      = 0;
	r.y      = 0;
	r.width  = w->width;
	r.height = w->height;

	rects = &r;
	n = 1;
    }
    else
    {
	rects = shapeRects;
    }

    rect.rects = &rect.extents;
    rect.numRects = rect.size = 1;

    for (i = 0; i < n; i++)
    {
	rect.extents.x1 = rects[i].x + w->attrib.x;
	rect.extents.y1 = rects[i].y + w->attrib.y;
	rect.extents.x2 = rect.extents.x1 + rects[i].width;
	rect.extents.y2 = rect.extents.y1 + rects[i].height;

	XUnionRegion (&rect, w->region, w->region);
    }

    if (shapeRects)
	XFree (shapeRects);
}

void
addWindow (CompScreen *screen,
	   Window     id,
	   Window     aboveId)
{
    CompWindow *w;

    w = (CompWindow *) malloc (sizeof (CompWindow));
    if (!w)
	return;

    w->next = NULL;
    w->prev = NULL;

    w->screen       = screen;
    w->texture.name = 0;
    w->pixmap       = None;
    w->destroyed    = FALSE;
    w->damaged      = FALSE;

    w->vertices   = 0;
    w->vertexSize = 0;
    w->indices    = 0;
    w->indexSize  = 0;
    w->vCount     = 0;

    if (screen->windowPrivateLen)
    {
	w->privates = malloc (screen->windowPrivateLen * sizeof (CompPrivate));
	if (!w->privates)
	{
	    free (w);
	    return;
	}
    }
    else
	w->privates = 0;

    w->region = XCreateRegion ();
    if (!w->region)
    {
	freeWindow (w);
	return;
    }

    w->clip = XCreateRegion ();
    if (!w->clip)
    {
	freeWindow (w);
	return;
    }

    if (!XGetWindowAttributes (screen->display->display, id, &w->attrib))
    {
	freeWindow (w);
	return;
    }

    XSelectInput (screen->display->display, id, PropertyChangeMask);

    w->id      = id;
    w->client  = clientWindow (screen->display, id);
    w->alpha   = (w->attrib.depth == 32);
    w->opacity = OPAQUE;
    w->type    = getWindowType (screen->display, w->client);

    w->width  = w->attrib.width + w->attrib.border_width * 2;
    w->height = w->attrib.height + w->attrib.border_width * 2;

    if (screen->display->shapeExtension)
	XShapeSelectInput (screen->display->display, id, ShapeNotifyMask);

    updateWindowRegion (w);

    insertWindowIntoScreen (screen, w, aboveId);

    if (w->attrib.class != InputOnly)
    {
	initTexture (screen, &w->texture);

	w->damage = XDamageCreate (screen->display->display, id,
				   XDamageReportRawRectangles);
    }
    else
    {
	w->damage = None;
	w->attrib.map_state = IsUnmapped;
    }

    if (testMode)
    {
	w->attrib.map_state = IsViewable;
	bindWindow (w);
    }

    w->invisible = TRUE;

    if (w->type == w->screen->display->winDesktopAtom)
	w->screen->desktopWindowCount++;

    windowInitPlugins (w);
}

void
removeWindow (CompWindow *w)
{
    if (w->attrib.map_state == IsViewable)
    {
	if (w->type == w->screen->display->winDesktopAtom)
	    w->screen->desktopWindowCount++;
    }

    unhookWindowFromScreen (w->screen, w);
    windowFiniPlugins (w);
    freeWindow (w);
}

void
destroyWindow (CompWindow *w)
{
    if (!w->destroyed)
    {
	w->destroyed = TRUE;
	w->screen->pendingDestroys++;
    }
}

void
mapWindow (CompWindow *w)
{
    if (w->attrib.class == InputOnly)
	return;

    if (w->attrib.map_state == IsViewable)
	return;

    if (w->type == w->screen->display->winDesktopAtom)
	w->screen->desktopWindowCount++;

    w->attrib.map_state = IsViewable;
    w->invisible = TRUE;
    w->damaged = FALSE;
}

void
unmapWindow (CompWindow *w)
{
    if (w->attrib.map_state != IsViewable)
	return;

    if (w->type == w->screen->display->winDesktopAtom)
	w->screen->desktopWindowCount--;

    addWindowDamage (w);

    w->attrib.map_state = IsUnmapped;
    w->invisible = TRUE;

    releaseWindow (w);
}

static int
restackWindow (CompWindow *w,
	       Window     aboveId)
{
    if (w->prev)
    {
	if (aboveId == w->prev->id)
	    return 0;
    }
    else if (aboveId == None)
	return 0;

    unhookWindowFromScreen (w->screen, w);
    insertWindowIntoScreen (w->screen, w, aboveId);

    return 1;
}

void
configureWindow (CompWindow	 *w,
		 XConfigureEvent *ce)
{
    Bool damage;

    if (w->attrib.width        != ce->width  ||
	w->attrib.height       != ce->height ||
	w->attrib.border_width != ce->border_width)
    {
	addWindowDamage (w);

	w->attrib.x	       = ce->x;
	w->attrib.y	       = ce->y;
	w->attrib.width        = ce->width;
	w->attrib.height       = ce->height;
	w->attrib.border_width = ce->border_width;

	w->width  = w->attrib.width  + w->attrib.border_width * 2;
	w->height = w->attrib.height + w->attrib.border_width * 2;

	releaseWindow (w);

	EMPTY_REGION (w->region);

	damage = TRUE;
    }
    else if (w->attrib.x != ce->x || w->attrib.y != ce->y)
    {
	addWindowDamage (w);

	XOffsetRegion (w->region, ce->x - w->attrib.x, ce->y - w->attrib.y);

	w->attrib.x = ce->x;
	w->attrib.y = ce->y;

	setWindowMatrix (w);

	damage = TRUE;
    }
    else
	damage = FALSE;

    w->attrib.override_redirect = ce->override_redirect;

    w->invisible = WINDOW_INVISIBLE (w);

    if (restackWindow (w, ce->above) || damage)
    {
	if (!REGION_NOT_EMPTY (w->region))
	    updateWindowRegion (w);

	addWindowDamage (w);
    }
}

void
circulateWindow (CompWindow	 *w,
		 XCirculateEvent *ce)
{
    Window newAboveId;

    if (ce->place == PlaceOnTop && w->screen->windows)
	newAboveId = w->screen->windows->id;
    else
	newAboveId = 0;

    if (restackWindow (w, newAboveId))
	addWindowDamage (w);
}

void
invisibleWindowMove (CompWindow *w,
		     int        dx,
		     int        dy)
{
    w->attrib.x += dx;
    w->attrib.y += dy;

    XOffsetRegion (w->region, dx, dy);

    setWindowMatrix (w);
}
