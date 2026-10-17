/* Dialogs: sign-in, server setup, certificates, channels, profile cards. */
#include "mirc.h"
#include <string.h>

static GtkWidget *field(GtkWidget *grid, int row, const char *labeltext, GtkWidget *w) {
    GtkWidget *l = gtk_label_new(labeltext);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(l), "muted");
    gtk_grid_attach(GTK_GRID(grid), l, 0, row * 2, 2, 1);
    gtk_widget_set_hexpand(w, TRUE);
    gtk_grid_attach(GTK_GRID(grid), w, 0, row * 2 + 1, 2, 1);
    return w;
}

static GtkWidget *dialog_new(const char *title, GtkWindow *parent, GtkWidget **content) {
    GtkWidget *d = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(d), title);
    if (parent) gtk_window_set_transient_for(GTK_WINDOW(d), parent);
    gtk_window_set_modal(GTK_WINDOW(d), TRUE);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(d), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(d), FALSE);
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 18);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(d))), grid, TRUE, TRUE, 0);
    *content = grid;
    return d;
}

static void style_button(GtkWidget *d, int response, const char *cls) {
    GtkWidget *b = gtk_dialog_get_widget_for_response(GTK_DIALOG(d), response);
    if (b) gtk_style_context_add_class(gtk_widget_get_style_context(b), cls);
}

/* ---------- Sign-in ---------- */

typedef struct {
    GtkWidget *dlg, *url, *user, *pass, *error, *button;
} LoginUI;

static void login_done(guint status, JsonNode *root, const char *error, gpointer data) {
    (void)status;
    LoginUI *l = data;
    gtk_widget_set_sensitive(l->button, TRUE);
    if (error || !root || !JSON_NODE_HOLDS_OBJECT(root)) {
        gtk_label_set_text(GTK_LABEL(l->error), error ? error : "Unknown error");
        return;
    }
    JsonObject *o = json_node_get_object(root);
    char *url = g_strdup(gtk_entry_get_text(GTK_ENTRY(l->url)));
    g_strstrip(url);
    while (*url && url[strlen(url) - 1] == '/') url[strlen(url) - 1] = 0;
    g_free(app.session_url);
    app.session_url = url;
    g_free(app.token);
    app.token = g_strdup(jstr(o, "token"));
    JsonObject *p = json_object_get_object_member(o, "profile");
    profile_free(app.me);
    app.me = profile_from_json(p);
    g_free(app.me_json);
    app.me_json = json_to_string_obj(p);
    config_set_session(app.session_url, app.me->username, app.token);
    gtk_widget_destroy(l->dlg);
    g_free(l);
    app_start_session();
}

static void login_submit(LoginUI *l) {
    const char *url = gtk_entry_get_text(GTK_ENTRY(l->url));
    if (!g_str_has_prefix(url, "http://") && !g_str_has_prefix(url, "https://")) {
        gtk_label_set_text(GTK_LABEL(l->error), "The address must start with http:// or https://.");
        return;
    }
    gtk_label_set_text(GTK_LABEL(l->error), "Signing in …");
    gtk_widget_set_sensitive(l->button, FALSE);
    char *u = g_strdup(url);
    g_strstrip(u);
    while (*u && u[strlen(u) - 1] == '/') u[strlen(u) - 1] = 0;
    session_login(u, gtk_entry_get_text(GTK_ENTRY(l->user)), gtk_entry_get_text(GTK_ENTRY(l->pass)), login_done, l);
    g_free(u);
}

static void login_response(GtkDialog *d, int resp, gpointer data) {
    LoginUI *l = data;
    if (resp == GTK_RESPONSE_OK) { login_submit(l); return; }
    if (resp == 1) { /* Registrieren */
        char *u = g_strdup_printf("%s/register", gtk_entry_get_text(GTK_ENTRY(l->url)));
        GError *err = NULL;
        if (!gtk_show_uri_on_window(GTK_WINDOW(d), u, GDK_CURRENT_TIME, &err)) {
            gtk_label_set_text(GTK_LABEL(l->error), err->message);
            g_error_free(err);
        }
        g_free(u);
        return;
    }
    g_application_quit(G_APPLICATION(app.gapp));
}

static void login_activate(GtkEntry *e, gpointer data) { (void)e; login_submit(data); }

void dialog_login(void) {
    LoginUI *l = g_new0(LoginUI, 1);
    GtkWidget *grid;
    l->dlg = dialog_new("MIRC – Sign in", NULL, &grid);
    gtk_window_set_application(GTK_WINDOW(l->dlg), app.gapp);
    gtk_window_set_default_size(GTK_WINDOW(l->dlg), 400, -1);
    gtk_window_set_icon_name(GTK_WINDOW(l->dlg), "mirc");

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span size=\"xx-large\" weight=\"heavy\">MIRC</span>\n<span foreground=\"#9a98a6\">Modern Internet Relay Chat</span>");
    gtk_label_set_xalign(GTK_LABEL(title), 0);
    gtk_grid_attach(GTK_GRID(grid), title, 0, 0, 2, 1);
    gtk_widget_set_margin_bottom(title, 10);

    GtkWidget *g2 = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(g2), 6);
    gtk_grid_attach(GTK_GRID(grid), g2, 0, 1, 2, 1);
    l->user = field(g2, 0, "Username", gtk_entry_new());
    l->pass = field(g2, 1, "Password", gtk_entry_new());
    gtk_entry_set_visibility(GTK_ENTRY(l->pass), FALSE);
    gtk_entry_set_input_purpose(GTK_ENTRY(l->pass), GTK_INPUT_PURPOSE_PASSWORD);
    l->url = field(g2, 2, "Session server", gtk_entry_new());
    gtk_entry_set_text(GTK_ENTRY(l->url), app.session_url);
    char *last = config_get_string("session", "username");
    if (last) { gtk_entry_set_text(GTK_ENTRY(l->user), last); g_free(last); }
    g_signal_connect(l->pass, "activate", G_CALLBACK(login_activate), l);
    g_signal_connect(l->user, "activate", G_CALLBACK(login_activate), l);

    l->error = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(l->error), 0);
    gtk_label_set_line_wrap(GTK_LABEL(l->error), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(l->error), 40);
    gtk_grid_attach(GTK_GRID(grid), l->error, 0, 2, 2, 1);

    gtk_dialog_add_button(GTK_DIALOG(l->dlg), "Create account (website)", 1);
    l->button = gtk_dialog_add_button(GTK_DIALOG(l->dlg), "Sign in", GTK_RESPONSE_OK);
    style_button(l->dlg, GTK_RESPONSE_OK, "primary");
    style_button(l->dlg, 1, "ghost");
    g_signal_connect(l->dlg, "response", G_CALLBACK(login_response), l);
    gtk_widget_show_all(l->dlg);
    gtk_window_set_focus(GTK_WINDOW(l->dlg), gtk_entry_get_text_length(GTK_ENTRY(l->user)) ? l->pass : l->user);
}

/* ---------- Add / edit server ---------- */

static void tls_toggled(GtkToggleButton *t, gpointer port) {
    const char *p = gtk_entry_get_text(GTK_ENTRY(port));
    gboolean on = gtk_toggle_button_get_active(t);
    if (!*p || !strcmp(p, on ? "6680" : "6697")) gtk_entry_set_text(GTK_ENTRY(port), on ? "6697" : "6680");
}

void dialog_add_server(ServerCfg *edit) {
    GtkWidget *grid;
    GtkWidget *d = dialog_new(edit ? "Edit server" : "Add server", ui_window(), &grid);
    gtk_window_set_default_size(GTK_WINDOW(d), 420, -1);
    GtkWidget *hint = gtk_label_new("Address of a MIRC chat server. Plain-text port is usually 6680, TLS 6697.");
    gtk_label_set_xalign(GTK_LABEL(hint), 0);
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(hint), "muted");
    gtk_grid_attach(GTK_GRID(grid), hint, 0, 0, 2, 1);
    GtkWidget *g2 = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(g2), 6);
    gtk_grid_attach(GTK_GRID(grid), g2, 0, 1, 2, 1);
    GtkWidget *host = field(g2, 0, "Host or IP", gtk_entry_new());
    GtkWidget *port = field(g2, 1, "Port", gtk_entry_new());
    GtkWidget *name = field(g2, 2, "Display name (optional)", gtk_entry_new());
    GtkWidget *tls = gtk_check_button_new_with_label("TLS encryption (recommended)");
    gtk_grid_attach(GTK_GRID(g2), tls, 0, 6, 2, 1);
    gtk_entry_set_placeholder_text(GTK_ENTRY(host), "chat.example.de");
    gtk_entry_set_placeholder_text(GTK_ENTRY(name), "otherwise provided by the server");
    if (edit) {
        gtk_entry_set_text(GTK_ENTRY(host), edit->host);
        char *p = g_strdup_printf("%d", edit->port);
        gtk_entry_set_text(GTK_ENTRY(port), p);
        g_free(p);
        if (edit->name) gtk_entry_set_text(GTK_ENTRY(name), edit->name);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tls), edit->tls);
    } else {
        gtk_entry_set_text(GTK_ENTRY(host), "localhost");
        gtk_entry_set_text(GTK_ENTRY(port), "6697");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tls), TRUE);
    }
    g_signal_connect(tls, "toggled", G_CALLBACK(tls_toggled), port);
    GtkWidget *err = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(err), 0);
    gtk_grid_attach(GTK_GRID(grid), err, 0, 2, 2, 1);
    gtk_dialog_add_buttons(GTK_DIALOG(d), "Cancel", GTK_RESPONSE_CANCEL, edit ? "Save" : "Connect", GTK_RESPONSE_OK, NULL);
    style_button(d, GTK_RESPONSE_OK, "primary");
    style_button(d, GTK_RESPONSE_CANCEL, "ghost");
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
    gtk_entry_set_activates_default(GTK_ENTRY(host), TRUE);
    gtk_widget_show_all(d);

    while (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        char *h = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(host))));
        int p = atoi(gtk_entry_get_text(GTK_ENTRY(port)));
        if (!*h || strchr(h, ' ') || strchr(h, '/') || p <= 0 || p > 65535) {
            gtk_label_set_text(GTK_LABEL(err), "Please enter a valid host and port (1–65535).");
            g_free(h);
            continue;
        }
        ServerCfg cfg = { 0 };
        cfg.host = h;
        cfg.port = p;
        cfg.tls = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tls));
        cfg.name = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(name))));
        Server *s;
        if (edit) {
            /* Replace the existing server (disconnect the old entry). */
            Server *old = NULL;
            for (guint i = 0; i < app.servers->len; i++) {
                Server *x = g_ptr_array_index(app.servers, i);
                if (&x->cfg == edit) { old = x; break; }
            }
            cfg.id = g_strdup(edit->id);
            s = server_new(&cfg);
            if (old) {
                guint idx;
                g_ptr_array_find(app.servers, old, &idx);
                server_disconnect(old, "Replaced");
                old->removed = TRUE;
                app.servers->pdata[idx] = s;
                server_unref(old);
            } else g_ptr_array_add(app.servers, s);
        } else {
            cfg.id = g_uuid_string_random();
            s = server_new(&cfg);
            g_ptr_array_add(app.servers, s);
        }
        g_free(cfg.id); g_free(cfg.host); g_free(cfg.name);
        config_save_servers();
        server_connect(s);
        ui_select(s, "*");
        break;
    }
    gtk_widget_destroy(d);
}

/* ---------- Confirm certificate (Trust on First Use) ---------- */

void dialog_trust_cert(Server *s) {
    char *hp = g_strdup_printf("%s:%d", s->cfg.host, s->cfg.port);
    char *hpl = str_lower(hp);
    GtkWidget *d = gtk_message_dialog_new(ui_window(), GTK_DIALOG_MODAL, s->pending_mismatch ? GTK_MESSAGE_WARNING : GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
                                          s->pending_mismatch ? "The certificate from %s has changed!" : "Unknown certificate from %s", hp);
    char *pin = config_get_pin(hpl);
    if (s->pending_mismatch)
        gtk_message_dialog_format_secondary_markup(GTK_MESSAGE_DIALOG(d),
            "This may mean that someone is intercepting the connection – or that the operator created a new certificate.\n\n"
            "<b>Stored:</b>\n<tt>%s</tt>\n\n<b>Current:</b>\n<tt>%s</tt>\n\nOnly trust it if the operator has confirmed the new fingerprint.",
            pin ? pin : "", s->pending_fp);
    else
        gtk_message_dialog_format_secondary_markup(GTK_MESSAGE_DIALOG(d),
            "The server uses a self-signed certificate. Compare the fingerprint with the one shown to the operator in the server log:\n\n<tt>%s</tt>\n\n"
            "If you trust it, MIRC will remember this certificate and warn you if it changes.", s->pending_fp);
    gtk_dialog_add_buttons(GTK_DIALOG(d), "Cancel", GTK_RESPONSE_CANCEL, "Trust and connect", GTK_RESPONSE_ACCEPT, NULL);
    style_button(d, GTK_RESPONSE_ACCEPT, s->pending_mismatch ? "danger" : "primary");
    int r = gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
    if (r == GTK_RESPONSE_ACCEPT && s->pending_fp) {
        config_set_pin(hpl, s->pending_fp);
        s->retry = 0;
        server_connect(s);
    } else {
        server_disconnect(s, "Certificate not trusted");
        s->state = ST_ERROR;
        ui_changed(s, NULL, FALSE);
    }
    g_free(pin); g_free(hp); g_free(hpl);
}

/* ---------- Channel beitreten / DM ---------- */

static char *ask_text(const char *title, const char *labeltext, const char *placeholder, const char *button) {
    GtkWidget *grid;
    GtkWidget *d = dialog_new(title, ui_window(), &grid);
    GtkWidget *e = field(grid, 0, labeltext, gtk_entry_new());
    gtk_entry_set_placeholder_text(GTK_ENTRY(e), placeholder);
    gtk_entry_set_activates_default(GTK_ENTRY(e), TRUE);
    gtk_dialog_add_buttons(GTK_DIALOG(d), "Cancel", GTK_RESPONSE_CANCEL, button, GTK_RESPONSE_OK, NULL);
    style_button(d, GTK_RESPONSE_OK, "primary");
    style_button(d, GTK_RESPONSE_CANCEL, "ghost");
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
    gtk_widget_set_size_request(d, 380, -1);
    gtk_widget_show_all(d);
    char *out = NULL;
    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) out = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(e))));
    gtk_widget_destroy(d);
    if (out && !*out) { g_free(out); out = NULL; }
    return out;
}

static void join_channel(Server *s, const char *name) {
    char *ch = name[0] == '#' ? g_strdup(name) : g_strconcat("#", name, NULL);
    char *key = str_lower(ch);
    ui_switch_when_joined(s, key);
    JsonBuilder *b = proto_begin("join");
    proto_str(b, "channel", ch);
    server_send(s, b);
    g_free(ch); g_free(key);
}

void dialog_join(Server *s) {
    if (s->state != ST_ONLINE) { ui_toast("Not connected."); return; }
    char *name = ask_text("Join channel", "Channel name – if it does not exist, it will be created and you will be the owner.", "#new-channel", "Join");
    if (name) { join_channel(s, name); g_free(name); }
}

static void open_dm(Server *s, const char *user) {
    char *low = str_lower(user[0] == '@' ? user + 1 : user);
    char *key = g_strconcat("@", low, NULL);
    ui_switch_when_joined(s, key);
    JsonBuilder *b = proto_begin("open_dm");
    proto_str(b, "username", low);
    server_send(s, b);
    g_free(low); g_free(key);
}

void dialog_new_dm(Server *s) {
    if (s->state != ST_ONLINE) { ui_toast("Not connected."); return; }
    char *user = ask_text("New direct message", "Username", "username", "Open conversation");
    if (user) { open_dm(s, user); g_free(user); }
}

/* ---------- Channel-Liste ---------- */

typedef struct { Server *s; GtkWidget *dlg; } ListCtx;

static void list_join_clicked(GtkButton *b, gpointer data) {
    ListCtx *c = data;
    const char *name = g_object_get_data(G_OBJECT(b), "channel");
    if (g_object_get_data(G_OBJECT(b), "joined")) {
        char *key = str_lower(name);
        ui_select(c->s, key);
        g_free(key);
    } else join_channel(c->s, name);
    gtk_widget_destroy(c->dlg);
}

void dialog_channel_list(Server *s, JsonArray *channels) {
    GtkWidget *grid;
    GtkWidget *d = dialog_new("Channels", ui_window(), &grid);
    gtk_window_set_resizable(GTK_WINDOW(d), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(d), 520, 460);
    ListCtx *ctx = g_new0(ListCtx, 1);
    ctx->s = s;
    ctx->dlg = d;
    g_object_set_data_full(G_OBJECT(d), "ctx", ctx, g_free);
    GtkWidget *list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    guint n = channels ? json_array_get_length(channels) : 0;
    for (guint i = 0; i < n; i++) {
        JsonObject *o = json_array_get_object_element(channels, i);
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_container_set_border_width(GTK_CONTAINER(row), 8);
        GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        char *esc = g_markup_escape_text(jstr(o, "name"), -1);
        char *topic = g_markup_escape_text(jstr(o, "topic") && *jstr(o, "topic") ? jstr(o, "topic") : "No topic", -1);
        char *m = g_strdup_printf("<b>%s</b>  <span foreground=\"#9a98a6\" size=\"small\">%" G_GINT64_FORMAT " members</span>\n<span foreground=\"#9a98a6\">%s</span>", esc, jint(o, "members"), topic);
        GtkWidget *l = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(l), m);
        gtk_label_set_xalign(GTK_LABEL(l), 0);
        gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
        g_free(esc); g_free(topic); g_free(m);
        gtk_box_pack_start(GTK_BOX(v), l, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), v, TRUE, TRUE, 0);
        gboolean joined = jbool(o, "joined");
        GtkWidget *b = gtk_button_new_with_label(joined ? "Open" : "Join");
        gtk_style_context_add_class(gtk_widget_get_style_context(b), joined ? "ghost" : "primary");
        gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
        g_object_set_data_full(G_OBJECT(b), "channel", g_strdup(jstr(o, "name")), g_free);
        if (joined) g_object_set_data(G_OBJECT(b), "joined", GINT_TO_POINTER(1));
        g_signal_connect(b, "clicked", G_CALLBACK(list_join_clicked), ctx);
        gtk_box_pack_end(GTK_BOX(row), b, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(list), row);
    }
    if (!n) gtk_container_add(GTK_CONTAINER(list), gtk_label_new("No channels yet."));
    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(sw, TRUE);
    gtk_widget_set_hexpand(sw, TRUE);
    gtk_widget_set_size_request(sw, 480, 360);
    gtk_container_add(GTK_CONTAINER(sw), list);
    gtk_grid_attach(GTK_GRID(grid), sw, 0, 0, 2, 1);
    gtk_dialog_add_button(GTK_DIALOG(d), "Close", GTK_RESPONSE_CLOSE);
    style_button(d, GTK_RESPONSE_CLOSE, "ghost");
    g_signal_connect(d, "response", G_CALLBACK(gtk_widget_destroy), NULL);
    gtk_widget_show_all(d);
}

/* ---------- Profilkarte ---------- */

typedef struct { Server *s; char *user; GtkWidget *pop; } CardCtx;

static void card_ctx_free(gpointer p) { CardCtx *c = p; g_free(c->user); g_free(c); }

static gboolean banner_draw(GtkWidget *w, cairo_t *cr, gpointer data) {
    const Profile *p = data;
    int width = gtk_widget_get_allocated_width(w), height = gtk_widget_get_allocated_height(w);
    GdkPixbuf *pb = p->banner_url ? image_get(p->banner_url) : NULL;
    if (pb) {
        double sx = (double)width / gdk_pixbuf_get_width(pb), sy = (double)height / gdk_pixbuf_get_height(pb), sc = MAX(sx, sy);
        cairo_translate(cr, (width - gdk_pixbuf_get_width(pb) * sc) / 2, (height - gdk_pixbuf_get_height(pb) * sc) / 2);
        cairo_scale(cr, sc, sc);
        gdk_cairo_set_source_pixbuf(cr, pb, 0, 0);
        cairo_paint(cr);
        return FALSE;
    }
    double r, g, b;
    if (!parse_color(p->banner_color, &r, &g, &b) && !parse_color(p->accent, &r, &g, &b)) color_for_name(p->username, &r, &g, &b);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_paint(cr);
    return FALSE;
}

static gboolean card_bg_draw(GtkWidget *w, cairo_t *cr, gpointer data) {
    const Profile *p = data;
    double r, g, b;
    if (!parse_color(p->background, &r, &g, &b)) { r = 0.07; g = 0.07; b = 0.075; }
    cairo_set_source_rgb(cr, r, g, b);
    cairo_rectangle(cr, 0, 0, gtk_widget_get_allocated_width(w), gtk_widget_get_allocated_height(w));
    cairo_fill(cr);
    return FALSE;
}

static void card_dm(GtkButton *b, gpointer data) {
    (void)b;
    CardCtx *c = data;
    open_dm(c->s, c->user);
    gtk_popover_popdown(GTK_POPOVER(c->pop));
}

static void card_mod(GtkButton *b, gpointer data) {
    CardCtx *c = data;
    const char *op = g_object_get_data(G_OBJECT(b), "op");
    Conv *conv = server_conv(c->s, app.active_key);
    if (conv && conv->type == CONV_CHANNEL) {
        JsonBuilder *jb = proto_begin(op);
        proto_str(jb, "channel", conv->target);
        proto_str(jb, "username", c->user);
        server_send(c->s, jb);
    }
    gtk_popover_popdown(GTK_POPOVER(c->pop));
}

static void card_edit(GtkButton *b, gpointer data) {
    (void)b;
    CardCtx *c = data;
    char *u = g_strconcat(app.session_url, "/account", NULL);
    ui_open_url(u);
    g_free(u);
    gtk_popover_popdown(GTK_POPOVER(c->pop));
}

void popover_profile(GtkWidget *relative, Server *s, const char *username) {
    if (!s || !username) return;
    Profile *p = profile_dup(server_profile(s, username));
    GtkWidget *pop = gtk_popover_new(relative);
    gtk_popover_set_position(GTK_POPOVER(pop), GTK_POS_RIGHT);
    gtk_style_context_add_class(gtk_widget_get_style_context(pop), "profile");
    g_object_set_data_full(G_OBJECT(pop), "profile", p, (GDestroyNotify)profile_free);
    CardCtx *ctx = g_new0(CardCtx, 1);
    ctx->s = s; ctx->user = g_strdup(p->username); ctx->pop = pop;
    g_object_set_data_full(G_OBJECT(pop), "ctx", ctx, card_ctx_free);

    /* Background uses the user's theme color; children are drawn over it. */
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(box, 300, -1);
    g_signal_connect(box, "draw", G_CALLBACK(card_bg_draw), p);

    /* Banner with overlapping avatar: overlay over banner plus spacing. */
    GtkWidget *top = gtk_overlay_new();
    GtkWidget *topbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *banner = gtk_drawing_area_new();
    gtk_widget_set_size_request(banner, 300, 96);
    g_signal_connect(banner, "draw", G_CALLBACK(banner_draw), p);
    gtk_box_pack_start(GTK_BOX(topbox), banner, FALSE, FALSE, 0);
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(spacer, 300, 42);
    gtk_box_pack_start(GTK_BOX(topbox), spacer, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(top), topbox);
    GtkWidget *av = avatar_new(p, 80);
    gtk_widget_set_halign(av, GTK_ALIGN_START);
    gtk_widget_set_valign(av, GTK_ALIGN_END);
    gtk_widget_set_margin_start(av, 16);
    gtk_overlay_add_overlay(GTK_OVERLAY(top), av);
    gtk_box_pack_start(GTK_BOX(box), top, FALSE, FALSE, 0);

    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_style_context_add_class(gtk_widget_get_style_context(body), "card-body");
    char *nm = name_markup(p, username, 150);
    GtkWidget *nl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(nl), nm);
    gtk_label_set_xalign(GTK_LABEL(nl), 0);
    g_free(nm);
    gtk_box_pack_start(GTK_BOX(body), nl, FALSE, FALSE, 0);
    char *at = g_strdup_printf("@%s", p->username);
    GtkWidget *al = gtk_label_new(at);
    gtk_label_set_xalign(GTK_LABEL(al), 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(al), "muted");
    g_free(at);
    gtk_box_pack_start(GTK_BOX(body), al, FALSE, FALSE, 0);
    if (p->bio) {
        GtkWidget *bl = gtk_label_new(p->bio);
        gtk_label_set_xalign(GTK_LABEL(bl), 0);
        gtk_label_set_line_wrap(GTK_LABEL(bl), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(bl), 34);
        gtk_widget_set_margin_top(bl, 8);
        gtk_box_pack_start(GTK_BOX(body), bl, FALSE, FALSE, 0);
    }
    Conv *conv = server_conv(s, app.active_key);
    char *low = str_lower(p->username);
    Member *mem = conv && conv->type == CONV_CHANNEL ? g_hash_table_lookup(conv->members, low) : NULL;
    if (mem) {
        GtkWidget *rl = gtk_label_new(!g_strcmp0(mem->role, "owner") ? "Channel owner" : !g_strcmp0(mem->role, "op") ? "Operator" : "Member");
        gtk_label_set_xalign(GTK_LABEL(rl), 0);
        gtk_style_context_add_class(gtk_widget_get_style_context(rl), "muted");
        gtk_box_pack_start(GTK_BOX(body), rl, FALSE, FALSE, 0);
    }
    if (p->created) {
        GDateTime *dt = g_date_time_new_from_unix_local(p->created);
        char *ds = g_date_time_format(dt, "Dabei seit %d.%m.%Y");
        GtkWidget *dl = gtk_label_new(ds);
        gtk_label_set_xalign(GTK_LABEL(dl), 0);
        gtk_style_context_add_class(gtk_widget_get_style_context(dl), "muted");
        gtk_style_context_add_class(gtk_widget_get_style_context(dl), "small");
        gtk_box_pack_start(GTK_BOX(body), dl, FALSE, FALSE, 0);
        g_free(ds);
        g_date_time_unref(dt);
    }

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_top(actions, 10);
    gboolean is_me = s->me && !g_strcmp0(low, s->me);
    if (is_me) {
        GtkWidget *b = gtk_button_new_with_label("Edit profile");
        gtk_style_context_add_class(gtk_widget_get_style_context(b), "primary");
        g_signal_connect(b, "clicked", G_CALLBACK(card_edit), ctx);
        gtk_box_pack_start(GTK_BOX(actions), b, TRUE, TRUE, 0);
    } else {
        GtkWidget *b = gtk_button_new_with_label("Message");
        gtk_style_context_add_class(gtk_widget_get_style_context(b), "primary");
        g_signal_connect(b, "clicked", G_CALLBACK(card_dm), ctx);
        gtk_box_pack_start(GTK_BOX(actions), b, TRUE, TRUE, 0);
        if (mem && server_can_moderate(s, conv) && g_strcmp0(mem->role, "owner")) {
            const char *ops[][2] = { { "kick", "Kicken" }, { "ban", "Bannen" } };
            for (int i = 0; i < 2; i++) {
                GtkWidget *mb = gtk_button_new_with_label(ops[i][1]);
                gtk_style_context_add_class(gtk_widget_get_style_context(mb), i ? "danger" : "ghost");
                g_object_set_data(G_OBJECT(mb), "op", (gpointer)ops[i][0]);
                g_signal_connect(mb, "clicked", G_CALLBACK(card_mod), ctx);
                gtk_box_pack_start(GTK_BOX(actions), mb, TRUE, TRUE, 0);
            }
        }
    }
    g_free(low);
    gtk_box_pack_start(GTK_BOX(body), actions, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), body, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(pop), box);
    gtk_widget_show_all(box);
    gtk_popover_popup(GTK_POPOVER(pop));
}

void dialog_about(void) {
    gtk_show_about_dialog(ui_window(),
        "program-name", "MIRC",
        "version", MIRC_VERSION,
        "comments", "Modern Internet Relay Chat\nIRC-style chat with accounts, DMs, and cosmetics.",
        "logo-icon-name", "mirc",
        "license-type", GTK_LICENSE_MIT_X11,
        NULL);
}
