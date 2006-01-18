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

#include <comp.h>

static CompMatrix _identity_matrix = {
    1, 0,
    0, 1,
    0, 0
};

void
initTexture (CompScreen  *screen,
	     CompTexture *texture)
{
    texture->name   = 0;
    texture->target = GL_TEXTURE_2D;
    texture->pixmap = None;
    texture->filter = COMP_TEXTURE_FILTER_FAST;
    texture->matrix = _identity_matrix;
}

void
finiTexture (CompScreen  *screen,
	     CompTexture *texture)
{
    if (texture->name)
    {
	releasePixmapFromTexture (screen, texture);
	glDeleteTextures (1, &texture->name);
    }
}

Bool
readImageToTexture (CompScreen   *screen,
		    CompTexture  *texture,
		    char	 *imageFileName,
		    unsigned int *returnWidth,
		    unsigned int *returnHeight)
{
    char	 *data, *image;
    unsigned int width, height;
    int		 i;

    if (!readPng (imageFileName, &image, &width, &height))
    {
	fprintf (stderr, "%s: Failed to load image: %s\n",
		 programName, imageFileName);
	return FALSE;
    }

    data = malloc (4 * width * height);
    if (!data)
    {
	free (image);
	return FALSE;
    }

    for (i = 0; i < height; i++)
	memcpy (&data[i * width * 4],
		&image[(height - i - 1) * width * 4],
		width * 4);

    free (image);

    releasePixmapFromTexture (screen, texture);

    if (screen->textureNonPowerOfTwo ||
	(POWER_OF_TWO (width) && POWER_OF_TWO (height)))
    {
	texture->target = GL_TEXTURE_2D;
	texture->matrix.xx = 1.0f / width;
	texture->matrix.yy = -1.0f / height;
	texture->matrix.y0 = 1.0f;
    }
    else
    {
	texture->target = GL_TEXTURE_RECTANGLE_NV;
	texture->matrix.xx = 1.0f;
	texture->matrix.yy = -1.0f;
	texture->matrix.y0 = height;
    }

    if (!texture->name)
	glGenTextures (1, &texture->name);

    glBindTexture (texture->target, texture->name);

    glTexImage2D (texture->target, 0, GL_RGB, width, height, 0, GL_BGRA,

#if IMAGE_BYTE_ORDER == MSBFirst
		  GL_UNSIGNED_INT_8_8_8_8_REV,
#else
		  GL_UNSIGNED_BYTE,
#endif

		  data);

    texture->filter = COMP_TEXTURE_FILTER_FAST;

    glTexParameteri (texture->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri (texture->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexParameteri (texture->target, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri (texture->target, GL_TEXTURE_WRAP_T, GL_CLAMP);

    glBindTexture (texture->target, 0);

    free (data);

    *returnWidth = width;
    *returnHeight = height;

    return TRUE;
}

Bool
bindPixmapToTexture (CompScreen  *screen,
		     CompTexture *texture,
		     Pixmap	 pixmap,
		     int	 width,
		     int	 height,
		     int	 depth)
{
    XVisualInfo  *visinfo;
    unsigned int target;
    int success = 0;

    visinfo = screen->glxPixmapVisuals[depth];
    if (!visinfo)
    {
	fprintf (stderr, "%s: No GL visual for depth %d\n",
		 programName, depth);

	return FALSE;
    }

    texture->pixmap = glXCreateGLXPixmap (screen->display->display,
					  visinfo, pixmap);
    if (!texture->pixmap)
    {
	fprintf (stderr, "%s: glXCreateGLXPixmap failed\n", programName);

	return FALSE;
    }

    if (screen->queryDrawable (screen->display->display,
			       texture->pixmap,
			       GLX_TEXTURE_TARGET_EXT,
			       &target))
    {
	fprintf (stderr, "%s: glXQueryDrawable failed\n", programName);

	glXDestroyGLXPixmap (screen->display->display, texture->pixmap);
	texture->pixmap = None;

	return FALSE;
    }

    switch (target) {
    case GLX_TEXTURE_2D_EXT:
	texture->target = GL_TEXTURE_2D;
	texture->matrix.xx = 1.0f / width;
	texture->matrix.yy = -1.0f / height;
	texture->matrix.y0 = 1.0f;
	break;
    case GLX_TEXTURE_RECTANGLE_EXT:
	texture->target = GL_TEXTURE_RECTANGLE_ARB;
	texture->matrix.xx = 1.0f;
	texture->matrix.yy = -1.0f;
	texture->matrix.y0 = height;
	break;
    default:
	fprintf (stderr, "%s: pixmap 0x%x can't be bound to texture\n",
		 programName, (int) pixmap);

	glXDestroyGLXPixmap (screen->display->display, texture->pixmap);
	texture->pixmap = None;

	return FALSE;
    }

    if (!texture->name)
	glGenTextures (1, &texture->name);

    glBindTexture (texture->target, texture->name);

    if (screen->bindTexImageExt)
        success = screen->bindTexImageExt(screen->display->display,
                                          texture->pixmap,
                                          GLX_FRONT_LEFT_EXT,
                                          NULL);
    else if (screen->bindTexImageMesa)
        success = screen->bindTexImageMesa(screen->display->display,
                                           texture->pixmap,
                                           GLX_FRONT_LEFT_EXT);

    if (!success)
    {
	fprintf (stderr, "%s: glXBindTexImage failed\n", programName);

	glXDestroyGLXPixmap (screen->display->display, texture->pixmap);
	texture->pixmap = None;

	return FALSE;
    }

    texture->filter = COMP_TEXTURE_FILTER_FAST;

    glTexParameteri (texture->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri (texture->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexParameteri (texture->target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (texture->target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture (texture->target, 0);

    return TRUE;
}

void
releasePixmapFromTexture (CompScreen  *screen,
			  CompTexture *texture)
{
    if (texture->pixmap)
    {
	glEnable (texture->target);
	glBindTexture (texture->target, texture->name);

	screen->releaseTexImage (screen->display->display,
				 texture->pixmap,
				 GLX_FRONT_LEFT_EXT);

	glBindTexture (texture->target, 0);
	glDisable (texture->target);

	glXDestroyGLXPixmap (screen->display->display, texture->pixmap);
	texture->pixmap = None;
    }
}

void
enableTexture (CompScreen	 *screen,
	       CompTexture	 *texture,
	       CompTextureFilter filter)
{
    glEnable (texture->target);
    glBindTexture (texture->target, texture->name);

    if (filter != texture->filter)
    {
	switch (filter) {
	case COMP_TEXTURE_FILTER_FAST:
	    glTexParameteri (texture->target,
			     GL_TEXTURE_MIN_FILTER,
			     GL_NEAREST);
	    glTexParameteri (texture->target,
			     GL_TEXTURE_MAG_FILTER,
			     GL_NEAREST);
	break;
	case COMP_TEXTURE_FILTER_GOOD:
	    glTexParameteri (texture->target,
			     GL_TEXTURE_MIN_FILTER,
			     screen->display->textureFilter);
	    glTexParameteri (texture->target,
			     GL_TEXTURE_MAG_FILTER,
			     screen->display->textureFilter);
	    break;
	}

	texture->filter = filter;
    }
}

void
disableTexture (CompTexture *texture)
{
   glBindTexture (texture->target, 0);
   glDisable (texture->target);
}
