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
#include <math.h>
#include <dlfcn.h>
#include <string.h>

#include <comp.h>

#define NUM_OPTIONS(s) (sizeof ((s)->opt) / sizeof (CompOption))

static int
reallocScreenPrivate (int  size,
		      void *closure)
{
    CompDisplay *d = (CompDisplay *) closure;
    CompScreen  *s;
    void        *privates;

    for (s = d->screens; s; s = s->next)
    {
	privates = realloc (s->privates, size * sizeof (CompPrivate));
	if (!privates)
	    return FALSE;

	s->privates = (CompPrivate *) privates;
    }

    return TRUE;
}

int
allocateScreenPrivateIndex (CompDisplay *display)
{
    return allocatePrivateIndex (&display->screenPrivateLen,
				 &display->screenPrivateIndices,
				 reallocScreenPrivate,
				 (void *) display);
}

void
freeScreenPrivateIndex (CompDisplay *display,
			int	    index)
{
    freePrivateIndex (display->screenPrivateLen,
		      display->screenPrivateIndices,
		      index);
}

CompOption *
compGetScreenOptions (CompScreen *screen,
		      int	 *count)
{
    *count = NUM_OPTIONS (screen);
    return screen->opt;
}

static Bool
setScreenOption (CompScreen      *screen,
		 char	         *name,
		 CompOptionValue *value)
{
    CompOption *o;
    int	       index;

    o = compFindOption (screen->opt, NUM_OPTIONS (screen), name, &index);
    if (!o)
	return FALSE;

    switch (index) {
    case COMP_SCREEN_OPTION_REFRESH_RATE:
	if (compSetIntOption (o, value))
	{
	    screen->redrawTime = 1000 / o->value.i;
	    return TRUE;
	}
    default:
	break;
    }

    return FALSE;
}

static Bool
setScreenOptionForPlugin (CompScreen      *screen,
			  char	          *plugin,
			  char	          *name,
			  CompOptionValue *value)
{
    CompPlugin *p;

    p = findActivePlugin (plugin);
    if (p && p->vTable->setScreenOption)
	return (*p->vTable->setScreenOption) (screen, name, value);

    return FALSE;
}

static void
compScreenInitOptions (CompScreen *screen)
{
    CompOption *o;

    o = &screen->opt[COMP_SCREEN_OPTION_REFRESH_RATE];
    o->name       = "refresh_rate";
    o->shortDesc  = "Refresh Rate";
    o->longDesc   = "The rate at which the screen is redrawn (times/second)";
    o->type       = CompOptionTypeInt;
    o->value.i    = defaultRefreshRate;
    o->rest.i.min = 1;
    o->rest.i.max = 200;
}

static Bool
initPluginForScreen (CompPlugin *p,
		     CompScreen *s)
{
    if (p->vTable->initScreen)
	return (*p->vTable->initScreen) (p, s);

    return FALSE;
}

static void
finiPluginForScreen (CompPlugin *p,
		     CompScreen *s)
{
    if (p->vTable->finiScreen)
	(*p->vTable->finiScreen) (p, s);
}

static void
frustum (GLfloat left,
	 GLfloat right,
	 GLfloat bottom,
	 GLfloat top,
	 GLfloat nearval,
	 GLfloat farval)
{
   GLfloat x, y, a, b, c, d;
   GLfloat m[16];

   x = (2.0 * nearval) / (right - left);
   y = (2.0 * nearval) / (top - bottom);
   a = (right + left) / (right - left);
   b = (top + bottom) / (top - bottom);
   c = -(farval + nearval) / ( farval - nearval);
   d = -(2.0 * farval * nearval) / (farval - nearval);

#define M(row,col)  m[col*4+row]
   M(0,0) = x;     M(0,1) = 0.0F;  M(0,2) = a;      M(0,3) = 0.0F;
   M(1,0) = 0.0F;  M(1,1) = y;     M(1,2) = b;      M(1,3) = 0.0F;
   M(2,0) = 0.0F;  M(2,1) = 0.0F;  M(2,2) = c;      M(2,3) = d;
   M(3,0) = 0.0F;  M(3,1) = 0.0F;  M(3,2) = -1.0F;  M(3,3) = 0.0F;
#undef M

   glMultMatrixf (m);
}

static void
perspective (GLfloat fovy,
	     GLfloat aspect,
	     GLfloat zNear,
	     GLfloat zFar)
{
   GLfloat xmin, xmax, ymin, ymax;

   ymax = zNear * tan (fovy * M_PI / 360.0);
   ymin = -ymax;
   xmin = ymin * aspect;
   xmax = ymax * aspect;

   frustum (xmin, xmax, ymin, ymax, zNear, zFar);
}

static void
reshape (CompScreen *s,
	 int	    w,
	 int	    h)
{
    s->width  = w;
    s->height = h;

    glViewport (0, 0, w, h);
    glMatrixMode (GL_PROJECTION);
    glLoadIdentity ();
    perspective (60.0f, 1.0f, 0.1f, 100.0f);
    glMatrixMode (GL_MODELVIEW);

    s->region.rects = &s->region.extents;
    s->region.numRects = 1;
    s->region.extents.x1 = 0;
    s->region.extents.y1 = 0;
    s->region.extents.x2 = w;
    s->region.extents.y2 = h;
    s->region.size = 1;
}

void
configureScreen (CompScreen	 *s,
		 XConfigureEvent *ce)
{
    if (s->attrib.width  != ce->width ||
	s->attrib.height != ce->height)
    {
	s->attrib.width  = ce->width;
	s->attrib.height = ce->height;

	reshape (s, ce->width, ce->height);

	damageScreen (s);
    }
}

static FuncPtr
getProcAddress (CompScreen *s,
		const char *name)
{
    static void *dlhand = NULL;
    FuncPtr     funcPtr = NULL;

    if (s->getProcAddress)
	funcPtr = s->getProcAddress ((GLubyte *) name);

    if (!funcPtr)
    {
	if (!dlhand)
	    dlhand = dlopen (NULL, RTLD_LAZY);

	if (dlhand)
	{
	    dlerror ();
	    funcPtr = (FuncPtr) dlsym (dlhand, name);
	    if (dlerror () != NULL)
		funcPtr = NULL;
	}
    }

    return funcPtr;
}

void
updateScreenBackground (CompScreen  *screen,
			CompTexture *texture)
{
    Display	  *dpy = screen->display->display;
    Atom	  pixmapAtom, actualType;
    int		  actualFormat, i, status;
    unsigned int  width = 1, height = 1, depth = 0;
    unsigned long nItems;
    unsigned long bytesAfter;
    unsigned char *prop;
    Pixmap	  pixmap = 0;

    pixmapAtom = XInternAtom (dpy, "PIXMAP", FALSE);

    for (i = 0; pixmap == 0 && i < 2; i++)
    {
	status = XGetWindowProperty (dpy, screen->root,
				     screen->display->xBackgroundAtom[i],
				     0, 4, FALSE, AnyPropertyType,
				     &actualType, &actualFormat, &nItems,
				     &bytesAfter, &prop);

	if (status == Success && nItems && prop)
	{
	    if (actualType   == pixmapAtom &&
		actualFormat == 32         &&
		nItems	     == 1)
	    {
		Pixmap p;

		memcpy (&p, prop, 4);

		if (p)
		{
		    unsigned int ui;
		    int		 i;
		    Window	 w;

		    if (XGetGeometry (dpy, p, &w, &i, &i,
				      &width, &height, &ui, &depth))
		    {
			if (depth == screen->attrib.depth)
			    pixmap = p;
		    }
		}
	    }

	    XFree (prop);
	}
    }

    if (!testMode && pixmap)
    {
	if (pixmap == texture->pixmap)
	    return;

	finiTexture (screen, texture);
	initTexture (screen, texture);

	if (!bindPixmapToTexture (screen, texture, pixmap,
				  width, height, depth))
	{
	    fprintf (stderr, "%s: Couldn't bind background pixmap 0x%x to "
		     "texture\n", programName, (int) pixmap);
	}
    }
    else
    {
	finiTexture (screen, texture);
	initTexture (screen, texture);
    }

    if (!texture->name)
	readImageToTexture (screen, texture, backgroundImage, &width, &height);

    if (texture->target == GL_TEXTURE_2D)
    {
	glBindTexture (texture->target, texture->name);
	glTexParameteri (texture->target, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri (texture->target, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glBindTexture (texture->target, 0);
    }

    screen->backgroundWidth  = width;
    screen->backgroundHeight = height;
}

Bool
addScreen (CompDisplay *display,
	   int	       screenNum)
{
    CompScreen   *s;
    Display	 *dpy = display->display;
    static char  data = 0;
    XColor	 black, dummy;
    Pixmap	 bitmap;
    XVisualInfo  templ;
    XVisualInfo  *visinfo;
    VisualID     visualIDs[MAX_DEPTH + 1];
    Window	 rootReturn, parentReturn;
    Window	 *children;
    unsigned int nchildren;
    int		 defaultDepth, nvisinfo, value, i;
    const char   *glxExtensions, *glExtensions;
    GLint	 stencilBits;

    s = malloc (sizeof (CompScreen));
    if (!s)
	return FALSE;

    s->windowPrivateIndices = 0;
    s->windowPrivateLen     = 0;

    if (display->screenPrivateLen)
    {
	s->privates = malloc (display->screenPrivateLen *
			      sizeof (CompPrivate));
	if (!s->privates)
	{
	    free (s);
	    return FALSE;
	}
    }
    else
	s->privates = 0;

    compScreenInitOptions (s);

    s->redrawTime = 1000 / s->opt[COMP_SCREEN_OPTION_REFRESH_RATE].value.i;

    s->display = display;

    s->damage = XCreateRegion ();
    if (!s->damage)
	return FALSE;

    s->buttonGrab  = 0;
    s->nButtonGrab = 0;
    s->keyGrab     = 0;
    s->nKeyGrab    = 0;

    s->grabs    = 0;
    s->grabSize = 0;
    s->maxGrab  = 0;

    s->pendingDestroys = 0;

    s->screenNum = screenNum;
    s->colormap  = DefaultColormap (dpy, screenNum);
    s->root	 = XRootWindow (dpy, screenNum);

    if (testMode)
    {
	XSetWindowAttributes attrib;
	XWMHints	     *wmHints;
	XSizeHints	     *normalHints;
	XClassHint	     *classHint;
	int		     glx_attrib[] = {
	    GLX_RGBA,
	    GLX_RED_SIZE, 1,
	    GLX_STENCIL_SIZE, 2,
	    GLX_DOUBLEBUFFER,
	    None
	};

	visinfo = glXChooseVisual (dpy, screenNum, glx_attrib);
	if (!visinfo)
	{
	    int glx_attrib2[] = {
		GLX_RGBA,
		GLX_RED_SIZE, 1,
		GLX_DOUBLEBUFFER,
		None
	    };

	    visinfo = glXChooseVisual (dpy, screenNum, glx_attrib2);
	    if (!visinfo)
	    {
		fprintf (stderr, "%s: Couldn't find a double buffered "
			 "RGB visual.\n", programName);
		return FALSE;
	    }
	}

	attrib.colormap = XCreateColormap (dpy, s->root, visinfo->visual,
					   AllocNone);

	normalHints = XAllocSizeHints ();
	normalHints->flags = 0;
	normalHints->x = 0;
	normalHints->y = 0;
	normalHints->width = 800;
	normalHints->height = 600;

	classHint = XAllocClassHint ();
	classHint->res_name = "glxcompmgr";
	classHint->res_class = "Glxcompmgr";

	wmHints = XAllocWMHints ();
	wmHints->flags = InputHint;
	wmHints->input = TRUE;

	s->root = XCreateWindow (dpy, s->root, 0, 0,
				 normalHints->width, normalHints->height, 0,
				 visinfo->depth, InputOutput, visinfo->visual,
				 CWColormap, &attrib);

	XSetWMProtocols (dpy, s->root, &display->wmDeleteWindowAtom, 1);

	XmbSetWMProperties (dpy, s->root,
			    "glxcompmgr - Test mode", "glxcompmgr",
			    programArgv, programArgc,
			    normalHints, wmHints, classHint);

	s->fake[0] = XCreateWindow (dpy, s->root, 64, 32, 1, 1, 0,
				    visinfo->depth, InputOutput,
				    visinfo->visual,
				    CWColormap, &attrib);

	s->fake[1] = XCreateWindow (dpy, s->root, 256, 256, 1, 1, 0,
				    visinfo->depth, InputOutput,
				    visinfo->visual,
				    CWColormap, &attrib);

	XMapWindow (dpy, s->root);

	XFree (wmHints);
	XFree (classHint);
	XFree (normalHints);
    }
    else
	s->fake[0] = s->fake[1] = 0;

    s->escapeKeyCode = XKeysymToKeycode (display->display,
					 XStringToKeysym ("Escape"));

    s->allDamaged  = TRUE;
    s->next	   = 0;
    s->exposeRects = 0;
    s->sizeExpose  = 0;
    s->nExpose     = 0;

    s->rasterX = 0;
    s->rasterY = 0;

    s->windows = 0;
    s->reverseWindows = 0;

    s->stencilRef = 0x1;

    s->nextRedraw = 0;

    gettimeofday (&s->lastRedraw, 0);

    s->setScreenOption	        = setScreenOption;
    s->setScreenOptionForPlugin = setScreenOptionForPlugin;

    s->initPluginForScreen = initPluginForScreen;
    s->finiPluginForScreen = finiPluginForScreen;

    s->preparePaintScreen     = preparePaintScreen;
    s->donePaintScreen        = donePaintScreen;
    s->paintScreen	      = paintScreen;
    s->paintTransformedScreen = paintTransformedScreen;
    s->paintBackground        = paintBackground;
    s->paintWindow            = paintWindow;
    s->invisibleWindowMove    = invisibleWindowMove;

    s->getProcAddress = 0;

    if (s->root)
    {
	XSetWindowAttributes attrib;

	attrib.override_redirect = 1;
	s->grabWindow = XCreateWindow (dpy, s->root, -100, -100, 1, 1, 0,
				       CopyFromParent, CopyFromParent,
				       CopyFromParent, CWOverrideRedirect,
				       &attrib);

	XMapWindow (dpy, s->grabWindow);
    }

    if (!XGetWindowAttributes (dpy, s->root, &s->attrib))
	return FALSE;

    s->activeWindow = None;

    templ.visualid = XVisualIDFromVisual (s->attrib.visual);

    visinfo = XGetVisualInfo (dpy, VisualIDMask, &templ, &nvisinfo);
    if (!nvisinfo)
    {
	fprintf (stderr, "%s: Couldn't get visual info for default visual\n",
		 programName);
	return FALSE;
    }

    defaultDepth = visinfo->depth;

    if (!XAllocNamedColor (dpy, s->colormap, "black", &black, &dummy))
    {
	fprintf (stderr, "%s: Couldn't allocate color\n", programName);
	return FALSE;
    }

    bitmap = XCreateBitmapFromData (dpy, s->root, &data, 1, 1);
    if (!bitmap)
    {
	fprintf (stderr, "%s: Couldn't create bitmap\n", programName);
	return FALSE;
    }

    s->invisibleCursor = XCreatePixmapCursor (dpy, bitmap, bitmap,
					      &black, &black, 0, 0);
    if (!s->invisibleCursor)
    {
	fprintf (stderr, "%s: Couldn't create invisible cursor\n",
		 programName);
	return FALSE;
    }

    XFreePixmap (dpy, bitmap);
    XFreeColors (dpy, s->colormap, &black.pixel, 1, 0);

    glXGetConfig (dpy, visinfo, GLX_USE_GL, &value);
    if (!value)
    {
	fprintf (stderr, "%s: Root visual is not a GL visual\n",
		 programName);
	return FALSE;
    }

    glXGetConfig (dpy, visinfo, GLX_DOUBLEBUFFER, &value);
    if (!value)
    {
	fprintf (stderr,
		 "%s: Root visual is not a double buffered GL visual\n",
		 programName);
	return FALSE;
    }

    s->ctx = glXCreateContext (dpy, visinfo, NULL, TRUE);
    if (!s->ctx)
    {
	fprintf (stderr, "%s: glXCreateContext failed\n", programName);
	return FALSE;
    }

    XFree (visinfo);

    /* we don't want to allocate back, stencil or depth buffers for pixmaps
       so lets see if we can find an approriate visual without these buffers */
    for (i = 0; i <= MAX_DEPTH; i++)
    {
	int j, db, stencil, depth;

	visualIDs[i] = 0;

	db	= MAXSHORT;
	stencil = MAXSHORT;
	depth	= MAXSHORT;

	templ.depth = i;

	visinfo = XGetVisualInfo (dpy, VisualDepthMask, &templ, &nvisinfo);
	for (j = 0; j < nvisinfo; j++)
	{
	    glXGetConfig (dpy, &visinfo[j], GLX_USE_GL, &value);
	    if (!value)
		continue;

	    glXGetConfig (dpy, &visinfo[j], GLX_DOUBLEBUFFER, &value);
	    if (value > db)
		continue;

	    db = value;
	    glXGetConfig (dpy, &visinfo[j], GLX_STENCIL_SIZE, &value);
	    if (value > stencil)
		continue;

	    stencil = value;
	    glXGetConfig (dpy, &visinfo[j], GLX_DEPTH_SIZE, &value);
	    if (value > depth)
		continue;

	    depth = value;
	    visualIDs[i] = visinfo[j].visualid;
	}

	if (nvisinfo)
	    XFree (visinfo);
    }

    /* create contexts for supported depths */
    for (i = 0; i <= MAX_DEPTH; i++)
    {
	templ.visualid = visualIDs[i];
	s->glxPixmapVisuals[i] = XGetVisualInfo (dpy,
						 VisualIDMask,
						 &templ,
						 &nvisinfo);
    }

    if (!s->glxPixmapVisuals[defaultDepth])
    {
	fprintf (stderr, "%s: No GL visual for default depth, "
		 "this isn't going to work.\n", programName);
	return FALSE;
    }

    glXMakeCurrent (dpy, s->root, s->ctx);
    currentRoot = s->root;

    glxExtensions = glXQueryExtensionsString (s->display->display, screenNum);
    if (!testMode && !strstr (glxExtensions, "GLX_MESA_render_texture"))
    {
	fprintf (stderr, "%s: GLX_MESA_render_texture is missing\n",
		 programName);
	return FALSE;
    }

    s->getProcAddress = (GLXGetProcAddressProc)
	getProcAddress (s, "glXGetProcAddressARB");
    s->bindTexImage = (GLXBindTexImageProc)
	getProcAddress (s, "glXBindTexImageMESA");
    s->releaseTexImage = (GLXReleaseTexImageProc)
	getProcAddress (s, "glXReleaseTexImageMESA");
    s->queryDrawable = (GLXQueryDrawableProc)
	getProcAddress (s, "glXQueryDrawable");

    if (!testMode && !s->bindTexImage)
    {
	fprintf (stderr, "%s: glXBindTexImageMESA is missing\n", programName);
	return FALSE;
    }

    if (!testMode && !s->releaseTexImage)
    {
	fprintf (stderr, "%s: glXReleaseTexImageMESA is missing\n",
		 programName);
	return FALSE;
    }

    if (!testMode && !s->queryDrawable)
    {
	fprintf (stderr, "%s: glXQueryDrawable is missing\n", programName);
	return FALSE;
    }

    glExtensions = (const char *) glGetString (GL_EXTENSIONS);
    if (strstr (glExtensions, "GL_NV_texture_rectangle")  ||
	strstr (glExtensions, "GL_EXT_texture_rectangle") ||
	strstr (glExtensions, "GL_ARB_texture_rectangle"))
	s->textureRectangle = 1;

    if (strstr (glExtensions, "GL_ARB_texture_non_power_of_two"))
	s->textureNonPowerOfTwo = 1;

    if (!(s->textureRectangle || s->textureNonPowerOfTwo))
    {
	fprintf (stderr, "%s: Support for non power of two textures missing\n",
		 programName);
	return FALSE;
    }

    initTexture (s, &s->backgroundTexture);

    s->desktopWindowCount = 0;

    glGetIntegerv (GL_STENCIL_BITS, &stencilBits);
    if (!stencilBits)
    {
	fprintf (stderr, "%s: No stencil buffer. Clipping of transformed "
		 "windows is not going to be correct when screen is "
		 "transformed.\n", programName);
    }

    glClearColor (0.0, 0.0, 0.0, 1.0);
    glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glEnable (GL_CULL_FACE);
    glDisable (GL_BLEND);
    glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnableClientState (GL_VERTEX_ARRAY);
    glEnableClientState (GL_TEXTURE_COORD_ARRAY);
    glColor4usv (defaultColor);

    s->activeWindow = getActiveWindow (display, s->root);

    reshape (s, s->attrib.width, s->attrib.height);

    s->next = display->screens;
    display->screens = s;

    screenInitPlugins (s);

    XSelectInput (dpy, s->root,
		  SubstructureNotifyMask |
		  StructureNotifyMask	 |
		  PropertyChangeMask	 |
		  ExposureMask		 |
		  ButtonPressMask	 |
		  ButtonReleaseMask	 |
		  ButtonMotionMask);

    XQueryTree (dpy, s->root,
		&rootReturn, &parentReturn,
		&children, &nchildren);

    for (i = 0; i < nchildren; i++)
    {
	if (children[i] == s->grabWindow)
	    continue;

	addWindow (s, children[i], i ? children[i - 1] : 0);
    }

    XFree (children);

    return TRUE;
}

void
damageScreenRegion (CompScreen *screen,
		    Region     region)
{
    if (screen->allDamaged)
	return;

    XUnionRegion (screen->damage, region, screen->damage);
}

void
damageScreen (CompScreen *s)
{
    s->allDamaged = TRUE;
}

CompWindow *
findWindowAtScreen (CompScreen *s,
		    Window     id)
{
    CompWindow *w;

    for (w = s->windows; w; w = w->next)
	if (w->id == id)
	    return w;

    return 0;
}

CompWindow *
findClientWindowAtScreen (CompScreen *s,
			  Window     id)
{
    CompWindow *w;

    for (w = s->windows; w; w = w->next)
	if (w->client == id)
	    return w;

    return 0;
}

void
insertWindowIntoScreen (CompScreen *s,
			CompWindow *w,
			Window	   aboveId)
{
    CompWindow *p;

    if (s->windows)
    {
	for (p = s->windows; p; p = p->next)
	{
	    if (p->id == aboveId)
	    {
		w->next = p->next;
		w->prev = p;
		if (p->next)
		    p->next->prev = w;
		p->next = w;

		if (s->reverseWindows == p)
		    s->reverseWindows = w;

		return;
	    }

	    if (!p->next)
	    {
		p->next = w;
		w->next = NULL;
		w->prev = p;

		s->reverseWindows = w;

		return;
	    }
	}
    }
    else
    {
	s->reverseWindows = s->windows = w;
	w->prev = w->next = NULL;
    }
}

void
unhookWindowFromScreen (CompScreen *s,
			CompWindow *w)
{
    CompWindow *p;

    if (s->windows == w)
    {
	s->windows = w->next;
	if (w->next)
	    w->next->prev = NULL;
    }

    if (s->reverseWindows == w)
    {
	s->reverseWindows = w->prev;
	if (w->prev)
	    w->prev->next = NULL;
    }

    for (p = s->windows; p; p = p->next)
    {
	if (p->next == w)
	{
	    p->next = w->next;
	    if (w->next)
		w->next->prev = p;

	    p->next = w->next;
	    break;
	}
    }
}

#define POINTER_GRAB_MASK (ButtonReleaseMask | \
			   ButtonPressMask   | \
			   PointerMotionMask)
int
pushScreenGrab (CompScreen *s,
		Cursor     cursor)
{
    if (s->maxGrab == 0)
    {
	int status;

	status = XGrabPointer (s->display->display, s->grabWindow, TRUE,
			       POINTER_GRAB_MASK,
			       GrabModeAsync, GrabModeAsync,
			       s->root, cursor,
			       CurrentTime);

	if (status == GrabSuccess)
	{
	    status = XGrabKeyboard (s->display->display,
				    s->grabWindow, TRUE,
				    GrabModeAsync, GrabModeAsync,
				    CurrentTime);
	    if (status != GrabSuccess)
	    {
		XUngrabPointer (s->display->display, CurrentTime);
		return 0;
	    }
	}
	else
	    return 0;
    }
    else
    {
	XChangeActivePointerGrab (s->display->display, POINTER_GRAB_MASK,
				  cursor, CurrentTime);
    }

    if (s->grabSize <= s->maxGrab)
    {
	s->grabs = realloc (s->grabs, sizeof (CompGrab) * (s->maxGrab + 1));
	if (!s->grabs)
	    return 0;

	s->grabSize = s->maxGrab + 1;
    }

    s->grabs[s->maxGrab].cursor = cursor;
    s->grabs[s->maxGrab].active = TRUE;

    s->maxGrab++;

    return s->maxGrab;
}

void
removeScreenGrab (CompScreen *s,
		  int	     index,
		  XPoint     *restorePointer)
{
    int maxGrab;

    index--;
    if (index < 0 || index >= s->maxGrab)
	abort ();

    s->grabs[index].cursor = None;
    s->grabs[index].active = FALSE;

    for (maxGrab = s->maxGrab; maxGrab; maxGrab--)
	if (s->grabs[maxGrab - 1].active)
	    break;

    if (maxGrab != s->maxGrab)
    {
	if (maxGrab)
	{
	    XChangeActivePointerGrab (s->display->display,
				      POINTER_GRAB_MASK,
				      s->grabs[s->maxGrab - 1].cursor,
				      CurrentTime);
	}
	else
	{
	    if (restorePointer)
		XWarpPointer (s->display->display, None, s->root, 0, 0, 0, 0,
			      restorePointer->x, restorePointer->y);

	    XUngrabPointer (s->display->display, CurrentTime);
	    XUngrabKeyboard (s->display->display, CurrentTime);
	}
	s->maxGrab = maxGrab;
    }
}

static Bool
addPassiveKeyGrab (CompScreen	  *s,
		   CompKeyBinding *key)
{
    CompKeyGrab  *keyGrab;
    unsigned int modifiers, mask;
    int          i;

    modifiers = key->modifiers & ~(CompPressMask | CompReleaseMask);
    if (modifiers == key->modifiers)
	return TRUE;

    for (i = 0; i < s->nKeyGrab; i++)
    {
	if (key->keycode == s->keyGrab[i].keycode &&
	    modifiers    == s->keyGrab[i].modifiers)
	{
	    s->keyGrab[i].count++;
	    return TRUE;
	}
    }

    keyGrab = realloc (s->keyGrab, sizeof (CompKeyGrab) * (s->nKeyGrab + 1));
    if (!keyGrab)
	return FALSE;

    s->keyGrab = keyGrab;

    mask = virtualToRealModMask (s->display, modifiers);
    if (!(mask & CompNoMask))
    {
	compCheckForError ();

	XGrabKey (s->display->display,
		  key->keycode,
		  mask,
		  s->root,
		  TRUE,
		  GrabModeAsync,
		  GrabModeAsync);

	XSync (s->display->display, FALSE);

	if (compCheckForError ())
	{

#ifdef DEBUG
	    KeySym keysym;
	    char   *keyname;

	    keysym = XKeycodeToKeysym (s->display->display,
				       key->keycode,
				       0);
	    keyname = XKeysymToString (keysym);

	    fprintf (stderr, "XGrabKey failed: %s 0x%x\n",
		     keyname, modifiers);
#endif

	    return FALSE;
	}
    }

    s->keyGrab[s->nKeyGrab].keycode   = key->keycode;
    s->keyGrab[s->nKeyGrab].modifiers = modifiers;
    s->keyGrab[s->nKeyGrab].count     = 1;

    s->nKeyGrab++;

    return TRUE;
}

static void
removePassiveKeyGrab (CompScreen     *s,
		      CompKeyBinding *key)
{
    unsigned int modifiers, mask;
    int          i;

    modifiers = key->modifiers & ~(CompPressMask | CompReleaseMask);
    if (modifiers == key->modifiers)
	return;

    for (i = 0; i < s->nKeyGrab; i++)
    {
	if (key->keycode == s->keyGrab[i].keycode &&
	    modifiers    == s->keyGrab[i].modifiers)
	{
	    s->keyGrab[i].count--;
	    if (s->keyGrab[i].count)
		return;

	    s->nKeyGrab--;
	    s->keyGrab = realloc (s->keyGrab,
				  sizeof (CompKeyGrab) * s->nKeyGrab);

	    mask = virtualToRealModMask (s->display, modifiers);
	    if (!(mask & CompNoMask))
	    {
		XUngrabKey (s->display->display,
			    key->keycode,
			    mask,
			    s->root);
	    }
	}
    }
}

static void
updatePassiveKeyGrabs (CompScreen *s)
{
    unsigned int mask;
    int          i;

    XUngrabKey (s->display->display, AnyKey, AnyModifier, s->root);

    for (i = 0; i < s->nKeyGrab; i++)
    {
	mask = virtualToRealModMask (s->display, s->keyGrab[i].modifiers);
	if (!(mask & CompNoMask))
	{
	    XGrabKey (s->display->display,
		      s->keyGrab[i].keycode,
		      mask,
		      s->root,
		      TRUE,
		      GrabModeAsync,
		      GrabModeAsync);
	}
    }
}

static Bool
addPassiveButtonGrab (CompScreen        *s,
		      CompButtonBinding *button)
{
    CompButtonGrab *buttonGrab;
    unsigned int   modifiers, mask;
    int            i;

    modifiers = button->modifiers & ~(CompPressMask | CompReleaseMask);
    if (modifiers == button->modifiers)
	return TRUE;

    for (i = 0; i < s->nButtonGrab; i++)
    {
	if (button->button  == s->buttonGrab[i].button &&
	    modifiers       == s->buttonGrab[i].modifiers)
	{
	    s->buttonGrab[i].count++;
	    return TRUE;
	}
    }

    buttonGrab = realloc (s->buttonGrab,
			  sizeof (CompButtonGrab) * (s->nButtonGrab + 1));
    if (!buttonGrab)
	return FALSE;

    s->buttonGrab = buttonGrab;

    mask = virtualToRealModMask (s->display, modifiers);
    if (!(mask & CompNoMask))
    {
	compCheckForError ();

	XGrabButton (s->display->display,
		     button->button,
		     mask,
		     s->root,
		     TRUE,
		     POINTER_GRAB_MASK,
		     GrabModeAsync,
		     GrabModeAsync,
		     None,
		     None);

	XSync (s->display->display, FALSE);

	if (compCheckForError ())
	{

#ifdef DEBUG
	    fprintf (stderr, "XGrabButton failed: %s 0x%x\n",
		     button->button, modifiers);
#endif

	return FALSE;
	}
    }

    s->buttonGrab[s->nButtonGrab].button    = button->button;
    s->buttonGrab[s->nButtonGrab].modifiers = modifiers;
    s->buttonGrab[s->nButtonGrab].count     = 1;

    s->nButtonGrab++;

    return TRUE;
}

static void
removePassiveButtonGrab (CompScreen        *s,
			 CompButtonBinding *button)
{
    unsigned int modifiers, mask;
    int          i;

    modifiers = button->modifiers & ~(CompPressMask | CompReleaseMask);
    if (modifiers == button->modifiers)
	return;

    for (i = 0; i < s->nButtonGrab; i++)
    {
	if (button->button == s->buttonGrab[i].button &&
	    modifiers      == s->buttonGrab[i].modifiers)
	{
	    s->buttonGrab[i].count--;
	    if (s->buttonGrab[i].count)
		return;

	    s->nButtonGrab--;
	    s->buttonGrab = realloc (s->buttonGrab,
				     sizeof (CompButtonGrab) * s->nButtonGrab);

	    mask = virtualToRealModMask (s->display, modifiers);
	    if (!(mask & CompNoMask))
	    {
		XUngrabButton (s->display->display,
			       button->button,
			       mask,
			       s->root);
	    }
	}
    }
}

static void
updatePassiveButtonGrabs (CompScreen *s)
{
    unsigned int mask;
    int          i;

    XUngrabButton (s->display->display, AnyButton, AnyModifier, s->root);

    for (i = 0; i < s->nButtonGrab; i++)
    {
	mask = virtualToRealModMask (s->display, s->buttonGrab[i].modifiers);
	if (!(mask & CompNoMask))
	{
	    XGrabButton (s->display->display,
			 s->buttonGrab[i].button,
			 mask,
			 s->root,
			 TRUE,
			 POINTER_GRAB_MASK,
			 GrabModeAsync,
			 GrabModeAsync,
			 None,
			 None);
	}
    }
}

Bool
addScreenBinding (CompScreen  *s,
		  CompBinding *binding)
{
    if (binding->type == CompBindingTypeKey)
	return addPassiveKeyGrab (s, &binding->u.key);
    else if (binding->type == CompBindingTypeButton)
	return addPassiveButtonGrab (s, &binding->u.button);

    return FALSE;
}

void
removeScreenBinding (CompScreen  *s,
		     CompBinding *binding)
{
    if (binding->type == CompBindingTypeKey)
	removePassiveKeyGrab (s, &binding->u.key);
    else if (binding->type == CompBindingTypeButton)
	removePassiveButtonGrab (s, &binding->u.button);
}

void
updatePassiveGrabs (CompScreen *s)
{
    updatePassiveButtonGrabs (s);
    updatePassiveKeyGrabs (s);
}
