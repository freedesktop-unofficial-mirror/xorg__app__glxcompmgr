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

#ifndef _COMP_H
#define _COMP_H

#include <sys/time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xdamage.h>

#include <GL/gl.h>
#include <GL/glx.h>

#include <region.h>

typedef struct _CompPlugin  CompPlugin;
typedef struct _CompDisplay CompDisplay;
typedef struct _CompScreen  CompScreen;
typedef struct _CompWindow  CompWindow;
typedef struct _CompTexture CompTexture;

/* virtual modifiers */

#define CompModAlt        0
#define CompModMeta       1
#define CompModSuper      2
#define CompModHyper      3
#define CompModModeSwitch 4
#define CompModNumLock    5
#define CompModScrollLock 6
#define CompModNum        7

#define CompAltMask        (1 << 16)
#define CompMetaMask       (1 << 17)
#define CompSuperMask      (1 << 18)
#define CompHyperMask      (1 << 19)
#define CompModeSwitchMask (1 << 20)
#define CompNumLockMask    (1 << 21)
#define CompScrollLockMask (1 << 22)

#define CompPressMask      (1 << 23)
#define CompReleaseMask    (1 << 24)

#define CompNoMask         (1 << 25)

#define OPAQUE 0xffff

extern char       *programName;
extern char       **programArgv;
extern int        programArgc;
extern char       *backgroundImage;
extern char       *windowImage;
extern REGION     emptyRegion;
extern REGION     infiniteRegion;
extern GLushort   defaultColor[4];
extern Window     currentRoot;
extern Bool       testMode;
extern Bool       restartSignal;
extern CompWindow *lastFoundWindow;
extern CompWindow *lastDamagedWindow;

extern int  defaultRefreshRate;
extern char *defaultTextureFilter;

#define RESTRICT_VALUE(value, min, max)				     \
    (((value) < (min)) ? (min): ((value) > (max)) ? (max) : (value))

#define MOD(a,b) ((a) < 0 ? ((b) - ((-(a) - 1) % (b))) - 1 : (a) % (b))


/* privates.h */

#define WRAP(priv, real, func, wrapFunc) \
    (priv)->func = (real)->func;	 \
    (real)->func = (wrapFunc)

#define UNWRAP(priv, real, func) \
    (real)->func = (priv)->func

typedef union _CompPrivate {
    void	  *ptr;
    long	  val;
    unsigned long uval;
    void	  *(*fptr) (void);
} CompPrivate;

typedef int (*ReallocPrivatesProc) (int size, void *closure);

int
allocatePrivateIndex (int		  *len,
		      char		  **indices,
		      ReallocPrivatesProc reallocProc,
		      void		  *closure);

void
freePrivateIndex (int  len,
		  char *indices,
		  int  index);


/* readpng.c */

Bool
readPng (const char   *filename,
	 char	      **data,
	 unsigned int *width,
	 unsigned int *height);


/* option.c */

typedef enum {
    CompOptionTypeBool,
    CompOptionTypeInt,
    CompOptionTypeFloat,
    CompOptionTypeString,
    CompOptionTypeColor,
    CompOptionTypeBinding,
    CompOptionTypeList
} CompOptionType;

typedef enum {
    CompBindingTypeKey,
    CompBindingTypeButton
} CompBindingType;

typedef struct _CompKeyBinding {
    int		 keycode;
    unsigned int modifiers;
} CompKeyBinding;

typedef struct _CompButtonBinding {
    int		 button;
    unsigned int modifiers;
} CompButtonBinding;

typedef struct {
    CompBindingType type;
    union {
	CompKeyBinding    key;
	CompButtonBinding button;
    } u;
} CompBinding;

typedef union _CompOptionValue CompOptionValue;

typedef struct {
    CompOptionType  type;
    CompOptionValue *value;
    int		    nValue;
} CompListValue;

union _CompOptionValue {
    Bool	   b;
    int		   i;
    float	   f;
    char	   *s;
    unsigned short c[4];
    CompBinding    bind;
    CompListValue  list;
};

typedef struct _CompOptionIntRestriction {
    int min;
    int max;
} CompOptionIntRestriction;

typedef struct _CompOptionFloatRestriction {
    float min;
    float max;
    float precision;
} CompOptionFloatRestriction;

typedef struct _CompOptionStringRestriction {
    char **string;
    int  nString;
} CompOptionStringRestriction;

typedef union {
    CompOptionIntRestriction    i;
    CompOptionFloatRestriction  f;
    CompOptionStringRestriction s;
} CompOptionRestriction;

typedef struct _CompOption {
    char		  *name;
    char		  *shortDesc;
    char		  *longDesc;
    CompOptionType	  type;
    CompOptionValue	  value;
    CompOptionRestriction rest;
} CompOption;

typedef CompOption *(*DisplayOptionsProc) (CompDisplay *display, int *count);
typedef CompOption *(*ScreenOptionsProc) (CompScreen *screen, int *count);

CompOption *
compFindOption (CompOption *option,
		int	    nOption,
		char	    *name,
		int	    *index);

Bool
compSetBoolOption (CompOption	   *option,
		   CompOptionValue *value);

Bool
compSetIntOption (CompOption	  *option,
		  CompOptionValue *value);

Bool
compSetFloatOption (CompOption	    *option,
		    CompOptionValue *value);

Bool
compSetStringOption (CompOption	     *option,
		     CompOptionValue *value);

Bool
compSetColorOption (CompOption	    *option,
		    CompOptionValue *value);

Bool
compSetBindingOption (CompOption      *option,
		      CompOptionValue *value);

Bool
compSetOptionList (CompOption      *option,
		   CompOptionValue *value);


/* display.c */

#define COMP_DISPLAY_OPTION_ACTIVE_PLUGINS 0
#define COMP_DISPLAY_OPTION_TEXTURE_FILTER 1
#define COMP_DISPLAY_OPTION_NUM            2

typedef CompOption *(*GetDisplayOptionsProc) (CompDisplay *display,
					      int	  *count);
typedef Bool (*SetDisplayOptionProc) (CompDisplay     *display,
				      char	      *name,
				      CompOptionValue *value);
typedef Bool (*SetDisplayOptionForPluginProc) (CompDisplay     *display,
					       char	       *plugin,
					       char	       *name,
					       CompOptionValue *value);

typedef Bool (*InitPluginForDisplayProc) (CompPlugin  *plugin,
					  CompDisplay *display);

typedef void (*FiniPluginForDisplayProc) (CompPlugin  *plugin,
					  CompDisplay *display);

typedef void (*HandleEventProc) (CompDisplay *display,
				 XEvent	     *event);

typedef Bool (*CallBackProc) (void *closure);

struct _CompDisplay {
    Display    *display;
    CompScreen *screens;

    char *screenPrivateIndices;
    int  screenPrivateLen;

    int compositeEvent, compositeError, compositeOpcode;
    int damageEvent, damageError;

    Bool shapeExtension;
    int  shapeEvent, shapeError;

    Atom winTypeAtom;
    Atom winDesktopAtom;
    Atom winDockAtom;
    Atom winToolbarAtom;
    Atom winMenuAtom;
    Atom winUtilAtom;
    Atom winSplashAtom;
    Atom winDialogAtom;
    Atom winNormalAtom;
    Atom winOpacityAtom;
    Atom winActiveAtom;

    Atom wmStateAtom;
    Atom wmDeleteWindowAtom;

    Atom xBackgroundAtom[2];

    GLenum textureFilter;

    unsigned int modMask[CompModNum];

    CompOption opt[COMP_DISPLAY_OPTION_NUM];

    CompOptionValue plugin;
    Bool	    dirtyPluginList;

    SetDisplayOptionProc	  setDisplayOption;
    SetDisplayOptionForPluginProc setDisplayOptionForPlugin;

    InitPluginForDisplayProc initPluginForDisplay;
    FiniPluginForDisplayProc finiPluginForDisplay;

    HandleEventProc handleEvent;

    CompPrivate *privates;
};

extern CompDisplay *compDisplays;

int
allocateDisplayPrivateIndex (void);

void
freeDisplayPrivateIndex (int index);

CompOption *
compGetDisplayOptions (CompDisplay *display,
		       int	   *count);


typedef int CompTimeoutHandle;

CompTimeoutHandle
compAddTimeout (int	     time,
		CallBackProc callBack,
		void	     *closure);

void
compRemoveTimeout (CompTimeoutHandle handle);

int
compCheckForError (void);

Bool
addDisplay (char *name,
	    char **plugin,
	    int  nPlugin);

CompScreen *
findScreenAtDisplay (CompDisplay *d,
		     Window      root);

CompWindow *
findWindowAtDisplay (CompDisplay *display,
		     Window      id);

unsigned int
virtualToRealModMask (CompDisplay  *d,
		      unsigned int modMask);

void
updateModifierMappings (CompDisplay *d);

void
eventLoop (void);


/* event.c */

#define EV_BUTTON(opt, event)						\
    ((opt)->value.bind.type == CompBindingTypeButton &&			\
     (opt)->value.bind.u.button.button == (event)->xbutton.button &&	\
     ((opt)->value.bind.u.button.modifiers & (event)->xbutton.state) == \
     (opt)->value.bind.u.button.modifiers)

#define EV_KEY(opt, event)					  \
    ((opt)->value.bind.type == CompBindingTypeKey &&		  \
     (opt)->value.bind.u.key.keycode == (event)->xkey.keycode &&  \
     ((opt)->value.bind.u.key.modifiers & (event)->xkey.state) == \
     (opt)->value.bind.u.key.modifiers)

void
handleEvent (CompDisplay *display,
	     XEvent      *event);

void
handleDamageEvent (CompWindow	      *window,
		   XDamageNotifyEvent *event);


/* paint.c */

#define MULTIPLY_USHORT(us1, us2)		 \
    (((GLuint) (us1) * (GLuint) (us2)) / 0xffff)

/* 0.5 + 0.5 * tan (FOV) */
#define BASE_Z_TRANSLATE 1.366025404f

typedef struct _ScreenPaintAttrib {
    GLfloat xRotate;
    GLfloat yRotate;
    GLfloat vRotate;
    GLfloat xTranslate;
    GLfloat yTranslate;
    GLfloat zTranslate;
} ScreenPaintAttrib;

typedef struct _WindowPaintAttrib {
    GLushort opacity;
    GLfloat  xTranslate;
    GLfloat  yTranslate;
    GLfloat  xScale;
    GLfloat  yScale;
} WindowPaintAttrib;

extern ScreenPaintAttrib defaultScreenPaintAttrib;
extern WindowPaintAttrib defaultWindowPaintAttrib;

typedef struct _CompMatrix {
    float xx; float yx;
    float xy; float yy;
    float x0; float y0;
} CompMatrix;

#define COMP_TEX_COORD_X(m, vx, vy) ((m)->xx * (vx) + (m)->yx * (vy) + (m)->x0)
#define COMP_TEX_COORD_Y(m, vx, vy) ((m)->xy * (vx) + (m)->yy * (vy) + (m)->y0)

typedef void (*PreparePaintScreenProc) (CompScreen *screen,
					int	   msSinceLastPaint);

typedef void (*DonePaintScreenProc) (CompScreen *screen);

#define PAINT_SCREEN_REGION_MASK		   (1 << 0)
#define PAINT_SCREEN_FULL_MASK			   (1 << 1)
#define PAINT_SCREEN_TRANSFORMED_MASK		   (1 << 2)
#define PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS_MASK (1 << 3)

typedef Bool (*PaintScreenProc) (CompScreen		 *screen,
				 const ScreenPaintAttrib *sAttrib,
				 const WindowPaintAttrib *wAttrib,
				 Region			 region,
				 unsigned int		 mask);

typedef void (*PaintTransformedScreenProc) (CompScreen		    *screen,
					    const ScreenPaintAttrib *sAttrib,
					    const WindowPaintAttrib *wAttrib,
					    unsigned int	    mask);


#define PAINT_WINDOW_SOLID_MASK			(1 << 0)
#define PAINT_WINDOW_TRANSLUCENT_MASK		(1 << 1)
#define PAINT_WINDOW_TRANSFORMED_MASK           (1 << 2)
#define PAINT_WINDOW_ON_TRANSFORMED_SCREEN_MASK (1 << 3)

typedef Bool (*PaintWindowProc) (CompWindow		 *window,
				 const WindowPaintAttrib *attrib,
				 Region			 region,
				 unsigned int		 mask);

typedef void (*AddWindowGeometryProc) (CompWindow *window,
				       CompMatrix *matrix,
				       int	  nMatrix,
				       Region	  region,
				       Region	  clip);

typedef void (*DrawWindowGeometryProc) (CompWindow *window);

#define PAINT_BACKGROUND_ON_TRANSFORMED_SCREEN_MASK (1 << 0)
#define PAINT_BACKGROUND_WITH_STENCIL_MASK          (1 << 1)

typedef void (*PaintBackgroundProc) (CompScreen   *screen,
				     Region	  region,
				     unsigned int mask);


void
preparePaintScreen (CompScreen *screen,
		    int	       msSinceLastPaint);

void
donePaintScreen (CompScreen *screen);

void
paintTransformedScreen (CompScreen		*screen,
			const ScreenPaintAttrib *sAttrib,
			const WindowPaintAttrib *wAttrib,
			unsigned int	        mask);

Bool
paintScreen (CompScreen		     *screen,
	     const ScreenPaintAttrib *sAttrib,
	     const WindowPaintAttrib *wAttrib,
	     Region		     region,
	     unsigned int	     mask);

Bool
moreWindowVertices (CompWindow *w,
		    int        newSize);

Bool
moreWindowIndices (CompWindow *w,
		   int        newSize);

void
addWindowGeometry (CompWindow *w,
		   CompMatrix *matrix,
		   int	      nMatrix,
		   Region     region,
		   Region     clip);

void
drawWindowGeometry (CompWindow *w);

Bool
paintWindow (CompWindow		     *w,
	     const WindowPaintAttrib *attrib,
	     Region		     region,
	     unsigned int	     mask);

void
paintBackground (CompScreen   *screen,
		 Region	      region,
		 unsigned int mask);


/* texture.c */

#define POWER_OF_TWO(v) ((v & (v - 1)) == 0)

typedef enum {
    COMP_TEXTURE_FILTER_FAST,
    COMP_TEXTURE_FILTER_GOOD
} CompTextureFilter;

struct _CompTexture {
    GLuint            name;
    GLenum            target;
    GLfloat	      dx, dy;
    GLXPixmap	      pixmap;
    CompTextureFilter filter;
    CompMatrix        matrix;
};

void
initTexture (CompScreen  *screen,
	     CompTexture *texture);

void
finiTexture (CompScreen  *screen,
	     CompTexture *texture);

Bool
readImageToTexture (CompScreen   *screen,
		    CompTexture  *texture,
		    char	 *imageFileName,
		    unsigned int *width,
		    unsigned int *height);

Bool
bindPixmapToTexture (CompScreen  *screen,
		     CompTexture *texture,
		     Pixmap	 pixmap,
		     int	 width,
		     int	 height,
		     int	 depth);

void
releasePixmapFromTexture (CompScreen  *screen,
			  CompTexture *texture);

void
enableTexture (CompScreen        *screen,
	       CompTexture	 *texture,
	       CompTextureFilter filter);

void
disableTexture (CompTexture *texture);


/* screen.c */

#define COMP_SCREEN_OPTION_REFRESH_RATE 0
#define COMP_SCREEN_OPTION_NUM          1

typedef void (*FuncPtr) (void);
typedef FuncPtr (*GLXGetProcAddressProc) (const GLubyte *procName);

#ifndef GLX_EXT_render_texture
#define GLX_TEXTURE_TARGET_EXT              0x6001
#define GLX_TEXTURE_2D_EXT                  0x6002
#define GLX_TEXTURE_RECTANGLE_EXT           0x6003
#define GLX_NO_TEXTURE_EXT                  0x6004
#define GLX_FRONT_LEFT_EXT                  0x6005
#endif

typedef Bool    (*GLXBindTexImageProc)    (Display	 *display,
					   GLXDrawable	 drawable,
					   int		 buffer);
typedef Bool    (*GLXReleaseTexImageProc) (Display	 *display,
					   GLXDrawable	 drawable,
					   int		 buffer);
typedef Bool    (*GLXQueryDrawableProc)   (Display	 *display,
					   GLXDrawable	 drawable,
					   int		 attribute,
					   unsigned int  *value);

typedef void (*GLActiveTextureProc) (GLenum texture);
typedef void (*GLClientActiveTextureProc) (GLenum texture);


#define MAX_DEPTH 32

typedef CompOption *(*GetScreenOptionsProc) (CompScreen *screen,
					     int	*count);
typedef Bool (*SetScreenOptionProc) (CompScreen      *screen,
				     char	     *name,
				     CompOptionValue *value);
typedef Bool (*SetScreenOptionForPluginProc) (CompScreen      *screen,
					      char	      *plugin,
					      char	      *name,
					      CompOptionValue *value);

typedef Bool (*InitPluginForScreenProc) (CompPlugin *plugin,
					 CompScreen *screen);

typedef void (*FiniPluginForScreenProc) (CompPlugin *plugin,
					 CompScreen *screen);

typedef void (*InvisibleWindowMoveProc) (CompWindow *w,
					 int        dx,
					 int        dy);

typedef Bool (*DamageWindowRectProc) (CompWindow *w,
				      Bool       initial,
				      BoxPtr     rect);

typedef Bool (*DamageWindowRegionProc) (CompWindow *w,
					Region     region);

typedef struct _CompKeyGrab {
    int		 keycode;
    unsigned int modifiers;
    int		 count;
} CompKeyGrab;

typedef struct _CompButtonGrab {
    int		 button;
    unsigned int modifiers;
    int		 count;
} CompButtonGrab;

typedef struct _CompGrab {
    Bool   active;
    Cursor cursor;
} CompGrab;

struct _CompScreen {
    CompScreen  *next;
    CompDisplay *display;
    CompWindow	*windows;
    CompWindow	*reverseWindows;

    char *windowPrivateIndices;
    int  windowPrivateLen;

    Colormap	      colormap;
    int		      screenNum;
    int		      width;
    int		      height;
    REGION	      region;
    Region	      damage;
    Bool	      allDamaged;
    Window	      root;
    Window	      fake[2];
    XWindowAttributes attrib;
    Window	      grabWindow;
    XVisualInfo       *glxPixmapVisuals[MAX_DEPTH + 1];
    int		      textureRectangle;
    int		      textureNonPowerOfTwo;
    int		      textureEnvCombine;
    int		      maxTextureUnits;
    Cursor	      invisibleCursor;
    XRectangle        *exposeRects;
    int		      sizeExpose;
    int		      nExpose;
    CompTexture       backgroundTexture;
    unsigned int      pendingDestroys;
    int		      desktopWindowCount;
    KeyCode	      escapeKeyCode;

    CompButtonGrab *buttonGrab;
    int		   nButtonGrab;
    CompKeyGrab    *keyGrab;
    int		   nKeyGrab;

    CompGrab *grabs;
    int	     grabSize;
    int	     maxGrab;

    int		   rasterX;
    int		   rasterY;
    struct timeval lastRedraw;
    int		   nextRedraw;
    int		   redrawTime;

    GLint stencilRef;

    Window activeWindow;

    GLXGetProcAddressProc  getProcAddress;
    GLXBindTexImageProc    bindTexImage;
    GLXReleaseTexImageProc releaseTexImage;
    GLXQueryDrawableProc   queryDrawable;

    GLActiveTextureProc       activeTexture;
    GLClientActiveTextureProc clientActiveTexture;

    GLXContext ctx;

    CompOption opt[COMP_SCREEN_OPTION_NUM];

    SetScreenOptionProc		 setScreenOption;
    SetScreenOptionForPluginProc setScreenOptionForPlugin;

    InitPluginForScreenProc initPluginForScreen;
    FiniPluginForScreenProc finiPluginForScreen;

    PreparePaintScreenProc     preparePaintScreen;
    DonePaintScreenProc	       donePaintScreen;
    PaintScreenProc	       paintScreen;
    PaintTransformedScreenProc paintTransformedScreen;
    PaintBackgroundProc        paintBackground;
    PaintWindowProc	       paintWindow;
    AddWindowGeometryProc      addWindowGeometry;
    DrawWindowGeometryProc     drawWindowGeometry;
    InvisibleWindowMoveProc    invisibleWindowMove;
    DamageWindowRectProc       damageWindowRect;
    DamageWindowRegionProc     damageWindowRegion;

    CompPrivate *privates;
};

int
allocateScreenPrivateIndex (CompDisplay *display);

void
freeScreenPrivateIndex (CompDisplay *display,
			int	    index);

CompOption *
compGetScreenOptions (CompScreen *screen,
		      int	 *count);

void
configureScreen (CompScreen	 *s,
		 XConfigureEvent *ce);

void
updateScreenBackground (CompScreen  *screen,
			CompTexture *texture);

Bool
addScreen (CompDisplay *display,
	   int	       screenNum);

void
damageScreenRegion (CompScreen *screen,
		    Region     region);

void
damageScreen (CompScreen *screen);

void
insertWindowIntoScreen (CompScreen *s,
			CompWindow *w,
			Window	   aboveId);

void
unhookWindowFromScreen (CompScreen *s,
			CompWindow *w);

CompWindow *
findWindowAtScreen (CompScreen *s,
		    Window     id);

CompWindow *
findClientWindowAtScreen (CompScreen *s,
			  Window     id);

int
pushScreenGrab (CompScreen *s,
		Cursor     cursor);

void
removeScreenGrab (CompScreen *s,
		  int	     index,
		  XPoint     *restorePointer);

Bool
addScreenBinding (CompScreen  *s,
		  CompBinding *binding);

void
removeScreenBinding (CompScreen  *s,
		     CompBinding *binding);

void
updatePassiveGrabs (CompScreen *s);


/* window.c */

#define WINDOW_INVISIBLE(w)		    \
    ((w)->attrib.map_state != IsViewable || \
     (!(w)->damaged)			 || \
     (w)->attrib.x + (w)->width  <= 0    || \
     (w)->attrib.y + (w)->height <= 0    || \
     (w)->attrib.x >= (w)->screen->width || \
     (w)->attrib.y >= (w)->screen->height)

typedef Bool (*InitPluginForWindowProc) (CompPlugin *plugin,
					 CompWindow *window);
typedef void (*FiniPluginForWindowProc) (CompPlugin *plugin,
					 CompWindow *window);

struct _CompWindow {
    CompScreen *screen;
    CompWindow *next;
    CompWindow *prev;

    int		      refcnt;
    Window	      id;
    Window	      client;
    XWindowAttributes attrib;
    Pixmap	      pixmap;
    CompTexture       texture;
    CompMatrix        matrix;
    Damage	      damage;
    Bool	      alpha;
    GLint	      width;
    GLint	      height;
    Region	      region;
    Region	      clip;
    Atom	      type;
    Bool	      invisible;
    GLushort	      opacity;
    Bool	      destroyed;
    Bool	      damaged;

    GLfloat  *vertices;
    int      vertexSize;
    GLushort *indices;
    int      indexSize;
    int      vCount;
    int      texUnits;

    CompPrivate *privates;
};

int
allocateWindowPrivateIndex (CompScreen *screen);

void
freeWindowPrivateIndex (CompScreen *screen,
			int	   index);


Atom
getWindowType (CompDisplay *display,
	       Window      id);

Window
getActiveWindow (CompDisplay *display,
		 Window      root);

unsigned short
getWindowOpacity (CompDisplay *display,
		  Window      id);

void
updateWindowRegion (CompWindow *w);

void
addWindow (CompScreen *screen,
	   Window     id,
	   Window     aboveId);

void
removeWindow (CompWindow *w);

void
destroyWindow (CompWindow *w);

void
mapWindow (CompWindow *w);

void
unmapWindow (CompWindow *w);

void
bindWindow (CompWindow *w);

void
releaseWindow (CompWindow *w);

void
configureWindow (CompWindow	 *w,
		 XConfigureEvent *ce);

void
circulateWindow (CompWindow	 *w,
		 XCirculateEvent *ce);

void
addWindowDamage (CompWindow *w);

Bool
damageWindowRect (CompWindow *w,
		  Bool       initial,
		  BoxPtr     rect);

Bool
damageWindowRegion (CompWindow *w,
		    Region     region);

void
invisibleWindowMove (CompWindow *w,
		     int        dx,
		     int        dy);


/* plugin.c */

typedef Bool (*InitPluginProc) (CompPlugin *plugin);
typedef void (*FiniPluginProc) (CompPlugin *plugin);

typedef struct _CompPluginVTable {
    char *name;
    char *shortDesc;
    char *longDesc;

    InitPluginProc init;
    FiniPluginProc fini;

    InitPluginForDisplayProc initDisplay;
    FiniPluginForDisplayProc finiDisplay;

    InitPluginForScreenProc initScreen;
    FiniPluginForScreenProc finiScreen;

    InitPluginForWindowProc initWindow;
    FiniPluginForWindowProc finiWindow;

    GetDisplayOptionsProc getDisplayOptions;
    SetDisplayOptionProc  setDisplayOption;
    GetScreenOptionsProc  getScreenOptions;
    SetScreenOptionProc   setScreenOption;
} CompPluginVTable;

typedef CompPluginVTable *(*PluginGetInfoProc) (void);

struct _CompPlugin {
    CompPlugin       *next;
    void	     *dlhand;
    CompPluginVTable *vTable;
};

CompPluginVTable *
getCompPluginInfo (void);

void
screenInitPlugins (CompScreen *s);

void
screenFiniPlugins (CompScreen *s);

void
windowInitPlugins (CompWindow *w);

void
windowFiniPlugins (CompWindow *w);

CompPlugin *
findActivePlugin (char *name);

CompPlugin *
loadPlugin (char *plugin);

void
unloadPlugin (CompPlugin *p);

Bool
pushPlugin (CompPlugin *p);

CompPlugin *
popPlugin (void);

#endif
