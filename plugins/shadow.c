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
#include <string.h>
#include <math.h>

#include <comp.h>

#define SHADOW_RADIUS_DEFAULT     6.0f
#define SHADOW_RADIUS_MIN         0.1f
#define SHADOW_RADIUS_MAX        50.0f
#define SHADOW_RADIUS_PRECISION   0.1f

#define SHADOW_OPACITY_DEFAULT   0.75f
#define SHADOW_OPACITY_MIN       0.01f
#define SHADOW_OPACITY_MAX        1.0f
#define SHADOW_OPACITY_PRECISION 0.01f

#define SHADOW_EXPAND_DEFAULT   8
#define SHADOW_EXPAND_MIN       0
#define SHADOW_EXPAND_MAX     100

#define SHADOW_OFFSET_X_DEFAULT   2
#define SHADOW_OFFSET_X_MIN     -50
#define SHADOW_OFFSET_X_MAX      50

#define SHADOW_OFFSET_Y_DEFAULT   2
#define SHADOW_OFFSET_Y_MIN     -50
#define SHADOW_OFFSET_Y_MAX      50

static int displayPrivateIndex;

typedef struct _ShadowDisplay {
    int	screenPrivateIndex;
} ShadowDisplay;

#define SHADOW_SCREEN_OPTION_RADIUS   0
#define SHADOW_SCREEN_OPTION_OPACITY  1
#define SHADOW_SCREEN_OPTION_EXPAND   2
#define SHADOW_SCREEN_OPTION_OFFSET_X 3
#define SHADOW_SCREEN_OPTION_OFFSET_Y 4
#define SHADOW_SCREEN_OPTION_NUM      5

typedef struct _ShadowScreen {
    CompOption opt[SHADOW_SCREEN_OPTION_NUM];

    GLenum target;
    GLuint texture;
    float  dx, dy;

    int size;
    int expand;
    int xOffset;
    int yOffset;

    PaintWindowProc        paintWindow;
    DamageWindowRegionProc damageWindowRegion;
    DamageWindowRectProc   damageWindowRect;
} ShadowScreen;

#define GET_SHADOW_DISPLAY(d)				       \
    ((ShadowDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define SHADOW_DISPLAY(d)		       \
    ShadowDisplay *sd = GET_SHADOW_DISPLAY (d)

#define GET_SHADOW_SCREEN(s, sd)				   \
    ((ShadowScreen *) (s)->privates[(sd)->screenPrivateIndex].ptr)

#define SHADOW_SCREEN(s)						      \
    ShadowScreen *ss = GET_SHADOW_SCREEN (s, GET_SHADOW_DISPLAY (s->display))

#define NUM_OPTIONS(s) (sizeof ((s)->opt) / sizeof (CompOption))

#define SHADOW_WINDOW(w) ((w)->type != (w)->screen->display->winDesktopAtom)

static float
gaussian (float r,
	  float x,
	  float y)
{
    return (1 / (sqrt (2 * M_PI * r))) * exp ((- (x * x + y * y)) / (2 * r * r));
}

typedef struct _ShadowMap {
    int	  size;
    float *data;
} ShadowMap;

static ShadowMap *
shadowCreateGaussianMap (float r)
{
    ShadowMap *m;
    int	      size = ((int) ceil ((r * 3)) + 1) & ~1;
    int	      center = size / 2;
    int	      x, y;
    float     t, g;

    m = malloc (sizeof (ShadowMap) + size * size * sizeof (float));
    m->size = size;
    m->data = (float *) (m + 1);
    t = 0.0;

    for (y = 0; y < size; y++)
    {
	for (x = 0; x < size; x++)
	{
	    g = gaussian (r, (float) (x - center), (float) (y - center));
	    t += g;
	    m->data[y * size + x] = g;
	}
    }

    for (y = 0; y < size; y++)
    {
	for (x = 0; x < size; x++)
	{
	    m->data[y * size + x] /= t;
	}
    }

    return m;
}

static unsigned char
shadowSumGaussian (ShadowMap *map,
		   float     opacity,
		   int       x,
		   int       y,
		   int       width,
		   int       height)
{
    int	  fx, fy;
    float *g_data;
    float *g_line = map->data;
    int	  g_size = map->size;
    int	  center = g_size / 2;
    int	  fx_start, fx_end;
    int	  fy_start, fy_end;
    float v;

    /*
     * Compute set of filter values which are "in range",
     * that's the set with:
     *	0 <= x + (fx-center) && x + (fx-center) < width &&
     *  0 <= y + (fy-center) && y + (fy-center) < height
     *
     *  0 <= x + (fx - center)	x + fx - center < width
     *  center - x <= fx	fx < width + center - x
     */
    fx_start = center - x;
    if (fx_start < 0)
	fx_start = 0;
    fx_end = width + center - x;
    if (fx_end > g_size)
	fx_end = g_size;

    fy_start = center - y;
    if (fy_start < 0)
	fy_start = 0;
    fy_end = height + center - y;
    if (fy_end > g_size)
	fy_end = g_size;

    g_line = g_line + fy_start * g_size + fx_start;

    v = 0;
    for (fy = fy_start; fy < fy_end; fy++)
    {
	g_data = g_line;
	g_line += g_size;

	for (fx = fx_start; fx < fx_end; fx++)
	    v += *g_data++;
    }
    if (v > 1)
	v = 1;

    return ((unsigned char) (v * opacity * 255.0));
}

static unsigned char *
shadowCreateImage (ShadowMap *map,
		   float     opacity,
		   int	     *w,
		   int	     *h)
{
    unsigned char *data;
    int		  gsize = map->size;
    int		  width = gsize;
    int		  height = gsize;
    int		  ylimit, xlimit;
    int		  swidth = width + gsize;
    int		  sheight = height + gsize;
    int		  center = gsize / 2;
    int		  x, y;
    unsigned char d;
    int		  x_diff;

    *w = swidth;
    *h = sheight;

    data = malloc (swidth * sheight * sizeof (unsigned char));
    if (!data)
	return 0;

    /* center (fill the complete data array) */
    d = shadowSumGaussian (map, opacity, center, center, width, height);
    memset (data, d, sheight * swidth);

    /* corners */
    ylimit = gsize;
    if (ylimit > sheight / 2)
	ylimit = (sheight + 1) / 2;
    xlimit = gsize;
    if (xlimit > swidth / 2)
	xlimit = (swidth + 1) / 2;

    for (y = 0; y < ylimit; y++)
    {
	for (x = 0; x < xlimit; x++)
	{
	    d = shadowSumGaussian (map, opacity,
				   x - center, y - center,
				   width, height);

	    data[y * swidth + x] = d;
	    data[(sheight - y - 1) * swidth + x] = d;
	    data[(sheight - y - 1) * swidth + (swidth - x - 1)] = d;
	    data[y * swidth + (swidth - x - 1)] = d;
	}
    }

    /* top/bottom */
    x_diff = swidth - (gsize * 2);
    if (x_diff > 0 && ylimit > 0)
    {
	for (y = 0; y < ylimit; y++)
	{
	    d = shadowSumGaussian (map, opacity,
				   center, y - center, width, height);

	    memset (&data[y * swidth + gsize], d, x_diff);
	    memset (&data[(sheight - y - 1) * swidth + gsize], d, x_diff);
	}
    }

    /* sides */
    for (x = 0; x < xlimit; x++)
    {
	d = shadowSumGaussian (map, opacity,
			       x - center, center,
			       width, height);

	for (y = gsize; y < sheight - gsize; y++)
	{
	    data[y * swidth + x] = d;
	    data[y * swidth + (swidth - x - 1)] = d;
	}
    }

    return data;
}

static void
shadowComputeGaussian (CompScreen *s,
		       float      radius,
		       float	  opacity)
{
    ShadowMap     *map;
    unsigned char *data;
    int		  w, h;

    SHADOW_SCREEN (s);

    map = shadowCreateGaussianMap (radius);
    if (!map)
	return;

    data = shadowCreateImage (map, opacity, &w, &h);
    if (!data)
    {
	free (map);
	return;
    }

    ss->size = map->size;

    if (s->textureNonPowerOfTwo || (POWER_OF_TWO (w) && POWER_OF_TWO (h)))
    {
	ss->target = GL_TEXTURE_2D;
	ss->dx = 1.0f / w;
	ss->dy = 1.0f / h;
    }
    else
    {
	ss->target = GL_TEXTURE_RECTANGLE_NV;
	ss->dx = 1.0f;
	ss->dy = 1.0f;
    }

    if (!ss->texture)
	glGenTextures (1, &ss->texture);

    glBindTexture (ss->target, ss->texture);
    glTexImage2D (ss->target, 0, GL_INTENSITY, w, h, 0,
		  GL_LUMINANCE, GL_UNSIGNED_BYTE, data);

    glTexParameteri (ss->target, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri (ss->target, GL_TEXTURE_WRAP_T, GL_CLAMP);

    glTexParameteri (ss->target, GL_TEXTURE_MIN_FILTER,
		     s->display->textureFilter);
    glTexParameteri (ss->target, GL_TEXTURE_MAG_FILTER,
		     s->display->textureFilter);

    glBindTexture (GL_TEXTURE_2D, 0);

    free (data);
    free (map);
}

static CompOption *
shadowGetScreenOptions (CompScreen *screen,
			int	   *count)
{
    SHADOW_SCREEN (screen);

    *count = NUM_OPTIONS (ss);
    return ss->opt;
}

static Bool
shadowSetScreenOption (CompScreen      *screen,
		       char	       *name,
		       CompOptionValue *value)
{
    CompOption *o;
    int	       index;

    SHADOW_SCREEN (screen);

    o = compFindOption (ss->opt, NUM_OPTIONS (ss), name, &index);
    if (!o)
	return FALSE;

    switch (index) {
    case SHADOW_SCREEN_OPTION_RADIUS:
	if (compSetFloatOption (o, value))
	{
	    shadowComputeGaussian (screen,
				   o->value.f,
				   ss->opt[SHADOW_SCREEN_OPTION_OPACITY].value.f);
	    damageScreen (screen);
	    return TRUE;
	}
	break;
    case SHADOW_SCREEN_OPTION_OPACITY:
	if (compSetFloatOption (o, value))
	{
	    shadowComputeGaussian (screen,
				   ss->opt[SHADOW_SCREEN_OPTION_RADIUS].value.f,
				   o->value.f);
	    damageScreen (screen);
	    return TRUE;
	}
	break;
    case SHADOW_SCREEN_OPTION_EXPAND:
	if (compSetIntOption (o, value))
	{
	    ss->expand = o->value.i;
	    damageScreen (screen);
	    return TRUE;
	}
	break;
    case SHADOW_SCREEN_OPTION_OFFSET_X:
	if (compSetIntOption (o, value))
	{
	    ss->xOffset = o->value.i;
	    damageScreen (screen);
	    return TRUE;
	}
	break;
    case SHADOW_SCREEN_OPTION_OFFSET_Y:
	if (compSetIntOption (o, value))
	{
	    ss->yOffset = o->value.i;
	    damageScreen (screen);
	    return TRUE;
	}
    default:
	break;
    }

    return FALSE;
}

static void
shadowScreenInitOptions (ShadowScreen *fs)
{
    CompOption *o;

    o = &fs->opt[SHADOW_SCREEN_OPTION_RADIUS];
    o->name		= "radius";
    o->shortDesc	= "Shadow radius";
    o->longDesc		= "Drop shadow gaussian radius";
    o->type		= CompOptionTypeFloat;
    o->value.f		= SHADOW_RADIUS_DEFAULT;
    o->rest.f.min	= SHADOW_RADIUS_MIN;
    o->rest.f.max	= SHADOW_RADIUS_MAX;
    o->rest.f.precision	= SHADOW_RADIUS_PRECISION;

    o = &fs->opt[SHADOW_SCREEN_OPTION_OPACITY];
    o->name		= "opacity";
    o->shortDesc	= "Shadow opacity";
    o->longDesc		= "Drop shadow opacity";
    o->type		= CompOptionTypeFloat;
    o->value.f		= SHADOW_OPACITY_DEFAULT;
    o->rest.f.min	= SHADOW_OPACITY_MIN;
    o->rest.f.max	= SHADOW_OPACITY_MAX;
    o->rest.f.precision	= SHADOW_OPACITY_PRECISION;

    o = &fs->opt[SHADOW_SCREEN_OPTION_EXPAND];
    o->name		= "expand";
    o->shortDesc	= "Shadow expansion";
    o->longDesc		= "Drop shadow expansion";
    o->type		= CompOptionTypeInt;
    o->value.i		= SHADOW_EXPAND_DEFAULT;
    o->rest.i.min	= SHADOW_EXPAND_MIN;
    o->rest.i.max	= SHADOW_EXPAND_MAX;

    o = &fs->opt[SHADOW_SCREEN_OPTION_OFFSET_X];
    o->name		= "shadow_offset_x";
    o->shortDesc	= "Shadow X Offset";
    o->longDesc		= "Drop shadow X offset";
    o->type		= CompOptionTypeInt;
    o->value.i		= SHADOW_OFFSET_X_DEFAULT;
    o->rest.i.min	= SHADOW_OFFSET_X_MIN;
    o->rest.i.max	= SHADOW_OFFSET_X_MAX;

    o = &fs->opt[SHADOW_SCREEN_OPTION_OFFSET_Y];
    o->name		= "shadow_offset_y";
    o->shortDesc	= "Shadow Y Offset";
    o->longDesc		= "Drop shadow Y offset";
    o->type		= CompOptionTypeInt;
    o->value.i		= SHADOW_OFFSET_Y_DEFAULT;
    o->rest.i.min	= SHADOW_OFFSET_Y_MIN;
    o->rest.i.max	= SHADOW_OFFSET_Y_MAX;
}

static Bool
shadowDamageWindowRegion (CompWindow *w,
			  Region     region)
{
    Bool status;

    SHADOW_SCREEN (w->screen);

    UNWRAP (ss, w->screen, damageWindowRegion);
    status = (*w->screen->damageWindowRegion) (w, region);
    WRAP (ss, w->screen, damageWindowRegion, shadowDamageWindowRegion);

    if (SHADOW_WINDOW (w))
    {
	REGION rect;

	rect.rects = &rect.extents;
	rect.numRects = rect.size = 1;

	rect.extents.x1 = region->extents.x1 - ss->expand + ss->xOffset;
	rect.extents.y1 = region->extents.y1 - ss->expand + ss->yOffset;
	rect.extents.x2 = region->extents.x2 + ss->expand + ss->xOffset;
	rect.extents.y2 = region->extents.y2 + ss->expand + ss->yOffset;

	damageScreenRegion (w->screen, &rect);

	if (ss->expand >= ss->xOffset && ss->expand >= ss->yOffset)
	    status = TRUE;
    }

    return status;
}

static Bool
shadowDamageWindowRect (CompWindow *w,
			Bool	   initial,
			BoxPtr     rect)
{
    Bool status;

    SHADOW_SCREEN (w->screen);

    UNWRAP (ss, w->screen, damageWindowRect);
    status = (*w->screen->damageWindowRect) (w, initial, rect);
    WRAP (ss, w->screen, damageWindowRect, shadowDamageWindowRect);

    if (SHADOW_WINDOW (w))
    {
	REGION region;

	region.rects = &region.extents;
	region.numRects = region.size = 1;

	if (initial)
	{
	    region.extents.x1 = w->region->extents.x1 - ss->expand + ss->xOffset;
	    region.extents.y1 = w->region->extents.y1 - ss->expand + ss->yOffset;
	    region.extents.x2 = w->region->extents.x2 + ss->expand + ss->xOffset;
	    region.extents.y2 = w->region->extents.y2 + ss->expand + ss->yOffset;
	}
	else if (w->alpha)
	{
	    region.extents.x1 = rect->x1 - ss->expand + ss->xOffset;
	    region.extents.y1 = rect->y1 - ss->expand + ss->yOffset;
	    region.extents.x2 = rect->x2 + ss->expand + ss->xOffset;
	    region.extents.y2 = rect->y2 + ss->expand + ss->yOffset;
	}
	else
	    return status;

	damageScreenRegion (w->screen, &region);

	if (ss->expand >= ss->xOffset && ss->expand >= ss->yOffset)
	    status = TRUE;
    }

    return status;
}

static Bool
shadowPaintWindow (CompWindow		   *w,
		   const WindowPaintAttrib *attrib,
		   Region		   region,
		   unsigned int		   mask)
{
    Bool status;

    SHADOW_SCREEN (w->screen);

    if (SHADOW_WINDOW (w) && (!(mask & PAINT_WINDOW_SOLID_MASK)))
    {
	CompMatrix matrix[2];
	GLushort   opacity;
	REGION     rect;
	BoxRec     box;
	int        nMatrix = 1;

	opacity = MULTIPLY_USHORT (w->opacity, attrib->opacity);
	if (w->alpha || opacity != OPAQUE)
	    mask |= PAINT_WINDOW_TRANSLUCENT_MASK;
	else
	    mask |= PAINT_WINDOW_SOLID_MASK;

	if (mask & PAINT_WINDOW_TRANSFORMED_MASK)
	    region = &infiniteRegion;

	w->vCount = 0;

	box.x1 = w->region->extents.x1 - ss->expand + ss->size;
	box.y1 = w->region->extents.y1 - ss->expand + ss->size;
	box.x2 = w->region->extents.x2 + ss->expand - ss->size;
	box.y2 = w->region->extents.y2 + ss->expand - ss->size;

	/* in case window width is too small */
	if (box.x1 >= box.x2)
	{
	    box.x1 = (w->region->extents.x1 + w->region->extents.x2) / 2;
	    box.x2 = box.x1 + 1;
	}

	/* in case window height is too small */
	if (box.y1 >= box.y2)
	{
	    box.y1 = (w->region->extents.y1 + w->region->extents.y2) / 2;
	    box.y2 = box.y1 + 1;
	}

	if (w->alpha &&
	    w->screen->textureEnvCombine &&
	    w->screen->maxTextureUnits > 1)
	{
	    if (!w->pixmap)
		bindWindow (w);

	    matrix[1] = w->texture.matrix;
	    matrix[1].x0 -= ((w->attrib.x + ss->xOffset) * w->matrix.xx);
	    matrix[1].y0 -= ((w->attrib.y + ss->yOffset) * w->matrix.yy);

	    nMatrix++;
	}

	rect.rects = &rect.extents;
	rect.numRects = rect.size = 1;

	/* top left */
	matrix->xx = ss->dx; matrix->xy = 0.0f;
	matrix->yx = 0.0f;   matrix->yy = ss->dy;

	matrix->x0 = -((box.x1 - ss->size + ss->xOffset) * ss->dx);
	matrix->y0 = -((box.y1 - ss->size + ss->yOffset) * ss->dy);

	rect.extents.x1 = box.x1 - ss->size + ss->xOffset;
	rect.extents.y1 = box.y1 - ss->size + ss->yOffset;
	rect.extents.x2 = rect.extents.x1 + ss->size;
	rect.extents.y2 = rect.extents.y1 + ss->size;

	(*w->screen->addWindowGeometry) (w, matrix, nMatrix, &rect, region);

	/* top */
	matrix->xx = 0.0f;
	matrix->x0 = ss->dx * ss->size;

	rect.extents.x1 = rect.extents.x2;
	rect.extents.x2 = box.x2 + ss->xOffset;

	(*w->screen->addWindowGeometry) (w, matrix, nMatrix, &rect, region);

	/* top right */
	matrix->x0 = -((box.x2 - ss->size + ss->xOffset) * ss->dx);
	matrix->xx = ss->dx;

	rect.extents.x1 = rect.extents.x2;
	rect.extents.x2 = box.x2 + ss->size + ss->xOffset;

	(*w->screen->addWindowGeometry) (w, matrix, nMatrix, &rect, region);

	/* left */
	matrix->x0 = -((box.x1 - ss->size + ss->xOffset) * ss->dx);
	matrix->xx = ss->dx;
	matrix->yy = 0.0f;
	matrix->y0 = ss->dy * ss->size;

	rect.extents.x1 = box.x1 - ss->size + ss->xOffset;
	rect.extents.y1 = rect.extents.y2;
	rect.extents.x2 = rect.extents.x1 + ss->size;
	rect.extents.y2 = box.y2 + ss->yOffset;

	(*w->screen->addWindowGeometry) (w, matrix, nMatrix, &rect, region);

	/* middle */
	matrix->xx = 0.0f;
	matrix->x0 = ss->dx * ss->size;

	rect.extents.x1 = rect.extents.x2;
	rect.extents.x2 = box.x2 + ss->xOffset;

	(*w->screen->addWindowGeometry) (w, matrix, nMatrix, &rect, region);

	/* right */
	matrix->x0 = -((box.x2 - ss->size + ss->xOffset) * ss->dx);
	matrix->xx = ss->dx;

	rect.extents.x1 = rect.extents.x2;
	rect.extents.x2 = box.x2 + ss->size + ss->xOffset;

	(*w->screen->addWindowGeometry) (w, matrix, nMatrix, &rect, region);

	/* bottom left */
	matrix->yy = ss->dy;
	matrix->x0 = -((box.x1 - ss->size + ss->xOffset) * ss->dx);
	matrix->y0 = -((box.y2 - ss->size + ss->yOffset) * ss->dy);

	rect.extents.x1 = box.x1 - ss->size + ss->xOffset;
	rect.extents.y1 = rect.extents.y2;
	rect.extents.x2 = rect.extents.x1 + ss->size;
	rect.extents.y2 = box.y2 + ss->size + ss->yOffset;

	(*w->screen->addWindowGeometry) (w, matrix, nMatrix, &rect, region);

	/* bottom */
	matrix->xx = 0.0f;
	matrix->x0 = ss->dx * ss->size;

	rect.extents.x1 = rect.extents.x2;
	rect.extents.x2 = box.x2 + ss->xOffset;

	(*w->screen->addWindowGeometry) (w, matrix, nMatrix, &rect, region);

	/* bottom right */
	matrix->x0 = -((box.x2 - ss->size + ss->xOffset) * ss->dx);
	matrix->xx = ss->dx;

	rect.extents.x1 = rect.extents.x2;
	rect.extents.x2 = box.x2 + ss->size + ss->xOffset;

	(*w->screen->addWindowGeometry) (w, matrix, nMatrix, &rect, region);

	if (w->vCount)
	{
	    glEnable (GL_BLEND);

	    glPushMatrix ();

	    if (mask & PAINT_WINDOW_TRANSFORMED_MASK)
	    {
		glTranslatef (w->attrib.x + attrib->xTranslate,
			      w->attrib.y + attrib->yTranslate, 0.0f);
		glScalef (attrib->xScale, attrib->yScale, 0.0f);
		glTranslatef (-w->attrib.x, -w->attrib.y, 0.0f);
	    }

	    glEnable (ss->target);
	    glBindTexture (ss->target, ss->texture);

	    glColor4us (0x0, 0x0, 0x0, opacity);
	    glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	    if (nMatrix > 1)
	    {
		w->screen->activeTexture (GL_TEXTURE1_ARB);

		if (mask & PAINT_WINDOW_TRANSFORMED_MASK)
		    enableTexture (w->screen, &w->texture,
				   COMP_TEXTURE_FILTER_GOOD);
		else
		    enableTexture (w->screen, &w->texture,
				   COMP_TEXTURE_FILTER_FAST);

		glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
		glTexEnvf (GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_REPLACE);
		glTexEnvf (GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PRIMARY_COLOR);
		glTexEnvf (GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
		glTexEnvf (GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
		glTexEnvf (GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_TEXTURE);
		glTexEnvf (GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_PREVIOUS);
		glTexEnvf (GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
		glTexEnvf (GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
	    }

	    (*w->screen->drawWindowGeometry) (w);

	    if (nMatrix > 1)
	    {
		disableTexture (&w->texture);
		w->screen->activeTexture (GL_TEXTURE0_ARB);
	    }

	    glBindTexture (ss->target, 0);
	    glDisable (ss->target);

	    glPopMatrix ();

	    glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	    glColor4usv (defaultColor);
	    glDisable (GL_BLEND);

	    w->vCount = 0;
	}
    }

    UNWRAP (ss, w->screen, paintWindow);
    status = (*w->screen->paintWindow) (w, attrib, region, mask);
    WRAP (ss, w->screen, paintWindow, shadowPaintWindow);

    return status;
}

static Bool
shadowInitDisplay (CompPlugin  *p,
		   CompDisplay *d)
{
    ShadowDisplay *sd;

    sd = malloc (sizeof (ShadowDisplay));
    if (!sd)
	return FALSE;

    sd->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (sd->screenPrivateIndex < 0)
    {
	free (sd);
	return FALSE;
    }

    d->privates[displayPrivateIndex].ptr = sd;

    return TRUE;
}

static void
shadowFiniDisplay (CompPlugin  *p,
		   CompDisplay *d)
{
    SHADOW_DISPLAY (d);

    freeScreenPrivateIndex (d, sd->screenPrivateIndex);

    free (sd);
}

static Bool
shadowInitScreen (CompPlugin *p,
		  CompScreen *s)
{
    ShadowScreen  *ss;

    SHADOW_DISPLAY (s->display);

    ss = malloc (sizeof (ShadowScreen));
    if (!ss)
	return FALSE;

    ss->texture = 0;

    ss->expand  = SHADOW_EXPAND_DEFAULT;
    ss->xOffset = SHADOW_OFFSET_X_DEFAULT;
    ss->yOffset = SHADOW_OFFSET_Y_DEFAULT;

    shadowScreenInitOptions (ss);

    WRAP (ss, s, paintWindow, shadowPaintWindow);
    WRAP (ss, s, damageWindowRect, shadowDamageWindowRect);
    WRAP (ss, s, damageWindowRegion, shadowDamageWindowRegion);

    s->privates[sd->screenPrivateIndex].ptr = ss;

    shadowComputeGaussian (s,
			   ss->opt[SHADOW_SCREEN_OPTION_RADIUS].value.f,
			   ss->opt[SHADOW_SCREEN_OPTION_OPACITY].value.f);

    return TRUE;
}

static void
shadowFiniScreen (CompPlugin *p,
		  CompScreen *s)
{
    SHADOW_SCREEN (s);

    if (ss->texture)
	glDeleteTextures (1, &ss->texture);

    UNWRAP (ss, s, paintWindow);
    UNWRAP (ss, s, damageWindowRect);
    UNWRAP (ss, s, damageWindowRegion);

    free (ss);
}

static Bool
shadowInit (CompPlugin *p)
{
    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
	return FALSE;

    return TRUE;
}

static void
shadowFini (CompPlugin *p)
{
    if (displayPrivateIndex >= 0)
	freeDisplayPrivateIndex (displayPrivateIndex);
}

static CompPluginVTable shadowVTable = {
    "shadow",
    "Window Shadows",
    "Window drop shadows",
    shadowInit,
    shadowFini,
    shadowInitDisplay,
    shadowFiniDisplay,
    shadowInitScreen,
    shadowFiniScreen,
    0, /* InitWindow */
    0, /* FiniWindow */
    0, /* GetDisplayOptions */
    0, /* SetDisplayOption */
    shadowGetScreenOptions,
    shadowSetScreenOption
};

CompPluginVTable *
getCompPluginInfo (void)
{
    return &shadowVTable;
}
