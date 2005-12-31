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
		(*screen->paintWindow) (w, wAttrib, &screen->region,
					windowMask);

	    glDisable (GL_STENCIL_TEST);

	    glPopMatrix ();

	    return;
	}
    }
    else
	windowMask = backgroundMask = 0;

    (*screen->paintBackground) (screen, &screen->region, backgroundMask);

    for (w = screen->windows; w; w = w->next)
	(*screen->paintWindow) (w, wAttrib, &screen->region, windowMask);

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
	if (w->invisible)
	    continue;

	if ((*screen->paintWindow) (w, wAttrib, tmpRegion,
				    PAINT_WINDOW_SOLID_MASK))
	    XSubtractRegion (tmpRegion, w->region, tmpRegion);

	/* copy region */
	XSubtractRegion (tmpRegion, &emptyRegion, w->clip);
    }

    (*screen->paintBackground) (screen, tmpRegion, 0);

    /* paint translucent windows */
    for (w = screen->windows; w; w = w->next)
    {
	if (w->invisible)
	    continue;

	(*screen->paintWindow) (w, wAttrib, w->clip,
				PAINT_WINDOW_TRANSLUCENT_MASK);
    }

    glPopMatrix ();

    return TRUE;
}

#define ADD_QUAD(data, w, x1, y1, x2, y2)	   \
    if (!(w)->pixmap)				   \
	bindWindow (w);				   \
    *(data)++ = X_WINDOW_TO_TEXTURE_SPACE (w, x1); \
    *(data)++ = Y_WINDOW_TO_TEXTURE_SPACE (w, y2); \
    *(data)++ = (x1);				   \
    *(data)++ = (y2);				   \
    *(data)++ = X_WINDOW_TO_TEXTURE_SPACE (w, x2); \
    *(data)++ = Y_WINDOW_TO_TEXTURE_SPACE (w, y2); \
    *(data)++ = (x2);				   \
    *(data)++ = (y2);				   \
    *(data)++ = X_WINDOW_TO_TEXTURE_SPACE (w, x2); \
    *(data)++ = Y_WINDOW_TO_TEXTURE_SPACE (w, y1); \
    *(data)++ = (x2);				   \
    *(data)++ = (y1);				   \
    *(data)++ = X_WINDOW_TO_TEXTURE_SPACE (w, x1); \
    *(data)++ = Y_WINDOW_TO_TEXTURE_SPACE (w, y1); \
    *(data)++ = (x1);				   \
    *(data)++ = (y1)

#define ADD_BOX(data, w, box)					   \
    ADD_QUAD (data, w, (box)->x1, (box)->y1, (box)->x2, (box)->y2)

Bool
paintWindow (CompWindow		     *w,
	     const WindowPaintAttrib *attrib,
	     Region		     region,
	     unsigned int	     mask)
{
    BoxPtr   pClip;
    int      nClip, n;
    GLfloat  *data, *d;
    GLushort opacity;
    int      x1, y1, x2, y2;

    if (!region->numRects)
	return TRUE;

    if (w->destroyed || w->attrib.map_state != IsViewable)
	return TRUE;

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

    if (attrib->xTranslate != 0.0f ||
	attrib->yTranslate != 0.0f ||
	attrib->xScale	   != 1.0f ||
	attrib->yScale	   != 1.0f)
    {
	nClip = w->region->numRects;
	pClip = w->region->rects;

	mask |= PAINT_WINDOW_ON_TRANSFORMED_SCREEN_MASK;

	data = malloc (sizeof (GLfloat) * nClip * 16);
	if (!data)
	    return FALSE;

	d = data;

	n = nClip;
	while (nClip--)
	{
	    x1 = pClip->x1 - w->attrib.x;
	    y1 = pClip->y1 - w->attrib.y;
	    x2 = pClip->x2 - w->attrib.x;
	    y2 = pClip->y2 - w->attrib.y;

	    ADD_QUAD (d, w, x1, y1, x2, y2);

	    pClip++;
	}
    }
    else
    {
	BoxRec clip, full;
	BoxPtr pExtent = &region->extents;
	BoxPtr pBox = region->rects;
	int    nBox = region->numRects;
	int    dataSize;

	full.x1 = 0;
	full.y1 = 0;
	full.x2 = w->width;
	full.y2 = w->height;

	x1 = pExtent->x1 - w->attrib.x;
	y1 = pExtent->y1 - w->attrib.y;
	x2 = pExtent->x2 - w->attrib.x;
	y2 = pExtent->y2 - w->attrib.y;

	if (x1 > 0)
	    full.x1 = x1;
	if (y1 > 0)
	    full.y1 = y1;
	if (x2 < w->width)
	    full.x2 = x2;
	if (y2 < w->height)
	    full.y2 = y2;

	if (full.x1 >= full.x2 || full.y1 >= full.y2)
	    return TRUE;

	dataSize = nBox * 16;
	data = malloc (sizeof (GLfloat) * dataSize);
	if (!data)
	    return FALSE;

	d = data;
	n = 0;

	pBox = region->rects;
	nBox = region->numRects;
	while (nBox--)
	{
	    x1 = pBox->x1 - w->attrib.x;
	    y1 = pBox->y1 - w->attrib.y;
	    x2 = pBox->x2 - w->attrib.x;
	    y2 = pBox->y2 - w->attrib.y;

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
		nClip = w->region->numRects;

		if (nClip == 1)
		{
		    ADD_QUAD (d, w, x1, y1, x2, y2);

		    n++;
		}
		else
		{
		    pClip = w->region->rects;

		    while (nClip--)
		    {
			clip.x1 = pClip->x1 - w->attrib.x;
			clip.y1 = pClip->y1 - w->attrib.y;
			clip.x2 = pClip->x2 - w->attrib.x;
			clip.y2 = pClip->y2 - w->attrib.y;

			pClip++;

			if (clip.x1 < x1)
			    clip.x1 = x1;
			if (clip.y1 < y1)
			    clip.y1 = y1;
			if (clip.x2 > x2)
			    clip.x2 = x2;
			if (clip.y2 > y2)
			    clip.y2 = y2;

			if (clip.x1 < clip.x2 && clip.y1 < clip.y2)
			{
			    if ((n << 4) == dataSize)
			    {
				dataSize <<= 2;
				data = realloc (data,
						sizeof (GLfloat) * dataSize);
				if (!data)
				    return FALSE;

				d = data + (n * 16);
			    }

			    ADD_BOX (d, w, &clip);

			    n++;
			}
		    }
		}
	    }
	}
    }

    if (n)
    {
	glTexCoordPointer (2, GL_FLOAT, sizeof (GLfloat) * 4, data);
	glVertexPointer (2, GL_FLOAT, sizeof (GLfloat) * 4, data + 2);

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

	if (mask & PAINT_WINDOW_ON_TRANSFORMED_SCREEN_MASK)
	{
	    glTranslatef (w->attrib.x + attrib->xTranslate,
			  w->attrib.y + attrib->yTranslate, 0.0f);
	    glScalef (attrib->xScale, attrib->yScale, 0.0f);

	    enableTexture (w->screen, &w->texture, COMP_TEXTURE_FILTER_GOOD);
	}
	else
	{
	    glTranslatef (w->attrib.x, w->attrib.y, 0.0f);

	    enableTexture (w->screen, &w->texture, COMP_TEXTURE_FILTER_FAST);
	}

	glDrawArrays (GL_QUADS, 0, n * 4);

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

    free (data);

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
	*d++ = bg->dx * pBox->x1;
	*d++ = bg->dy * (s->backgroundHeight - pBox->y2);

	*d++ = pBox->x1;
	*d++ = pBox->y2;

	*d++ = bg->dx * pBox->x2;
	*d++ = bg->dy * (s->backgroundHeight - pBox->y2);

	*d++ = pBox->x2;
	*d++ = pBox->y2;

	*d++ = bg->dx * pBox->x2;
	*d++ = bg->dy * (s->backgroundHeight - pBox->y1);

	*d++ = pBox->x2;
	*d++ = pBox->y1;

	*d++ = bg->dx * pBox->x1;
	*d++ = bg->dy * (s->backgroundHeight - pBox->y1);

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
