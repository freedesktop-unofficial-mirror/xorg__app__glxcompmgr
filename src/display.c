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
#include <sys/poll.h>
#include <unistd.h>

#define XK_MISCELLANY
#include <X11/keysymdef.h>

#include <X11/Xlib.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/shape.h>

#include <comp.h>

static unsigned int virtualModMask[] = {
    CompAltMask, CompMetaMask, CompSuperMask, CompHyperMask,
    CompModeSwitchMask, CompNumLockMask, CompScrollLockMask
};

typedef struct _CompTimeout {
    struct _CompTimeout *next;
    int			time;
    int			left;
    CallBackProc	callBack;
    void		*closure;
    CompTimeoutHandle   handle;
} CompTimeout;

static CompTimeout       *timeouts = 0;
static struct timeval    lastTimeout;
static CompTimeoutHandle lastTimeoutHandle = 1;

#define NUM_OPTIONS(d) (sizeof ((d)->opt) / sizeof (CompOption))

static char *textureFilter[] = { "Fast", "Good", "Best" };

#define NUM_TEXTURE_FILTER (sizeof (textureFilter) / sizeof (textureFilter[0]))

CompDisplay *compDisplays = 0;

static CompDisplay compDisplay;

static char *displayPrivateIndices = 0;
static int  displayPrivateLen = 0;

static int
reallocDisplayPrivate (int  size,
		       void *closure)
{
    CompDisplay *d = compDisplays;
    void        *privates;

    if (d)
    {
	privates = realloc (d->privates, size * sizeof (CompPrivate));
	if (!privates)
	    return FALSE;

	d->privates = (CompPrivate *) privates;
    }

    return TRUE;
}

int
allocateDisplayPrivateIndex (void)
{
    return allocatePrivateIndex (&displayPrivateLen,
				 &displayPrivateIndices,
				 reallocDisplayPrivate,
				 0);
}

void
freeDisplayPrivateIndex (int index)
{
    freePrivateIndex (displayPrivateLen, displayPrivateIndices, index);
}

static void
compDisplayInitOptions (CompDisplay *display,
			char	    **plugin,
			int	    nPlugin)
{
    CompOption *o;
    int        i;

    o = &display->opt[COMP_DISPLAY_OPTION_ACTIVE_PLUGINS];
    o->name	         = "active_plugins";
    o->shortDesc         = "Active Plugins";
    o->longDesc	         = "List of currently active plugins";
    o->type	         = CompOptionTypeList;
    o->value.list.type   = CompOptionTypeString;
    o->value.list.nValue = nPlugin;
    o->value.list.value  = malloc (sizeof (CompOptionValue) * nPlugin);
    for (i = 0; i < nPlugin; i++)
	o->value.list.value[i].s = strdup (plugin[i]);
    o->rest.s.string     = 0;
    o->rest.s.nString    = 0;

    display->dirtyPluginList = TRUE;

    o = &display->opt[COMP_DISPLAY_OPTION_TEXTURE_FILTER];
    o->name	      = "texture_filter";
    o->shortDesc      = "Texture Filter";
    o->longDesc	      = "Texture filtering";
    o->type	      = CompOptionTypeString;
    o->value.s	      = strdup (defaultTextureFilter);
    o->rest.s.string  = textureFilter;
    o->rest.s.nString = NUM_TEXTURE_FILTER;
}

CompOption *
compGetDisplayOptions (CompDisplay *display,
		       int	   *count)
{
    *count = NUM_OPTIONS (display);
    return display->opt;
}

static Bool
setDisplayOption (CompDisplay     *display,
		  char	          *name,
		  CompOptionValue *value)
{
    CompOption *o;
    int	       index;

    o = compFindOption (display->opt, NUM_OPTIONS (display), name, &index);
    if (!o)
	return FALSE;

    switch (index) {
    case COMP_DISPLAY_OPTION_ACTIVE_PLUGINS:
	if (compSetOptionList (o, value))
	{
	    display->dirtyPluginList = TRUE;
	    return TRUE;
	}
	break;
    case COMP_DISPLAY_OPTION_TEXTURE_FILTER:
	if (compSetStringOption (o, value))
	{
	    CompScreen *s;

	    for (s = display->screens; s; s = s->next)
		damageScreen (s);

	    if (strcmp (o->value.s, "Fast") == 0)
		display->textureFilter = GL_NEAREST;
	    else
		display->textureFilter = GL_LINEAR;

	    return TRUE;
	}
    default:
	break;
    }

    return FALSE;
}

static Bool
setDisplayOptionForPlugin (CompDisplay     *display,
			   char	           *plugin,
			   char	           *name,
			   CompOptionValue *value)
{
    CompPlugin *p;

    p = findActivePlugin (plugin);
    if (p && p->vTable->setDisplayOption)
	return (*p->vTable->setDisplayOption) (display, name, value);

    return FALSE;
}

static Bool
initPluginForDisplay (CompPlugin  *p,
		      CompDisplay *d)
{
    return (*p->vTable->initDisplay) (p, d);
}

static void
finiPluginForDisplay (CompPlugin  *p,
		      CompDisplay *d)
{
    (*p->vTable->finiDisplay) (p, d);
}

static void
updatePlugins (CompDisplay *d)
{
    CompOption *o;
    CompPlugin *p, **pop = 0;
    int	       nPop, i, j;

    d->dirtyPluginList = FALSE;

    o = &d->opt[COMP_DISPLAY_OPTION_ACTIVE_PLUGINS];
    for (i = 0; i < d->plugin.list.nValue && i < o->value.list.nValue; i++)
    {
	if (strcmp (d->plugin.list.value[i].s, o->value.list.value[i].s))
	    break;
    }

    nPop = d->plugin.list.nValue - i;

    if (nPop)
    {
	pop = malloc (sizeof (CompPlugin *) * nPop);
	if (!pop)
	{
	    (*d->setDisplayOption) (d, o->name, &d->plugin);
	    return;
	}
    }

    for (j = 0; j < nPop; j++)
    {
	pop[j] = popPlugin ();
	d->plugin.list.nValue--;
	free (d->plugin.list.value[d->plugin.list.nValue].s);
    }

    for (; i < o->value.list.nValue; i++)
    {
	p = 0;
	for (j = 0; j < nPop; j++)
	{
	    if (pop[j] && strcmp (pop[j]->vTable->name,
				  o->value.list.value[i].s) == 0)
	    {
		if (pushPlugin (pop[j]))
		{
		    p = pop[j];
		    pop[j] = 0;
		    break;
		}
	    }
	}

	if (p == 0)
	{
	    p = loadPlugin (o->value.list.value[i].s);
	    if (p)
	    {
		if (!pushPlugin (p))
		{
		    unloadPlugin (p);
		    p = 0;
		}
	    }
	}

	if (p)
	{
	    CompOptionValue *value;

	    value = realloc (d->plugin.list.value, sizeof (CompOption) *
			     (d->plugin.list.nValue + 1));
	    if (value)
	    {
		value[d->plugin.list.nValue].s = strdup (p->vTable->name);

		d->plugin.list.value = value;
		d->plugin.list.nValue++;
	    }
	    else
	    {
		p = popPlugin ();
		unloadPlugin (p);
	    }
	}
    }

    for (j = 0; j < nPop; j++)
    {
	if (pop[j])
	    unloadPlugin (pop[j]);
    }

    if (nPop)
	free (pop);

    (*d->setDisplayOption) (d, o->name, &d->plugin);
}

static void
addTimeout (CompTimeout *timeout)
{
    CompTimeout *p = 0, *t;

    for (t = timeouts; t; t = t->next)
    {
	if (timeout->time < t->left)
	    break;

	p = t;
    }

    timeout->next = t;
    timeout->left = timeout->time;

    if (p)
	p->next = timeout;
    else
	timeouts = timeout;
}

CompTimeoutHandle
compAddTimeout (int	     time,
		CallBackProc callBack,
		void	     *closure)
{
    CompTimeout *timeout;

    timeout = malloc (sizeof (CompTimeout));
    if (!timeout)
	return 0;

    timeout->time     = time;
    timeout->callBack = callBack;
    timeout->closure  = closure;
    timeout->handle   = lastTimeoutHandle++;

    if (lastTimeoutHandle == MAXSHORT)
	lastTimeoutHandle = 1;

    if (!timeouts)
	gettimeofday (&lastTimeout, 0);

    addTimeout (timeout);

    return timeout->handle;
}

void
compRemoveTimeout (CompTimeoutHandle handle)
{
    CompTimeout *p = 0, *t;

    for (t = timeouts; t; t = t->next)
    {
	if (t->handle == handle)
	    break;

	p = t;
    }

    if (t)
    {
	if (p)
	    p->next = t->next;
	else
	    timeouts = t->next;

	free (t);
    }
}

#define TIMEVALDIFF(tv1, tv2)						   \
    ((tv1)->tv_sec == (tv2)->tv_sec || (tv1)->tv_usec >= (tv2)->tv_usec) ? \
    ((((tv1)->tv_sec - (tv2)->tv_sec) * 1000000) +			   \
     ((tv1)->tv_usec - (tv2)->tv_usec)) / 1000 :			   \
    ((((tv1)->tv_sec - 1 - (tv2)->tv_sec) * 1000000) +			   \
     (1000000 + (tv1)->tv_usec - (tv2)->tv_usec)) / 1000

static int
getTimeToNextRedraw (CompScreen     *s,
		     struct timeval *lastTv)
{
    struct timeval tv;
    int		   diff;

    gettimeofday (&tv, 0);

    diff = TIMEVALDIFF (&tv, lastTv);

    if (diff > s->redrawTime)
	return 0;

    return s->redrawTime - diff;
}

static CompWindow *
findWindowAt (CompDisplay *d,
	      Window      root,
	      int	  x,
	      int	  y)
{
    CompScreen *s;
    CompWindow *w;

    for (s = d->screens; s; s = s->next)
    {
	if (s->root == root && s->maxGrab == 0)
	{
	    for (w = s->reverseWindows; w; w = w->prev)
	    {
		if (x >= w->attrib.x &&
		    y >= w->attrib.y &&
		    x <  w->attrib.x + w->width &&
		    y <  w->attrib.y + w->height)
		    return w;
	    }
	}
    }

    return 0;
}

static Window
translateToRootWindow (CompDisplay *d,
		       Window      child)
{
    CompScreen *s;

    for (s = d->screens; s; s = s->next)
    {
	if (s->root == child || s->grabWindow == child)
	    return s->root;
    }

    return child;
}

void
updateModifierMappings (CompDisplay *d)
{
    XModifierKeymap *modmap;
    unsigned int    i, modMask[CompModNum];

    for (i = 0; i < CompModNum; i++)
	modMask[i] = 0;

    modmap = XGetModifierMapping (d->display);
    if (modmap && modmap->max_keypermod > 0)
    {
	static int maskTable[] = {
	    ShiftMask, LockMask, ControlMask, Mod1Mask,
	    Mod2Mask, Mod3Mask, Mod4Mask, Mod5Mask
	};
	KeySym keysym;
	int    i, size, mask;

	size = (sizeof (maskTable) / sizeof (int)) * modmap->max_keypermod;

	for (i = 0; i < size; ++i)
	{
	    if (!modmap->modifiermap[i])
		continue;

	    keysym = XKeycodeToKeysym (d->display, modmap->modifiermap[i], 0);
	    if (keysym)
	    {
		mask = maskTable[i / modmap->max_keypermod];

		if (keysym == XK_Alt_L ||
		    keysym == XK_Alt_R)
		{
		    modMask[CompModAlt] |= mask;
		}
		else if (keysym == XK_Meta_L ||
			 keysym == XK_Meta_R)
		{
		    modMask[CompModMeta] |= mask;
		}
		else if (keysym == XK_Super_L ||
			 keysym == XK_Super_R)
		{
		    modMask[CompModSuper] |= mask;
		}
		else if (keysym == XK_Hyper_L ||
			 keysym == XK_Hyper_R)
		{
		    modMask[CompModHyper] |= mask;
		}
		else if (keysym == XK_Mode_switch)
		{
		    modMask[CompModModeSwitch] |= mask;
		}
		else if (keysym == XK_Scroll_Lock)
		{
		    modMask[CompModScrollLock] |= mask;
		}
		else if (keysym == XK_Num_Lock)
		{
		    modMask[CompModNumLock] |= mask;
		}
	    }
	}

	if (modmap)
	    XFreeModifiermap (modmap);

	for (i = 0; i < CompModNum; i++)
	{
	    if (!modMask[i])
		modMask[i] = CompNoMask;
	}

	if (memcmp (modMask, d->modMask, sizeof (modMask)))
	{
	    CompScreen *s;

	    memcpy (d->modMask, modMask, sizeof (modMask));

	    for (s = d->screens; s; s = s->next)
		updatePassiveGrabs (s);
	}
    }
}

unsigned int
virtualToRealModMask (CompDisplay  *d,
		      unsigned int modMask)
{
    int i;

    for (i = 0; i < CompModNum; i++)
    {
	if (modMask & virtualModMask[i])
	{
	    modMask &= ~virtualModMask[i];
	    modMask |= d->modMask[i];
	}
    }

    return (modMask & ~(CompPressMask | CompReleaseMask));
}

static unsigned int
realToVirtualModMask (CompDisplay  *d,
		      unsigned int modMask)
{
    int i;

    for (i = 0; i < CompModNum; i++)
    {
	if (modMask & d->modMask[i])
	    modMask |= virtualModMask[i];
    }

    return modMask;
}

void
eventLoop (void)
{
    XEvent	   event;
    struct pollfd  ufd;
    int		   timeDiff;
    struct timeval tv;
    Region	   tmpRegion;
    CompDisplay    *display = compDisplays;
    CompScreen	   *s = display->screens;
    int		   timeToNextRedraw = 0;
    CompWindow	   *move = 0;
    int		   px = 0, py = 0;
    CompTimeout    *t;

    tmpRegion = XCreateRegion ();
    if (!tmpRegion)
    {
	fprintf (stderr, "%s: Couldn't create region\n", programName);
	return;
    }

    ufd.fd = ConnectionNumber (display->display);
    ufd.events = POLLIN;

    for (;;)
    {
	if (display->dirtyPluginList)
	    updatePlugins (display);

	if (restartSignal)
	{
	     execvp (programName, programArgv);
	     exit (1);
	}

	while (XPending (display->display))
	{
	    XNextEvent (display->display, &event);

	    /* translate root window */
	    if (testMode)
	    {
		Window root, child;

		switch (event.type) {
		case ButtonPress:
		    if (!move)
		    {
			px = event.xbutton.x;
			py = event.xbutton.y;

			move = findWindowAt (display, event.xbutton.window,
					     px, py);
			if (move)
			{
			    XRaiseWindow (display->display, move->id);
			    continue;
			}
		    }
		case ButtonRelease:
		    move = 0;

		    root = translateToRootWindow (display,
						  event.xbutton.window);
		    XTranslateCoordinates (display->display,
					   event.xbutton.root, root,
					   event.xbutton.x_root,
					   event.xbutton.y_root,
					   &event.xbutton.x_root,
					   &event.xbutton.y_root,
					   &child);
		    event.xbutton.root = root;
		    break;
		case KeyPress:
		case KeyRelease:
		    root = translateToRootWindow (display, event.xkey.window);
		    XTranslateCoordinates (display->display,
					   event.xkey.root, root,
					   event.xkey.x_root,
					   event.xkey.y_root,
					   &event.xkey.x_root,
					   &event.xkey.y_root,
					   &child);
		    event.xkey.root = root;
		    break;
		case MotionNotify:
		    if (move)
		    {
			XMoveWindow (display->display, move->id,
				     move->attrib.x + event.xbutton.x - px,
				     move->attrib.y + event.xbutton.y - py);

			px = event.xbutton.x;
			py = event.xbutton.y;
			continue;
		    }

		    root = translateToRootWindow (display,
						  event.xmotion.window);
		    XTranslateCoordinates (display->display,
					   event.xmotion.root, root,
					   event.xmotion.x_root,
					   event.xmotion.y_root,
					   &event.xmotion.x_root,
					   &event.xmotion.y_root,
					   &child);
		    event.xmotion.root = root;
		default:
		    break;
		}
	    }

	    /* add virtual modifiers */
	    switch (event.type) {
	    case ButtonPress:
		event.xbutton.state |= CompPressMask;
		event.xbutton.state =
		    realToVirtualModMask (display, event.xbutton.state);
		break;
	    case ButtonRelease:
		event.xbutton.state |= CompReleaseMask;
		event.xbutton.state =
		    realToVirtualModMask (display, event.xbutton.state);
		break;
	    case KeyPress:
		event.xkey.state |= CompPressMask;
		event.xkey.state = realToVirtualModMask (display,
							 event.xkey.state);
		break;
	    case KeyRelease:
		event.xkey.state |= CompReleaseMask;
		event.xkey.state = realToVirtualModMask (display,
							 event.xkey.state);
		break;
	    case MotionNotify:
		event.xmotion.state =
		    realToVirtualModMask (display, event.xmotion.state);
		break;
	    default:
		break;
	    }

	    (*display->handleEvent) (display, &event);
	}

	if (s->allDamaged || REGION_NOT_EMPTY (s->damage))
	{
	    if (timeToNextRedraw == 0)
	    {
		/* wait for X drawing requests to finish
		   glXWaitX (); */

		gettimeofday (&tv, 0);

		timeDiff = TIMEVALDIFF (&tv, &s->lastRedraw);

		(*s->preparePaintScreen) (s, timeDiff);

		if (s->allDamaged)
		{
		    EMPTY_REGION (s->damage);
		    s->allDamaged = 0;

		    (*s->paintScreen) (s,
				       &defaultScreenPaintAttrib,
				       &defaultWindowPaintAttrib,
				       &s->region,
				       PAINT_SCREEN_REGION_MASK |
				       PAINT_SCREEN_FULL_MASK);

		    glXSwapBuffers (s->display->display, s->root);
		}
		else
		{
		    XIntersectRegion (s->damage, &s->region, tmpRegion);

		    EMPTY_REGION (s->damage);

		    if ((*s->paintScreen) (s,
					   &defaultScreenPaintAttrib,
					   &defaultWindowPaintAttrib,
					   tmpRegion,
					   PAINT_SCREEN_REGION_MASK))
		    {
			BoxPtr pBox;
			int    nBox, y;

			glEnable (GL_SCISSOR_TEST);
			glDrawBuffer (GL_FRONT);

			pBox = tmpRegion->rects;
			nBox = tmpRegion->numRects;
			while (nBox--)
			{
			    y = s->height - pBox->y2;

			    glBitmap (0, 0, 0, 0,
				      pBox->x1 - s->rasterX, y - s->rasterY,
				      NULL);

			    s->rasterX = pBox->x1;
			    s->rasterY = y;

			    glScissor (pBox->x1, y,
				       pBox->x2 - pBox->x1,
				       pBox->y2 - pBox->y1);

			    glCopyPixels (pBox->x1, y,
					  pBox->x2 - pBox->x1,
					  pBox->y2 - pBox->y1,
					  GL_COLOR);

			    pBox++;
			}

			glDrawBuffer (GL_BACK);
			glDisable (GL_SCISSOR_TEST);
			glFlush ();
		    }
		    else
		    {
			(*s->paintScreen) (s,
					   &defaultScreenPaintAttrib,
					   &defaultWindowPaintAttrib,
					   &s->region,
					   PAINT_SCREEN_FULL_MASK);

			glXSwapBuffers (s->display->display, s->root);
		    }
		}

		s->lastRedraw = tv;

		(*s->donePaintScreen) (s);

		/* remove destroyed windows */
		while (s->pendingDestroys)
		{
		    CompWindow *w;

		    for (w = s->windows; w; w = w->next)
		    {
			if (w->destroyed)
			{
			    addWindowDamage (w);
			    removeWindow (w);
			    break;
			}
		    }

		    s->pendingDestroys--;
		}
	    }

	    timeToNextRedraw = getTimeToNextRedraw (s, &s->lastRedraw);
	    if (timeToNextRedraw)
		timeToNextRedraw = poll (&ufd, 1, timeToNextRedraw);
	}
	else
	{
	    if (timeouts)
	    {
		if (timeouts->left > 0)
		    poll (&ufd, 1, timeouts->left);

		gettimeofday (&tv, 0);

		timeDiff = TIMEVALDIFF (&tv, &lastTimeout);

		for (t = timeouts; t; t = t->next)
		    t->left -= timeDiff;

		while (timeouts && timeouts->left <= 0)
		{
		    t = timeouts;
		    if ((*t->callBack) (t->closure))
		    {
			timeouts = t->next;
			addTimeout (t);
		    }
		    else
		    {
			timeouts = t->next;
			free (t);
		    }
		}

		s->lastRedraw = lastTimeout = tv;
	    }
	    else
	    {
		poll (&ufd, 1, 1000);
		gettimeofday (&s->lastRedraw, 0);
	    }

	    /* just redraw immediately */
	    timeToNextRedraw = 0;
	}
    }
}

static int errors = 0;
static int redirectFailed;

static int
errorHandler (Display     *dpy,
	      XErrorEvent *e)
{

#ifdef DEBUG
    char str[128];
    char *name = 0;
    int  o;
#endif

    errors++;

    if (e->request_code == compDisplays->compositeOpcode &&
	e->minor_code   == X_CompositeRedirectSubwindows)
    {
	redirectFailed = 1;
	return 0;
    }

#ifdef DEBUG
    XGetErrorDatabaseText (dpy, "XlibMessage", "XError", "", str, 128);
    fprintf (stderr, "%s", str);

    o = e->error_code - compDisplays->damageError;
    switch (o) {
    case BadDamage:
	name = "BadDamage";
	break;
    default:
	break;
    }

    if (name)
    {
	fprintf (stderr, ": %s\n  ", name);
    }
    else
    {
	XGetErrorText (dpy, e->error_code, str, 128);
	fprintf (stderr, ": %s\n  ", str);
    }

    XGetErrorDatabaseText (dpy, "XlibMessage", "MajorCode", "%d", str, 128);
    fprintf (stderr, str, e->request_code);

    sprintf (str, "%d", e->request_code);
    XGetErrorDatabaseText (dpy, "XRequest", str, "", str, 128);
    if (strcmp (str, ""))
	fprintf (stderr, " (%s)", str);
    fprintf (stderr, "\n  ");

    XGetErrorDatabaseText (dpy, "XlibMessage", "MinorCode", "%d", str, 128);
    fprintf (stderr, str, e->minor_code);
    fprintf (stderr, "\n  ");

    XGetErrorDatabaseText (dpy, "XlibMessage", "ResourceID", "%d", str, 128);
    fprintf (stderr, str, e->resourceid);
    fprintf (stderr, "\n");

    /* abort (); */
#endif

    return 0;
}

int
compCheckForError (void)
{
    int e;

    e = errors;
    errors = 0;

    return e;
}

Bool
addDisplay (char *name,
	    char **plugin,
	    int  nPlugin)
{
    CompDisplay *d;
    Display	*dpy;
    int		i;

    d = &compDisplay;

    if (displayPrivateLen)
    {
	d->privates = malloc (displayPrivateLen * sizeof (CompPrivate));
	if (!d->privates)
	    return FALSE;
    }
    else
	d->privates = 0;

    d->screenPrivateIndices = 0;
    d->screenPrivateLen     = 0;

    for (i = 0; i < CompModNum; i++)
	d->modMask[i] = CompNoMask;

    d->plugin.list.type   = CompOptionTypeString;
    d->plugin.list.nValue = 0;
    d->plugin.list.value  = 0;

    compDisplayInitOptions (d, plugin, nPlugin);

    d->textureFilter = GL_LINEAR;

    d->display = dpy = XOpenDisplay (name);
    if (!d->display)
    {
	fprintf (stderr, "%s: Couldn't open display %s\n",
		 programName, XDisplayName (name));
	return FALSE;
    }

#ifdef DEBUG
    XSynchronize (dpy, TRUE);
#endif

    XSetErrorHandler (errorHandler);

    updateModifierMappings (d);

    d->setDisplayOption		 = setDisplayOption;
    d->setDisplayOptionForPlugin = setDisplayOptionForPlugin;

    d->initPluginForDisplay = initPluginForDisplay;
    d->finiPluginForDisplay = finiPluginForDisplay;

    d->handleEvent       = handleEvent;
    d->handleDamageEvent = handleDamageEvent;

    d->winTypeAtom    = XInternAtom (dpy, "_NET_WM_WINDOW_TYPE", 0);
    d->winDesktopAtom = XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", 0);
    d->winDockAtom    = XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_DOCK", 0);
    d->winToolbarAtom = XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_TOOLBAR", 0);
    d->winMenuAtom    = XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_MENU", 0);
    d->winUtilAtom    = XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_UTILITY", 0);
    d->winSplashAtom  = XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_SPLASH", 0);
    d->winDialogAtom  = XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_DIALOG", 0);
    d->winNormalAtom  = XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_NORMAL", 0);
    d->winOpacityAtom = XInternAtom (dpy, "_NET_WM_WINDOW_OPACITY", 0);
    d->winActiveAtom  = XInternAtom (dpy, "_NET_ACTIVE_WINDOW", 0);

    d->wmStateAtom	  = XInternAtom (dpy, "WM_STATE", 0);
    d->wmDeleteWindowAtom = XInternAtom (dpy, "WM_DELETE_WINDOW", 0);

    d->xBackgroundAtom[0] = XInternAtom (dpy, "_XSETROOT_ID", 0);
    d->xBackgroundAtom[1] = XInternAtom (dpy, "_XROOTPMAP_ID", 0);

    if (testMode)
    {
	d->compositeOpcode = MAXSHORT;
	d->compositeEvent  = MAXSHORT;
	d->compositeError  = MAXSHORT;

	d->damageEvent = MAXSHORT;
	d->damageError = MAXSHORT;
    }
    else
    {
	int compositeMajor, compositeMinor;

	if (!XQueryExtension (dpy,
			      COMPOSITE_NAME,
			      &d->compositeOpcode,
			      &d->compositeEvent,
			      &d->compositeError))
	{
	    fprintf (stderr, "%s: No composite extension\n", programName);
	    return FALSE;
	}

	XCompositeQueryVersion (dpy, &compositeMajor, &compositeMinor);
	if (compositeMajor == 0 && compositeMinor < 2)
	{
	    fprintf (stderr, "%s: Old composite extension\n", programName);
	    return FALSE;
	}

	if (!XDamageQueryExtension (dpy, &d->damageEvent, &d->damageError))
	{
	    fprintf (stderr, "%s: No damage extension\n", programName);
	    return FALSE;
	}
    }

    d->shapeExtension = XShapeQueryExtension (dpy,
					      &d->shapeEvent,
					      &d->shapeError);

    compDisplays = d;

    if (testMode)
    {
	addScreen (d, 0);
    }
    else
    {
	XGrabServer (dpy);

	for (i = 0; i < ScreenCount (dpy); i++)
	{
	    redirectFailed = 0;
	    XCompositeRedirectSubwindows (dpy, XRootWindow (dpy, i),
					  CompositeRedirectManual);
	    XSync (dpy, FALSE);
	    if (redirectFailed)
	    {
		fprintf (stderr, "%s: Another composite manager is already "
			 "running on screen: %d\n", programName, i);
	    }
	    else
	    {
		if (!addScreen (d, i))
		{
		    fprintf (stderr, "%s: Failed to manage screen: %d\n",
			     programName, i);
		}
	    }
	}

	XUngrabServer (dpy);
    }

    if (!d->screens)
    {
	fprintf (stderr, "%s: No managable screens found on display %s\n",
		 programName, XDisplayName (name));
	return FALSE;
    }

    return TRUE;
}

CompScreen *
findScreenAtDisplay (CompDisplay *d,
		     Window      root)
{
    CompScreen *s;

    for (s = d->screens; s; s = s->next)
    {
	if (s->root == root)
	    return s;
    }

    return 0;
}

CompWindow *
findWindowAtDisplay (CompDisplay *d,
		     Window      id)
{
    CompScreen *s;
    CompWindow *w;

    for (s = d->screens; s; s = s->next)
    {
	w = findWindowAtScreen (s, id);
	if (w)
	    return w;
    }

    return 0;
}
