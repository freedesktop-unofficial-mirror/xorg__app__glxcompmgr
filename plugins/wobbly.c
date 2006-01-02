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

/*
 * Spring model implemented by Kristian Hogsberg.
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <comp.h>

#define GRID_WIDTH  4
#define GRID_HEIGHT 4

#define MODEL_MAX_SPRINGS (GRID_WIDTH * GRID_HEIGHT * 2)

typedef struct _xy_pair {
    float x, y;
} Point, Vector;

typedef struct _Object {
    Vector force;
    Point  position;
    Vector velocity;
    float  theta;
    Bool   immobile;
} Object;

typedef struct _Spring {
    Object *a;
    Object *b;
    Vector offset;
} Spring;

typedef struct _Model {
    Object *objects;
    int    numObjects;
    Spring springs[MODEL_MAX_SPRINGS];
    int    numSprings;
    Object *anchorObject;
    float  steps;
    Vector translate;
    Vector scale;
    Bool   transformed;
} Model;

#define WOBBLY_FRICTION_DEFAULT    2.5f
#define WOBBLY_FRICTION_MIN        0.1f
#define WOBBLY_FRICTION_MAX       10.0f
#define WOBBLY_FRICTION_PRECISION  0.1f

#define WOBBLY_SPRING_K_DEFAULT    3.5f
#define WOBBLY_SPRING_K_MIN        0.1f
#define WOBBLY_SPRING_K_MAX       10.0f
#define WOBBLY_SPRING_K_PRECISION  0.1f

#define WOBBLY_GRID_RESOLUTION_DEFAULT  8
#define WOBBLY_GRID_RESOLUTION_MIN      1
#define WOBBLY_GRID_RESOLUTION_MAX      64

#define WOBBLY_MIN_GRID_SIZE_DEFAULT  8
#define WOBBLY_MIN_GRID_SIZE_MIN      4
#define WOBBLY_MIN_GRID_SIZE_MAX      128

typedef enum {
    WobblyEffectNone = 0,
    WobblyEffectExplode,
    WobblyEffectShiver
} WobblyEffect;

static char *effectName[] = {
    "None",
    "Explode",
    "Shiver"
};

static WobblyEffect effectType[] = {
    WobblyEffectNone,
    WobblyEffectExplode,
    WobblyEffectShiver
};

#define NUM_EFFECT (sizeof (effectType) / sizeof (effectType[0]))

#define WOBBLY_MAP_DEFAULT   (effectName[2])
#define WOBBLY_FOCUS_DEFAULT (effectName[0])

static int displayPrivateIndex;

typedef struct _WobblyDisplay {
    int		    screenPrivateIndex;
    HandleEventProc handleEvent;
} WobblyDisplay;

#define WOBBLY_SCREEN_OPTION_FRICTION        0
#define WOBBLY_SCREEN_OPTION_SPRING_K        1
#define WOBBLY_SCREEN_OPTION_GRID_RESOLUTION 2
#define WOBBLY_SCREEN_OPTION_MIN_GRID_SIZE   3
#define WOBBLY_SCREEN_OPTION_MAP_EFFECT      4
#define WOBBLY_SCREEN_OPTION_FOCUS_EFFECT    5
#define WOBBLY_SCREEN_OPTION_NUM             6

typedef struct _WobblyScreen {
    int			     windowPrivateIndex;
    PreparePaintScreenProc   preparePaintScreen;
    DonePaintScreenProc      donePaintScreen;
    PaintScreenProc	     paintScreen;
    PaintWindowProc	     paintWindow;
    DamageWindowRectProc     damageWindowRect;
    AddWindowGeometryProc    addWindowGeometry;
    DrawWindowGeometryProc   drawWindowGeometry;
    InvisibleWindowMoveProc  invisibleWindowMove;

    CompOption opt[WOBBLY_SCREEN_OPTION_NUM];

    Bool wobblyWindows;

    WobblyEffect mapEffect;
    WobblyEffect focusEffect;
} WobblyScreen;

typedef struct _WobblyWindow {
    Model *model;
    Bool  wobbly;
} WobblyWindow;

#define GET_WOBBLY_DISPLAY(d)				       \
    ((WobblyDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define WOBBLY_DISPLAY(d)		       \
    WobblyDisplay *wd = GET_WOBBLY_DISPLAY (d)

#define GET_WOBBLY_SCREEN(s, wd)				   \
    ((WobblyScreen *) (s)->privates[(wd)->screenPrivateIndex].ptr)

#define WOBBLY_SCREEN(s)						      \
    WobblyScreen *ws = GET_WOBBLY_SCREEN (s, GET_WOBBLY_DISPLAY (s->display))

#define GET_WOBBLY_WINDOW(w, ws)				   \
    ((WobblyWindow *) (w)->privates[(ws)->windowPrivateIndex].ptr)

#define WOBBLY_WINDOW(w)				         \
    WobblyWindow *ww = GET_WOBBLY_WINDOW  (w,		         \
		       GET_WOBBLY_SCREEN  (w->screen,	         \
		       GET_WOBBLY_DISPLAY (w->screen->display)))

#define NUM_OPTIONS(s) (sizeof ((s)->opt) / sizeof (CompOption))

static CompOption *
wobblyGetScreenOptions (CompScreen *screen,
			int	   *count)
{
    WOBBLY_SCREEN (screen);

    *count = NUM_OPTIONS (ws);
    return ws->opt;
}

static Bool
wobblySetScreenOption (CompScreen      *screen,
		     char	     *name,
		     CompOptionValue *value)
{
    CompOption *o;
    int	       index;

    WOBBLY_SCREEN (screen);

    o = compFindOption (ws->opt, NUM_OPTIONS (ws), name, &index);
    if (!o)
	return FALSE;

    switch (index) {
    case WOBBLY_SCREEN_OPTION_FRICTION:
    case WOBBLY_SCREEN_OPTION_SPRING_K:
	if (compSetFloatOption (o, value))
	    return TRUE;
	break;
    case WOBBLY_SCREEN_OPTION_GRID_RESOLUTION:
	if (compSetIntOption (o, value))
	    return TRUE;
	break;
    case WOBBLY_SCREEN_OPTION_MIN_GRID_SIZE:
	if (compSetIntOption (o, value))
	    return TRUE;
	break;
    case WOBBLY_SCREEN_OPTION_MAP_EFFECT:
	if (compSetStringOption (o, value))
	{
	    int i;

	    for (i = 0; i < NUM_EFFECT; i++)
	    {
		if (strcmp (o->value.s, effectName[i]) == 0)
		{
		    ws->mapEffect = effectType[i];
		    return TRUE;
		}
	    }
	}
	break;
    case WOBBLY_SCREEN_OPTION_FOCUS_EFFECT:
	if (compSetStringOption (o, value))
	{
	    int i;

	    for (i = 0; i < NUM_EFFECT; i++)
	    {
		if (strcmp (o->value.s, effectName[i]) == 0)
		{
		    ws->focusEffect = effectType[i];
		    return TRUE;
		}
	    }
	}
    default:
	break;
    }

    return FALSE;
}

static void
wobblyScreenInitOptions (WobblyScreen *ws)
{
    CompOption *o;

    o = &ws->opt[WOBBLY_SCREEN_OPTION_FRICTION];
    o->name		= "friction";
    o->shortDesc	= "Friction";
    o->longDesc		= "Spring Friction";
    o->type		= CompOptionTypeFloat;
    o->value.f		= WOBBLY_FRICTION_DEFAULT;
    o->rest.f.min	= WOBBLY_FRICTION_MIN;
    o->rest.f.max	= WOBBLY_FRICTION_MAX;
    o->rest.f.precision = WOBBLY_FRICTION_PRECISION;

    o = &ws->opt[WOBBLY_SCREEN_OPTION_SPRING_K];
    o->name		= "spring_k";
    o->shortDesc	= "Spring K";
    o->longDesc		= "Spring Konstant";
    o->type		= CompOptionTypeFloat;
    o->value.f		= WOBBLY_SPRING_K_DEFAULT;
    o->rest.f.min	= WOBBLY_SPRING_K_MIN;
    o->rest.f.max	= WOBBLY_SPRING_K_MAX;
    o->rest.f.precision = WOBBLY_SPRING_K_PRECISION;

    o = &ws->opt[WOBBLY_SCREEN_OPTION_GRID_RESOLUTION];
    o->name	  = "grid_resolution";
    o->shortDesc  = "Grid Resolution";
    o->longDesc	  = "Vertex Grid Resolution";
    o->type	  = CompOptionTypeInt;
    o->value.i	  = WOBBLY_GRID_RESOLUTION_DEFAULT;
    o->rest.i.min = WOBBLY_GRID_RESOLUTION_MIN;
    o->rest.i.max = WOBBLY_GRID_RESOLUTION_MAX;

    o = &ws->opt[WOBBLY_SCREEN_OPTION_MIN_GRID_SIZE];
    o->name	  = "min_grid_size";
    o->shortDesc  = "Minimum Grid Size";
    o->longDesc	  = "Minimum Vertex Grid Size";
    o->type	  = CompOptionTypeInt;
    o->value.i	  = WOBBLY_MIN_GRID_SIZE_DEFAULT;
    o->rest.i.min = WOBBLY_MIN_GRID_SIZE_MIN;
    o->rest.i.max = WOBBLY_MIN_GRID_SIZE_MAX;

    o = &ws->opt[WOBBLY_SCREEN_OPTION_MAP_EFFECT];
    o->name	      = "map_effect";
    o->shortDesc      = "Map Effect";
    o->longDesc	      = "Map Window Effect";
    o->type	      = CompOptionTypeString;
    o->value.s	      = strdup (WOBBLY_MAP_DEFAULT);
    o->rest.s.string  = effectName;
    o->rest.s.nString = NUM_EFFECT;

    o = &ws->opt[WOBBLY_SCREEN_OPTION_FOCUS_EFFECT];
    o->name	      = "focus_effect";
    o->shortDesc      = "Focus Effect";
    o->longDesc	      = "Focus Window Effect";
    o->type	      = CompOptionTypeString;
    o->value.s	      = strdup (WOBBLY_FOCUS_DEFAULT);
    o->rest.s.string  = effectName;
    o->rest.s.nString = NUM_EFFECT;
}

static void
objectInit (Object *object,
	    float  positionX,
	    float  positionY,
	    float  velocityX,
	    float  velocityY)
{
    object->force.x = 0;
    object->force.y = 0;

    object->position.x = positionX;
    object->position.y = positionY;

    object->velocity.x = velocityX;
    object->velocity.y = velocityY;

    object->theta    = 0;
    object->immobile = FALSE;
}

static void
springInit (Spring *spring,
	    Object *a,
	    Object *b,
	    float  offsetX,
	    float  offsetY)
{
    spring->a = a;
    spring->b = b;
    spring->offset.x = offsetX;
    spring->offset.y = offsetY;
}

static void
modelAddSpring (Model  *model,
		Object *a,
		Object *b,
		float  offsetX,
		float  offsetY)
{
    Spring *spring;

    spring = &model->springs[model->numSprings];
    model->numSprings++;

    springInit (spring, a, b, offsetX, offsetY);
}

static void
modelSetMiddleAnchor (Model *model,
		      int   x,
		      int   y,
		      int   width,
		      int   height)
{
    float w, h;

    if (model->anchorObject)
	model->anchorObject->immobile = FALSE;

    w = (float) width  * model->scale.x;
    h = (float) height * model->scale.y;
    x += model->translate.x;
    y += model->translate.y;

    model->anchorObject = &model->objects[GRID_WIDTH *
					  ((GRID_HEIGHT - 1) / 2) +
					  (GRID_WIDTH - 1) / 2];
    model->anchorObject->position.x = x +
	((GRID_WIDTH - 1) / 2 * w) / (float) (GRID_WIDTH - 1);
    model->anchorObject->position.y = y +
	((GRID_HEIGHT - 1) / 2 * h) / (float) (GRID_HEIGHT - 1);

    model->anchorObject->immobile = TRUE;
}

static void
modelInitObjects (Model *model,
		  int   x,
		  int   y,
		  int   width,
		  int   height)
{
    int gridX, gridY, i = 0;
    float w, h;

    w = (float) width  * model->scale.x;
    h = (float) height * model->scale.y;
    x += model->translate.x;
    y += model->translate.y;

    for (gridY = 0; gridY < GRID_HEIGHT; gridY++)
    {
	for (gridX = 0; gridX < GRID_WIDTH; gridX++)
	{
	    objectInit (&model->objects[i],
			x + (gridX * w) / (float) (GRID_WIDTH - 1),
			y + (gridY * h) / (float) (GRID_HEIGHT - 1),
			0, 0);
	    i++;
	}
    }

    modelSetMiddleAnchor (model, x, y, width, height);
}

static void
modelAdjustObjectsForExplosion (Model *model,
				int   x,
				int   y,
				int   width,
				int   height)
{
    int   gridX, gridY, i = 0;
    int   vX, vY;
    float scale;
    float w, h;

    w = (float) width  * model->scale.x;
    h = (float) height * model->scale.y;
    x += model->translate.x;
    y += model->translate.y;

    for (gridY = 0; gridY < GRID_HEIGHT; gridY++)
    {
	for (gridX = 0; gridX < GRID_WIDTH; gridX++)
	{
	    if (!model->objects[i].immobile)
	    {
		scale = ((float) rand () * 0.1f) / RAND_MAX;
		vX = (model->objects[i].position.x - (x + w / 2.0f)) * scale;
		vY = (model->objects[i].position.y - (y + h / 2.0f)) * scale;

		model->objects[i].position.x = x + w / 2.0f;
		model->objects[i].position.y = y + h / 2.0f;

		model->objects[i].velocity.x += vX;
		model->objects[i].velocity.y += vY;
	    }

	    i++;
	}
    }
}

static void
modelAdjustObjectsForShiver (Model *model,
			     int   x,
			     int   y,
			     int   width,
			     int   height)
{
    int   gridX, gridY, i = 0;
    float vX, vY;
    float scale;
    float w, h;

    w = (float) width  * model->scale.x;
    h = (float) height * model->scale.y;
    x += model->translate.x;
    y += model->translate.y;

    for (gridY = 0; gridY < GRID_HEIGHT; gridY++)
    {
	for (gridX = 0; gridX < GRID_WIDTH; gridX++)
	{
	    if (!model->objects[i].immobile)
	    {
		vX = model->objects[i].position.x - (x + w  / 2);
		vY = model->objects[i].position.y - (y + h / 2);

		vX /= w;
		vY /= h;

		scale = ((float) rand () * 7.5f) / RAND_MAX;

		model->objects[i].velocity.x += vX * scale;
		model->objects[i].velocity.y += vY * scale;
	    }

	    i++;
	}
    }
}

static void
modelInitSprings (Model *model,
		  int   x,
		  int   y,
		  int   width,
		  int   height)
{
    int   gridX, gridY, i = 0;
    float hpad, vpad;
    float w, h;

    model->numSprings = 0;

    w = (float) width  * model->scale.x;
    h = (float) height * model->scale.y;

    hpad = w / (GRID_WIDTH - 1);
    vpad = h / (GRID_HEIGHT - 1);

    for (gridY = 0; gridY < GRID_HEIGHT; gridY++)
    {
	for (gridX = 0; gridX < GRID_WIDTH; gridX++)
	{
	    if (gridX > 0)
		modelAddSpring (model,
				&model->objects[i - 1],
				&model->objects[i],
				hpad, 0);

	    if (gridY > 0)
		modelAddSpring (model,
				&model->objects[i - GRID_WIDTH],
				&model->objects[i],
				0, vpad);

	    i++;
	}
    }
}

static void
modelMove (Model *model,
	   int   dx,
	   int   dy)
{
    int i;

    for (i = 0; i < model->numObjects; i++)
    {
	model->objects[i].position.x += dx;
	model->objects[i].position.y += dy;
    }
}

static Model *
createModel (int x,
	     int y,
	     int width,
	     int height)
{
    Model *model;

    model = malloc (sizeof (Model));
    if (!model)
	return 0;

    model->numObjects = GRID_WIDTH * GRID_HEIGHT;
    model->objects = malloc (sizeof (Object) * model->numObjects);
    if (!model->objects)
	return 0;

    model->anchorObject = 0;
    model->numSprings = 0;

    model->steps = 0;

    model->translate.x = 0.0f;
    model->translate.y = 0.0f;

    model->scale.x = 1.0f;
    model->scale.y = 1.0f;

    model->transformed = FALSE;

    modelInitObjects (model, x, y, width, height);
    modelInitSprings (model, x, y, width, height);

    return model;
}

static void
objectApplyForce (Object *object,
		  float  fx,
		  float  fy)
{
    object->force.x += fx;
    object->force.y += fy;
}

static void
springExertForces (Spring *spring,
		   float  k)
{
    Vector da, db;
    Vector a, b;

    a = spring->a->position;
    b = spring->b->position;

    da.x = 0.5f * (b.x - a.x - spring->offset.x);
    da.y = 0.5f * (b.y - a.y - spring->offset.y);

    db.x = 0.5f * (a.x - b.x + spring->offset.x);
    db.y = 0.5f * (a.y - b.y + spring->offset.y);

    objectApplyForce (spring->a, k * da.x, k * da.y);
    objectApplyForce (spring->b, k * db.x, k * db.y);
}

static float
modelStepObject (Model  *model,
		 Object *object,
		 float  friction)
{
    object->theta += 0.05f;

    object->force.x -= friction * object->velocity.x;
    object->force.y -= friction * object->velocity.y;

    if (object->immobile)
    {
	object->velocity.x = 0.0f;
	object->velocity.y = 0.0f;
    }
    else
    {
	object->velocity.x += object->force.x / 20.0f;
	object->velocity.y += object->force.y / 20.0f;

	object->position.x += object->velocity.x;
	object->position.y += object->velocity.y;
    }

    object->force.x = 0.0f;
    object->force.y = 0.0f;

    return fabs (object->velocity.x) + fabs (object->velocity.y);
}

static Bool
modelStep (Model *model,
	   float friction,
	   float k,
	   float time)
{
    int   i, j, steps;
    float velocitySum = 0.0f;

    model->steps += time / 15.0f;
    steps = floor (model->steps);
    model->steps -= steps;

    if (!steps)
	return TRUE;

    for (j = 0; j < steps; j++)
    {
	for (i = 0; i < model->numSprings; i++)
	    springExertForces (&model->springs[i], k);

	for (i = 0; i < model->numObjects; i++)
	    velocitySum += modelStepObject (model,
					    &model->objects[i],
					    friction);
    }

    return (velocitySum > 0.5f);
}

static void
bezierPatchEvaluate (Model *model,
		     float u,
		     float v,
		     float *patchX,
		     float *patchY)
{
    float coeffsU[4], coeffsV[4];
    float x, y;
    int   i, j;

    coeffsU[0] = (1 - u) * (1 - u) * (1 - u);
    coeffsU[1] = 3 * u * (1 - u) * (1 - u);
    coeffsU[2] = 3 * u * u * (1 - u);
    coeffsU[3] = u * u * u;

    coeffsV[0] = (1 - v) * (1 - v) * (1 - v);
    coeffsV[1] = 3 * v * (1 - v) * (1 - v);
    coeffsV[2] = 3 * v * v * (1 - v);
    coeffsV[3] = v * v * v;

    x = y = 0.0f;

    for (i = 0; i < 4; i++)
    {
	for (j = 0; j < 4; j++)
	{
	    x += coeffsU[i] * coeffsV[j] *
		model->objects[j * GRID_WIDTH + i].position.x;
	    y += coeffsU[i] * coeffsV[j] *
		model->objects[j * GRID_WIDTH + i].position.y;
	}
    }

    *patchX = x;
    *patchY = y;
}

static Bool
wobblyEnsureModel (CompWindow *w)
{
    WOBBLY_WINDOW (w);

    if (!ww->model)
    {
	ww->model = createModel (w->attrib.x, w->attrib.y,
				 w->width, w->height);
	if (!ww->model)
	    return FALSE;
    }

    return TRUE;
}

static float
objectDistance (Object *object,
		float  x,
		float  y)
{
    float dx, dy;

    dx = object->position.x - x;
    dy = object->position.y - y;

    return sqrt (dx * dx + dy * dy);
}

static Object *
modelFindNearestObject (Model *model,
			float x,
			float y)
{
    Object *object = &model->objects[0];
    float  distance, minDistance = 0.0;
    int    i;

    for (i = 0; i < model->numObjects; i++)
    {
	distance = objectDistance (&model->objects[i], x, y);
	if (i == 0 || distance < minDistance)
	{
	    minDistance = distance;
	    object = &model->objects[i];
	}
    }

    return object;
}

static void
wobblyMoveWindow (CompWindow *w,
		  int	     dx,
		  int	     dy)
{
    WOBBLY_WINDOW (w);

    if (!wobblyEnsureModel (w))
	return;

    if (!ww->wobbly)
    {
	Window	     win;
	int	     i, x, y;
	unsigned int ui;

	WOBBLY_SCREEN (w->screen);

	XQueryPointer (w->screen->display->display, w->screen->root,
		       &win, &win, &x, &y, &i, &i, &ui);

	ww->model->anchorObject->immobile = FALSE;

	if (x < w->attrib.x || x > w->attrib.x + w->width ||
	    y < w->attrib.y || y > w->attrib.y + w->height)
	{
	    x = w->attrib.x + w->width / 2;
	    y = w->attrib.y + w->height / 2;
	}

	ww->model->anchorObject = modelFindNearestObject (ww->model, x, y);
	ww->model->anchorObject->immobile = TRUE;

	ww->wobbly = ws->wobblyWindows = TRUE;
    }

    ww->model->anchorObject->position.x += dx;
    ww->model->anchorObject->position.y += dy;

    damageScreen (w->screen);
}

static Bool
isWobblyWin (CompWindow *w)
{
    if (w->type == w->screen->display->winDesktopAtom ||
	w->type == w->screen->display->winDockAtom)
	return FALSE;

    /* avoid tiny windows */
    if (w->width == 1 && w->height == 1)
	return FALSE;

    /* avoid fullscreen windows */
    if (w->attrib.x <= 0 &&
	w->attrib.y <= 0 &&
	w->attrib.x + w->width >= w->screen->width &&
	w->attrib.y + w->height >= w->screen->height)
	return FALSE;

    return TRUE;
}

static void
wobblyPreparePaintScreen (CompScreen *s,
			  int	     msSinceLastPaint)
{
    WobblyWindow *ww;
    CompWindow   *w;

    WOBBLY_SCREEN (s);

    if (ws->wobblyWindows)
    {
	float friction, springK;

	friction = ws->opt[WOBBLY_SCREEN_OPTION_FRICTION].value.f;
	springK  = ws->opt[WOBBLY_SCREEN_OPTION_SPRING_K].value.f;

	ws->wobblyWindows = FALSE;
	for (w = s->windows; w; w = w->next)
	{
	    ww = GET_WOBBLY_WINDOW (w, ws);

	    if (ww->wobbly)
	    {
		if (w->attrib.map_state == IsViewable &&
		    modelStep (ww->model, friction, springK, msSinceLastPaint))
		{
		    ws->wobblyWindows = TRUE;
		}
		else
		{
		    modelSetMiddleAnchor (ww->model,
					  w->attrib.x, w->attrib.y,
					  w->width, w->height);
		    ww->wobbly = FALSE;
		}
	    }
	}
    }

    UNWRAP (ws, s, preparePaintScreen);
    (*s->preparePaintScreen) (s, msSinceLastPaint);
    WRAP (ws, s, preparePaintScreen, wobblyPreparePaintScreen);
}

static void
wobblyDonePaintScreen (CompScreen *s)
{
    WOBBLY_SCREEN (s);

    if (ws->wobblyWindows)
	damageScreen (s);

    UNWRAP (ws, s, donePaintScreen);
    (*s->donePaintScreen) (s);
    WRAP (ws, s, donePaintScreen, wobblyDonePaintScreen);
}

static void
wobblyTransformWindow (CompWindow              *w,
		       const WindowPaintAttrib *attrib)
{
    WOBBLY_WINDOW (w);
    WOBBLY_SCREEN (w->screen);

    if (wobblyEnsureModel (w))
    {
	modelSetMiddleAnchor (ww->model,
			      w->attrib.x, w->attrib.y,
			      w->width, w->height);

	if (ww->model->scale.x != attrib->xScale ||
	    ww->model->scale.y != attrib->yScale)
	{
	    ww->model->scale.x = attrib->xScale;
	    ww->model->scale.y = attrib->yScale;

	    modelInitSprings (ww->model,
			      w->attrib.x, w->attrib.y,
			      w->width, w->height);

	    ww->wobbly = ws->wobblyWindows = TRUE;
	}

	if (ww->model->translate.x != attrib->xTranslate ||
	    ww->model->translate.y != attrib->yTranslate)
	{
	    ww->model->anchorObject->position.x +=
		attrib->xTranslate - ww->model->translate.x;
	    ww->model->anchorObject->position.y +=
		attrib->yTranslate - ww->model->translate.y;

	    ww->model->translate.x = attrib->xTranslate;
	    ww->model->translate.y = attrib->yTranslate;

	    ww->wobbly = ws->wobblyWindows = TRUE;
	}

	if (ww->model->translate.x != 0.0f ||
	    ww->model->translate.y != 0.0f ||
	    ww->model->scale.x	   != 1.0f ||
	    ww->model->scale.y	   != 1.0f)
	    ww->model->transformed = 1;
	else
	    ww->model->transformed = 0;
    }
}

static void
wobblyAddWindowGeometry (CompWindow *w,
			 CompMatrix *matrix,
			 int	    nMatrix,
			 Region     region,
			 Region     clip)
{
    WOBBLY_WINDOW (w);
    WOBBLY_SCREEN (w->screen);

    if (ww->wobbly)
    {
	BoxPtr   pClip;
	int      nClip, nVertices, nIndices;
	GLushort *i;
	GLfloat  *v;
	int      x1, y1, x2, y2;
	float    width, height;
	float    deformedX, deformedY;
	int      x, y, iw, ih;
	int      vSize, it;
	int      gridW, gridH;

	width  = w->width;
	height = w->height;

	gridW = width / ws->opt[WOBBLY_SCREEN_OPTION_GRID_RESOLUTION].value.i;
	if (gridW < ws->opt[WOBBLY_SCREEN_OPTION_MIN_GRID_SIZE].value.i)
	    gridW = ws->opt[WOBBLY_SCREEN_OPTION_MIN_GRID_SIZE].value.i;

	gridH = height / ws->opt[WOBBLY_SCREEN_OPTION_GRID_RESOLUTION].value.i;
	if (gridH < ws->opt[WOBBLY_SCREEN_OPTION_MIN_GRID_SIZE].value.i)
	    gridH = ws->opt[WOBBLY_SCREEN_OPTION_MIN_GRID_SIZE].value.i;

	nClip = region->numRects;
	pClip = region->rects;

	w->texUnits = nMatrix;

	vSize = 2 + nMatrix * 2;

	nVertices = w->vCount;
	nIndices  = w->vCount;

	v = w->vertices + (nVertices * vSize);
	i = w->indices  + nIndices;

	while (nClip--)
	{
	    x1 = pClip->x1;
	    y1 = pClip->y1;
	    x2 = pClip->x2;
	    y2 = pClip->y2;

	    iw = ((x2 - x1 - 1) / gridW) + 1;
	    ih = ((y2 - y1 - 1) / gridH) + 1;

	    if (nIndices + (iw * ih * 4) > w->indexSize)
	    {
		if (!moreWindowIndices (w, nIndices + (iw * ih * 4)))
		    return;

		i = w->indices + nIndices;
	    }

	    iw++;
	    ih++;

	    for (y = 0; y < ih - 1; y++)
	    {
		for (x = 0; x < iw - 1; x++)
		{
		    *i++ = nVertices + iw * (y + 1) + x;
		    *i++ = nVertices + iw * (y + 1) + x + 1;
		    *i++ = nVertices + iw * y + x + 1;
		    *i++ = nVertices + iw * y + x;

		    nIndices += 4;
		}
	    }

	    if (((nVertices + iw * ih) * vSize) > w->vertexSize)
	    {
		if (!moreWindowVertices (w, (nVertices + iw * ih) * vSize))
		    return;

		v = w->vertices + (nVertices * vSize);
	    }

	    for (y = y1;; y += gridH)
	    {
		if (y > y2)
		    y = y2;

		for (x = x1;; x += gridW)
		{
		    if (x > x2)
			x = x2;

		    bezierPatchEvaluate (ww->model,
					 (x - w->attrib.x) / width,
					 (y - w->attrib.y) / height,
					 &deformedX,
					 &deformedY);

		    for (it = 0; it < nMatrix; it++)
		    {
			*v++ = COMP_TEX_COORD_X (&matrix[it], x, y);
			*v++ = COMP_TEX_COORD_Y (&matrix[it], x, y);
		    }

		    *v++ = deformedX;
		    *v++ = deformedY;

		    nVertices++;

		    if (x == x2)
			break;
		}

		if (y == y2)
		    break;
	    }

	    pClip++;
	}

	w->vCount = nIndices;
    }
    else
    {
	UNWRAP (ws, w->screen, addWindowGeometry);
	(*w->screen->addWindowGeometry) (w, matrix, nMatrix, region, clip);
	WRAP (ws, w->screen, addWindowGeometry, wobblyAddWindowGeometry);
    }
}

static void
wobblyDrawWindowGeometry (CompWindow *w)
{
    WOBBLY_WINDOW (w);

    if (ww->wobbly)
    {
	int     texUnit = w->texUnits;
	int     currentTexUnit = 0;
	int     stride = (1 + texUnit) * 2;
	GLfloat *vertices = w->vertices + (stride - 2);

	stride *= sizeof (GLfloat);

	glVertexPointer (2, GL_FLOAT, stride, vertices);

	while (texUnit--)
	{
	    if (texUnit != currentTexUnit)
	    {
		w->screen->clientActiveTexture (GL_TEXTURE0_ARB + texUnit);
		currentTexUnit = texUnit;
	    }
	    vertices -= 2;
	    glTexCoordPointer (2, GL_FLOAT, stride, vertices);
	}

	glDrawElements (GL_QUADS, w->vCount, GL_UNSIGNED_SHORT, w->indices);
    }
    else
    {
	WOBBLY_SCREEN (w->screen);

	UNWRAP (ws, w->screen, drawWindowGeometry);
	(*w->screen->drawWindowGeometry) (w);
	WRAP (ws, w->screen, drawWindowGeometry, wobblyDrawWindowGeometry);
    }
}

static Bool
wobblyPaintWindow (CompWindow		   *w,
		   const WindowPaintAttrib *attrib,
		   Region		   region,
		   unsigned int		   mask)
{
    Bool status;

    WOBBLY_WINDOW (w);
    WOBBLY_SCREEN (w->screen);

    if (mask & PAINT_WINDOW_TRANSFORMED_MASK)
    {
	if (ww->model || isWobblyWin (w))
	    wobblyTransformWindow (w, attrib);

	if (ww->wobbly)
	{
	    WindowPaintAttrib wobblyAttrib = *attrib;

	    wobblyAttrib.xTranslate = 0.0f;
	    wobblyAttrib.yTranslate = 0.0f;
	    wobblyAttrib.xScale     = 1.0f;
	    wobblyAttrib.yScale     = 1.0f;

	    UNWRAP (ws, w->screen, paintWindow);
	    status = (*w->screen->paintWindow) (w, &wobblyAttrib, region, mask);
	    WRAP (ws, w->screen, paintWindow, wobblyPaintWindow);

	    return status;
	}
    }
    else if (ww->model && ww->model->transformed)
    {
	wobblyTransformWindow (w, attrib);
    }

    if (ww->wobbly)
	mask |= PAINT_WINDOW_TRANSFORMED_MASK;

    UNWRAP (ws, w->screen, paintWindow);
    status = (*w->screen->paintWindow) (w, attrib, region, mask);
    WRAP (ws, w->screen, paintWindow, wobblyPaintWindow);

    return status;
}

static void
wobblyHandleEvent (CompDisplay *d,
		   XEvent      *event)
{
    Window     activeWindow = 0;
    CompWindow *w;

    WOBBLY_DISPLAY (d);

    switch (event->type) {
    case ConfigureNotify:
	w = findWindowAtDisplay (d, event->xmap.window);
	if (w && isWobblyWin (w))
	{
	    if (w->attrib.width        != event->xconfigure.width  ||
		w->attrib.height       != event->xconfigure.height ||
		w->attrib.border_width != event->xconfigure.border_width)
	    {
		int width, height;

		WOBBLY_WINDOW (w);
		WOBBLY_SCREEN (w->screen);

		width = event->xconfigure.width;
		width += event->xconfigure.border_width * 2;

		height = event->xconfigure.height;
		height += event->xconfigure.border_width * 2;

		if (w->damaged && w->attrib.map_state == IsViewable &&
		    wobblyEnsureModel (w))
		{
		    modelSetMiddleAnchor (ww->model,
					  w->attrib.x, w->attrib.y,
					  width, height);

		    modelInitSprings (ww->model,
				      w->attrib.x, w->attrib.y,
				      width, height);

		    ww->wobbly = ws->wobblyWindows = TRUE;
		    damageScreen (w->screen);
		}
		else if (ww->model)
		{
		    modelInitObjects (ww->model,
				      w->attrib.x, w->attrib.y,
				      width, height);

		    modelInitSprings (ww->model,
				      w->attrib.x, w->attrib.y,
				      width, height);
		}
	    }

	    if (w->attrib.x != event->xconfigure.x ||
		w->attrib.y != event->xconfigure.y)
	    {
		WOBBLY_WINDOW (w);

		if (w->damaged && w->attrib.map_state == IsViewable &&
		    wobblyEnsureModel (w))
		{
		    wobblyMoveWindow (w,
				      event->xconfigure.x - w->attrib.x,
				      event->xconfigure.y - w->attrib.y);
		}
		else if (ww->model)
		{
		    modelMove (ww->model,
			       event->xconfigure.x - w->attrib.x,
			       event->xconfigure.y - w->attrib.y);
		}
	    }
	}
	break;
    case PropertyNotify:
	if (event->xproperty.atom == d->winActiveAtom)
	{
	    CompScreen *s;

	    s = findScreenAtDisplay (d, event->xproperty.window);
	    if (s)
		activeWindow = s->activeWindow;
	}
    default:
	break;
    }

    UNWRAP (wd, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (wd, d, handleEvent, wobblyHandleEvent);

    switch (event->type) {
    case PropertyNotify:
	if (event->xproperty.atom == d->winActiveAtom)
	{
	    CompScreen *s;

	    s = findScreenAtDisplay (d, event->xproperty.window);
	    if (s && s->activeWindow != activeWindow)
	    {
		CompWindow *w;

		w = findClientWindowAtScreen (s, s->activeWindow);
		if (w && isWobblyWin (w))
		{
		    WOBBLY_WINDOW (w);
		    WOBBLY_SCREEN (w->screen);

		    if (ws->focusEffect && wobblyEnsureModel (w))
		    {
			switch (ws->focusEffect) {
			case WobblyEffectExplode:
			    modelAdjustObjectsForExplosion (ww->model,
							    w->attrib.x,
							    w->attrib.y,
							    w->width,
							    w->height);
			    break;
			case WobblyEffectShiver:
			    modelAdjustObjectsForShiver (ww->model,
							 w->attrib.x,
							 w->attrib.y,
							 w->width,
							 w->height);
			default:
			    break;
			}

			ww->wobbly = ws->wobblyWindows = TRUE;
			damageScreen (w->screen);
		    }
		}
	    }
	}
    default:
	break;
    }
}

static Bool
wobblyDamageWindowRect (CompWindow *w,
			Bool	   initial,
			BoxPtr     rect)
{
    Bool status;

    WOBBLY_SCREEN (w->screen);

    if (initial)
    {
	if (isWobblyWin (w))
	{
	    WOBBLY_WINDOW (w);
	    WOBBLY_SCREEN (w->screen);

	    if (ws->mapEffect && wobblyEnsureModel (w))
	    {
		switch (ws->mapEffect) {
		case WobblyEffectExplode:
		    modelAdjustObjectsForExplosion (ww->model,
						    w->attrib.x, w->attrib.y,
						    w->width, w->height);
		    break;
		case WobblyEffectShiver:
		    modelAdjustObjectsForShiver (ww->model,
						 w->attrib.x, w->attrib.y,
						 w->width, w->height);
		default:
		    break;
		}

		ww->wobbly = ws->wobblyWindows = TRUE;
		damageScreen (w->screen);
	    }
	}
    }

    UNWRAP (ws, w->screen, damageWindowRect);
    status = (*w->screen->damageWindowRect) (w, initial, rect);
    WRAP (ws, w->screen, damageWindowRect, wobblyDamageWindowRect);

    return status;
}

static Bool
wobblyPaintScreen (CompScreen		   *s,
		   const ScreenPaintAttrib *sAttrib,
		   const WindowPaintAttrib *wAttrib,
		   Region		   region,
		   unsigned int		   mask)
{
    Bool status;

    WOBBLY_SCREEN (s);

    if (ws->wobblyWindows)
    {
	mask &= ~PAINT_SCREEN_REGION_MASK;
	mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS_MASK;
    }

    UNWRAP (ws, s, paintScreen);
    status = (*s->paintScreen) (s, sAttrib, wAttrib, region, mask);
    WRAP (ws, s, paintScreen, wobblyPaintScreen);

    return status;
}

static void
wobblyInvisibleWindowMove (CompWindow *w,
			   int        dx,
			   int        dy)
{
    WOBBLY_SCREEN (w->screen);
    WOBBLY_WINDOW (w);

    if (ww->model)
	modelMove (ww->model, dx, dy);

    UNWRAP (ws, w->screen, invisibleWindowMove);
    (*w->screen->invisibleWindowMove) (w, dx, dy);
    WRAP (ws, w->screen, invisibleWindowMove, wobblyInvisibleWindowMove);
}

static Bool
wobblyInitDisplay (CompPlugin  *p,
		   CompDisplay *d)
{
    WobblyDisplay *wd;

    wd = malloc (sizeof (WobblyDisplay));
    if (!wd)
	return FALSE;

    wd->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (wd->screenPrivateIndex < 0)
    {
	free (wd);
	return FALSE;
    }

    WRAP (wd, d, handleEvent, wobblyHandleEvent);

    d->privates[displayPrivateIndex].ptr = wd;

    return TRUE;
}

static void
wobblyFiniDisplay (CompPlugin  *p,
		   CompDisplay *d)
{
    WOBBLY_DISPLAY (d);

    freeScreenPrivateIndex (d, wd->screenPrivateIndex);

    UNWRAP (wd, d, handleEvent);

    free (wd);
}

static Bool
wobblyInitScreen (CompPlugin *p,
		  CompScreen *s)
{
    WobblyScreen *ws;

    WOBBLY_DISPLAY (s->display);

    ws = malloc (sizeof (WobblyScreen));
    if (!ws)
	return FALSE;

    ws->windowPrivateIndex = allocateWindowPrivateIndex (s);
    if (ws->windowPrivateIndex < 0)
    {
	free (ws);
	return FALSE;
    }

    ws->wobblyWindows = FALSE;

    ws->mapEffect   = WobblyEffectShiver;
    ws->focusEffect = WobblyEffectNone;

    wobblyScreenInitOptions (ws);

    WRAP (ws, s, preparePaintScreen, wobblyPreparePaintScreen);
    WRAP (ws, s, donePaintScreen, wobblyDonePaintScreen);
    WRAP (ws, s, paintScreen, wobblyPaintScreen);
    WRAP (ws, s, paintWindow, wobblyPaintWindow);
    WRAP (ws, s, damageWindowRect, wobblyDamageWindowRect);
    WRAP (ws, s, addWindowGeometry, wobblyAddWindowGeometry);
    WRAP (ws, s, drawWindowGeometry, wobblyDrawWindowGeometry);
    WRAP (ws, s, invisibleWindowMove, wobblyInvisibleWindowMove);

    s->privates[wd->screenPrivateIndex].ptr = ws;

    return TRUE;
}

static void
wobblyFiniScreen (CompPlugin *p,
		  CompScreen *s)
{
    WOBBLY_SCREEN (s);

    freeWindowPrivateIndex (s, ws->windowPrivateIndex);

    free (ws->opt[WOBBLY_SCREEN_OPTION_MAP_EFFECT].value.s);
    free (ws->opt[WOBBLY_SCREEN_OPTION_FOCUS_EFFECT].value.s);

    UNWRAP (ws, s, preparePaintScreen);
    UNWRAP (ws, s, donePaintScreen);
    UNWRAP (ws, s, paintScreen);
    UNWRAP (ws, s, paintWindow);
    UNWRAP (ws, s, damageWindowRect);
    UNWRAP (ws, s, addWindowGeometry);
    UNWRAP (ws, s, drawWindowGeometry);
    UNWRAP (ws, s, invisibleWindowMove);

    free (ws);
}

static Bool
wobblyInitWindow (CompPlugin *p,
		  CompWindow *w)
{
    WobblyWindow *ww;

    WOBBLY_SCREEN (w->screen);

    ww = malloc (sizeof (WobblyWindow));
    if (!ww)
	return FALSE;

    ww->model  = 0;
    ww->wobbly = FALSE;

    w->privates[ws->windowPrivateIndex].ptr = ww;

    return TRUE;
}

static void
wobblyFiniWindow (CompPlugin *p,
		  CompWindow *w)
{
    WOBBLY_WINDOW (w);

    if (ww->model)
    {
	free (ww->model->objects);
	free (ww->model);
    }

    free (ww);
}

static Bool
wobblyInit (CompPlugin *p)
{
    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
	return FALSE;

    return TRUE;
}

static void
wobblyFini (CompPlugin *p)
{
    if (displayPrivateIndex >= 0)
	freeDisplayPrivateIndex (displayPrivateIndex);
}

CompPluginVTable wobblyVTable = {
    "wobbly",
    "Wobbly Windows",
    "Use spring model for wobbly window effect",
    wobblyInit,
    wobblyFini,
    wobblyInitDisplay,
    wobblyFiniDisplay,
    wobblyInitScreen,
    wobblyFiniScreen,
    wobblyInitWindow,
    wobblyFiniWindow,
    0, /* GetDisplayOptions */
    0, /* SetDisplayOption */
    wobblyGetScreenOptions,
    wobblySetScreenOption
};

CompPluginVTable *
getCompPluginInfo (void)
{
    return &wobblyVTable;
}
