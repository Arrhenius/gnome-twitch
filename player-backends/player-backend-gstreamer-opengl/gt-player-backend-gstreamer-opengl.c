/*
 *  This file is part of GNOME Twitch - 'Enjoy Twitch on your GNU/Linux desktop'
 *  Copyright © 2017 Vincent Szolnoky <vinszent@vinszent.com>
 *
 *  GNOME Twitch is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GNOME Twitch is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNOME Twitch. If not, see <http://www.gnu.org/licenses/>.
 */

#include "gt-player-backend-gstreamer-opengl.h"
#include "gnome-twitch/gt-player-backend.h"
#include <gst/gst.h>

#define TAG "GtPlayerBackendGstreamerOpenGL"
#include "gnome-twitch/gt-log.h"

typedef struct
{
    GstElement* playbin;
    GstElement* upload;
    GstElement* video_sink;
    GstElement* video_bin;
    GstBus* bus;
    GstPad* pad;
    GstPad* ghost_pad;

    GtkWidget* widget;

    gchar* uri;
    GtPlayerBackendState state;
    gdouble volume;
    gdouble buffer_fill;
    gboolean seekable;
    gint64 duration;
    gint64 position;

    guint position_tick_id;
} GtPlayerBackendGstreamerOpenGLPrivate;

static void gt_player_backend_iface_init(GtPlayerBackendInterface* iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED(GtPlayerBackendGstreamerOpenGL, gt_player_backend_gstreamer_opengl,
    PEAS_TYPE_EXTENSION_BASE, 0,
    G_IMPLEMENT_INTERFACE_DYNAMIC(GT_TYPE_PLAYER_BACKEND, gt_player_backend_iface_init)
    G_ADD_PRIVATE_DYNAMIC(GtPlayerBackendGstreamerOpenGL))

enum
{
    PROP_0,
    PROP_VOLUME,
    PROP_BUFFER_FILL,
    PROP_DURATION,
    PROP_POSITION,
    PROP_SEEKABLE,
    PROP_STATE,
    NUM_PROPS
};

static GParamSpec* props[NUM_PROPS];

static gboolean
position_tick_cb(gpointer udata)
{
    RETURN_VAL_IF_FAIL(GT_IS_PLAYER_BACKEND_GSTREAMER_OPENGL(udata), G_SOURCE_REMOVE);

    GtPlayerBackendGstreamerOpenGL* self = GT_PLAYER_BACKEND_GSTREAMER_OPENGL(udata);
    GtPlayerBackendGstreamerOpenGLPrivate* priv = gt_player_backend_gstreamer_opengl_get_instance_private(self);
    gint64 position;
    gint64 duration;
    GstState current_state;

    gst_element_query_position(priv->playbin, GST_FORMAT_TIME, &position);
    gst_element_get_state(priv->playbin, &current_state, NULL, GST_CLOCK_TIME_NONE);

    if (position != priv->position && current_state == GST_STATE_PLAYING)
    {
        priv->position = GST_TIME_AS_SECONDS(position);
        g_object_notify_by_pspec(G_OBJECT(self), props[PROP_POSITION]);
    }

    return G_SOURCE_CONTINUE;
}

static inline void
reconfigure_position_tick(GtPlayerBackendGstreamerOpenGL* self, guint timeout)
{
    GtPlayerBackendGstreamerOpenGLPrivate* priv = gt_player_backend_gstreamer_opengl_get_instance_private(self);

    if (priv->position_tick_id > 0)
    {
        g_source_remove(priv->position_tick_id);
        priv->position_tick_id = 0;
    }

    if (timeout > 0)
        priv->position_tick_id = g_timeout_add(200, position_tick_cb, self);
}

static gboolean
gst_message_cb(GstBus* bus, GstMessage* msg, gpointer udata)
{
    GtPlayerBackendGstreamerOpenGL* self = GT_PLAYER_BACKEND_GSTREAMER_OPENGL(udata);
    GtPlayerBackendGstreamerOpenGLPrivate* priv = gt_player_backend_gstreamer_opengl_get_instance_private(self);

    switch (GST_MESSAGE_TYPE(msg))
    {
        case GST_MESSAGE_BUFFERING:
        {
            gint perc;

            gst_message_parse_buffering(msg, &perc);
            gst_element_set_state(priv->playbin, perc < 100 ? GST_STATE_PAUSED : GST_STATE_PLAYING);
            priv->buffer_fill = ((gdouble) perc) / 100.0;
            priv->state = GT_PLAYER_BACKEND_STATE_BUFFERING;
            g_object_notify_by_pspec(G_OBJECT(self), props[PROP_STATE]);
            g_object_notify_by_pspec(G_OBJECT(self), props[PROP_BUFFER_FILL]);

            break;
        }
        case GST_MESSAGE_STATE_CHANGED:
        {
            GstState old_state, new_state;

            gst_message_parse_state_changed(msg, &old_state, &new_state, NULL);

            if (GST_MESSAGE_SRC(msg) != GST_OBJECT(priv->playbin)) break;
            if (old_state == new_state) break;

            reconfigure_position_tick(self, new_state <= GST_STATE_PAUSED ? 0 : 200);

            switch (new_state)
            {
                case GST_STATE_PAUSED:
                    priv->state = GT_PLAYER_BACKEND_STATE_PAUSED;
                    break;
                case GST_STATE_READY:
                case GST_STATE_NULL:
                    priv->state = GT_PLAYER_BACKEND_STATE_STOPPED;
                    break;
                case GST_STATE_PLAYING:
                    priv->state = GT_PLAYER_BACKEND_STATE_PLAYING;
                    break;
                default:
                    break;
            }

            g_object_notify_by_pspec(G_OBJECT(self), props[PROP_STATE]);

            break;
        }
        case GST_MESSAGE_DURATION_CHANGED:
        {
            priv->seekable = gst_element_query_duration(priv->playbin, GST_FORMAT_TIME, &priv->duration);
            priv->duration = GST_TIME_AS_SECONDS(priv->duration);
            g_object_notify_by_pspec(G_OBJECT(self), props[PROP_DURATION]);
            g_object_notify_by_pspec(G_OBJECT(self), props[PROP_SEEKABLE]);
            break;
        }
        case GST_MESSAGE_WARNING:
        {
            g_autoptr(GError) err = NULL;
            gst_message_parse_warning(msg, &err, NULL);
            WARNING("Warning received from GStreamer: %s", err->message);
            break;
        }
        case GST_MESSAGE_ERROR:
        {
            g_autoptr(GError) err = NULL;
            gst_message_parse_error(msg, &err, NULL);
            ERROR("Error received from GStreamer: %s", err->message);
            break;
        }
        default:
            break;
    }

    return G_SOURCE_CONTINUE;
}

static void
play(GtPlayerBackend* backend)
{
    RETURN_IF_FAIL(GT_IS_PLAYER_BACKEND_GSTREAMER_OPENGL(backend));

    GtPlayerBackendGstreamerOpenGL* self = GT_PLAYER_BACKEND_GSTREAMER_OPENGL(backend);
    GtPlayerBackendGstreamerOpenGLPrivate* priv = gt_player_backend_gstreamer_opengl_get_instance_private(self);

    gst_element_set_state(priv->playbin, GST_STATE_PLAYING);

    /* NOTE: Set state as loading until we confirm we are playing in the message callback*/
    priv->state = GT_PLAYER_BACKEND_STATE_LOADING;
    g_object_notify_by_pspec(G_OBJECT(self), props[PROP_STATE]);
}

static void
stop(GtPlayerBackend* backend)
{
    RETURN_IF_FAIL(GT_PLAYER_BACKEND_GSTREAMER_OPENGL(backend));

    GtPlayerBackendGstreamerOpenGL* self = GT_PLAYER_BACKEND_GSTREAMER_OPENGL(backend);
    GtPlayerBackendGstreamerOpenGLPrivate* priv = gt_player_backend_gstreamer_opengl_get_instance_private(self);

    gst_element_set_state(priv->playbin, GST_STATE_NULL);
}

/* NOTE: We need the underscore because pause is already defined in unistd.h */
static void
_pause(GtPlayerBackend* backend)
{
    RETURN_IF_FAIL(GT_IS_PLAYER_BACKEND_GSTREAMER_OPENGL(backend));

    GtPlayerBackendGstreamerOpenGL* self = GT_PLAYER_BACKEND_GSTREAMER_OPENGL(backend);
    GtPlayerBackendGstreamerOpenGLPrivate* priv = gt_player_backend_gstreamer_opengl_get_instance_private(self);

    gst_element_set_state(priv->playbin, GST_STATE_PAUSED);
}

static void
set_uri(GtPlayerBackend* backend, const gchar* uri)
{
    RETURN_IF_FAIL(GT_IS_PLAYER_BACKEND_GSTREAMER_OPENGL(backend));

    GtPlayerBackendGstreamerOpenGL* self = GT_PLAYER_BACKEND_GSTREAMER_OPENGL(backend);
    GtPlayerBackendGstreamerOpenGLPrivate* priv = gt_player_backend_gstreamer_opengl_get_instance_private(self);

    g_free(priv->uri);
    priv->uri = g_strdup(uri);

    g_object_set(priv->playbin, "uri", priv->uri, NULL);
}

static void
set_position(GtPlayerBackend* backend, gint64 position)
{
    RETURN_IF_FAIL(GT_IS_PLAYER_BACKEND(backend));

    GtPlayerBackendGstreamerOpenGL* self = GT_PLAYER_BACKEND_GSTREAMER_OPENGL(backend);
    GtPlayerBackendGstreamerOpenGLPrivate* priv = gt_player_backend_gstreamer_opengl_get_instance_private(self);

    gst_element_set_state(priv->playbin, GST_STATE_PAUSED);
    gst_element_seek_simple(priv->playbin, GST_FORMAT_TIME,
        GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, position * GST_SECOND);
}

static GtkWidget*
get_widget(GtPlayerBackend* backend)
{
    GtPlayerBackendGstreamerOpenGL* self = GT_PLAYER_BACKEND_GSTREAMER_OPENGL(backend);
    GtPlayerBackendGstreamerOpenGLPrivate* priv = gt_player_backend_gstreamer_opengl_get_instance_private(self);

    return priv->widget;
}

static void
finalize(GObject* obj)
{
    GtPlayerBackendGstreamerOpenGL* self = GT_PLAYER_BACKEND_GSTREAMER_OPENGL(obj);
    GtPlayerBackendGstreamerOpenGLPrivate* priv = gt_player_backend_gstreamer_opengl_get_instance_private(self);

    MESSAGE("Finalize");

    G_OBJECT_CLASS(gt_player_backend_gstreamer_opengl_parent_class)->finalize(obj);

    stop(GT_PLAYER_BACKEND(self));

    reconfigure_position_tick(self, 0);

    gst_object_unref(priv->playbin);
//    gst_object_unref(priv->ghost_pad);
//    gst_object_unref(priv->pad);
//    gst_object_unref(priv->video_bin);
//    gst_object_unref(priv->video_sink);
//    gst_object_unref(priv->upload);

    g_clear_object(&priv->widget);

    g_free(priv->uri);
}

static void
get_property(GObject* obj,
             guint prop,
             GValue* val,
             GParamSpec* pspec)
{
    GtPlayerBackendGstreamerOpenGL* self = GT_PLAYER_BACKEND_GSTREAMER_OPENGL(obj);
    GtPlayerBackendGstreamerOpenGLPrivate* priv = gt_player_backend_gstreamer_opengl_get_instance_private(self);

    switch (prop)
    {
        case PROP_VOLUME:
            g_value_set_double(val, priv->volume);
            break;
        case PROP_BUFFER_FILL:
            g_value_set_double(val, priv->buffer_fill);
            break;
        case PROP_DURATION:
            g_value_set_int64(val, priv->duration);
            break;
        case PROP_POSITION:
            g_value_set_int64(val, priv->position);
            break;
        case PROP_SEEKABLE:
            g_value_set_boolean(val, priv->seekable);
            break;
        case PROP_STATE:
            g_value_set_enum(val, priv->state);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop, pspec);
    }
}

static void
set_property(GObject* obj,
             guint prop,
             const GValue* val,
             GParamSpec* pspec)
{
    GtPlayerBackendGstreamerOpenGL* self = GT_PLAYER_BACKEND_GSTREAMER_OPENGL(obj);
    GtPlayerBackendGstreamerOpenGLPrivate* priv = gt_player_backend_gstreamer_opengl_get_instance_private(self);

    switch (prop)
    {
        case PROP_VOLUME:
            priv->volume = g_value_get_double(val);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop, pspec);
    }
}

static void
gt_player_backend_iface_init(GtPlayerBackendInterface* iface)
{
    iface->get_widget = get_widget;
    iface->play = play;
    iface->stop = stop;
    iface->pause = _pause;
    iface->set_uri = set_uri;
    iface->set_position = set_position;
}

static void
gt_player_backend_gstreamer_opengl_class_finalize(GtPlayerBackendGstreamerOpenGLClass* klass)
{
    if (gst_is_initialized())
        gst_deinit();
}

static void
gt_player_backend_gstreamer_opengl_class_init(GtPlayerBackendGstreamerOpenGLClass* klass)
{
    GObjectClass* obj_class = G_OBJECT_CLASS(klass);

    obj_class->finalize = finalize;
    obj_class->get_property = get_property;
    obj_class->set_property = set_property;

    props[PROP_VOLUME] = g_param_spec_double("volume", "Volume", "Volume of player",
        0.0, 1.0, 0.3, G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

    props[PROP_BUFFER_FILL] = g_param_spec_double("buffer-fill", "Buffer Fill", "Current buffer fill",
        0, 1.0, 0, G_PARAM_READABLE);

    props[PROP_DURATION] = g_param_spec_int64("duration", "Duration", "Current duration",
        0, G_MAXINT64, 0, G_PARAM_READABLE);

    props[PROP_POSITION] = g_param_spec_int64("position", "Position", "Current position",
        0, G_MAXINT64, 0, G_PARAM_READABLE);

    props[PROP_SEEKABLE] = g_param_spec_boolean("seekable", "Seekable", "Whether seekable",
        FALSE, G_PARAM_READABLE);

    props[PROP_STATE] = g_param_spec_enum("state", "State", "Current state",
        GT_TYPE_PLAYER_BACKEND_STATE, GT_PLAYER_BACKEND_STATE_STOPPED, G_PARAM_READABLE);

    g_object_class_override_property(obj_class, PROP_VOLUME, "volume");
    g_object_class_override_property(obj_class, PROP_BUFFER_FILL, "buffer-fill");
    g_object_class_override_property(obj_class, PROP_DURATION, "duration");
    g_object_class_override_property(obj_class, PROP_POSITION, "position");
    g_object_class_override_property(obj_class, PROP_SEEKABLE, "seekable");
    g_object_class_override_property(obj_class, PROP_STATE, "state");

    if (!gst_is_initialized())
        gst_init(NULL, NULL);
}

static void
gt_player_backend_gstreamer_opengl_init(GtPlayerBackendGstreamerOpenGL* self)
{
    GtPlayerBackendGstreamerOpenGLPrivate* priv = gt_player_backend_gstreamer_opengl_get_instance_private(self);

    MESSAGE("Init");

    priv->uri = NULL;
    priv->state = GT_PLAYER_BACKEND_STATE_STOPPED;
    priv->buffer_fill = 0.0;

    priv->playbin = gst_element_factory_make("playbin", NULL);
    priv->video_sink = gst_element_factory_make("gtkglsink", NULL);
    priv->video_bin = gst_bin_new("video_bin");
    priv->upload = gst_element_factory_make("glupload", NULL);

    priv->bus = gst_element_get_bus(priv->playbin);

    gst_bus_add_watch(priv->bus, (GstBusFunc) gst_message_cb, self);
    gst_bin_add_many(GST_BIN(priv->video_bin), priv->upload, priv->video_sink, NULL);
    gst_element_link_many(priv->upload, priv->video_sink, NULL);

    priv->pad = gst_element_get_static_pad(priv->upload, "sink");
    priv->ghost_pad = gst_ghost_pad_new("sink", priv->pad);
    gst_pad_set_active(priv->ghost_pad, TRUE);
    gst_element_add_pad(priv->video_bin, priv->ghost_pad);

    g_object_get(priv->video_sink, "widget", &priv->widget, NULL);
    g_object_set(priv->playbin, "video-sink", priv->video_bin, NULL);

    g_object_bind_property(self, "volume", priv->playbin, "volume",
        G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
}

G_MODULE_EXPORT
void
peas_register_types(PeasObjectModule* module)
{
    gt_player_backend_gstreamer_opengl_register_type(G_TYPE_MODULE(module));

    peas_object_module_register_extension_type(module,
        GT_TYPE_PLAYER_BACKEND, GT_TYPE_PLAYER_BACKEND_GSTREAMER_OPENGL);
}
