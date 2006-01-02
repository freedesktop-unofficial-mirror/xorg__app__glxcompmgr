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

ScreenPaintAttrib defaultScreenPaintAttrib = {
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f
};

WindowPaintAttrib defaultWindowPaintAttrib = {
    OPAQUE, 0.0f, 0.0f, 1.0f, 1.0f
};

void
preparePaintScreen (CompScreen *screen,
		    int	       msSinceLastPaint) {}

void
donePaintScreen (CompScreen *screen) {}

void
paintTransformedScreen (CompScreen		*screen,
			const ScreenPaintAttrib *sAttrib,
			const WindowPaintAttrib *wAttrib,
			unsigned int		mask)
{
    CompWindow *w;
    int	       windowMask;
    int	       backgroundMask;

    glPushMatrix ();

    glTranslatef (sAttrib->xTranslate,
		  sAttrib->yTranslate,
		  sAttrib->zTranslate - BASE_Z_TRANSLATE);

    glRotatef (sAttrib->xRotate, 0.0f, 1.0f, 0.0f);
    glRotatef (sAttrib->vRotate,
	       1.0f - sAttrib->xRotate / 90.0f,
	       0.0f,
	       sAttrib->xRotate / 90.0f);
    glRotatef (sAttrib->yRotate, 0.0f, 1.0f, 0.0f);

    glTranslatef (-0.5f, -0.5f, 0.5f);
    glScalef (1.0f / screen->width, -1.0f / screen->height, 1.0f);
    glTranslatef (0.0f, -screen->height, 0.0f);

    if (mask & PAINT_SCREEN_TRANSFORMED_MASK)
    {
	windowMask = PAINT_WINDOW_ON_TRANSFORMED_SCREEN_MASK;
	backgroundMask = PAINT_BACKGROUND_ON_TRANSFORMED_SCREEN_MASK;

	if (mask & PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS_MASK)
	{
	    backgroundMask |= PAINT_BACKGROUND_WITH_STENCIL_MASK;

	    (*screen->paintBackground) (screen, &screen->region,
					backgroundMask);

	    glEnable (GL_STENCIL_TEST);

	    for (w = screen->windows; w; w = w->next)
	    {
		if (w->destroyed || w->attrib.map_state != IsViewable)
		    continue;

		if (w->damaged)
		    (*screen->paintWindow) (w, wAttrib, &screen->region,
					    windowMask);
	    }

	    glDisable (GL_STENCIL_TEST);

	    glPopMatrix ();

	    return;
	}
    }
    else
	windowMask = backgroundMask = 0;

    (*screen->paintBackground) (screen, &screen->region, backgroundMask);

    for (w = screen->windows; w; w = w->next)
    {
	if (w->destroyed || w->attrib.map_state != IsViewable)
	    continue;

	if (w->damaged)
	    (*screen->paintWindow) (w, wAttrib, &screen->region, windowMask);
    }

    glPopMatrix ();
}

Bool
paintScreen (CompScreen		     *screen,
	     const ScreenPaintAttrib *sAttrib,
	     const WindowPaintAttrib *wAttrib,
	     Region		     region,
	     unsigned int	     mask)
{
    static Region tmpRegion = NULL;
    CompWindow	  *w;

    if (mask & PAINT_SCREEN_REGION_MASK)
    {
	if ((mask & PAINT_SCREEN_TRANSFORMED_MASK) ||
	    (mask & PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS_MASK))
	{
	    if (mask & PAINT_SCREEN_FULL_MASK)
	    {
		(*screen->paintTransformedScreen) (screen, sAttrib, wAttrib,
						   mask);

		return TRUE;
	    }

	    return FALSE;
	}

	/* fall through and redraw region */
    }
    else if (mask & PAINT_SCREEN_FULL_MASK)
    {
	(*screen->paintTransformedScreen) (screen, sAttrib, wAttrib, mask);

	return TRUE;
    }
    else
	return FALSE;

    if (!tmpRegion)
    {
	tmpRegion = XCreateRegion ();
	if (!tmpRegion)
	    return FALSE;
    }

    XSubtractRegion (region, &emptyRegion, tmpRegion);

    glPushMatrix ();

    glTranslatef (0.0f, 0.0f, -BASE_Z_TRANSLATE);

    glTranslatef (-0.5f, -0.5f, 0.5f);
    glScalef (1.0f / screen->width, -1.0f / screen->height, 1.0f);
    glTranslatef (0.0f, -screen->height, 0.0f);

    /* paint solid windows */
    for (w = screen->reverseWindows; w; w = w->prev)
    {
	if (w->destroyed || w->invisible)
	    continue;

	if ((*screen->paintWindow) (w, wAttrib, tmpRegion,
				    PAINT_WINDOW_SOLID_MASK))
	    XSubtractRegion (tmpRegion, w->region, tmpRegion);

	/* copy region */
	XSubtractRegion (tmpRegion, &emptyRegion, w->clip);

	if (!tmpRegion->numRects)
	    break;
    }

    if (tmpRegion->numRects)
	(*screen->paintBackground) (screen, tmpRegion, 0);

    /* paint translucent windows */
    for (w = screen->windows; w; w = w->next)
    {
	if (w->destroyed || w->invisible)
	    continue;

	if (w->clip->numRects)
	    (*screen->paintWindow) (w, wAttrib, w->clip,
				    PAINT_WINDOW_TRANSLUCENT_MASK);
    }

    glPopMatrix ();

    return TRUE;
}

#define ADD_QUAD(data, m, n, x1, y1, x2, y2)	       \
    for (it = 0; it < n; it++)			       \
    {						       \
	*(data)++ = COMP_TEX_COORD_X (&m[it], x1, y2); \
	*(data)++ = COMP_TEX_COORD_Y (&m[it], x1, y2); \
    }						       \
    *(data)++ = (x1);				       \
    *(data)++ = (y2);				       \
    for (it = 0; it < n; it++)			       \
    {						       \
	*(data)++ = COMP_TEX_COORD_X (&m[it], x2, y2); \
	*(data)++ = COMP_TEX_COORD_Y (&m[it], x2, y2); \
    }						       \
    *(data)++ = (x2);				       \
    *(data)++ = (y2);				       \
    for (it = 0; it < n; it++)			       \
    {						       \
	*(data)++ = COMP_TEX_COORD_X (&m[it], x2, y1); \
	*(data)++ = COMP_TEX_COORD_Y (&m[it], x2, y1); \
    }						       \
    *(data)++ = (x2);				       \
    *(data)++ = (y1);				       \
    for (it = 0; it < n; it++)			       \
    {						       \
	*(data)++ = COMP_TEX_COORD_X (&m[it], x1, y1); \
	*(data)++ = COMP_TEX_COORD_Y (&m[it], x1, y1); \
    }						       \
    *(data)++ = (x1);				       \
    *(data)++ = (y1)

Bool
moreWindowVertices (CompWindow *w,
		    int        newSize)
{
    if (newSize > w->vertexSize)
    {
	GLfloat *vertices;

	vertices = realloc (w->vertices, sizeof (GLfloat) * newSize);
	if (!vertices)
	    return FALSE;

	w->vertices = vertices;
	w->vertexSize = newSize;
    }

    return TRUE;
}

Bool
moreWindowIndices (CompWindow *w,
		   int        newSize)
{
    if (newSize > w->indexSize)
    {
	GLushort *indices;

	indices = realloc (w->indices, sizeof (GLushort) * newSize);
	if (!indices)
	    return FALSE;

	w->indices = indices;
	w->indexSize = newSize;
    }

    return TRUE;
}

void
addWindowGeometry (CompWindow *w,
		   CompMatrix *matrix,
		   int	      nMatrix,
		   Region     region,
		   Region     clip)
{
    BoxRec full;

    w->texUnits = nMatrix;

    full = clip->extents;
    if (region->extents.x1 > full.x1)
	full.x1 = region->extents.x1;
    if (region->extents.y1 > full.y1)
	full.y1 = region->extents.y1;
    if (region->extents.x2 < full.x2)
	full.x2 = region->extents.x2;
    if (region->extents.y2 < full.y2)
	full.y2 = region->extents.y2;

    if (full.x1 < full.x2 && full.y1 < full.y2)
    {
	BoxPtr  pBox;
	int     nBox;
	BoxPtr  pClip;
	int     nClip;
	BoxRec  cbox;
	int     vSize;
	int     n, it, x1, y1, x2, y2;
	GLfloat *d;

	pBox = region->rects;
	nBox = region->numRects;

	vSize = 2 + nMatrix * 2;

	n = w->vCount / 4;

	if ((n + nBox) * vSize * 4 > w->vertexSize)
	{
	    if (!moreWindowVertices (w, (n + nBox) * vSize * 4))
		return;
	}

	d = w->vertices + (w->vCount * vSize);

	while (nBox--)
	{
	    x1 = pBox->x1;
	    y1 = pBox->y1;
	    x2 = pBox->x2;
	    y2 = pBox->y2;

	    pBox++;

	    if (x1 < full.x1)
		x1 = full.x1;
	    if (y1 < full.y1)
		y1 = full.y1;
	    if (x2 > full.x2)
		x2 = full.x2;
	    if (y2 > full.y2)
		y2 = full.y2;

	    if (x1 < x2 && y1 < y2)
	    {
		nClip = clip->numRects;

		if (nClip == 1)
		{
		    ADD_QUAD (d, matrix, nMatrix, x1, y1, x2, y2);

		    n++;
		}
		else
		{
		    pClip = clip->rects;

		    if (((n + nClip) * vSize * 4) > w->vertexSize)
		    {
			if (!moreWindowVertices (w, (n + nClip) * vSize * 4))
			    return;

			d = w->vertices + (n * vSize * 4);
		    }

		    while (nClip--)
		    {
			cbox = *pClip;

			pClip++;

			if (cbox.x1 < x1)
			    cbox.x1 = x1;
			if (cbox.y1 < y1)
			    cbox.y1 = y1;
			if (cbox.x2 > x2)
			    cbox.x2 = x2;
			if (cbox.y2 > y2)
			    cbox.y2 = y2;

			if (cbox.x1 < cbox.x2 && cbox.y1 < cbox.y2)
			{
			    ADD_QUAD (d, matrix, nMatrix,
				      cbox.x1, cbox.y1, cbox.x2, cbox.y2);

			    n++;
			}
		    }
		}
	    }
	}
	w->vCount = n * 4;
    }
}

void
drawWindowGeometry (CompWindow *w)
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

    glDrawArrays (GL_QUADS, 0, w->vCount);
}

Bool
paintWindow (CompWindow		     *w,
	     const WindowPaintAttrib *attrib,
	     Region		     region,
	     unsigned int	     mask)
{
    GLushort opacity;

    if (mask & PAINT_WINDOW_SOLID_MASK)
    {
	if (w->alpha)
	    return FALSE;

	opacity = MULTIPLY_USHORT (w->opacity, attrib->opacity);
	if (opacity != OPAQUE)
	    return FALSE;
    }
    else if (mask & PAINT_WINDOW_TRANSLUCENT_MASK)
    {
	opacity = MULTIPLY_USHORT (w->opacity, attrib->opacity);
	if (!w->alpha && opacity == OPAQUE)
	    return FALSE;
    }
    else
    {
	opacity = MULTIPLY_USHORT (w->opacity, attrib->opacity);
	if (w->alpha || opacity != OPAQUE)
	    mask |= PAINT_WINDOW_TRANSLUCENT_MASK;
	else
	    mask |= PAINT_WINDOW_SOLID_MASK;
    }

    if (!w->pixmap)
	bindWindow (w);

    if (mask & PAINT_WINDOW_TRANSFORMED_MASK)
	region = &infiniteRegion;

    w->vCount = 0;
    (*w->screen->addWindowGeometry) (w, &w->matrix, 1, w->region, region);
    if (w->vCount)
    {
	if (mask & PAINT_WINDOW_TRANSLUCENT_MASK)
	{
	    glEnable (GL_BLEND);
	    if (opacity != OPAQUE)
	    {
		glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glColor4us (opacity, opacity, opacity, opacity);
	    }
	}

	glPushMatrix ();

	if (mask & PAINT_WINDOW_TRANSFORMED_MASK)
	{
	    glTranslatef (w->attrib.x + attrib->xTranslate,
			  w->attrib.y + attrib->yTranslate, 0.0f);
	    glScalef (attrib->xScale, attrib->yScale, 0.0f);
	    glTranslatef (-w->attrib.x, -w->attrib.y, 0.0f);

	    enableTexture (w->screen, &w->texture, COMP_TEXTURE_FILTER_GOOD);
	}
	else if (mask & PAINT_WINDOW_ON_TRANSFORMED_SCREEN_MASK)
	{
	    enableTexture (w->screen, &w->texture, COMP_TEXTURE_FILTER_GOOD);
	}
	else
	{
	    enableTexture (w->screen, &w->texture, COMP_TEXTURE_FILTER_FAST);
	}

	(*w->screen->drawWindowGeometry) (w);

	disableTexture (&w->texture);

	glPopMatrix ();

	if (mask & PAINT_WINDOW_TRANSLUCENT_MASK)
	{
	    if (opacity != OPAQUE)
	    {
		glColor4usv (defaultColor);
		glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	    }
	    glDisable (GL_BLEND);
	}
    }

    return TRUE;
}

void
paintBackground (CompScreen   *s,
		 Region	      region,
		 unsigned int mask)
{
    CompTexture *bg = &s->backgroundTexture;
    BoxPtr      pBox = region->rects;
    int	        n, nBox = region->numRects;
    GLfloat     *d, *data;

    if (!nBox)
	return;

    if (s->desktopWindowCount)
    {
	if (bg->name)
	{
	    finiTexture (s, bg);
	    initTexture (s, bg);
	}

	if (!(mask & PAINT_BACKGROUND_WITH_STENCIL_MASK))
	    return;
    }
    else
    {
	if (!bg->name)
	    updateScreenBackground (s, bg);
    }

    data = malloc (sizeof (GLfloat) * nBox * 16);
    if (!data)
	return;

    d = data;
    n = nBox;
    while (n--)
    {
	*d++ = COMP_TEX_COORD_X (&bg->matrix, pBox->x1, pBox->y2);
	*d++ = COMP_TEX_COORD_Y (&bg->matrix, pBox->x1, pBox->y2);

	*d++ = pBox->x1;
	*d++ = pBox->y2;

	*d++ = COMP_TEX_COORD_X (&bg->matrix, pBox->x2, pBox->y2);
	*d++ = COMP_TEX_COORD_Y (&bg->matrix, pBox->x2, pBox->y2);

	*d++ = pBox->x2;
	*d++ = pBox->y2;

	*d++ = COMP_TEX_COORD_X (&bg->matrix, pBox->x2, pBox->y1);
	*d++ = COMP_TEX_COORD_Y (&bg->matrix, pBox->x2, pBox->y1);

	*d++ = pBox->x2;
	*d++ = pBox->y1;

	*d++ = COMP_TEX_COORD_X (&bg->matrix, pBox->x1, pBox->y1);
	*d++ = COMP_TEX_COORD_Y (&bg->matrix, pBox->x1, pBox->y1);

	*d++ = pBox->x1;
	*d++ = pBox->y1;

	pBox++;
    }

    if (mask & PAINT_BACKGROUND_WITH_STENCIL_MASK)
    {
	glEnable (GL_STENCIL_TEST);
	glStencilFunc (GL_ALWAYS, s->stencilRef, ~0);
	glStencilOp (GL_REPLACE, GL_REPLACE, GL_REPLACE);
    }

    glTexCoordPointer (2, GL_FLOAT, sizeof (GLfloat) * 4, data);
    glVertexPointer (2, GL_FLOAT, sizeof (GLfloat) * 4, data + 2);

    if (s->desktopWindowCount)
    {
	glDrawArrays (GL_QUADS, 0, nBox * 4);
    }
    else
    {
	if (mask & PAINT_BACKGROUND_ON_TRANSFORMED_SCREEN_MASK)
	    enableTexture (s, bg, COMP_TEXTURE_FILTER_GOOD);
	else
	    enableTexture (s, bg, COMP_TEXTURE_FILTER_FAST);

	glDrawArrays (GL_QUADS, 0, nBox * 4);

	disableTexture (bg);
    }

    if (mask & PAINT_BACKGROUND_WITH_STENCIL_MASK)
    {
	glStencilFunc (GL_EQUAL, s->stencilRef, ~0);
	glStencilOp (GL_KEEP, GL_KEEP, GL_KEEP);
	glDisable (GL_STENCIL_TEST);
    }

    free (data);
}
