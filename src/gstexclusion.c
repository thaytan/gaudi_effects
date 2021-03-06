/*
 * GStreamer
 * Copyright (C) 2010 Luis de Bethencourt <luis@debethencourt.com>
 * 
 * Exclusion - color exclusion video effect.
 * Based on Pete Warden's FreeFrame plugin with the same name.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-exclusion
 *
 * Exclusion saturates the colors of a video stream in realtime.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc ! exclusion ! ffmpegcolorspace ! autovideosink
 * ]| This pipeline shows the effect of exclusion on a test stream
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <math.h>

#include "gstplugin.h"
#include "gstexclusion.h"

#include <gst/video/video.h>
#include <gst/controller/gstcontroller.h>

GST_DEBUG_CATEGORY_STATIC (gst_exclusion_debug);
#define GST_CAT_DEFAULT gst_exclusion_debug

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define CAPS_STR GST_VIDEO_CAPS_BGRx ";" GST_VIDEO_CAPS_RGBx
#else
#define CAPS_STR GST_VIDEO_CAPS_xRGB ";" GST_VIDEO_CAPS_xBGR
#endif

/* Filter signals and args. */
enum
{
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_SILENT
};

/* Initializations */

static gint gate_int (gint value, gint min, gint max);
static void transform (guint32 * src, guint32 * dest, gint video_area);

/* The capabilities of the inputs and outputs. */

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CAPS_STR)
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CAPS_STR)
    );

GST_BOILERPLATE (Gstexclusion, gst_exclusion, GstElement,
    GST_TYPE_ELEMENT);

static void gst_exclusion_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_exclusion_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_exclusion_set_caps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_exclusion_chain (GstPad * pad, GstBuffer * buf);

/* GObject vmethod implementations */

static void
gst_exclusion_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple(element_class,
    "Exclusion",
    "Filter/Effect/Video",
    "Exclusion exclodes the colors in the video signal.",
    "Luis de Bethencourt <luis@debethencourt.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}

/* Initialize the exclusion's class. */
static void
gst_exclusion_class_init (GstexclusionClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_exclusion_set_property;
  gobject_class->get_property = gst_exclusion_get_property;

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE));
}

/* Initialize the new element,
 * instantiate pads and add them to element,
 * set pad calback functions, and
 * initialize instance structure.
 */
static void
gst_exclusion_init (Gstexclusion * filter,
    GstexclusionClass * gclass)
{
  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_setcaps_function (filter->sinkpad,
                                GST_DEBUG_FUNCPTR(gst_exclusion_set_caps));
  gst_pad_set_getcaps_function (filter->sinkpad,
                                GST_DEBUG_FUNCPTR(gst_pad_proxy_getcaps));
  gst_pad_set_chain_function (filter->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_exclusion_chain));

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_set_getcaps_function (filter->srcpad,
                                GST_DEBUG_FUNCPTR(gst_pad_proxy_getcaps));

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);
  filter->silent = FALSE;
}

static void
gst_exclusion_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Gstexclusion *filter = GST_EXCLUSION (object);

  switch (prop_id) {
    case PROP_SILENT:
      filter->silent = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_exclusion_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Gstexclusion *filter = GST_EXCLUSION (object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, filter->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */

/* Handle the link with other elements. */
static gboolean
gst_exclusion_set_caps (GstPad * pad, GstCaps * caps)
{
  Gstexclusion *filter;
  GstStructure *structure;
  GstPad *otherpad;

  filter = GST_EXCLUSION (gst_pad_get_parent (pad));
  otherpad = (pad == filter->srcpad) ? filter->sinkpad : filter->srcpad;

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "width", &filter->width);
  gst_structure_get_int (structure, "height", &filter->height);

  gst_object_unref (filter);

  return gst_pad_set_caps (otherpad, caps);
}

/* Actual processing. */
static GstFlowReturn
gst_exclusion_chain (GstPad * pad, GstBuffer * in_buf)
{
  Gstexclusion *filter;
  GstBuffer * out_buf = gst_buffer_copy(in_buf); 
  gint width, height, video_size;

  guint32 *src = (guint32 * ) GST_BUFFER_DATA (in_buf);
  guint32 *dest = (guint32 * ) GST_BUFFER_DATA (out_buf);

  filter = GST_EXCLUSION (GST_OBJECT_PARENT (pad));
  width = filter->width;
  height = filter->height;
  video_size = width * height;

  transform (src, dest, video_size);

  return gst_pad_push (filter->srcpad, out_buf);
}

/* Entry point to initialize the plug-in.
 * Register the element factories and other features. */
gboolean
gst_exclusion_plugin_init (GstPlugin * exclusion)
{
  /* debug category for fltering log messages */
  GST_DEBUG_CATEGORY_INIT (gst_exclusion_debug, "exclusion",
      0, "Template exclusion");

  return gst_element_register (exclusion, "exclusion", GST_RANK_NONE,
      GST_TYPE_EXCLUSION);
}

/*** Now the image processing work.... ***/

/* Keep the values inbounds. */
static gint gate_int ( gint value, gint min, gint max)
{
  if (value < min) {
    return min;
  } else if (value > max) {
    return max;
  } else {
    return value;
  }
}

/* Transform processes each frame. */
static void transform (guint32 * src, guint32 * dest, gint video_area)
{
  guint32 in, red, green, blue;
  gint x;
  gint factor = 175;

  for (x = 0; x < video_area; x++) {
    in = *src++;

    red = (in >> 16) & 0xff;
    green = (in >> 8) & 0xff;
    blue = (in) & 0xff;

    red = factor - 
          (((factor - red) * (factor - red) / factor) +
          ((green * red) / factor));
    green = factor - 
          (((factor - green) * (factor - green) / factor) +
          ((green * green) / factor));
    blue = factor - 
          (((factor - blue) * (factor - blue) / factor) +
          ((blue * blue) / factor));

    red = gate_int (red, 0, 255);
    green = gate_int (green, 0, 255);
    blue = gate_int (blue, 0, 255);

    *dest++ = (red << 16) | (green << 8) | blue;
  }
}
