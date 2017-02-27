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

#include "gt-settings-dlg.h"
#include "gt-enums.h"
#include <glib/gi18n.h>
#include <libpeas-gtk/peas-gtk.h>

typedef struct
{
    GtkWidget* prefer_dark_theme_button;
    GtkWidget* quality_combo;
    GtkWidget* language_filter_combo;
    GtkWidget* settings_switcher;
    GtkWidget* players_view;
    GtkWidget* settings_stack;
    GtWin* win;

    GSettings* settings;
} GtSettingsDlgPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(GtSettingsDlg, gt_settings_dlg, GTK_TYPE_DIALOG)

enum
{
    PROP_0,
    NUM_PROPS
};

static GParamSpec* props[NUM_PROPS];

GtSettingsDlg*
gt_settings_dlg_new(GtWin* win)
{
    return g_object_new(GT_TYPE_SETTINGS_DLG,
                        "transient-for", win,
                        "use-header-bar", TRUE,
                        NULL);
}

static void
finalize(GObject* object)
{
    GtSettingsDlg* self = (GtSettingsDlg*) object;
    GtSettingsDlgPrivate* priv = gt_settings_dlg_get_instance_private(self);

    g_object_unref(priv->settings);

    G_OBJECT_CLASS(gt_settings_dlg_parent_class)->finalize(object);
}

static void
get_property (GObject*    obj,
              guint       prop,
              GValue*     val,
              GParamSpec* pspec)
{
    GtSettingsDlg* self = GT_SETTINGS_DLG(obj);
    GtSettingsDlgPrivate* priv = gt_settings_dlg_get_instance_private(self);

    switch (prop)
    {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop, pspec);
    }
}

static void
set_property(GObject*      obj,
             guint         prop,
             const GValue* val,
             GParamSpec*   pspec)
{
    GtSettingsDlg* self = GT_SETTINGS_DLG(obj);
    GtSettingsDlgPrivate* priv = gt_settings_dlg_get_instance_private(self);

    switch (prop)
    {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop, pspec);
    }
}

static void
constructed(GObject* obj)
{
    GtSettingsDlg* self = GT_SETTINGS_DLG(obj);
    GtSettingsDlgPrivate* priv = gt_settings_dlg_get_instance_private(self);

    G_OBJECT_CLASS(gt_settings_dlg_parent_class)->constructed(obj);

    priv->win = GT_WIN(gtk_window_get_transient_for(GTK_WINDOW(self)));
}

static void
gt_settings_dlg_class_init(GtSettingsDlgClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);

    object_class->constructed = constructed;
    object_class->finalize = finalize;
    object_class->get_property = get_property;
    object_class->set_property = set_property;

    gtk_widget_class_set_template_from_resource(GTK_WIDGET_CLASS(klass), "/com/vinszent/GnomeTwitch/ui/gt-settings-dlg.ui");
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS(klass), GtSettingsDlg, prefer_dark_theme_button);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS(klass), GtSettingsDlg, quality_combo);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS(klass), GtSettingsDlg, language_filter_combo);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS(klass), GtSettingsDlg, settings_switcher);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS(klass), GtSettingsDlg, settings_stack);
    gtk_widget_class_bind_template_child_private(GTK_WIDGET_CLASS(klass), GtSettingsDlg, players_view);
}

static void
gt_settings_dlg_init(GtSettingsDlg* self)
{
    GtSettingsDlgPrivate* priv = gt_settings_dlg_get_instance_private(self);

    //So it's not optimised away
    volatile gpointer type = (gpointer) PEAS_GTK_TYPE_PLUGIN_MANAGER;

    gtk_widget_init_template(GTK_WIDGET(self));

    gtk_widget_show_all(priv->players_view);

    GtkWidget* header_bar = gtk_dialog_get_header_bar(GTK_DIALOG(self));
    gtk_header_bar_set_custom_title(GTK_HEADER_BAR(header_bar), priv->settings_switcher);

    GList* children = gtk_container_get_children(GTK_CONTAINER(priv->players_view));
    GtkToolbar* toolbar = GTK_TOOLBAR(g_list_nth(children, 1)->data);
    GtkToolItem* item = gtk_tool_item_new();
    gtk_container_add(GTK_CONTAINER(item), gtk_link_button_new_with_label(
            "https://github.com/vinszent/gnome-twitch/wiki/How-to-install-players",
            _("How to install players")));
    gtk_widget_show_all(GTK_WIDGET(item));
    gtk_toolbar_insert(toolbar, item, 0);
    g_list_free(children);

    //TODO: Change this to use main_app->settings
    priv->settings = g_settings_new("com.vinszent.GnomeTwitch");

    g_settings_bind(priv->settings, "prefer-dark-theme",
        priv->prefer_dark_theme_button, "active",
        G_SETTINGS_BIND_DEFAULT);
    g_settings_bind(priv->settings, "default-quality",
        priv->quality_combo, "active-id",
        G_SETTINGS_BIND_DEFAULT);
    g_settings_bind(priv->settings, "language-filter",
        priv->language_filter_combo, "active-id",
        G_SETTINGS_BIND_DEFAULT);
}

void
gt_settings_dlg_set_view(GtSettingsDlg* self, GtSettingsDlgView view)
{
    GtSettingsDlgPrivate* priv = gt_settings_dlg_get_instance_private(self);

    switch (view)
    {
        case GT_SETTINGS_DLG_VIEW_GENERAL:
            gtk_stack_set_visible_child_name(GTK_STACK(priv->settings_stack), "general");
            break;
        case GT_SETTINGS_DLG_VIEW_PLAYERS:
            gtk_stack_set_visible_child_name(GTK_STACK(priv->settings_stack), "players");
            break;
    }
}
