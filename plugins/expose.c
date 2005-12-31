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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include <X11/cursorfont.h>

#include <comp.h>

#define EXPOSE_SPACING_DEFAULT 25
#define EXPOSE_SPACING_MIN     0
#define EXPOSE_SPACING_MAX     250

#define EXPOSE_SLOPPY_FOCUS_DEFAULT FALSE

#define EXPOSE_INITIATE_KEY_DEFAULT       "Tab"
#define EXPOSE_INITIATE_MODIFIERS_DEFAULT (CompPressMask | CompSuperMask)

#define EXPOSE_TERMINATE_KEY_DEFAULT       "Super_L"
#define EXPOSE_TERMINATE_MODIFIERS_DEFAULT CompReleaseMask

#define EXPOSE_NEXT_WINDOW_KEY_DEFAULT       "Tab"
#define EXPOSE_NEXT_WINDOW_MODIFIERS_DEFAULT (CompPressMask | CompSuperMask)

#define EXPOSE_STATE_NONE 0
#define EXPOSE_STATE_OUT  1
#define EXPOSE_STATE_WAIT 2
#define EXPOSE_STATE_IN   3

static int displayPrivateIndex;

typedef struct _ExposeSlot {
    int x1, y1, x2, y2;
    int line;
} ExposeSlot;

typedef struct _ExposeDisplay {
    int		    screenPrivateIndex;
    HandleEventProc handleEvent;
} ExposeDisplay;

#define EXPOSE_SCREEN_OPTION_SPACING      0
#define EXPOSE_SCREEN_OPTION_SLOPPY_FOCUS 1
#define EXPOSE_SCREEN_OPTION_INITIATE     2
#define EXPOSE_SCREEN_OPTION_TERMINATE    3
#define EXPOSE_SCREEN_OPTION_NEXT_WINDOW  4
#define EXPOSE_SCREEN_OPTION_NUM          5

typedef struct _ExposeScreen {
    int windowPrivateIndex;

    PreparePaintScreenProc preparePaintScreen;
    DonePaintScreenProc    donePaintScreen;
    PaintScreenProc        paintScreen;
    PaintWindowProc        paintWindow;

    CompOption opt[EXPOSE_SCREEN_OPTION_NUM];

    int spacing;

    int grabIndex;

    int state;
    int moreAdjust;

    Cursor cursor;

    ExposeSlot *slots;
    int        slotsSize;
    int        nSlots;

    int *line;
    int lineSize;
    int nLine;

    /* only used for sorting */
    CompWindow **windows;
    int        windowsSize;
    int        nWindows;

    GLfloat scale;
} ExposeScreen;

typedef struct _ExposeWindow {
    ExposeSlot *slot;

    GLfloat xVelocity, yVelocity, scaleVelocity;
    GLfloat scale;
    GLfloat tx, ty;
    Bool    adjust;
} ExposeWindow;


#define GET_EXPOSE_DISPLAY(d)				       \
    ((ExposeDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define EXPOSE_DISPLAY(d)		       \
    ExposeDisplay *ed = GET_EXPOSE_DISPLAY (d)

#define GET_EXPOSE_SCREEN(s, ed)				   \
    ((ExposeScreen *) (s)->privates[(ed)->screenPrivateIndex].ptr)

#define EXPOSE_SCREEN(s)						      \
    ExposeScreen *es = GET_EXPOSE_SCREEN (s, GET_EXPOSE_DISPLAY (s->display))

#define GET_EXPOSE_WINDOW(w, es)					   \
    ((ExposeWindow *) (w)->privates[(es)->windowPrivateIndex].ptr)

#define EXPOSE_WINDOW(w)					 \
    ExposeWindow *ew = GET_EXPOSE_WINDOW  (w,			 \
		       GET_EXPOSE_SCREEN  (w->screen,		 \
		       GET_EXPOSE_DISPLAY (w->screen->display)))

#define NUM_OPTIONS(s) (sizeof ((s)->opt) / sizeof (CompOption))

static CompOption *
exposeGetScreenOptions (CompScreen *screen,
			int	   *count)
{
    EXPOSE_SCREEN (screen);

    *count = NUM_OPTIONS (es);
    return es->opt;
}

static Bool
exposeSetScreenOption (CompScreen      *screen,
		       char	       *name,
		       CompOptionValue *value)
{
    CompOption *o;
    int	       index;

    EXPOSE_SCREEN (screen);

    o = compFindOption (es->opt, NUM_OPTIONS (es), name, &index);
    if (!o)
	return FALSE;

    switch (index) {
    case EXPOSE_SCREEN_OPTION_SPACING:
	if (compSetIntOption (o, value))
	{
	    es->spacing = o->value.i;
	    return TRUE;
	}
	break;
    case EXPOSE_SCREEN_OPTION_SLOPPY_FOCUS:
	if (compSetBoolOption (o, value))
	    return TRUE;
	break;
    case EXPOSE_SCREEN_OPTION_INITIATE:
	if (addScreenBinding (screen, &value->bind))
	{
	    removeScreenBinding (screen, &o->value.bind);

	    if (compSetBindingOption (o, value))
		return TRUE;
	}
	break;
    case EXPOSE_SCREEN_OPTION_TERMINATE:
    case EXPOSE_SCREEN_OPTION_NEXT_WINDOW:
	if (compSetBindingOption (o, value))
	    return TRUE;
	break;
    default:
	break;
    }

    return FALSE;
}

static void
exposeScreenInitOptions (ExposeScreen *es,
			 Display      *display)
{
    CompOption *o;

    o = &es->opt[EXPOSE_SCREEN_OPTION_SPACING];
    o->name	  = "spacing";
    o->shortDesc  = "Spacing";
    o->longDesc   = "Space between windows";
    o->type	  = CompOptionTypeInt;
    o->value.i	  = EXPOSE_SPACING_DEFAULT;
    o->rest.i.min = EXPOSE_SPACING_MIN;
    o->rest.i.max = EXPOSE_SPACING_MAX;

    o = &es->opt[EXPOSE_SCREEN_OPTION_SLOPPY_FOCUS];
    o->name	  = "sloppy_focus";
    o->shortDesc  = "Sloppy Focus";
    o->longDesc   = "Focus window when mouse moves over them";
    o->type	  = CompOptionTypeBool;
    o->value.b	  = EXPOSE_SLOPPY_FOCUS_DEFAULT;

    o = &es->opt[EXPOSE_SCREEN_OPTION_INITIATE];
    o->name			  = "initiate";
    o->shortDesc		  = "Initiate";
    o->longDesc			  = "Layout and start transforming windows";
    o->type			  = CompOptionTypeBinding;
    o->value.bind.type		  = CompBindingTypeKey;
    o->value.bind.u.key.modifiers = EXPOSE_INITIATE_MODIFIERS_DEFAULT;
    o->value.bind.u.key.keycode   =
	XKeysymToKeycode (display,
			  XStringToKeysym (EXPOSE_INITIATE_KEY_DEFAULT));

    o = &es->opt[EXPOSE_SCREEN_OPTION_TERMINATE];
    o->name			  = "terminate";
    o->shortDesc		  = "Terminate";
    o->longDesc			  = "Return from expose view";
    o->type			  = CompOptionTypeBinding;
    o->value.bind.type		  = CompBindingTypeKey;
    o->value.bind.u.key.modifiers = EXPOSE_TERMINATE_MODIFIERS_DEFAULT;
    o->value.bind.u.key.keycode   =
	XKeysymToKeycode (display,
			  XStringToKeysym (EXPOSE_TERMINATE_KEY_DEFAULT));

    o = &es->opt[EXPOSE_SCREEN_OPTION_NEXT_WINDOW];
    o->name			  = "next_window";
    o->shortDesc		  = "Next Window";
    o->longDesc			  = "Focus nextwindow";
    o->type			  = CompOptionTypeBinding;
    o->value.bind.type		  = CompBindingTypeKey;
    o->value.bind.u.key.modifiers = EXPOSE_NEXT_WINDOW_MODIFIERS_DEFAULT;
    o->value.bind.u.key.keycode   =
	XKeysymToKeycode (display,
			  XStringToKeysym (EXPOSE_NEXT_WINDOW_KEY_DEFAULT));
}

static Bool
exposePaintWindow (CompWindow		   *w,
		   const WindowPaintAttrib *attrib,
		   Region		   region,
		   unsigned int		   mask)
{
    CompScreen *s = w->screen;
    Bool       status;

    EXPOSE_SCREEN (s);

    if (es->grabIndex)
    {
	WindowPaintAttrib exposeAttrib = *attrib;

	EXPOSE_WINDOW (w);

	exposeAttrib.xTranslate += ew->tx;
	exposeAttrib.yTranslate += ew->ty;
	exposeAttrib.xScale *= ew->scale;
	exposeAttrib.yScale *= ew->scale;

	UNWRAP (es, s, paintWindow);
	status = (*s->paintWindow) (w, &exposeAttrib, region, mask);
	WRAP (es, s, paintWindow, exposePaintWindow);
    }
    else
    {
	UNWRAP (es, s, paintWindow);
	status = (*s->paintWindow) (w, attrib, region, mask);
	WRAP (es, s, paintWindow, exposePaintWindow);
    }

    return status;
}

static Bool
isExposeWin (CompWindow *w)
{
    if (w->attrib.map_state != IsViewable)
	return FALSE;

    if (w->type == w->screen->display->winDesktopAtom ||
	w->type == w->screen->display->winDockAtom)
	return FALSE;

    if (w->attrib.x >= w->screen->width   ||
	w->attrib.y >= w->screen->height  ||
	w->attrib.x + w->width  <= 0 ||
	w->attrib.y + w->height <= 0)
	return FALSE;

    return TRUE;
}

static int
compareWindows (const void *elem1,
		const void *elem2)
{
    CompWindow *w1 = *((CompWindow **) elem1);
    CompWindow *w2 = *((CompWindow **) elem2);
    int        s1, s2;

    s1 = (w1->screen->width - (w1->attrib.x + w1->attrib.width)) +
	(w1->screen->height - (w1->attrib.y + w1->attrib.height)) -
	w1->attrib.x - w1->attrib.y;

    s2 = (w2->screen->width - (w2->attrib.x + w2->attrib.width)) +
	(w2->screen->height - (w2->attrib.y + w2->attrib.height)) -
	w2->attrib.x - w2->attrib.y;

    return s2 - s1;
}

/* TODO: Place window thumbnails at smarter positions and use
   WM_STRUT HINTS instead of static top/bottom offsets == 24 */
static Bool
layoutThumbs (CompScreen *s)
{
    CompWindow *w;
    int	       i, j, y2;
    int        cx, cy;
    int        lineLength, itemsPerLine;
    float      scaleW, scaleH;
    int        totalWidth, totalHeight;

    EXPOSE_SCREEN (s);

    cx = cy = es->nWindows = 0;

    for (w = s->windows; w; w = w->next)
    {
	EXPOSE_WINDOW (w);

	if (ew->slot)
	    ew->adjust = TRUE;

	ew->slot = 0;

	if (!isExposeWin (w))
	    continue;

	if (es->windowsSize <= es->nWindows)
	{
	    es->windows = realloc (es->windows,
				  sizeof (CompWindow *) * (es->nWindows + 32));
	    if (!es->windows)
		return FALSE;

	    es->windowsSize = es->nWindows + 32;
	}

	es->windows[es->nWindows++] = w;
    }

    if (!es->nWindows)
	return FALSE;

    qsort (es->windows, es->nWindows, sizeof (CompWindow *), compareWindows);

    itemsPerLine = (sqrt (es->nWindows) * s->width) / s->height;
    if (itemsPerLine < 1)
	itemsPerLine = 1;

    if (es->lineSize <= es->nWindows / itemsPerLine)
    {
	es->line = realloc (es->line, sizeof (int) *
			    (es->nWindows / itemsPerLine + 1));
	if (!es->line)
	    return FALSE;

	es->lineSize = es->nWindows / itemsPerLine + 1;
    }

    totalWidth = totalHeight = 0;

    es->line[0] = 0;
    es->nLine = 1;
    lineLength = itemsPerLine;

    if (es->slotsSize <= es->nWindows)
    {
	es->slots = realloc (es->slots, sizeof (ExposeSlot) *
			     (es->nWindows + 1));
	if (!es->slots)
	    return FALSE;

	es->slotsSize = es->nWindows + 1;
    }
    es->nSlots = 0;

    for (i = 0; i < es->nWindows; i++)
    {
	EXPOSE_WINDOW (es->windows[i]);

	w = es->windows[i];

	/* find a good place between other elements */
	for (j = 0; j < es->nSlots; j++)
	{
	    y2 = es->slots[j].y2 + es->spacing + w->height;
	    if (w->width < es->slots[j].x2 - es->slots[j].x1 &&
		y2 <= es->line[es->slots[j].line])
		break;
	}

	/* otherwise append or start a new line */
	if (j == es->nSlots)
	{
	    if (lineLength < itemsPerLine)
	    {
		lineLength++;

		es->slots[es->nSlots].x1 = cx;
		es->slots[es->nSlots].y1 = cy;
		es->slots[es->nSlots].x2 = cx + w->width;
		es->slots[es->nSlots].y2 = cy + w->height;
		es->slots[es->nSlots].line = es->nLine - 1;

		es->line[es->nLine - 1] = MAX (es->line[es->nLine - 1],
					       es->slots[es->nSlots].y2);
	    }
	    else
	    {
		lineLength = 1;

		cx = es->spacing;
		cy = es->line[es->nLine - 1] + es->spacing;

		es->slots[es->nSlots].x1 = cx;
		es->slots[es->nSlots].y1 = cy;
		es->slots[es->nSlots].x2 = cx + w->width;
		es->slots[es->nSlots].y2 = cy + w->height;
		es->slots[es->nSlots].line = es->nLine - 1;

		es->line[es->nLine] = es->slots[es->nSlots].y2;

		es->nLine++;
	    }

	    if (es->slots[es->nSlots].y2 > totalHeight)
		totalHeight = es->slots[es->nSlots].y2;
	}
	else
	{
	    es->slots[es->nSlots].x1 = es->slots[j].x1;
	    es->slots[es->nSlots].y1 = es->slots[j].y2 + es->spacing;
	    es->slots[es->nSlots].x2 = es->slots[es->nSlots].x1 + w->width;
	    es->slots[es->nSlots].y2 = es->slots[es->nSlots].y1 + w->height;
	    es->slots[es->nSlots].line = es->slots[j].line;

	    es->slots[j].line = 0;
	}

	cx = es->slots[es->nSlots].x2;
	if (cx > totalWidth)
	    totalWidth = cx;

	cx += es->spacing;

	ew->slot   = &es->slots[es->nSlots];
	ew->adjust = TRUE;

	es->nSlots++;
    }

    totalWidth  += es->spacing;
    totalHeight += es->spacing;

    scaleW = (GLfloat) (s->width) / totalWidth;
    scaleH = (GLfloat) (s->height - 48) / totalHeight;

    es->scale = MIN (MIN (scaleH, scaleW), 1.0f);

    for (i = 0; i < es->nSlots; i++)
    {
	es->slots[i].y1 = (float) es->slots[i].y1 * es->scale;
	es->slots[i].x1 = (float) es->slots[i].x1 * es->scale;
	es->slots[i].y1 += 24;
    }

    return TRUE;
}

static int
adjustExposeVelocity (CompWindow *w)
{
    float dx, dy, ds, adjust, amount;
    float x1, y1, scale;

    EXPOSE_SCREEN (w->screen);
    EXPOSE_WINDOW (w);

    if (ew->slot)
    {
	x1 = ew->slot->x1;
	y1 = ew->slot->y1;
	scale = es->scale;
    }
    else
    {
	x1 = w->attrib.x;
	y1 = w->attrib.y;
	scale = 1.0f;
    }

    dx = x1 - (w->attrib.x + ew->tx);

    adjust = dx * 0.15f;
    amount = fabs (dx) * 1.5f;
    if (amount < 0.5f)
	amount = 0.5f;
    else if (amount > 5.0f)
	amount = 5.0f;

    ew->xVelocity = (amount * ew->xVelocity + adjust) / (amount + 1.0f);

    dy = y1 - (w->attrib.y + ew->ty);

    adjust = dy * 0.15f;
    amount = fabs (dy) * 1.5f;
    if (amount < 0.5f)
	amount = 0.5f;
    else if (amount > 5.0f)
	amount = 5.0f;

    ew->yVelocity = (amount * ew->yVelocity + adjust) / (amount + 1.0f);

    ds = scale - ew->scale;

    adjust = ds * 0.1f;
    amount = fabs (ds) * 7.0f;
    if (amount < 0.01f)
	amount = 0.01f;
    else if (amount > 0.15f)
	amount = 0.15f;

    ew->scaleVelocity = (amount * ew->scaleVelocity + adjust) /
	(amount + 1.0f);

    if (fabs (dx) < 0.1f && fabs (ew->xVelocity) < 0.2f &&
	fabs (dy) < 0.1f && fabs (ew->yVelocity) < 0.2f &&
	fabs (ds) < 0.001f && fabs (ew->scaleVelocity) < 0.002f)
    {
	ew->xVelocity = ew->yVelocity = ew->scaleVelocity = 0.0f;
	ew->tx = x1 - w->attrib.x;
	ew->ty = y1 - w->attrib.y;
	ew->scale = scale;

	return 0;
    }

    return 1;
}

static Bool
exposePaintScreen (CompScreen		   *s,
		   const ScreenPaintAttrib *sAttrib,
		   const WindowPaintAttrib *wAttrib,
		   Region		   region,
		   unsigned int		   mask)
{
    Bool status;

    EXPOSE_SCREEN (s);

    if (es->grabIndex)
    {
	mask &= ~PAINT_SCREEN_REGION_MASK;
	mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS_MASK;
    }

    UNWRAP (es, s, paintScreen);
    status = (*s->paintScreen) (s, sAttrib, wAttrib, region, mask);
    WRAP (es, s, paintScreen, exposePaintScreen);

    return status;
}

static void
exposePreparePaintScreen (CompScreen *s,
			  int	     msSinceLastPaint)
{
    EXPOSE_SCREEN (s);

    if (es->grabIndex && es->state != EXPOSE_STATE_WAIT)
    {
	CompWindow *w;

	es->moreAdjust = 0;

	for (w = s->windows; w; w = w->next)
	{
	    EXPOSE_WINDOW (w);

	    if (ew->adjust)
	    {
		ew->adjust = adjustExposeVelocity (w);

		es->moreAdjust |= ew->adjust;

		ew->tx += (ew->xVelocity * msSinceLastPaint) / s->redrawTime;
		ew->ty += (ew->yVelocity * msSinceLastPaint) / s->redrawTime;
		ew->scale += (ew->scaleVelocity * msSinceLastPaint) /
		    s->redrawTime;
	    }
	}
    }

    UNWRAP (es, s, preparePaintScreen);
    (*s->preparePaintScreen) (s, msSinceLastPaint);
    WRAP (es, s, preparePaintScreen, exposePreparePaintScreen);
}

static void
exposeDonePaintScreen (CompScreen *s)
{
    EXPOSE_SCREEN (s);

    if (es->grabIndex)
    {
	if (es->moreAdjust)
	{
	    damageScreen (s);
	}
	else
	{
	    if (es->state == EXPOSE_STATE_IN)
	    {
		removeScreenGrab (s, es->grabIndex, 0);
		es->grabIndex = 0;
	    }
	    else
		es->state = EXPOSE_STATE_WAIT;
	}
    }

    UNWRAP (es, s, donePaintScreen);
    (*s->donePaintScreen) (s);
    WRAP (es, s, donePaintScreen, exposeDonePaintScreen);
}

static CompWindow *
exposeCheckForWindowAt (CompScreen *s,
			int        x,
			int        y)
{
    int        x1, y1, x2, y2;
    CompWindow *w;

    for (w = s->reverseWindows; w; w = w->prev)
    {
	EXPOSE_WINDOW (w);

	if (ew->slot)
	{
	    x1 = w->attrib.x + ew->tx;
	    y1 = w->attrib.y + ew->ty;
	    x2 = x1 + ((float) w->width  * ew->scale);
	    y2 = y1 + ((float) w->height * ew->scale);

	    if (x1 <= x && y1 <= y && x2 > x && y2 > y)
		return w;
	}
    }

    return 0;
}

static void
exposeInitiate (CompScreen *s)
{
    EXPOSE_SCREEN (s);

    if (layoutThumbs (s))
    {
	if (!es->grabIndex)
	    es->grabIndex = pushScreenGrab (s, es->cursor);

	if (es->grabIndex)
	{
	    damageScreen (s);
	    gettimeofday (&s->lastRedraw, 0);
	    es->state = EXPOSE_STATE_OUT;
	}
    }
}

static void
exposeTerminate (CompScreen *s)
{
    EXPOSE_SCREEN (s);

    if (es->grabIndex)
    {
	CompWindow *w;

	for (w = s->windows; w; w = w->next)
	{
	    EXPOSE_WINDOW (w);

	    ew->slot = 0;
	    ew->adjust = TRUE;
	}

	es->state = EXPOSE_STATE_IN;

	damageScreen (s);
    }
}

static Bool
exposeSelectWindow (CompWindow *w)
{
    EXPOSE_WINDOW (w);

    if (ew->slot && w->client && w->client != w->screen->activeWindow)
    {
	XClientMessageEvent cm;

	cm.type = ClientMessage;
	cm.display = w->screen->display->display;
	cm.format = 32;

	cm.message_type = w->screen->display->winActiveAtom;
	cm.window = w->client;

	cm.data.l[0] = 2;
	cm.data.l[1] = cm.data.l[2] = cm.data.l[3] = cm.data.l[4] = 0;

	XSendEvent (w->screen->display->display, w->screen->root, FALSE,
		    StructureNotifyMask, (XEvent *) &cm);

	return TRUE;
    }

    return FALSE;
}

static void
exposeSelectWindowAt (CompScreen *s,
		      int	 x,
		      int	 y)

{
    CompWindow *w;

    w = exposeCheckForWindowAt (s, x, y);
    if (w)
	exposeSelectWindow (w);
}

static void
exposeNextWindow (CompScreen *s)

{
    CompWindow *w;

    for (w = s->windows; w; w = w->next)
	if (s->activeWindow == w->client)
	    break;

    if (w)
    {
	for (w = w->next; w; w = w->next)
	    if (exposeSelectWindow (w))
		return;

	for (w = s->windows; w; w = w->next)
	{
	    if (s->activeWindow == w->client)
		break;

	    if (exposeSelectWindow (w))
		return;
	}
    }
}

static void
exposeHandleEvent (CompDisplay *d,
		   XEvent      *event)
{
    CompScreen *s;

    EXPOSE_DISPLAY (d);

    switch (event->type) {
    case KeyPress:
    case KeyRelease:
	s = findScreenAtDisplay (d, event->xkey.root);
	if (s)
	{
	    EXPOSE_SCREEN (s);

	    if (EV_KEY (&es->opt[EXPOSE_SCREEN_OPTION_INITIATE], event))
		exposeInitiate (s);

	    if (EV_KEY (&es->opt[EXPOSE_SCREEN_OPTION_NEXT_WINDOW], event))
		exposeNextWindow (s);

	    if (EV_KEY (&es->opt[EXPOSE_SCREEN_OPTION_TERMINATE], event) ||
		(event->type	     == KeyPress &&
		 event->xkey.keycode == s->escapeKeyCode))
		exposeTerminate (s);
	}
	break;
    case ButtonPress:
    case ButtonRelease:
	s = findScreenAtDisplay (d, event->xbutton.root);
	if (s)
	{
	    EXPOSE_SCREEN (s);

	    if (es->grabIndex && es->state != EXPOSE_STATE_IN)
		exposeSelectWindowAt (s,
				      event->xbutton.x_root,
				      event->xbutton.y_root);

	    if (EV_BUTTON (&es->opt[EXPOSE_SCREEN_OPTION_INITIATE], event))
		exposeInitiate (s);

	    if (EV_BUTTON (&es->opt[EXPOSE_SCREEN_OPTION_NEXT_WINDOW], event))
		exposeNextWindow (s);

	    if (EV_BUTTON (&es->opt[EXPOSE_SCREEN_OPTION_TERMINATE], event))
		exposeTerminate (s);
	}
	break;
    case MotionNotify:
	s = findScreenAtDisplay (d, event->xmotion.root);
	if (s)
	{
	    EXPOSE_SCREEN (s);

	    if (es->grabIndex		     &&
		es->state != EXPOSE_STATE_IN &&
		es->opt[EXPOSE_SCREEN_OPTION_SLOPPY_FOCUS].value.b)
		exposeSelectWindowAt (s,
				      event->xmotion.x_root,
				      event->xmotion.y_root);
	}
    default:
	break;
    }

    UNWRAP (ed, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (ed, d, handleEvent, exposeHandleEvent);

    switch (event->type) {
    case CreateNotify:
    case MapNotify: {
	CompWindow *w;

	w = findWindowAtDisplay (d, (event->type == CreateNotify) ?
				 event->xcreatewindow.window :
				 event->xmap.window);
	if (w && isExposeWin (w))
	{
	    EXPOSE_SCREEN (w->screen);

	    if (es->grabIndex && layoutThumbs (w->screen))
	    {
		es->state = EXPOSE_STATE_OUT;
		damageScreen (w->screen);
		gettimeofday (&w->screen->lastRedraw, 0);
	    }
	}
    }
    default:
	break;
    }
}

static Bool
exposeInitDisplay (CompPlugin  *p,
		   CompDisplay *d)
{
    ExposeDisplay *ed;

    ed = malloc (sizeof (ExposeDisplay));
    if (!ed)
	return FALSE;

    ed->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (ed->screenPrivateIndex < 0)
    {
	free (ed);
	return FALSE;
    }

    WRAP (ed, d, handleEvent, exposeHandleEvent);

    d->privates[displayPrivateIndex].ptr = ed;

    return TRUE;
}

static void
exposeFiniDisplay (CompPlugin  *p,
		   CompDisplay *d)
{
    EXPOSE_DISPLAY (d);

    freeScreenPrivateIndex (d, ed->screenPrivateIndex);

    UNWRAP (ed, d, handleEvent);

    free (ed);
}

static Bool
exposeInitScreen (CompPlugin *p,
		  CompScreen *s)
{
    ExposeScreen *es;

    EXPOSE_DISPLAY (s->display);

    es = malloc (sizeof (ExposeScreen));
    if (!es)
	return FALSE;

    es->windowPrivateIndex = allocateWindowPrivateIndex (s);
    if (es->windowPrivateIndex < 0)
    {
	free (es);
	return FALSE;
    }

    es->grabIndex = 0;

    es->state = EXPOSE_STATE_NONE;

    es->slots = 0;
    es->slotsSize = 0;

    es->windows = 0;
    es->windowsSize = 0;

    es->line = 0;
    es->lineSize = 0;

    es->scale = 1.0f;

    es->spacing = EXPOSE_SPACING_DEFAULT;

    exposeScreenInitOptions (es, s->display->display);

    addScreenBinding (s, &es->opt[EXPOSE_SCREEN_OPTION_INITIATE].value.bind);

    WRAP (es, s, preparePaintScreen, exposePreparePaintScreen);
    WRAP (es, s, donePaintScreen, exposeDonePaintScreen);
    WRAP (es, s, paintScreen, exposePaintScreen);
    WRAP (es, s, paintWindow, exposePaintWindow);

    es->cursor = XCreateFontCursor (s->display->display, XC_left_ptr);

    s->privates[ed->screenPrivateIndex].ptr = es;

    return TRUE;
}

static void
exposeFiniScreen (CompPlugin *p,
		  CompScreen *s)
{
    EXPOSE_SCREEN (s);

    UNWRAP (es, s, preparePaintScreen);
    UNWRAP (es, s, donePaintScreen);
    UNWRAP (es, s, paintScreen);
    UNWRAP (es, s, paintWindow);

    if (es->slotsSize)
	free (es->slots);

    if (es->lineSize)
	free (es->line);

    if (es->windowsSize)
	free (es->windows);

    free (es);
}

static Bool
exposeInitWindow (CompPlugin *p,
		  CompWindow *w)
{
    ExposeWindow *ew;

    EXPOSE_SCREEN (w->screen);

    ew = malloc (sizeof (ExposeWindow));
    if (!ew)
	return FALSE;

    ew->slot = 0;
    ew->scale = 1.0f;
    ew->tx = ew->ty = 0.0f;
    ew->adjust = FALSE;
    ew->xVelocity = ew->yVelocity = ew->scaleVelocity = 0.0f;

    w->privates[es->windowPrivateIndex].ptr = ew;

    return TRUE;
}

static void
exposeFiniWindow (CompPlugin *p,
		  CompWindow *w)
{
    EXPOSE_WINDOW (w);

    free (ew);
}

static Bool
exposeInit (CompPlugin *p)
{
    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
	return FALSE;

    return TRUE;
}

static void
exposeFini (CompPlugin *p)
{
    if (displayPrivateIndex >= 0)
	freeDisplayPrivateIndex (displayPrivateIndex);
}

CompPluginVTable exposeVTable = {
    "expose",
    "Expose",
    "Expose-like Window Switcher",
    exposeInit,
    exposeFini,
    exposeInitDisplay,
    exposeFiniDisplay,
    exposeInitScreen,
    exposeFiniScreen,
    exposeInitWindow,
    exposeFiniWindow,
    0, /* GetDisplayOptions */
    0, /* SetDisplayOption */
    exposeGetScreenOptions,
    exposeSetScreenOption
};

CompPluginVTable *
getCompPluginInfo (void)
{
    return &exposeVTable;
}
