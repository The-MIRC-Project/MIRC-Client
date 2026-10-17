#include "mirc.h"
#include <math.h>

typedef struct {
    Profile *p;
    int size;
} AvatarData;

static void avatar_data_free(gpointer d) {
    AvatarData *a = d;
    profile_free(a->p);
    g_free(a);
}

static gboolean avatar_draw(GtkWidget *w, cairo_t *cr, gpointer data) {
    (void)w;
    AvatarData *a = data;
    double sz = a->size, r = sz / 2;
    cairo_arc(cr, r, r, r, 0, 2 * G_PI);
    cairo_clip(cr);

    GdkPixbuf *pb = image_get(a->p->avatar_url);
    if (pb) {
        int pw = gdk_pixbuf_get_width(pb), ph = gdk_pixbuf_get_height(pb);
        double scale = sz / MIN(pw, ph);
        cairo_save(cr);
        cairo_translate(cr, (sz - pw * scale) / 2, (sz - ph * scale) / 2);
        cairo_scale(cr, scale, scale);
        gdk_cairo_set_source_pixbuf(cr, pb, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);
        cairo_restore(cr);
        return FALSE;
    }

    double cr_, cg, cb;
    if (!parse_color(a->p->accent, &cr_, &cg, &cb)) color_for_name(a->p->username, &cr_, &cg, &cb);
    cairo_set_source_rgb(cr, cr_, cg, cb);
    cairo_paint(cr);

    const char *name = a->p->display_name && *a->p->display_name ? a->p->display_name : a->p->username;
    gunichar ch = g_unichar_toupper(g_utf8_get_char(name ? name : "?"));
    char buf[8] = { 0 };
    g_unichar_to_utf8(ch, buf);
    PangoLayout *l = gtk_widget_create_pango_layout(w, buf);
    PangoFontDescription *fd = pango_font_description_from_string("Sans Bold");
    pango_font_description_set_absolute_size(fd, sz * 0.44 * PANGO_SCALE);
    pango_layout_set_font_description(l, fd);
    int tw, th;
    pango_layout_get_pixel_size(l, &tw, &th);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_move_to(cr, (sz - tw) / 2.0, (sz - th) / 2.0);
    pango_cairo_show_layout(cr, l);
    pango_font_description_free(fd);
    g_object_unref(l);
    return FALSE;
}

static void avatar_closure_free(gpointer d, GClosure *c) { (void)c; avatar_data_free(d); }

GtkWidget *avatar_new(const Profile *p, int size) {
    GtkWidget *da = gtk_drawing_area_new();
    gtk_widget_set_size_request(da, size, size);
    gtk_widget_set_valign(da, GTK_ALIGN_START);
    gtk_widget_set_halign(da, GTK_ALIGN_CENTER);
    AvatarData *a = g_new0(AvatarData, 1);
    a->p = p ? profile_dup(p) : profile_fallback("?");
    a->size = size;
    g_signal_connect_data(da, "draw", G_CALLBACK(avatar_draw), a, avatar_closure_free, 0);
    return da;
}
