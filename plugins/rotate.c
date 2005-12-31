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

#include <comp.h>

#define ROTATE_POINTER_INVERT_Y_DEFAULT FALSE

#define ROTATE_POINTER_SENSITIVITY_DEFAULT   1.0f
#define ROTATE_POINTER_SENSITIVITY_MIN       0.01f
#define ROTATE_POINTER_SENSITIVITY_MAX       100.0f
#define ROTATE_POINTER_SENSITIVITY_PRECISION 0.01f

#define ROTATE_POINTER_SENSITIVITY_FACTOR 0.05f

#define ROTATE_ACCELERATION_DEFAULT   4.0f
#define ROTATE_ACCELERATION_MIN       1.0f
#define ROTATE_ACCELERATION_MAX       20.0f
#define ROTATE_ACCELERATION_PRECISION 0.1f

#define ROTATE_INITIATE_BUTTON_DEFAULT    Button1
#define ROTATE_INITIATE_MODIFIERS_DEFAULT (CompPressMask | CompSuperMask)

#define ROTATE_TERMINATE_BUTTON_DEFAULT    Button1
#define ROTATE_TERMINATE_MODIFIERS_DEFAULT CompReleaseMask

#define ROTATE_LEFT_KEY_DEFAULT       "Left"
#define ROTATE_LEFT_MODIFIERS_DEFAULT		  \
    (CompPressMask | ControlMask | CompSuperMask)

#define ROTATE_RIGHT_KEY_DEFAULT       "Right"
#define ROTATE_RIGHT_MODIFIERS_DEFAULT		  \
    (CompPressMask | ControlMask | CompSuperMask)

#define ROTATE_SNAP_TOP_DEFAULT FALSE

static int displayPrivateIndex;

typedef struct _RotateDisplay {
    int		    screenPrivateIndex;
    HandleEventProc handleEvent;
} RotateDisplay;

#define ROTATE_SCREEN_OPTION_POINTER_INVERT_Y	 0
#define ROTATE_SCREEN_OPTION_POINTER_SENSITIVITY 1
#define ROTATE_SCREEN_OPTION_ACCELERATION        2
#define ROTATE_SCREEN_OPTION_INITIATE		 3
#define ROTATE_SCREEN_OPTION_TERMINATE		 4
#define ROTATE_SCREEN_OPTION_LEFT		 5
#define ROTATE_SCREEN_OPTION_RIGHT		 6
#define ROTATE_SCREEN_OPTION_SNAP_TOP		 7
#define ROTATE_SCREEN_OPTION_NUM		 8

typedef struct _RotateScreen {
    PreparePaintScreenProc preparePaintScreen;
    DonePaintScreenProc    donePaintScreen;
    PaintScreenProc        paintScreen;

    CompOption opt[ROTATE_SCREEN_OPTION_NUM];

    Bool  pointerInvertY;
    float pointerSensitivity;
    Bool  snapTop;
    float acceleration;

    int grabIndex;

    GLfloat xrot, xVelocity;
    GLfloat yrot, yVelocity;

    GLfloat baseXrot;

    Bool    moving;
    GLfloat moveTo;

    int    prevPointerX;
    int    prevPointerY;
    XPoint savedPointer;
    Bool   grabbed;
} RotateScreen;

#define GET_ROTATE_DISPLAY(d)				       \
    ((RotateDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define ROTATE_DISPLAY(d)		       \
    RotateDisplay *rd = GET_ROTATE_DISPLAY (d)

#define GET_ROTATE_SCREEN(s, rd)				   \
    ((RotateScreen *) (s)->privates[(rd)->screenPrivateIndex].ptr)

#define ROTATE_SCREEN(s)						      \
    RotateScreen *rs = GET_ROTATE_SCREEN (s, GET_ROTATE_DISPLAY (s->display))

#define NUM_OPTIONS(s) (sizeof ((s)->opt) / sizeof (CompOption))

static CompOption *
rotateGetScreenOptions (CompScreen *screen,
			int	   *count)
{
    ROTATE_SCREEN (screen);

    *count = NUM_OPTIONS (rs);
    return rs->opt;
}

static Bool
rotateSetScreenOption (CompScreen      *screen,
		       char	       *name,
		       CompOptionValue *value)
{
    CompOption *o;
    int	       index;

    ROTATE_SCREEN (screen);

    o = compFindOption (rs->opt, NUM_OPTIONS (rs), name, &index);
    if (!o)
	return FALSE;

    switch (index) {
    case ROTATE_SCREEN_OPTION_POINTER_INVERT_Y:
	if (compSetBoolOption (o, value))
	{
	    rs->pointerInvertY = o->value.b;
	    return TRUE;
	}
	break;
    case ROTATE_SCREEN_OPTION_POINTER_SENSITIVITY:
	if (compSetFloatOption (o, value))
	{
	    rs->pointerSensitivity = o->value.f *
		ROTATE_POINTER_SENSITIVITY_FACTOR;
	    return TRUE;
	}
	break;
    case ROTATE_SCREEN_OPTION_ACCELERATION:
	if (compSetFloatOption (o, value))
	{
	    rs->acceleration = o->value.f;
	    return TRUE;
	}
	break;
    case ROTATE_SCREEN_OPTION_INITIATE:
    case ROTATE_SCREEN_OPTION_LEFT:
    case ROTATE_SCREEN_OPTION_RIGHT:
	if (addScreenBinding (screen, &value->bind))
	{
	    removeScreenBinding (screen, &o->value.bind);

	    if (compSetBindingOption (o, value))
		return TRUE;
	}
	break;
    case ROTATE_SCREEN_OPTION_TERMINATE:
	if (compSetBindingOption (o, value))
	    return TRUE;
	break;
    case ROTATE_SCREEN_OPTION_SNAP_TOP:
	if (compSetBoolOption (o, value))
	{
	    rs->snapTop = o->value.b;
	    return TRUE;
	}
    default:
	break;
    }

    return FALSE;
}

static void
rotateScreenInitOptions (RotateScreen *rs,
			 Display      *display)
{
    CompOption *o;

    o = &rs->opt[ROTATE_SCREEN_OPTION_POINTER_INVERT_Y];
    o->name      = "invert_y";
    o->shortDesc = "Pointer Invert Y";
    o->longDesc  = "Invert Y axis for pointer movement";
    o->type      = CompOptionTypeBool;
    o->value.b   = ROTATE_POINTER_INVERT_Y_DEFAULT;

    o = &rs->opt[ROTATE_SCREEN_OPTION_POINTER_SENSITIVITY];
    o->name		= "sensitivity";
    o->shortDesc	= "Pointer Sensitivity";
    o->longDesc		= "Sensitivity of pointer movement";
    o->type		= CompOptionTypeFloat;
    o->value.f		= ROTATE_POINTER_SENSITIVITY_DEFAULT;
    o->rest.f.min	= ROTATE_POINTER_SENSITIVITY_MIN;
    o->rest.f.max	= ROTATE_POINTER_SENSITIVITY_MAX;
    o->rest.f.precision = ROTATE_POINTER_SENSITIVITY_PRECISION;

    o = &rs->opt[ROTATE_SCREEN_OPTION_ACCELERATION];
    o->name		= "acceleration";
    o->shortDesc	= "Acceleration";
    o->longDesc		= "Rotation Acceleration";
    o->type		= CompOptionTypeFloat;
    o->value.f		= ROTATE_ACCELERATION_DEFAULT;
    o->rest.f.min	= ROTATE_ACCELERATION_MIN;
    o->rest.f.max	= ROTATE_ACCELERATION_MAX;
    o->rest.f.precision = ROTATE_ACCELERATION_PRECISION;

    o = &rs->opt[ROTATE_SCREEN_OPTION_INITIATE];
    o->name			     = "initiate";
    o->shortDesc		     = "Initiate";
    o->longDesc			     = "Start Rotation";
    o->type			     = CompOptionTypeBinding;
    o->value.bind.type		     = CompBindingTypeButton;
    o->value.bind.u.button.modifiers = ROTATE_INITIATE_MODIFIERS_DEFAULT;
    o->value.bind.u.button.button    = ROTATE_INITIATE_BUTTON_DEFAULT;

    o = &rs->opt[ROTATE_SCREEN_OPTION_TERMINATE];
    o->name			     = "terminate";
    o->shortDesc		     = "Terminate";
    o->longDesc			     = "Stop Rotation";
    o->type			     = CompOptionTypeBinding;
    o->value.bind.type		     = CompBindingTypeButton;
    o->value.bind.u.button.modifiers = ROTATE_TERMINATE_MODIFIERS_DEFAULT;
    o->value.bind.u.button.button    = ROTATE_TERMINATE_BUTTON_DEFAULT;

    o = &rs->opt[ROTATE_SCREEN_OPTION_LEFT];
    o->name			  = "rotate_left";
    o->shortDesc		  = "Rotate Left";
    o->longDesc			  = "Rotate Left";
    o->type			  = CompOptionTypeBinding;
    o->value.bind.type		  = CompBindingTypeKey;
    o->value.bind.u.key.modifiers = ROTATE_LEFT_MODIFIERS_DEFAULT;
    o->value.bind.u.key.keycode   =
		XKeysymToKeycode (display,
			  XStringToKeysym (ROTATE_LEFT_KEY_DEFAULT));

    o = &rs->opt[ROTATE_SCREEN_OPTION_RIGHT];
    o->name			  = "rotate_right";
    o->shortDesc		  = "Rotate Right";
    o->longDesc			  = "Rotate Right";
    o->type			  = CompOptionTypeBinding;
    o->value.bind.type		  = CompBindingTypeKey;
    o->value.bind.u.key.modifiers = ROTATE_RIGHT_MODIFIERS_DEFAULT;
    o->value.bind.u.key.keycode   =
	XKeysymToKeycode (display,
			  XStringToKeysym (ROTATE_RIGHT_KEY_DEFAULT));

    o = &rs->opt[ROTATE_SCREEN_OPTION_SNAP_TOP];
    o->name      = "snap_top";
    o->shortDesc = "Snap To Top Face";
    o->longDesc  = "Snap Cube Rotation to Top Face";
    o->type      = CompOptionTypeBool;
    o->value.b   = ROTATE_SNAP_TOP_DEFAULT;
}

static int
adjustVelocity (RotateScreen *rs)
{
    float xrot, yrot, adjust, amount;

    if (rs->moving)
    {
	xrot = rs->moveTo + (rs->xrot + rs->baseXrot);
    }
    else
    {
	xrot = rs->xrot;
	if (rs->xrot < -45.0f)
	    xrot = 90.0f + rs->xrot;
	else if (rs->xrot > 45.0f)
	    xrot = rs->xrot - 90.0f;
    }

    adjust = -xrot * 0.05f * rs->acceleration;
    amount = fabs (xrot);
    if (amount < 10.0f)
	amount = 10.0f;
    else if (amount > 30.0f)
	amount = 30.0f;

    rs->xVelocity = (amount * rs->xVelocity + adjust) / (amount + 2.0f);

    if (rs->snapTop && rs->yrot > 50.0f)
	yrot = -(90.f - rs->yrot);
    else
	yrot = rs->yrot;

    adjust = -yrot * 0.05f * rs->acceleration;
    amount = fabs (rs->yrot);
    if (amount < 10.0f)
	amount = 10.0f;
    else if (amount > 30.0f)
	amount = 30.0f;

    rs->yVelocity = (amount * rs->yVelocity + adjust) / (amount + 2.0f);

    return (fabs (xrot) < 0.1f && fabs (rs->xVelocity) < 0.2f &&
	    fabs (yrot) < 0.1f && fabs (rs->yVelocity) < 0.2f);
}

static void
rotatePreparePaintScreen (CompScreen *s,
			  int	     msSinceLastPaint)
{
    ROTATE_SCREEN (s);

    if (rs->grabIndex)
    {
	rs->xrot += (rs->xVelocity * msSinceLastPaint) / s->redrawTime;
	rs->yrot += (rs->yVelocity * msSinceLastPaint) / s->redrawTime;

	if (rs->xrot > 90.0f)
	{
	    rs->baseXrot += 90.0f;
	    rs->xrot -= 90.0f;
	}
	else if (rs->xrot < 0.0f)
	{
	    rs->baseXrot -= 90.0f;
	    rs->xrot += 90.0f;
	}

	if (rs->yrot > 100.0f)
	{
	    rs->yVelocity = -0.5f;
	    rs->yrot = 100.0f;
	}
	else if (rs->yrot < -100.0f)
	{
	    rs->yVelocity = 0.5f;
	    rs->yrot = -100.0f;
	}

	if (rs->grabbed)
	{
	    rs->xVelocity /= 1.25f;
	    rs->yVelocity /= 1.25f;

	    if (fabs (rs->xVelocity) < 0.01f)
		rs->xVelocity = 0.0f;
	    if (fabs (rs->yVelocity) < 0.01f)
		rs->yVelocity = 0.0f;
	}
	else if (adjustVelocity (rs))
	{
	    rs->xVelocity = 0.0f;
	    rs->yVelocity = 0.0f;

	    if (fabs (rs->yrot) < 0.1f)
	    {
		int move;

		rs->xrot += rs->baseXrot;
		if (rs->xrot < 0.0f)
		    move = (rs->xrot / 90.0f) - 0.5f;
		else
		    move = (rs->xrot / 90.0f) + 0.5f;

		if (move)
		{
		    CompWindow *w;

		    for (w = s->windows; w; w = w->next)
		    {
			if (w->attrib.map_state != IsViewable)
			    continue;

			if (w->type == s->display->winDesktopAtom ||
			    w->type == s->display->winDockAtom)
			    continue;

			(*s->invisibleWindowMove) (w, s->width * move, 0);

			w->invisible = WINDOW_INVISIBLE (w);

			XMoveWindow (s->display->display,
				     (w->client) ? w->client : w->id,
				     w->attrib.x, w->attrib.y);
		    }
		}

		rs->xrot = 0.0f;
		rs->yrot = 0.0f;
		rs->baseXrot = rs->moveTo = 0.0f;
		rs->moving = FALSE;

		removeScreenGrab (s, rs->grabIndex, &rs->savedPointer);
		rs->grabIndex = 0;
	    }
	}
    }

    UNWRAP (rs, s, preparePaintScreen);
    (*s->preparePaintScreen) (s, msSinceLastPaint);
    WRAP (rs, s, preparePaintScreen, rotatePreparePaintScreen);
}

static void
rotateDonePaintScreen (CompScreen *s)
{
    ROTATE_SCREEN (s);

    if (rs->grabIndex)
    {
	if ((!rs->grabbed && !rs->snapTop) || rs->xVelocity || rs->yVelocity)
	    damageScreen (s);
    }

    UNWRAP (rs, s, donePaintScreen);
    (*s->donePaintScreen) (s);
    WRAP (rs, s, donePaintScreen, rotateDonePaintScreen);
}

static Bool
rotatePaintScreen (CompScreen		   *s,
		   const ScreenPaintAttrib *sAttrib,
		   const WindowPaintAttrib *wAttrib,
		   Region		   region,
		   unsigned int		   mask)
{
    Bool status;

    ROTATE_SCREEN (s);

    if (rs->grabIndex)
    {
	ScreenPaintAttrib sa = *sAttrib;

	sa.xRotate += rs->baseXrot + rs->xrot;
	sa.vRotate += rs->yrot;

	mask &= ~PAINT_SCREEN_REGION_MASK;
	mask |= PAINT_SCREEN_TRANSFORMED_MASK;

	UNWRAP (rs, s, paintScreen);
	status = (*s->paintScreen) (s, &sa, wAttrib, region, mask);
	WRAP (rs, s, paintScreen, rotatePaintScreen);
    }
    else
    {
	UNWRAP (rs, s, paintScreen);
	status = (*s->paintScreen) (s, sAttrib, wAttrib, region, mask);
	WRAP (rs, s, paintScreen, rotatePaintScreen);
    }

    return status;
}

static void
rotateInitiate (CompScreen *s,
		int	   x,
		int	   y)
{
    ROTATE_SCREEN (s);

    rs->prevPointerX = x;
    rs->prevPointerY = y;

    rs->moving = FALSE;

    if (!rs->grabIndex)
    {
	rs->grabIndex = pushScreenGrab (s, s->invisibleCursor);
	if (rs->grabIndex)
	{
	    rs->savedPointer.x = rs->prevPointerX;
	    rs->savedPointer.y = rs->prevPointerY;

	    gettimeofday (&s->lastRedraw, 0);
	}
    }

    if (rs->grabIndex)
    {
	rs->grabbed = TRUE;
	rs->snapTop = rs->opt[ROTATE_SCREEN_OPTION_SNAP_TOP].value.b;
    }
}

static void
rotateLeft (CompScreen *s,
	    int	       x,
	    int	       y)
{
    ROTATE_SCREEN (s);

    if (!rs->grabIndex)
	rotateInitiate (s, x, y);

    if (rs->grabIndex)
    {
	rs->moving = TRUE;
	rs->moveTo -= 90.0f;
	rs->grabbed = FALSE;
	damageScreen (s);
    }
}

static void
rotateRight (CompScreen *s,
	     int	x,
	     int	y)
{
    ROTATE_SCREEN (s);

    if (!rs->grabIndex)
	rotateInitiate (s, x, y);

    if (rs->grabIndex)
    {
	rs->moving = TRUE;
	rs->moveTo += 90.0f;
	rs->grabbed = FALSE;

	damageScreen (s);
    }
}

static void
rotateTerminate (CompScreen *s)
{
    ROTATE_SCREEN (s);

    if (rs->grabIndex)
    {
	rs->grabbed = FALSE;
	damageScreen (s);
    }
}

static void
rotateHandleEvent (CompDisplay *d,
		   XEvent      *event)
{
    CompScreen *s;

    ROTATE_DISPLAY (d);

    switch (event->type) {
    case KeyPress:
    case KeyRelease:
	s = findScreenAtDisplay (d, event->xkey.root);
	if (s)
	{
	    ROTATE_SCREEN (s);

	    if (EV_KEY (&rs->opt[ROTATE_SCREEN_OPTION_INITIATE], event))
		rotateInitiate (s, event->xkey.x_root, event->xkey.y_root);

	    if (EV_KEY (&rs->opt[ROTATE_SCREEN_OPTION_LEFT], event))
		rotateLeft (s, event->xkey.x_root, event->xkey.y_root);

	    if (EV_KEY (&rs->opt[ROTATE_SCREEN_OPTION_RIGHT], event))
		rotateRight (s, event->xkey.x_root, event->xkey.y_root);

	    if (EV_KEY (&rs->opt[ROTATE_SCREEN_OPTION_TERMINATE], event))
		rotateTerminate (s);

	    if (event->type	    == KeyPress &&
		event->xkey.keycode == s->escapeKeyCode)
	    {
		rs->snapTop = FALSE;
		rotateTerminate (s);
	    }
	}
	break;
    case ButtonPress:
    case ButtonRelease:
	s = findScreenAtDisplay (d, event->xbutton.root);
	if (s)
	{
	    ROTATE_SCREEN (s);

	    if (EV_BUTTON (&rs->opt[ROTATE_SCREEN_OPTION_INITIATE], event))
		rotateInitiate (s,
				event->xbutton.x_root,
				event->xbutton.y_root);

	    if (EV_BUTTON (&rs->opt[ROTATE_SCREEN_OPTION_LEFT], event))
		rotateLeft (s, event->xbutton.x_root, event->xbutton.y_root);

	    if (EV_BUTTON (&rs->opt[ROTATE_SCREEN_OPTION_RIGHT], event))
		rotateRight (s, event->xbutton.x_root, event->xbutton.y_root);

	    if (EV_BUTTON (&rs->opt[ROTATE_SCREEN_OPTION_TERMINATE], event))
		rotateTerminate (s);
	}
	break;
    case MotionNotify:
	s = findScreenAtDisplay (d, event->xmotion.root);
	if (s)
	{
	    ROTATE_SCREEN (s);

	    if (rs->grabIndex && rs->grabbed)
	    {
		GLfloat pointerDx;
		GLfloat pointerDy;

		pointerDx = event->xmotion.x_root - rs->prevPointerX;
		pointerDy = event->xmotion.y_root - rs->prevPointerY;
		rs->prevPointerX = event->xmotion.x_root;
		rs->prevPointerY = event->xmotion.y_root;

		if (event->xmotion.x_root < 50	           ||
		    event->xmotion.y_root < 50	           ||
		    event->xmotion.x_root > s->width  - 50 ||
		    event->xmotion.y_root > s->height - 50)
		{
		    rs->prevPointerX = s->width / 2;
		    rs->prevPointerY = s->height / 2;

		    XWarpPointer (d->display, None, s->root, 0, 0, 0, 0,
				  rs->prevPointerX, rs->prevPointerY);
		}

		if (rs->pointerInvertY)
		    pointerDy = -pointerDy;

		rs->xVelocity += pointerDx * rs->pointerSensitivity;
		rs->yVelocity += pointerDy * rs->pointerSensitivity;

		damageScreen (s);
		return;
	    }
	}
    default:
	break;
    }

    UNWRAP (rd, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (rd, d, handleEvent, rotateHandleEvent);
}

static Bool
rotateInitDisplay (CompPlugin  *p,
		   CompDisplay *d)
{
    RotateDisplay *rd;

    rd = malloc (sizeof (RotateDisplay));
    if (!rd)
	return FALSE;

    rd->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (rd->screenPrivateIndex < 0)
    {
	free (rd);
	return FALSE;
    }

    WRAP (rd, d, handleEvent, rotateHandleEvent);

    d->privates[displayPrivateIndex].ptr = rd;

    return TRUE;
}

static void
rotateFiniDisplay (CompPlugin  *p,
		   CompDisplay *d)
{
    ROTATE_DISPLAY (d);

    freeScreenPrivateIndex (d, rd->screenPrivateIndex);

    UNWRAP (rd, d, handleEvent);

    free (rd);
}

static Bool
rotateInitScreen (CompPlugin *p,
		  CompScreen *s)
{
    RotateScreen *rs;

    ROTATE_DISPLAY (s->display);

    rs = malloc (sizeof (RotateScreen));
    if (!rs)
	return FALSE;

    rs->grabIndex = 0;

    rs->xrot = 0.0f;
    rs->xVelocity = 0.0f;
    rs->yrot = 0.0f;
    rs->yVelocity = 0.0f;

    rs->baseXrot = 0.0f;

    rs->moving = FALSE;
    rs->moveTo = 0.0f;

    rs->savedPointer.x = 0;
    rs->savedPointer.y = 0;
    rs->prevPointerX = 0;
    rs->prevPointerY = 0;

    rs->grabbed = FALSE;
    rs->snapTop = FALSE;

    rs->acceleration = ROTATE_ACCELERATION_DEFAULT;

    rs->pointerInvertY     = ROTATE_POINTER_INVERT_Y_DEFAULT;
    rs->pointerSensitivity = ROTATE_POINTER_SENSITIVITY_DEFAULT *
	ROTATE_POINTER_SENSITIVITY_FACTOR;

    rotateScreenInitOptions (rs, s->display->display);

    addScreenBinding (s, &rs->opt[ROTATE_SCREEN_OPTION_INITIATE].value.bind);
    addScreenBinding (s, &rs->opt[ROTATE_SCREEN_OPTION_LEFT].value.bind);
    addScreenBinding (s, &rs->opt[ROTATE_SCREEN_OPTION_RIGHT].value.bind);

    WRAP (rs, s, preparePaintScreen, rotatePreparePaintScreen);
    WRAP (rs, s, donePaintScreen, rotateDonePaintScreen);
    WRAP (rs, s, paintScreen, rotatePaintScreen);

    s->privates[rd->screenPrivateIndex].ptr = rs;

    return TRUE;
}

static void
rotateFiniScreen (CompPlugin *p,
		  CompScreen *s)
{
    ROTATE_SCREEN (s);

    UNWRAP (rs, s, preparePaintScreen);
    UNWRAP (rs, s, donePaintScreen);
    UNWRAP (rs, s, paintScreen);

    free (rs);
}

static Bool
rotateInit (CompPlugin *p)
{
    if (!findActivePlugin ("cube"))
    {
	fprintf (stderr, "%s: 'cube' required but not loaded\n", programName);
	return FALSE;
    }

    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
	return FALSE;

    return TRUE;
}

static void
rotateFini (CompPlugin *p)
{
    if (displayPrivateIndex >= 0)
	freeDisplayPrivateIndex (displayPrivateIndex);
}

CompPluginVTable rotateVTable = {
    "rotate",
    "Rotate Cube",
    "Rotate desktop cube",
    rotateInit,
    rotateFini,
    rotateInitDisplay,
    rotateFiniDisplay,
    rotateInitScreen,
    rotateFiniScreen,
    0, /* InitWindow */
    0, /* FiniWindow */
    0, /* GetDisplayOptions */
    0, /* SetDisplayOption */
    rotateGetScreenOptions,
    rotateSetScreenOption
};

CompPluginVTable *
getCompPluginInfo (void)
{
    return &rotateVTable;
}
