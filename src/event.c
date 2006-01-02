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

#include <X11/Xlib.h>
#include <X11/extensions/shape.h>

#include <comp.h>

void
handleEvent (CompDisplay *display,
	     XEvent      *event)
{
    CompScreen *s;
    CompWindow *w;

    switch (event->type) {
    case Expose:
	s = findScreenAtDisplay (display, event->xexpose.window);
	if (s)
	{
	    int more = event->xexpose.count + 1;

	    if (s->nExpose == s->sizeExpose)
	    {
		if (s->exposeRects)
		{
		    s->exposeRects = realloc (s->exposeRects,
					      (s->sizeExpose + more) *
					      sizeof (XRectangle));
		    s->sizeExpose += more;
		}
		else
		{
		    s->exposeRects = malloc (more * sizeof (XRectangle));
		    s->sizeExpose = more;
		}
	    }

	    s->exposeRects[s->nExpose].x      = event->xexpose.x;
	    s->exposeRects[s->nExpose].y      = event->xexpose.y;
	    s->exposeRects[s->nExpose].width  = event->xexpose.width;
	    s->exposeRects[s->nExpose].height = event->xexpose.height;
	    s->nExpose++;

	    if (event->xexpose.count == 0)
	    {
		REGION rect;

		rect.rects = &rect.extents;
		rect.numRects = rect.size = 1;

		while (s->nExpose--)
		{
		    rect.extents.x1 = s->exposeRects[s->nExpose].x;
		    rect.extents.y1 = s->exposeRects[s->nExpose].y;
		    rect.extents.x2 = rect.extents.x1 +
			s->exposeRects[s->nExpose].width;
		    rect.extents.y2 = rect.extents.y1 +
			s->exposeRects[s->nExpose].height;

		    damageScreenRegion (s, &rect);
		}
		s->nExpose = 0;
	    }
	}
	break;
    case ConfigureNotify:
	w = findWindowAtDisplay (display, event->xconfigure.window);
	if (w)
	{
	    configureWindow (w, &event->xconfigure);
	}
	else
	{
	    s = findScreenAtDisplay (display, event->xconfigure.window);
	    if (s)
		configureScreen (s, &event->xconfigure);
	}
	break;
    case CreateNotify:
	s = findScreenAtDisplay (display, event->xcreatewindow.parent);
	if (s)
	    addWindow (s, event->xcreatewindow.window, 0);
	break;
    case DestroyNotify:
	w = findWindowAtDisplay (display, event->xdestroywindow.window);
	if (w)
	{
	    addWindowDamage (w);
	    removeWindow (w);
	}
	break;
    case MapNotify:
	w = findWindowAtDisplay (display, event->xmap.window);
	if (w)
	    mapWindow (w);
	break;
    case UnmapNotify:
	w = findWindowAtDisplay (display, event->xunmap.window);
	if (w)
	    unmapWindow (w);
	break;
    case ReparentNotify:
	s = findScreenAtDisplay (display, event->xreparent.parent);
	if (s)
	{
	    addWindow (s, event->xreparent.window, 0);
	}
	else
	{
	    w = findWindowAtDisplay (display, event->xreparent.window);
	    if (w)
	    {
		addWindowDamage (w);
		removeWindow (w);
	    }
	}
	break;
    case CirculateNotify:
	w = findWindowAtDisplay (display, event->xcirculate.window);
	if (w)
	    circulateWindow (w, &event->xcirculate);
	break;
    case ButtonPress:
    case ButtonRelease:
    case KeyPress:
    case KeyRelease:
	break;
    case PropertyNotify:
	if (event->xproperty.atom == display->winActiveAtom)
	{
	    s = findScreenAtDisplay (display, event->xproperty.window);
	    if (s)
		s->activeWindow = getActiveWindow (display, s->root);
	}
	else if (event->xproperty.atom == display->winTypeAtom)
	{
	    w = findWindowAtDisplay (display, event->xproperty.window);
	    if (w)
	    {
		Atom type;

		type = getWindowType (display, w->client);
		if (type != w->type)
		{

		    if (w->attrib.map_state == IsViewable)
		    {
			if (w->type == display->winDesktopAtom)
			    w->screen->desktopWindowCount--;
			else if (type == display->winDesktopAtom)
			    w->screen->desktopWindowCount++;

			addWindowDamage (w);
		    }
		    w->type = type;
		}
	    }
	}
	else if (event->xproperty.atom == display->winOpacityAtom)
	{
	    w = findWindowAtDisplay (display, event->xproperty.window);
	    if (w)
	    {
		GLuint opacity;

		opacity = getWindowOpacity (display, w->id);
		if (opacity != w->opacity)
		{
		    w->opacity = opacity;
		    if (w->attrib.map_state == IsViewable)
			addWindowDamage (w);
		}
	    }
	}
	else if (event->xproperty.atom == display->xBackgroundAtom[0] ||
		 event->xproperty.atom == display->xBackgroundAtom[1])
	{
	    s = findScreenAtDisplay (display, event->xproperty.window);
	    if (s)
	    {
		finiTexture (s, &s->backgroundTexture);
		initTexture (s, &s->backgroundTexture);

		damageScreen (s);
	    }
	}
	break;
    case MotionNotify:
	break;
    case ClientMessage:
	if (event->xclient.format    == 32 &&
	    event->xclient.data.l[0] == display->wmDeleteWindowAtom)
	    exit (0);
	break;
    case MappingNotify:
	updateModifierMappings (display);
	break;
    default:
	if (display->shapeExtension &&
	    event->type == display->shapeEvent + ShapeNotify)
	{
	    w = findWindowAtDisplay (display, ((XShapeEvent *) event)->window);
	    if (w)
		updateWindowRegion (w);
	}
	else if (event->type == display->damageEvent + XDamageNotify)
	{
	    XDamageNotifyEvent *de = (XDamageNotifyEvent *) event;

	    if (lastDamagedWindow && de->drawable == lastDamagedWindow->id)
	    {
		w = lastDamagedWindow;
	    }
	    else
	    {
		w = findWindowAtDisplay (display, de->drawable);
		if (w)
		    lastDamagedWindow = w;
	    }

	    if (w)
	    {
		REGION region;
		Bool   initial = FALSE;

		if (!w->damaged)
		{
		    w->damaged = initial = TRUE;
		    w->invisible = WINDOW_INVISIBLE (w);
		}

		region.extents.x1 = de->geometry.x + de->area.x;
		region.extents.y1 = de->geometry.y + de->area.y;
		region.extents.x2 = region.extents.x1 + de->area.width;
		region.extents.y2 = region.extents.y1 + de->area.height;

		if (!(*w->screen->damageWindowRect) (w, initial, 
						     &region.extents))
		{
		    region.rects = &region.extents;
		    region.numRects = region.size = 1;

		    damageScreenRegion (w->screen, &region);
		}
	    }
	}
	break;
    }
}
