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

CompOption *
compFindOption (CompOption *option,
		int	    nOption,
		char	    *name,
		int	    *index)
{
    int i;

    for (i = 0; i < nOption; i++)
    {
	if (strcmp (option[i].name, name) == 0)
	{
	    if (index)
		*index = i;

	    return &option[i];
	}
    }

    return 0;
}

Bool
compSetBoolOption (CompOption	   *option,
		   CompOptionValue *value)
{
    option->value.i = (value->b) ? TRUE : FALSE;

    return TRUE;
}

Bool
compSetIntOption (CompOption	  *option,
		  CompOptionValue *value)
{
    if (value->i <= option->rest.i.min ||
	value->i >= option->rest.i.max ||
	value->i == option->value.i)
	return FALSE;

    option->value.i = value->i;

    return TRUE;
}

Bool
compSetFloatOption (CompOption	    *option,
		    CompOptionValue *value)
{
    float v, p;

    p = 1.0f / option->rest.f.precision;
    v = ((int) (value->f * p + 0.5f)) / p;

    if (v <= option->rest.f.min ||
	v >= option->rest.f.max ||
	v == option->value.f)
	return FALSE;

    option->value.f = v;

    return TRUE;
}

Bool
compSetStringOption (CompOption	     *option,
		     CompOptionValue *value)
{
    if (option->rest.s.nString)
    {
	int i;

	if (!value->s)
	    return FALSE;

	for (i = 0; i < option->rest.s.nString; i++)
	{
	    if (strcmp (option->rest.s.string[i], value->s) == 0)
		break;
	}

	if (i == option->rest.s.nString)
	    return FALSE;
    }

    if (option->value.s == value->s)
	return FALSE;

    if (option->value.s && value->s)
    {
	if (strcmp (option->value.s, value->s) == 0)
	    return FALSE;
    }

    if (option->value.s)
	free (option->value.s);

    if (value->s)
	option->value.s = strdup (value->s);
    else
	option->value.s = 0;

    return TRUE;
}

Bool
compSetColorOption (CompOption	    *option,
		    CompOptionValue *value)
{
    if (memcmp (value->c, option->value.c, sizeof (value->c)) == 0)
	return FALSE;

    memcpy (option->value.c, value->c, sizeof (value->c));

    return TRUE;
}

Bool
compSetBindingOption (CompOption      *option,
		      CompOptionValue *value)
{
    CompBinding *binding;

    binding = &option->value.bind;
    if (value->bind.type == CompBindingTypeButton)
    {
	if (binding->type               == CompBindingTypeButton &&
	    binding->u.button.button    == value->bind.u.button.button &&
	    binding->u.button.modifiers == value->bind.u.button.modifiers)
	    return FALSE;
    }
    else
    {
	if (binding->type            == CompBindingTypeKey &&
	    binding->u.key.keycode   == value->bind.u.key.keycode &&
	    binding->u.key.modifiers == value->bind.u.key.modifiers)
	    return FALSE;
    }

    *binding = value->bind;

    return TRUE;
}

Bool
compSetOptionList (CompOption      *option,
		   CompOptionValue *value)
{
    CompOption o;
    Bool       status = FALSE;
    int        i, min;

    if (value->list.nValue != option->value.list.nValue)
    {
	CompOptionValue *v;

	v = malloc (sizeof (CompOptionValue) * value->list.nValue);
	if (!v)
	    return FALSE;

	min = MIN (value->list.nValue, option->value.list.nValue);

	if (min < option->value.list.nValue)
	{
	    switch (option->value.list.type) {
	    case CompOptionTypeString:
		for (i = min; i < option->value.list.nValue; i++)
		{
		    if (option->value.list.value[i].s)
			free (option->value.list.value[i].s);
		}
	    default:
		break;
	    }
	}

	memset (v, 0, sizeof (CompOptionValue) * value->list.nValue);

	if (min)
	    memcpy (v, option->value.list.value,
		    sizeof (CompOptionValue) * min);

	if (option->value.list.value)
	    free (option->value.list.value);

	option->value.list.value = v;
	option->value.list.nValue = value->list.nValue;

	status = TRUE;
    }

    o = *option;
    o.type = option->value.list.type;

    for (i = 0; i < value->list.nValue; i++)
    {
	o.value = option->value.list.value[i];

	switch (o.type) {
	case CompOptionTypeBool:
	    status |= compSetBoolOption (&o, &value->list.value[i]);
	    break;
	case CompOptionTypeInt:
	    status |= compSetIntOption (&o, &value->list.value[i]);
	    break;
	case CompOptionTypeFloat:
	    status |= compSetFloatOption (&o, &value->list.value[i]);
	    break;
	case CompOptionTypeString:
	    status |= compSetStringOption (&o, &value->list.value[i]);
	    break;
	case CompOptionTypeColor:
	    status |= compSetColorOption (&o, &value->list.value[i]);
	    break;
	case CompOptionTypeBinding:
	    status |= compSetBindingOption (&o, &value->list.value[i]);
	default:
	    break;
	}

	option->value.list.value[i] = o.value;
    }

    return status;
}
