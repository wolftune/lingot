/*
 * lingot, a musical instrument tuner.
 *
 * Copyright (C) 2004-2019  Iban Cereijo.
 * Copyright (C) 2004-2008  Jairo Chapela.

 *
 * This file is part of lingot.
 *
 * lingot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * lingot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with lingot; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

#include "lingot-defs.h"

#include "lingot-config.h"
#include "lingot-gui-mainframe.h"
#include "lingot-gui-config-dialog.h"
#include "lingot-gauge.h"
#include "lingot-i18n.h"
#include "lingot-io-config.h"
#include "lingot-msg.h"

static void lingot_gui_mainframe_draw_gauge_background(cairo_t *cr, const LingotMainFrame* frame);
static void lingot_gui_mainframe_draw_spectrum_background(cairo_t *cr, const LingotMainFrame* frame);
void lingot_gui_mainframe_draw_gauge(cairo_t *cr, const LingotMainFrame*);
void lingot_gui_mainframe_draw_spectrum(cairo_t *cr, const LingotMainFrame*);
void lingot_gui_mainframe_draw_labels(const LingotMainFrame*);

// sizes

static int gauge_size_x = 0;
static int gauge_size_y = 0;

static int spectrum_size_x = 0;
static int spectrum_size_y = 0;

static FLT spectrum_bottom_margin;
static FLT spectrum_top_margin;
static FLT spectrum_left_margin;
static FLT spectrum_right_margin;

static FLT spectrum_inner_x;
static FLT spectrum_inner_y;

static const FLT spectrum_min_db = 0; // TODO
static const FLT spectrum_max_db = 52;
static FLT spectrum_db_density = 0;

static int labelsbox_size_x = 0;
static int labelsbox_size_y = 0;

static gchar* filechooser_config_last_folder = NULL;

static const gdouble aspect_ratio_spectrum_visible = 1.14;
static const gdouble aspect_ratio_spectrum_invisible = 2.07;

// TODO: keep here?
static int closest_note_index = 0;
static FLT frequency = 0.0;

void lingot_gui_mainframe_callback_redraw_gauge(GtkWidget *w, cairo_t *cr, const LingotMainFrame* data) {
    (void)w;                //  Unused parameter.
    lingot_gui_mainframe_draw_gauge(cr, data);
}

void lingot_gui_mainframe_callback_redraw_spectrum(GtkWidget* w, cairo_t *cr, const LingotMainFrame* frame) {
    (void)w;                //  Unused parameter.
    lingot_gui_mainframe_draw_spectrum(cr, frame);
}

void lingot_gui_mainframe_callback_destroy(GtkWidget* w, LingotMainFrame* frame) {
    (void)w;                //  Unused parameter.
    g_source_remove(frame->visualization_timer_uid);
    g_source_remove(frame->freq_computation_timer_uid);
    g_source_remove(frame->gauge_computation_uid);
    gtk_main_quit();
}

void lingot_gui_mainframe_callback_about(GtkWidget* w, LingotMainFrame* frame) {
    (void)w;                //  Unused parameter.
    (void)frame;            //  Unused parameter.
    static const gchar* authors[] = {
        "Iban Cereijo <ibancg@gmail.com>",
        "Jairo Chapela <jairochapela@gmail.com>",
        NULL };

    char buff[512];
    snprintf(buff, sizeof(buff), "Matthew Blissett (%s)", _("Logo design"));
    const gchar* artists[] = { buff, NULL };

    GError *error = NULL;
    GtkIconTheme *icon_theme = NULL;
    GdkPixbuf *pixbuf = NULL;

    // we use the property "logo" instead of "logo-icon-name", so we can specify
    // here at what size we want to scale the icon in this dialog
    icon_theme = gtk_icon_theme_get_default ();
    pixbuf = gtk_icon_theme_load_icon (icon_theme,
                                       "org.nongnu.lingot", // icon name
                                       80, // icon size
                                       0,  // flags
                                       &error);

    if (error) {
        g_warning("Couldn’t load icon: %s", error->message);
        g_error_free(error);
    }

    gtk_show_about_dialog(NULL,
                          "name", "Lingot",
                          "version", VERSION,
                          "copyright", "\xC2\xA9 2004-2019 Iban Cereijo\n\xC2\xA9 2004-2019 Jairo Chapela",
                          "comments", _("Accurate and easy to use musical instrument tuner"),
                          "authors", authors,
                          "artists", artists,
                          "website-label", "https://www.nongnu.org/lingot/",
                          "website", "https://www.nongnu.org/lingot/",
                          "license-type", GTK_LICENSE_GPL_2_0,
                          "translator-credits", _("translator-credits"),
                          //"logo-icon-name", "org.nongnu.lingot",
                          "logo", pixbuf,
                          NULL);

    if (pixbuf) {
        g_object_unref(pixbuf);
    }
}

void lingot_gui_mainframe_callback_view_spectrum(GtkWidget* w, LingotMainFrame* frame) {
    (void)w;                //  Unused parameter.
    gboolean visible = gtk_check_menu_item_get_active(
                GTK_CHECK_MENU_ITEM(frame->view_spectrum_item));

    GtkAllocation alloc;
    gtk_widget_get_allocation(frame->win, &alloc);
    GdkGeometry hints;
    gdouble aspect_ratio =
            visible ?
                aspect_ratio_spectrum_visible :
                aspect_ratio_spectrum_invisible;
    hints.min_aspect = aspect_ratio;
    hints.max_aspect = aspect_ratio;
    gtk_window_set_geometry_hints(GTK_WINDOW(frame->win), frame->win, &hints,
                                  GDK_HINT_ASPECT);

    gtk_widget_set_visible(frame->spectrum_frame, visible);
}

void lingot_gui_mainframe_callback_config_dialog(GtkWidget* w,
                                                 LingotMainFrame* frame) {
    (void)w;                //  Unused parameter.
    lingot_gui_config_dialog_show(frame, NULL);
}

/* timeout for gauge and labels visualization */
gboolean lingot_gui_mainframe_callback_tout_visualization(gpointer data) {
    unsigned int period;

    LingotMainFrame* frame = (LingotMainFrame*) data;

    period = 1000 / frame->conf.visualization_rate;
    frame->visualization_timer_uid = g_timeout_add(period,
                                                   lingot_gui_mainframe_callback_tout_visualization, frame);

    gtk_widget_queue_draw(frame->gauge_area);

    return 0;
}

/* timeout for spectrum computation and display */
gboolean lingot_gui_mainframe_callback_tout_spectrum_computation_display(
        gpointer data) {
    unsigned int period;

    LingotMainFrame* frame = (LingotMainFrame*) data;

    period = 1000 / frame->conf.calculation_rate;
    frame->freq_computation_timer_uid = g_timeout_add(period,
                                                      lingot_gui_mainframe_callback_tout_spectrum_computation_display,
                                                      frame);

    gtk_widget_queue_draw(frame->spectrum_area);
    lingot_gui_mainframe_draw_labels(frame);

    return 0;
}

/* timeout for a new gauge position computation */
gboolean lingot_gui_mainframe_callback_gauge_computation(gpointer data) {
    unsigned int period;
    LingotMainFrame* frame = (LingotMainFrame*) data;

    period = 1000 / GAUGE_RATE;
    frame->gauge_computation_uid = g_timeout_add(period,
                                                 lingot_gui_mainframe_callback_gauge_computation, frame);

    // ignore continuous component
    if (!frame->core.running || isnan(frame->core.freq)
            || (frame->core.freq <= frame->conf.internal_min_frequency)) {
        frequency = 0.0;
        lingot_gauge_compute(&frame->gauge, frame->conf.gauge_rest_value);
    } else {
        FLT error_cents; // do not use, unfiltered
        frequency = lingot_filter_filter_sample(&frame->freq_filter,
                                                frame->core.freq);
        closest_note_index = lingot_config_scale_get_closest_note_index(
                    &frame->conf.scale, frame->core.freq,
                    frame->conf.root_frequency_error, &error_cents);
        if (!isnan(error_cents)) {
            lingot_gauge_compute(&frame->gauge, error_cents);
        }
    }

    return 0;
}

/* timeout for dispatching the error queue */
gboolean lingot_gui_mainframe_callback_error_dispatcher(gpointer data) {
    unsigned int period;
    GtkWidget* message_dialog;
    LingotMainFrame* frame = (LingotMainFrame*) data;

    char* error_message = NULL;
    message_type_t message_type;
    int error_code;
    int more_messages;

    do {
        more_messages = lingot_msg_get(&error_message, &message_type, &error_code);

        if (more_messages) {
            GtkWindow* parent =
                    GTK_WINDOW(
                        (frame->config_dialog != NULL) ? frame->config_dialog->win : frame->win);
            GtkButtonsType buttonsType;

            char message[2000];
            char* message_pointer = message;

            message_pointer += snprintf(message_pointer,
                                        (message - message_pointer) + sizeof(message), "%s",
                                        error_message);

            if (error_code == EBUSY) {
                message_pointer +=
                        snprintf(message_pointer,
                                 (message - message_pointer) + sizeof(message),
                                 "\n\n%s",
                                 _(
                                     "Please check that there are not other processes locking the requested device. Also, consider that some audio servers can sometimes hold the resources for a few seconds since the last time they were used. In such a case, you can try again."));
            }

            if ((message_type == ERROR) && !frame->core.running) {
                buttonsType = GTK_BUTTONS_OK;
                message_pointer +=
                        snprintf(message_pointer,
                                 (message - message_pointer) + sizeof(message),
                                 "\n\n%s",
                                 _(
                                     "The core is not running, you must check your configuration."));
            } else {
                buttonsType = GTK_BUTTONS_OK;
            }

            message_dialog = gtk_message_dialog_new(parent,
                                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    (message_type == ERROR) ? GTK_MESSAGE_ERROR :
                                                                              ((message_type == WARNING) ? GTK_MESSAGE_WARNING : GTK_MESSAGE_INFO),
                                                    buttonsType, "%s", message);

            gtk_window_set_title(GTK_WINDOW(message_dialog),
                                 (message_type == ERROR) ? _("Error") :
                                                           ((message_type == WARNING) ? _("Warning") : _("Info")));
            gtk_window_set_icon(GTK_WINDOW(message_dialog),
                                gtk_window_get_icon(GTK_WINDOW(frame->win)));
            gtk_dialog_run(GTK_DIALOG(message_dialog));
            gtk_widget_destroy(message_dialog);
            free(error_message);

            //			if ((message_type == ERROR) && !frame->core.running) {
            //				lingot_gui_mainframe_callback_config_dialog(NULL, frame);
            //			}

        }
    } while (more_messages);

    period = 1000 / ERROR_DISPATCH_RATE;
    frame->error_dispatcher_uid = g_timeout_add(period,
                                                lingot_gui_mainframe_callback_error_dispatcher, frame);

    return 0;
}

void lingot_gui_mainframe_callback_open_config(gpointer data,
                                               LingotMainFrame* frame) {
    (void)data;             //  Unused parameter.
    GtkWidget * dialog = gtk_file_chooser_dialog_new(
                _("Open Configuration File"), GTK_WINDOW(frame->win),
                GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL,
                "_Open", GTK_RESPONSE_ACCEPT, NULL);
    GtkFileFilter *filefilter;
    char config_used = 0;
    LingotConfig config;
    filefilter = gtk_file_filter_new();

    gtk_file_filter_set_name(filefilter,
                             (const gchar *) _("Lingot configuration files"));
    gtk_file_filter_add_pattern(filefilter, "*.conf");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filefilter);
    gtk_file_chooser_set_show_hidden(GTK_FILE_CHOOSER(dialog), TRUE);

    if (filechooser_config_last_folder != NULL) {
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),
                                            filechooser_config_last_folder);
    }

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename;
        filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        free(filechooser_config_last_folder);
        filechooser_config_last_folder = strdup(
                    gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(dialog)));
        lingot_config_new(&config);
        lingot_io_config_load(&config, filename);
        config_used = 1;
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
    //g_free(filefilter);

    if (config_used) {
        lingot_gui_config_dialog_show(frame, &config);
    }
}

void lingot_gui_mainframe_callback_save_config(gpointer data, LingotMainFrame* frame) {
    (void)data;             //  Unused parameter.
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
                _("Save Configuration File"), GTK_WINDOW(frame->win),
                GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel", GTK_RESPONSE_CANCEL,
                "_Save", GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog),
                                                   TRUE);

    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), _("untitled.conf"));
    GtkFileFilter* filefilter = gtk_file_filter_new();

    gtk_file_filter_set_name(filefilter,
                             (const gchar *) _("Lingot configuration files"));
    gtk_file_filter_add_pattern(filefilter, "*.conf");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filefilter);
    gtk_file_chooser_set_show_hidden(GTK_FILE_CHOOSER(dialog), TRUE);

    if (filechooser_config_last_folder != NULL) {
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),
                                            filechooser_config_last_folder);
    }

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename;
        filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (filechooser_config_last_folder != NULL)
            free(filechooser_config_last_folder);
        filechooser_config_last_folder = strdup(
                    gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(dialog)));
        lingot_io_config_save(&frame->conf, filename);
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

void lingot_gui_mainframe_color(GdkColor* color, int red, int green, int blue) {
    color->red = red;
    color->green = green;
    color->blue = blue;
}

void lingot_gui_mainframe_callback_window_resize(GtkWidget *widget,
                                                 GtkAllocation *allocation, void *data) {
    (void)widget;           //  Unused parameter.
    (void)allocation;       //  Unused parameter.

    const LingotMainFrame* frame = (LingotMainFrame*) data;

    GtkAllocation req;
    gtk_widget_get_allocation(frame->gauge_area, &req);

    if ((req.width != gauge_size_x) || (req.height != gauge_size_y)) {
        gauge_size_x = req.width;
        gauge_size_y = req.height;
    }

    gtk_widget_get_allocation(frame->spectrum_area, &req);

    if ((req.width != spectrum_size_x) || (req.height != spectrum_size_y)) {
        spectrum_size_x = req.width;
        spectrum_size_y = req.height;
    }

    gtk_widget_get_allocation(frame->labelsbox, &req);

    labelsbox_size_x = req.width;
    labelsbox_size_y = req.height;
}

void lingot_gui_mainframe_create(int argc, char *argv[]) {

    LingotMainFrame* frame;

    if (filechooser_config_last_folder == NULL) {
        char buff[1000];
        snprintf(buff, sizeof(buff), "%s/%s", getenv("HOME"), CONFIG_DIR_NAME);
        filechooser_config_last_folder = strdup(buff);
    }

    frame = malloc(sizeof(LingotMainFrame));

    frame->config_dialog = NULL;

    LingotConfig* const conf = &frame->conf;
    lingot_config_new(conf);
    lingot_io_config_load(conf, CONFIG_FILE_NAME);

    lingot_gauge_new(&frame->gauge, conf->gauge_rest_value); // gauge in rest situation

    // ----- FREQUENCY FILTER CONFIGURATION ------

    // low pass IIR filter.
    FLT freq_filter_a[] = { 1.0, -0.5 };
    FLT freq_filter_b[] = { 0.5 };

    lingot_filter_new(&frame->freq_filter, 1, 0, freq_filter_a, freq_filter_b);

    // ---------------------------------------------------

    gtk_init(&argc, &argv);
    //	gtk_set_locale();

    GtkBuilder* builder = gtk_builder_new();

    gtk_builder_add_from_resource(builder, "/org/nongnu/lingot/lingot-gui-mainframe.glade", NULL);

    frame->win = GTK_WIDGET(gtk_builder_get_object(builder, "window1"));

    gtk_window_set_default_icon_name("org.nongnu.lingot");
    gtk_window_set_icon_name(GTK_WINDOW(frame->win), "org.nongnu.lingot");

    frame->gauge_area = GTK_WIDGET(
                gtk_builder_get_object(builder, "gauge_area"));
    frame->spectrum_area = GTK_WIDGET(
                gtk_builder_get_object(builder, "spectrum_area"));

    frame->freq_label = GTK_WIDGET(
                gtk_builder_get_object(builder, "freq_label"));
    frame->tone_label = GTK_WIDGET(
                gtk_builder_get_object(builder, "tone_label"));
    frame->error_label = GTK_WIDGET(
                gtk_builder_get_object(builder, "error_label"));

    frame->spectrum_frame = GTK_WIDGET(
                gtk_builder_get_object(builder, "spectrum_frame"));
    frame->view_spectrum_item = GTK_WIDGET(
                gtk_builder_get_object(builder, "spectrum_item"));
    frame->labelsbox = GTK_WIDGET(gtk_builder_get_object(builder, "labelsbox"));

    gtk_check_menu_item_set_active(
                GTK_CHECK_MENU_ITEM(frame->view_spectrum_item), TRUE);

    // show all
    gtk_widget_show_all(frame->win);

    GtkAllocation alloc;
    gtk_widget_get_allocation(frame->win, &alloc);
    GdkGeometry hints;
    gdouble aspect_ratio = aspect_ratio_spectrum_visible;
    hints.min_aspect = aspect_ratio;
    hints.max_aspect = aspect_ratio;
    gtk_window_set_geometry_hints(GTK_WINDOW(frame->win), frame->win, &hints,
                                  GDK_HINT_ASPECT);

    // GTK signals
    g_signal_connect(gtk_builder_get_object(builder, "preferences_item"),
                     "activate", G_CALLBACK(lingot_gui_mainframe_callback_config_dialog),
                     frame);
    g_signal_connect(gtk_builder_get_object(builder, "quit_item"), "activate",
                     G_CALLBACK(lingot_gui_mainframe_callback_destroy), frame);
    g_signal_connect(gtk_builder_get_object(builder, "about_item"), "activate",
                     G_CALLBACK(lingot_gui_mainframe_callback_about), frame);
    g_signal_connect(gtk_builder_get_object(builder, "spectrum_item"),
                     "activate", G_CALLBACK(lingot_gui_mainframe_callback_view_spectrum),
                     frame);
    g_signal_connect(gtk_builder_get_object(builder, "open_config_item"),
                     "activate", G_CALLBACK(lingot_gui_mainframe_callback_open_config),
                     frame);
    g_signal_connect(gtk_builder_get_object(builder, "save_config_item"),
                     "activate", G_CALLBACK(lingot_gui_mainframe_callback_save_config),
                     frame);

    g_signal_connect(frame->gauge_area, "draw",
                     G_CALLBACK(lingot_gui_mainframe_callback_redraw_gauge), frame);
    g_signal_connect(frame->spectrum_area, "draw",
                     G_CALLBACK(lingot_gui_mainframe_callback_redraw_spectrum), frame);
    g_signal_connect(frame->win, "destroy",
                     G_CALLBACK(lingot_gui_mainframe_callback_destroy), frame);

    // TODO: remove
    g_signal_connect(frame->win, "size-allocate",
                     G_CALLBACK(lingot_gui_mainframe_callback_window_resize), frame);

    GtkAccelGroup* accel_group = gtk_accel_group_new();
    gtk_widget_add_accelerator(
                GTK_WIDGET(gtk_builder_get_object(builder, "preferences_item")),
                "activate", accel_group, 'p', GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_window_add_accel_group(GTK_WINDOW(frame->win), accel_group);

    unsigned int period;
    period = 1000 / conf->visualization_rate;
    frame->visualization_timer_uid = g_timeout_add(period,
                                                   lingot_gui_mainframe_callback_tout_visualization, frame);

    period = 1000 / conf->calculation_rate;
    frame->freq_computation_timer_uid = g_timeout_add(period,
                                                      lingot_gui_mainframe_callback_tout_spectrum_computation_display,
                                                      frame);

    period = 1000 / GAUGE_RATE;
    frame->gauge_computation_uid = g_timeout_add(period,
                                                 lingot_gui_mainframe_callback_gauge_computation, frame);

    period = 1000 / ERROR_DISPATCH_RATE;
    frame->error_dispatcher_uid = g_timeout_add(period,
                                                lingot_gui_mainframe_callback_error_dispatcher, frame);

    lingot_core_new(&frame->core, conf);
    lingot_core_start(&frame->core);

    g_object_unref(builder);

    gtk_main();
}

void lingot_gui_mainframe_destroy(LingotMainFrame* frame) {

    lingot_core_stop(&frame->core);
    lingot_core_destroy(&frame->core);

    lingot_gauge_destroy(&frame->gauge);
    lingot_filter_destroy(&frame->freq_filter);
    lingot_config_destroy(&frame->conf);
    if (frame->config_dialog) {
        lingot_gui_config_dialog_destroy(frame->config_dialog);
    }

    free(frame);
}

// ---------------------------------------------------------------------------

static void lingot_gui_mainframe_cairo_set_source_argb(cairo_t *cr,
                                                       unsigned int color) {
    cairo_set_source_rgba(cr,
                          0.00392156862745098 * ((color >> 16) & 0xff),
                          0.00392156862745098 * ((color >> 8) & 0xff),
                          0.00392156862745098 * (color & 0xff),
                          1.0 - 0.00392156862745098 * ((color >> 24) & 0xff));
}

typedef struct {
    FLT x;
    FLT y;
} point_t;

static void lingot_gui_mainframe_draw_gauge_tic(cairo_t *cr,
                                                const point_t* gaugeCenter, double radius1, double radius2,
                                                double angle) {
    cairo_move_to(cr, gaugeCenter->x + radius1 * sin(angle),
                  gaugeCenter->y - radius1 * cos(angle));
    cairo_rel_line_to(cr, (radius2 - radius1) * sin(angle),
                      (radius1 - radius2) * cos(angle));
    cairo_stroke(cr);
}

static void lingot_gui_mainframe_draw_gauge_background(cairo_t *cr,
                                                       const LingotMainFrame* frame) {

    // normalized dimensions
    static const FLT gauge_gaugeCenterY = 0.94;
    static const FLT gauge_centsBarStroke = 0.025;
    static const FLT gauge_centsBarRadius = 0.75;
    static const FLT gauge_centsBarMajorTicRadius = 0.04;
    static const FLT gauge_centsBarMinorTicRadius = 0.03;
    static const FLT gauge_centsBarMajorTicStroke = 0.03;
    static const FLT gauge_centsBarMinorTicStroke = 0.01;
    static const FLT gauge_centsTextSize = 0.09;
    static const FLT gauge_frequencyBarStroke = 0.025;
    static const FLT gauge_frequencyBarRadius = 0.78;
    static const FLT gauge_frequencyBarMajorTicRadius = 0.04;
    static const FLT gauge_okBarStroke = 0.07;
    static const FLT gauge_okBarRadius = 0.48;

    static const FLT overtureAngle = 65.0 * M_PI / 180.0;

    // colors
    static const unsigned int gauge_centsBarColor = 0x333355;
    static const unsigned int gauge_frequencyBarColor = 0x555533;
    static const unsigned int gauge_okColor = 0x99dd99;
    static const unsigned int gauge_koColor = 0xddaaaa;

    const int width = gauge_size_x;
    int height = gauge_size_y;

    // dimensions applied to the current size
    point_t gaugeCenter = { .x = width / 2, .y = height * gauge_gaugeCenterY };

    if (width < 1.6 * height) {
        height = width / 1.6;
        gaugeCenter.y = 0.5 * (gauge_size_y - height)
                + height * gauge_gaugeCenterY;
    }

    const FLT centsBarRadius = height * gauge_centsBarRadius;
    const FLT centsBarStroke = height * gauge_centsBarStroke;
    const FLT centsBarMajorTicRadius = centsBarRadius
            - height * gauge_centsBarMajorTicRadius;
    const FLT centsBarMinorTicRadius = centsBarRadius
            - height * gauge_centsBarMinorTicRadius;
    const FLT centsBarMajorTicStroke = height * gauge_centsBarMajorTicStroke;
    const FLT centsBarMinorTicStroke = height * gauge_centsBarMinorTicStroke;
    const FLT centsTextSize = height * gauge_centsTextSize;
    const FLT frequencyBarRadius = height * gauge_frequencyBarRadius;
    const FLT frequencyBarMajorTicRadius = frequencyBarRadius
            + height * gauge_frequencyBarMajorTicRadius;
    const FLT frequencyBarStroke = height * gauge_frequencyBarStroke;
    const FLT okBarRadius = height * gauge_okBarRadius;
    const FLT okBarStroke = height * gauge_okBarStroke;

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_save(cr);
    const GdkRectangle r = { .x = 0, .y = 0, .width = gauge_size_x, .height =
                             gauge_size_y };
    gdk_cairo_rectangle(cr, &r);
    cairo_fill_preserve(cr);
    cairo_restore(cr);
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_stroke(cr);

    // draw ok/ko bar
    cairo_set_line_width(cr, okBarStroke);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
    lingot_gui_mainframe_cairo_set_source_argb(cr, gauge_koColor);
    cairo_arc(cr, gaugeCenter.x, gaugeCenter.y, okBarRadius,
              -0.5 * M_PI - overtureAngle, -0.5 * M_PI + overtureAngle);
    cairo_stroke(cr);
    lingot_gui_mainframe_cairo_set_source_argb(cr, gauge_okColor);
    cairo_arc(cr, gaugeCenter.x, gaugeCenter.y, okBarRadius,
              -0.5 * M_PI - 0.1 * overtureAngle,
              -0.5 * M_PI + 0.1 * overtureAngle);
    cairo_stroke(cr);

    // draw cents bar
    cairo_set_line_width(cr, centsBarStroke);
    lingot_gui_mainframe_cairo_set_source_argb(cr, gauge_centsBarColor);
    cairo_arc(cr, gaugeCenter.x, gaugeCenter.y, centsBarRadius,
              -0.5 * M_PI - 1.05 * overtureAngle,
              -0.5 * M_PI + 1.05 * overtureAngle);
    cairo_stroke(cr);

    // cent tics
    const double maxOffsetRounded = frame->conf.scale.max_offset_rounded;
    static const int maxMinorDivisions = 20;
    double centsPerMinorDivision = maxOffsetRounded / maxMinorDivisions;
    const double base = pow(10.0, floor(log10(centsPerMinorDivision)));
    double normalizedCentsPerDivision = centsPerMinorDivision / base;
    if (normalizedCentsPerDivision >= 6.0) {
        normalizedCentsPerDivision = 10.0;
    } else if (normalizedCentsPerDivision >= 2.5) {
        normalizedCentsPerDivision = 5.0;
    } else if (normalizedCentsPerDivision >= 1.2) {
        normalizedCentsPerDivision = 2.0;
    } else {
        normalizedCentsPerDivision = 1.0;
    }
    centsPerMinorDivision = normalizedCentsPerDivision * base;
    const double centsPerMajorDivision = 5.0 * centsPerMinorDivision;

    // minor tics
    cairo_set_line_width(cr, centsBarMinorTicStroke);
    int maxIndex = (int) floor(0.5 * maxOffsetRounded / centsPerMinorDivision);
    double angleStep = 2.0 * overtureAngle * centsPerMinorDivision
            / maxOffsetRounded;
    int index;
    for (index = -maxIndex; index <= maxIndex; index++) {
        const double angle = index * angleStep;
        lingot_gui_mainframe_draw_gauge_tic(cr, &gaugeCenter,
                                            centsBarMinorTicRadius, centsBarRadius, angle);
    }

    // major tics
    maxIndex = (int) floor(0.5 * maxOffsetRounded / centsPerMajorDivision);
    angleStep = 2.0 * overtureAngle * centsPerMajorDivision / maxOffsetRounded;
    cairo_set_line_width(cr, centsBarMajorTicStroke);
    for (index = -maxIndex; index <= maxIndex; index++) {
        double angle = index * angleStep;
        lingot_gui_mainframe_draw_gauge_tic(cr, &gaugeCenter,
                                            centsBarMajorTicRadius, centsBarRadius, angle);
    }

    // cents text
    cairo_set_line_width(cr, 1.0);
    double oldAngle = 0.0;

    cairo_save(cr);

    static char buff[10];

    cairo_text_extents_t te;
    cairo_select_font_face(cr, "Helvetica", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, centsTextSize);

    sprintf(buff, "%s", "cent");
    cairo_text_extents(cr, buff, &te);
    cairo_move_to(cr, gaugeCenter.x - te.width / 2 - te.x_bearing,
                  gaugeCenter.y - 0.81 * centsBarMajorTicRadius - te.height / 2
                  - te.y_bearing);
    cairo_show_text(cr, buff);

    cairo_translate(cr, gaugeCenter.x, gaugeCenter.y);
    for (index = -maxIndex; index <= maxIndex; index++) {
        double angle = index * angleStep;
        cairo_rotate(cr, angle - oldAngle);
        int cents = (int) (index * centsPerMajorDivision);
        sprintf(buff, "%s%i", ((cents > 0) ? "+" : ""), cents);
        cairo_text_extents(cr, buff, &te);
        cairo_move_to(cr, -te.width / 2 - te.x_bearing,
                      -0.92 * centsBarMajorTicRadius - te.height / 2 - te.y_bearing);
        cairo_show_text(cr, buff);
        oldAngle = angle;
    }
    cairo_restore(cr);
    cairo_stroke(cr);

    // draw frequency bar
    cairo_set_line_width(cr, frequencyBarStroke);
    lingot_gui_mainframe_cairo_set_source_argb(cr, gauge_frequencyBarColor);
    cairo_arc(cr, gaugeCenter.x, gaugeCenter.y, frequencyBarRadius,
              -0.5 * M_PI - 1.05 * overtureAngle,
              -0.5 * M_PI + 1.05 * overtureAngle);
    cairo_stroke(cr);

    // frequency tics
    lingot_gui_mainframe_draw_gauge_tic(cr, &gaugeCenter,
                                        frequencyBarMajorTicRadius, frequencyBarRadius, 0.0);
}

void lingot_gui_mainframe_draw_gauge(cairo_t *cr, const LingotMainFrame* frame) {

    // normalized dimensions
    static const FLT gauge_gaugeCenterY = 0.94;
    static const FLT gauge_gaugeLength = 0.85;
    static const FLT gauge_gaugeLengthBack = 0.08;
    static const FLT gauge_gaugeCenterRadius = 0.045;
    static const FLT gauge_gaugeStroke = 0.012;
    static const FLT gauge_gaugeShadowOffsetX = 0.015;
    static const FLT gauge_gaugeShadowOffsetY = 0.01;

    static const FLT overtureAngle = 65.0 * M_PI / 180.0;

    // colors
    static const unsigned int gauge_gaugeColor = 0xaa3333;
    static const unsigned int gauge_gaugeShadowColor = 0x88000000;

    const int width = gauge_size_x;
    int height = gauge_size_y;

    // dimensions applied to the current size
    point_t gaugeCenter = { .x = width / 2, .y = height * gauge_gaugeCenterY };

    if (width < 1.6 * height) {
        height = width / 1.6;
        gaugeCenter.y = 0.5 * (gauge_size_y - height) + height * gauge_gaugeCenterY;
    }

    const point_t gaugeShadowCenter = { .x = gaugeCenter.x
                                        + height * gauge_gaugeShadowOffsetX, .y = gaugeCenter.y
                                        + height * gauge_gaugeShadowOffsetY };
    const FLT gaugeLength = height * gauge_gaugeLength;
    const FLT gaugeLengthBack = height * gauge_gaugeLengthBack;
    const FLT gaugeCenterRadius = height * gauge_gaugeCenterRadius;
    const FLT gaugeStroke = height * gauge_gaugeStroke;

    lingot_gui_mainframe_draw_gauge_background(cr, frame);

    const double normalized_error = frame->gauge.position
            / frame->conf.scale.max_offset_rounded;
    const double angle = 2.0 * normalized_error * overtureAngle;
    cairo_set_line_width(cr, gaugeStroke);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
    lingot_gui_mainframe_cairo_set_source_argb(cr, gauge_gaugeShadowColor);
    lingot_gui_mainframe_draw_gauge_tic(cr, &gaugeShadowCenter,
                                        -gaugeLengthBack, -0.99 * gaugeCenterRadius, angle);
    lingot_gui_mainframe_draw_gauge_tic(cr, &gaugeShadowCenter,
                                        0.99 * gaugeCenterRadius, gaugeLength, angle);
    cairo_arc(cr, gaugeShadowCenter.x, gaugeShadowCenter.y, gaugeCenterRadius,
              0, 2 * M_PI);
    cairo_fill(cr);
    lingot_gui_mainframe_cairo_set_source_argb(cr, gauge_gaugeColor);
    lingot_gui_mainframe_draw_gauge_tic(cr, &gaugeCenter, -gaugeLengthBack,
                                        gaugeLength, angle);
    cairo_arc(cr, gaugeCenter.x, gaugeCenter.y, gaugeCenterRadius, 0, 2 * M_PI);
    cairo_fill(cr);
}

FLT lingot_gui_mainframe_get_signal(const LingotMainFrame* frame, int i,
                                    FLT min, FLT max) {
    FLT signal = frame->core.SPL[i];
    if (signal < min) {
        signal = min;
    } else if (signal > max) {
        signal = max;
    }
    return signal - min;
}

FLT lingot_gui_mainframe_get_noise(const LingotMainFrame* frame, FLT min,
                                   FLT max) {
    FLT noise = frame->conf.min_overall_SNR;
    if (noise < min) {
        noise = min;
    } else if (noise > max) {
        noise = max;
    }
    return noise - min;
}

static char* lingot_gui_mainframe_format_frequency(FLT freq, char* buff) {
    if (freq == 0.0) {
        sprintf(buff, "0 Hz");
    } else if (floor(freq) == freq)
        sprintf(buff, "%0.0f kHz", freq);
    else if (floor(10 * freq) == 10 * freq) {
        if (freq <= 1000.0)
            sprintf(buff, "%0.0f Hz", 1e3 * freq);
        else
            sprintf(buff, "%0.1f kHz", freq);
    } else {
        if (freq <= 100.0)
            sprintf(buff, "%0.0f Hz", 1e3 * freq);
        else
            sprintf(buff, "%0.2f kHz", freq);
    }

    return buff;
}

void lingot_gui_mainframe_draw_spectrum_background(cairo_t *cr, const LingotMainFrame* frame) {

    const FLT font_size = 8 + spectrum_size_y / 30;

    static char buff[10];
    static char buff2[10];
    cairo_select_font_face(cr, "Helvetica", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, font_size);
    sprintf(buff, "%0.0f", spectrum_min_db);
    sprintf(buff2, "%0.0f", spectrum_max_db);
    cairo_text_extents_t te;
    cairo_text_extents_t te2;
    cairo_text_extents(cr, buff, &te);
    cairo_text_extents(cr, buff2, &te2);
    if (te2.width > te.width) {
        te = te2;
    }

    // spectrum area margins
    spectrum_bottom_margin = 1.6 * te.height;
    spectrum_top_margin = spectrum_bottom_margin;
    spectrum_left_margin = te.width * 1.5;
    spectrum_right_margin = 0.03 * spectrum_size_x;
    if (spectrum_right_margin > 0.8 * spectrum_left_margin) {
        spectrum_right_margin = 0.8 * spectrum_left_margin;
    }
    spectrum_inner_x = spectrum_size_x - spectrum_left_margin
            - spectrum_right_margin;
    spectrum_inner_y = spectrum_size_y - spectrum_bottom_margin
            - spectrum_top_margin;

    sprintf(buff, "000 Hz");
    cairo_text_extents(cr, buff, &te);
    // minimum grid size in pixels
    const int minimum_grid_width = 1.5 * te.width;
    const int minimum_grid_height = 3.0 * te.height;

    // clear all
    cairo_set_source_rgba(cr, 0.06, 0.2, 0.06, 1.0);
    GdkRectangle r = { .x = 0, .y = 0, .width = spectrum_size_x, .height =
                       spectrum_size_y };
    gdk_cairo_rectangle(cr, &r);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 0.56, 0.56, 0.56, 1.0);
    cairo_set_line_width(cr, 1.0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
    cairo_move_to(cr, spectrum_left_margin,
                  spectrum_size_y - spectrum_bottom_margin);
    cairo_rel_line_to(cr, spectrum_inner_x, 0);
    cairo_stroke(cr);

    // choose scale factor
    const FLT spectrum_max_frequency = 0.5 * frame->conf.sample_rate
            / frame->conf.oversampling;

    // scale factors (in KHz) to draw the grid. We will choose the smaller
    // factor that respects the minimum_grid_width
    static const double scales[] = { 0.01, 0.05, 0.1, 0.2, 0.5, 1, 2, 4, 11, 22,
                                     -1.0 };

    int i;
    for (i = 0; scales[i + 1] > 0.0; i++) {
        if ((1e3 * scales[i] * spectrum_inner_x / spectrum_max_frequency)
                > minimum_grid_width)
            break;
    }

    FLT scale = scales[i];
    FLT grid_width = 1e3 * scales[i] * spectrum_inner_x
            / spectrum_max_frequency;

    FLT freq = 0.0;
    FLT x;
    for (x = 0.0; x <= spectrum_inner_x; x += grid_width) {
        cairo_move_to(cr, spectrum_left_margin + x, spectrum_top_margin);
        cairo_rel_line_to(cr, 0, spectrum_inner_y + 3); // TODO: proportion
        cairo_stroke(cr);

        lingot_gui_mainframe_format_frequency(freq, buff);

        cairo_text_extents(cr, buff, &te);
        cairo_move_to(cr,
                      spectrum_left_margin + x + 6 - te.width / 2 - te.x_bearing,
                      spectrum_size_y - 0.5 * spectrum_bottom_margin - te.height / 2
                      - te.y_bearing);
        cairo_show_text(cr, buff);
        freq += scale;
    }

    spectrum_db_density = (spectrum_inner_y)
            / (spectrum_max_db - spectrum_min_db);

    sprintf(buff, "dB");

    cairo_text_extents(cr, buff, &te);
    cairo_move_to(cr, spectrum_left_margin - te.x_bearing,
                  0.5 * spectrum_top_margin - te.height / 2 - te.y_bearing);
    cairo_show_text(cr, buff);

    // scale factors (in KHz) to draw the grid. We will choose the smallest
    // factor that respects the minimum_grid_width
    static const int db_scales[] = { 5, 10, 20, 25, 50, 75, 100, -1 };
    for (i = 0; db_scales[i + 1] > 0; i++) {
        if ((db_scales[i] * spectrum_db_density) > minimum_grid_height)
            break;
    }

    const int db_scale = db_scales[i];

    FLT y = 0;
    int i0 = ceil(spectrum_min_db / db_scale);
    if (spectrum_min_db < 0.0) {
        i0--;
    }
    int i1 = ceil(spectrum_max_db / db_scale);
    for (i = i0; i <= i1; i++) {
        y = spectrum_db_density * (i * db_scale - spectrum_min_db);
        if ((y < 0.0) || (y > spectrum_inner_y)) {
            continue;
        }
        sprintf(buff, "%d", i * db_scale);

        cairo_text_extents(cr, buff, &te);
        cairo_move_to(cr,
                      0.45 * spectrum_left_margin - te.width / 2 - te.x_bearing,
                      spectrum_size_y - spectrum_bottom_margin - y - te.height / 2
                      - te.y_bearing);
        cairo_show_text(cr, buff);

        cairo_move_to(cr, spectrum_left_margin - 3,
                      spectrum_size_y - spectrum_bottom_margin - y);
        cairo_rel_line_to(cr, spectrum_inner_x + 3, 0);
        cairo_stroke(cr);
    }
}

void lingot_gui_mainframe_draw_spectrum(cairo_t *cr, const LingotMainFrame* frame) {

    unsigned int i;

    lingot_gui_mainframe_draw_spectrum_background(cr, frame);

    // TODO: change access to frame->core.X
    // spectrum drawing.
    if (frame->core.running) {

        cairo_set_line_width(cr, 1.0);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);

        FLT x;
        FLT y = -1;

        const unsigned int min_index = 0;
        const unsigned int max_index = frame->conf.fft_size / 2;

        FLT index_density = spectrum_inner_x / max_index;
        // TODO: step
        const unsigned int index_step = 1;

        static const double dashed1[] = { 5.0, 5.0 };
        static int len1 = sizeof(dashed1) / sizeof(dashed1[0]);

        const FLT x0 = spectrum_left_margin;
        const FLT y0 = spectrum_size_y - spectrum_bottom_margin;

        FLT dydxm1 = 0;

        cairo_set_source_rgba(cr, 0.13, 1.0, 0.13, 1.0);

        cairo_translate(cr, x0, y0);
        cairo_rectangle(cr, 1.0, -1.0, spectrum_inner_x - 2,
                        -(spectrum_inner_y - 2));
        cairo_clip(cr);
        cairo_new_path(cr); // path not consumed by clip()

        y = -spectrum_db_density
                * lingot_gui_mainframe_get_signal(frame, min_index,
                                                  spectrum_min_db, spectrum_max_db); // dB.

        cairo_move_to(cr, 0, 0);
        cairo_line_to(cr, 0, y);

        FLT yp1 = -spectrum_db_density
                * lingot_gui_mainframe_get_signal(frame, min_index + 1,
                                                  spectrum_min_db, spectrum_max_db);
        FLT ym1 = y;

        for (i = index_step; i < max_index - 1; i += index_step) {

            x = index_density * i;
            ym1 = y;
            y = yp1;
            yp1 = -spectrum_db_density
                    * lingot_gui_mainframe_get_signal(frame, i + 1,
                                                      spectrum_min_db, spectrum_max_db);
            FLT dydx = (yp1 - ym1) / (2 * index_density);
            static const FLT dx = 0.4;
            FLT x1 = x - (1 - dx) * index_density;
            FLT x2 = x - dx * index_density;
            FLT y1 = ym1 + dydxm1 * dx;
            FLT y2 = y - dydx * dx;

            dydxm1 = dydx;
            cairo_curve_to(cr, x1, y1, x2, y2, x, y);
            //			cairo_line_to(cr, x, y);
        }

        y = -spectrum_db_density
                * lingot_gui_mainframe_get_signal(frame, max_index - 1,
                                                  spectrum_min_db, spectrum_max_db); // dB.
        cairo_line_to(cr, index_density * max_index, y);
        cairo_line_to(cr, index_density * max_index, 0);
        cairo_close_path(cr);
        cairo_fill_preserve(cr);
        //		cairo_restore(cr);
        cairo_stroke(cr);

#ifdef DRAW_MARKERS
        cairo_set_source_rgba(cr, 1.0, 1.0, 0.13, 1.0);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_width(cr, 10.0);

        for (i = 0; i < frame->core.markers_size2; i++) {

            x = index_density * frame->core.markers2[i];
            y = -spectrum_db_density
                    * lingot_gui_mainframe_get_signal(frame,
                                                      frame->core.markers2[i], spectrum_min_db,
                                                      spectrum_max_db); // dB.
            cairo_move_to(cr, x, y);
            cairo_rel_line_to(cr, 0, 0);
            cairo_stroke(cr);
        }

        cairo_set_line_width(cr, 4.0);
        cairo_set_source_rgba(cr, 0.13, 0.13, 1.0, 1.0);

        for (i = 0; i < frame->core.markers_size; i++) {

            x = index_density * frame->core.markers[i];
            y = -spectrum_db_density
                    * lingot_gui_mainframe_get_signal(frame,
                                                      frame->core.markers[i], spectrum_min_db,
                                                      spectrum_max_db); // dB.
            cairo_move_to(cr, x, y);
            cairo_rel_line_to(cr, 0, 0);
            cairo_stroke(cr);
        }
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
        cairo_set_line_width(cr, 1.0);
#endif

        if (frame->core.freq != 0.0) {

            cairo_set_dash(cr, dashed1, len1, 0);

            // fundamental frequency mark with a red vertical line.
            cairo_set_source_rgba(cr, 1.0, 0.13, 0.13, 1.0);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

            cairo_set_line_width(cr, 1.0);

            // index of closest sample to fundamental frequency.
            x = index_density * frame->core.freq * frame->conf.fft_size
                    * frame->conf.oversampling / frame->conf.sample_rate;
            cairo_move_to(cr, x, 0);
            cairo_rel_line_to(cr, 0.0, -spectrum_inner_y);
            cairo_stroke(cr);

            //			i = (int) rint(
            //					frame->core.freq * frame->conf.fft_size
            //							* frame->conf.oversampling
            //							/ frame->conf.sample_rate);
            //			y = -spectrum_db_density
            //					* lingot_gui_mainframe_get_signal(frame, i, spectrum_min_db,
            //							spectrum_max_db); // dB.
            //			cairo_set_line_width(cr, 4.0);
            //			cairo_move_to(cr, x, y);
            //			cairo_rel_line_to(cr, 0, 0);
            //			cairo_stroke(cr);

            cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
            cairo_set_line_width(cr, 1.0);
        }

        cairo_set_dash(cr, dashed1, len1, 0);
        cairo_set_source_rgba(cr, 1.0, 1.0, 0.2, 1.0);

        y = -spectrum_db_density
                * lingot_gui_mainframe_get_noise(frame, spectrum_min_db,
                                                 spectrum_max_db); // dB.
        cairo_move_to(cr, 0, y);
        // noise threshold drawing.
        for (i = min_index + index_step; i < max_index - 1; i += index_step) {

            x = index_density * i;
            y = -spectrum_db_density
                    * lingot_gui_mainframe_get_noise(frame, spectrum_min_db,
                                                     spectrum_max_db); // dB.
            cairo_line_to(cr, x, y);
        }
        cairo_stroke(cr);

    }

}

void lingot_gui_mainframe_draw_labels(const LingotMainFrame* frame) {

    char* note_string;
    static char error_string[30];
    static char freq_string[30];
    static char octave_string[30];

    // draw note, error and frequency labels

    if (frequency == 0.0) {
        note_string = "---";
        strcpy(error_string, "e = ---");
        strcpy(freq_string, "f = ---");
        strcpy(octave_string, "");
    } else {
        note_string =
                frame->conf.scale.note_name[lingot_config_scale_get_note_index(
                    &frame->conf.scale, closest_note_index)];
        sprintf(error_string, "e = %+2.0f cents", frame->gauge.position);
        sprintf(freq_string, "f = %6.2f Hz", frequency);
        sprintf(octave_string, "%d",
                lingot_config_scale_get_octave(&frame->conf.scale,
                                               closest_note_index) + 4);
    }

    int font_size = 9 + labelsbox_size_x / 80;
    char* markup = g_markup_printf_escaped("<span font_desc=\"%d\">%s</span>",
                                           font_size, freq_string);
    gtk_label_set_markup(GTK_LABEL(frame->freq_label), markup);
    g_free(markup);
    markup = g_markup_printf_escaped("<span font_desc=\"%d\">%s</span>",
                                     font_size, error_string);
    gtk_label_set_markup(GTK_LABEL(frame->error_label), markup);
    g_free(markup);

    font_size = 10 + labelsbox_size_x / 22;
    markup =
            g_markup_printf_escaped(
                "<span font_desc=\"%d\" weight=\"bold\">%s</span><span font_desc=\"%d\" weight=\"bold\"><sub>%s</sub></span>",
                font_size, note_string, (int) (0.75 * font_size),
                octave_string);
    gtk_label_set_markup(GTK_LABEL(frame->tone_label), markup);
    g_free(markup);
}

void lingot_gui_mainframe_change_config(LingotMainFrame* frame,
                                        LingotConfig* conf) {
    lingot_core_stop(&frame->core);
    lingot_core_destroy(&frame->core);

    // dup.
    lingot_config_copy(&frame->conf, conf);

    lingot_core_new(&frame->core, &frame->conf);
    lingot_core_start(&frame->core);

    // some parameters may have changed
    lingot_config_copy(conf, &frame->conf);
}
