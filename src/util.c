/* Helpers: profiles, messages, colors, Pango markup. */
#include "mirc.h"
#include <string.h>
#include <math.h>

const char *jstr(JsonObject *o, const char *key) {
    if (!o || !json_object_has_member(o, key)) return NULL;
    JsonNode *n = json_object_get_member(o, key);
    if (!JSON_NODE_HOLDS_VALUE(n) || json_node_get_value_type(n) != G_TYPE_STRING) return NULL;
    return json_node_get_string(n);
}

gint64 jint(JsonObject *o, const char *key) {
    if (!o || !json_object_has_member(o, key)) return 0;
    JsonNode *n = json_object_get_member(o, key);
    if (!JSON_NODE_HOLDS_VALUE(n)) return 0;
    GType t = json_node_get_value_type(n);
    if (t == G_TYPE_INT64) return json_node_get_int(n);
    if (t == G_TYPE_DOUBLE) return (gint64)json_node_get_double(n);
    return 0;
}

gboolean jbool(JsonObject *o, const char *key) {
    if (!o || !json_object_has_member(o, key)) return FALSE;
    JsonNode *n = json_object_get_member(o, key);
    return JSON_NODE_HOLDS_VALUE(n) && json_node_get_value_type(n) == G_TYPE_BOOLEAN && json_node_get_boolean(n);
}

char *json_to_string_obj(JsonObject *o) {
    JsonNode *n = json_node_new(JSON_NODE_OBJECT);
    json_node_set_object(n, o);
    char *s = json_to_string(n, FALSE);
    json_node_free(n);
    return s;
}

static char *dupnz(const char *s) { return (s && *s) ? g_strdup(s) : NULL; }

char *str_lower(const char *s) { return s ? g_utf8_strdown(s, -1) : g_strdup(""); }

Profile *profile_from_json(JsonObject *o) {
    if (!o) return NULL;
    Profile *p = g_new0(Profile, 1);
    p->username = g_strdup(jstr(o, "username") ? jstr(o, "username") : "?");
    p->display_name = g_strdup(jstr(o, "displayName") && *jstr(o, "displayName") ? jstr(o, "displayName") : p->username);
    p->bio = dupnz(jstr(o, "bio"));
    p->created = jint(o, "createdAt");
    JsonObject *c = json_object_has_member(o, "cosmetics") ? json_object_get_object_member(o, "cosmetics") : NULL;
    if (c) {
        p->name_color = dupnz(jstr(c, "nameColor"));
        p->avatar_url = dupnz(jstr(c, "avatarUrl"));
        p->banner_url = dupnz(jstr(c, "bannerUrl"));
        p->banner_color = dupnz(jstr(c, "bannerColor"));
        if (json_object_has_member(c, "nameGradient")) {
            JsonNode *gn = json_object_get_member(c, "nameGradient");
            if (JSON_NODE_HOLDS_ARRAY(gn)) {
                JsonArray *g = json_node_get_array(gn);
                for (guint i = 0; i < json_array_get_length(g) && p->grad_n < 4; i++)
                    p->grad[p->grad_n++] = g_strdup(json_array_get_string_element(g, i));
            }
        }
        JsonObject *t = json_object_has_member(c, "theme") ? json_object_get_object_member(c, "theme") : NULL;
        if (t) {
            p->accent = dupnz(jstr(t, "accent"));
            p->background = dupnz(jstr(t, "background"));
        }
    }
    return p;
}

Profile *profile_fallback(const char *username) {
    Profile *p = g_new0(Profile, 1);
    p->username = g_strdup(username ? username : "?");
    p->display_name = g_strdup(p->username);
    return p;
}

Profile *profile_dup(const Profile *s) {
    if (!s) return NULL;
    Profile *p = g_new0(Profile, 1);
    p->username = g_strdup(s->username);
    p->display_name = g_strdup(s->display_name);
    p->bio = g_strdup(s->bio);
    p->name_color = g_strdup(s->name_color);
    for (int i = 0; i < s->grad_n; i++) p->grad[i] = g_strdup(s->grad[i]);
    p->grad_n = s->grad_n;
    p->avatar_url = g_strdup(s->avatar_url);
    p->banner_url = g_strdup(s->banner_url);
    p->banner_color = g_strdup(s->banner_color);
    p->accent = g_strdup(s->accent);
    p->background = g_strdup(s->background);
    p->created = s->created;
    return p;
}

void profile_free(Profile *p) {
    if (!p) return;
    g_free(p->username); g_free(p->display_name); g_free(p->bio); g_free(p->name_color);
    for (int i = 0; i < p->grad_n; i++) g_free(p->grad[i]);
    g_free(p->avatar_url); g_free(p->banner_url); g_free(p->banner_color);
    g_free(p->accent); g_free(p->background);
    g_free(p);
}

/* ---------- Farben ---------- */

gboolean parse_color(const char *hex, double *r, double *g, double *b) {
    if (!hex || strlen(hex) != 7 || hex[0] != '#') return FALSE;
    unsigned v;
    if (sscanf(hex + 1, "%06x", &v) != 1) return FALSE;
    *r = ((v >> 16) & 0xff) / 255.0;
    *g = ((v >> 8) & 0xff) / 255.0;
    *b = (v & 0xff) / 255.0;
    return TRUE;
}

/* Stable, readable color derived from the name (for users without cosmetics). */
void color_for_name(const char *name, double *r, double *g, double *b) {
    guint h = g_str_hash(name ? name : "");
    double hue = (h % 360) / 60.0, s = 0.55, l = 0.62;
    double c = (1 - fabs(2 * l - 1)) * s, x = c * (1 - fabs(fmod(hue, 2) - 1)), m = l - c / 2;
    double rr = 0, gg = 0, bb = 0;
    switch ((int)hue) {
    case 0: rr = c; gg = x; break;
    case 1: rr = x; gg = c; break;
    case 2: gg = c; bb = x; break;
    case 3: gg = x; bb = c; break;
    case 4: rr = x; bb = c; break;
    default: rr = c; bb = x; break;
    }
    *r = rr + m; *g = gg + m; *b = bb + m;
}

/* Name as Pango markup: solid color or gradient (one color per character). */
char *name_markup(const Profile *p, const char *fallback, int size_pct) {
    const char *name = p ? p->display_name : fallback;
    if (!name) name = "?";
    GString *out = g_string_new(NULL);
    if (size_pct > 0) g_string_append_printf(out, "<span size=\"%d%%\">", size_pct);
    g_string_append(out, "<b>");

    if (p && p->grad_n >= 2) {
        glong n = g_utf8_strlen(name, -1);
        const char *ch = name;
        for (glong i = 0; i < n; i++) {
            double t = n > 1 ? (double)i / (n - 1) : 0;
            double pos = t * (p->grad_n - 1);
            int seg = (int)pos;
            if (seg >= p->grad_n - 1) seg = p->grad_n - 2;
            double f = pos - seg, r1, g1, b1, r2, g2, b2;
            if (!parse_color(p->grad[seg], &r1, &g1, &b1) || !parse_color(p->grad[seg + 1], &r2, &g2, &b2)) { r1 = g1 = b1 = r2 = g2 = b2 = 0.9; }
            int R = (int)((r1 + (r2 - r1) * f) * 255), G = (int)((g1 + (g2 - g1) * f) * 255), B = (int)((b1 + (b2 - b1) * f) * 255);
            const char *next = g_utf8_next_char(ch);
            char *esc = g_markup_escape_text(ch, next - ch);
            g_string_append_printf(out, "<span foreground=\"#%02x%02x%02x\">%s</span>", R, G, B, esc);
            g_free(esc);
            ch = next;
        }
    } else {
        char color[8];
        double r, g, b;
        if (p && p->name_color && parse_color(p->name_color, &r, &g, &b))
            g_strlcpy(color, p->name_color, sizeof color);
        else if (p && p->accent) /* MIRC-Nutzer ohne Namensfarbe: normale Textfarbe */
            g_strlcpy(color, "#e9e7ef", sizeof color);
        else {
            color_for_name(name, &r, &g, &b);
            g_snprintf(color, sizeof color, "#%02x%02x%02x", (int)(r * 255), (int)(g * 255), (int)(b * 255));
        }
        char *esc = g_markup_escape_text(name, -1);
        g_string_append_printf(out, "<span foreground=\"%s\">%s</span>", color, esc);
        g_free(esc);
    }
    g_string_append(out, "</b>");
    if (size_pct > 0) g_string_append(out, "</span>");
    return g_string_free(out, FALSE);
}

/* Escape text and make http(s) links clickable. */
char *linkify_markup(const char *text) {
    static GRegex *re = NULL;
    if (!re) re = g_regex_new("https?://[^\\s<>\"]+[^\\s<>\".,;:!?)\\]]", G_REGEX_CASELESS, 0, NULL);
    GString *out = g_string_new(NULL);
    GMatchInfo *mi = NULL;
    const char *last = text;
    g_regex_match(re, text, 0, &mi);
    while (g_match_info_matches(mi)) {
        int s, e;
        g_match_info_fetch_pos(mi, 0, &s, &e);
        char *pre = g_markup_escape_text(last, text + s - last);
        char *url = g_markup_escape_text(text + s, e - s);
        g_string_append_printf(out, "%s<a href=\"%s\">%s</a>", pre, url, url);
        g_free(pre); g_free(url);
        last = text + e;
        g_match_info_next(mi, NULL);
    }
    g_match_info_free(mi);
    char *rest = g_markup_escape_text(last, -1);
    g_string_append(out, rest);
    g_free(rest);
    return g_string_free(out, FALSE);
}

char *format_time(gint64 ms, gboolean with_day) {
    GDateTime *dt = g_date_time_new_from_unix_local(ms / 1000);
    GDateTime *now = g_date_time_new_now_local();
    char *s;
    if (!with_day) s = g_date_time_format(dt, "%H:%M");
    else if (g_date_time_get_year(dt) == g_date_time_get_year(now) && g_date_time_get_day_of_year(dt) == g_date_time_get_day_of_year(now)) {
        char *t = g_date_time_format(dt, "%H:%M");
        s = g_strdup_printf("Today at %s", t);
        g_free(t);
    } else s = g_date_time_format(dt, "%d.%m.%Y %H:%M");
    g_date_time_unref(dt);
    g_date_time_unref(now);
    return s;
}

char *format_day(gint64 ms) {
    static const char *days[] = { "", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday" };
    static const char *months[] = { "", "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
    GDateTime *dt = g_date_time_new_from_unix_local(ms / 1000);
    char *s = g_strdup_printf("%s, %d. %s %d", days[g_date_time_get_day_of_week(dt)], g_date_time_get_day_of_month(dt),
                              months[g_date_time_get_month(dt)], g_date_time_get_year(dt));
    g_date_time_unref(dt);
    return s;
}

/* ---------- Messages ---------- */

Message *message_from_json(JsonObject *o) {
    if (!o) return NULL;
    Message *m = g_new0(Message, 1);
    m->id = g_strdup(jstr(o, "id"));
    m->target = g_strdup(jstr(o, "target"));
    m->from = g_strdup(jstr(o, "from"));
    m->text = g_strdup(jstr(o, "text") ? jstr(o, "text") : "");
    m->kind = g_strdup(jstr(o, "kind") ? jstr(o, "kind") : "text");
    m->ts = jint(o, "ts");
    m->edited = jint(o, "edited");
    return m;
}

Message *message_new_system(const char *text, const char *kind) {
    static guint seq = 0;
    Message *m = g_new0(Message, 1);
    m->id = g_strdup_printf("sys%u", ++seq);
    m->from = g_strdup("");
    m->text = g_strdup(text);
    m->kind = g_strdup(kind ? kind : "system");
    m->ts = g_get_real_time() / 1000;
    return m;
}

void message_free(Message *m) {
    if (!m) return;
    g_free(m->id); g_free(m->target); g_free(m->from); g_free(m->text); g_free(m->kind);
    g_free(m);
}

/* Mention: username as a whole word (optionally prefixed with @). */
gboolean text_mentions(const char *text, const char *username) {
    if (!text || !username || !*username) return FALSE;
    char *esc = g_regex_escape_string(username, -1);
    char *pat = g_strdup_printf("(^|[^\\w])@?%s($|[^\\w])", esc);
    gboolean r = g_regex_match_simple(pat, text, G_REGEX_CASELESS, 0);
    g_free(pat); g_free(esc);
    return r;
}
