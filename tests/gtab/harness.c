/*
 * Copyright (C) 2026 The HIME team, Taiwan
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation version 2.1
 * of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*
 * Characterization harness for the gtab engine.
 *
 * Links the real key dispatch (eve.c) and table engine (gtab*.c), but
 * replaces the gtab window (win-gtab.c) and the X11 main program
 * (hime.c) with recorders.  Reads a key script on stdin and prints
 * what the engine displayed and committed after every key, so that
 * refactoring can be checked against a golden transcript.
 *
 * Script syntax (one token per whitespace-separated word):
 *   a  ,  0       a printable key (pressed and released)
 *   <space> <enter> <bs> <esc> <tab> <left> <right> <up> <down>
 *   <pgup> <pgdn> <shift> <ctrl>
 *   S-x  C-x      x with Shift / Control held (combinable: C-S-x)
 *   # ...         comment to end of line (echoed as a section header)
 *
 * Needs HOME and HIME_TABLE_DIR set by the caller; see run-tests.sh.
 */

#include <X11/keysym.h>

#include "hime.h"

#include "gtab.h"

/* ---- recorder --------------------------------------------------------- */

static char rec_preedit[1024];
static char rec_cand[4096];
static char rec_keycodes[1024];
static char rec_page[128];
static char rec_buf[4096];
static char rec_presel[4096];
static gboolean rec_win_visible;
static int rec_bells;

/* Strip pango markup and unescape entities so transcripts stay readable. */
static void strip_markup (const char *in, char *out, size_t outsz) {
    size_t n = 0;
    while (*in && n + 1 < outsz) {
        if (*in == '<') {
            while (*in && *in != '>')
                in++;
            if (*in)
                in++;
            continue;
        }
        if (*in == '&') {
            static const struct {
                const char *ent;
                char ch;
            } ents[] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
            size_t i;
            for (i = 0; i < sizeof (ents) / sizeof (ents[0]); i++) {
                size_t len = strlen (ents[i].ent);
                if (!strncmp (in, ents[i].ent, len)) {
                    out[n++] = ents[i].ch;
                    in += len;
                    break;
                }
            }
            if (i < sizeof (ents) / sizeof (ents[0]))
                continue;
        }
        out[n++] = *in++;
    }
    out[n] = 0;
}

/* ---- globals normally owned by hime.c --------------------------------- */

XIMS current_ims;
int display_width = 1920, display_height = 1080;
int input_window_width, input_window_height;
gboolean key_press_shift;
gboolean last_cursor_off;
Window root;
gboolean win_kbm_inited;
int win_x, win_y;

extern unich_t *fullchar[];

char *half_char_to_full_char (KeySym xkey) {
    if (xkey < ' ' || xkey > 127)
        return NULL;
    return _ (fullchar[xkey - ' ']);
}

void message_cb (char *message) {
}

/* ---- gtab window, normally win-gtab.c --------------------------------- */

GtkWidget *win_gtab;
char str_key_codes[128];
int win_gtab_max_key_press;

void init_win_gtab (void) {
}

gboolean is_win_gtab_visible (void) {
    return rec_win_visible;
}

void show_win_gtab () {
    rec_win_visible = TRUE;
}

void hide_win_gtab () {
    rec_win_visible = FALSE;
}

void move_win_gtab (int x, int y) {
}

void get_win_gtab_geom () {
}

void disp_gtab (char *str) {
    snprintf (rec_preedit, sizeof (rec_preedit), "%s", str);
}

void clear_gtab_in_area () {
    rec_preedit[0] = 0;
}

void gtab_disp_empty (char *tt, int N) {
}

void disp_gtab_sel (char *s) {
    strip_markup (s, rec_cand, sizeof (rec_cand));
}

void disp_gtab_pre_sel (char *s) {
    strip_markup (s, rec_presel, sizeof (rec_presel));
}

void hide_gtab_pre_sel () {
    rec_presel[0] = 0;
}

void set_key_codes_label (char *s, int better) {
    if (s)
        strip_markup (s, rec_keycodes, sizeof (rec_keycodes));
    else
        rec_keycodes[0] = 0;
}

void set_page_label (char *s) {
    snprintf (rec_page, sizeof (rec_page), "%s", s ? s : "");
}

void disp_label_edit (char *str) {
    strip_markup (str, rec_buf, sizeof (rec_buf));
}

void set_gtab_input_error_color () {
}

void clear_gtab_input_error_color () {
}

void change_win_fg_bg (GtkWidget *win, GtkWidget *label) {
}

void show_input_method_name_on_gtab () {
}

void win_gtab_disp_half_full () {
}

/* bell() lives in hime-common.c and calls XBell; the link wraps it. */
void __wrap_bell (void) {
    rec_bells++;
}

/* ---- driver ----------------------------------------------------------- */

extern ClientState *current_CS;
extern char *output_buffer;
extern uint32_t output_bufferN;
void clear_output_buffer (void);
gboolean ProcessKeyPress (KeySym keysym, uint32_t kev_state);
gboolean ProcessKeyRelease (KeySym keysym, uint32_t kev_state);
gboolean init_in_method (int in_no);
void init_TableDir (void);
void load_settings (void);
void load_gtab_list (gboolean skip_disabled);

static const struct {
    const char *name;
    KeySym sym;
} named_keys[] = {
    {"<space>", XK_space},
    {"<enter>", XK_Return},
    {"<bs>", XK_BackSpace},
    {"<esc>", XK_Escape},
    {"<tab>", XK_Tab},
    {"<left>", XK_Left},
    {"<right>", XK_Right},
    {"<up>", XK_Up},
    {"<down>", XK_Down},
    {"<pgup>", XK_Prior},
    {"<pgdn>", XK_Next},
    {"<shift>", XK_Shift_L},
    {"<ctrl>", XK_Control_L},
};

static gboolean parse_key (const char *tok, KeySym *sym, uint32_t *state) {
    *state = 0;
    for (;;) {
        if (!strncmp (tok, "S-", 2) && tok[2])
            *state |= ShiftMask;
        else if (!strncmp (tok, "C-", 2) && tok[2])
            *state |= ControlMask;
        else
            break;
        tok += 2;
    }

    size_t i;
    for (i = 0; i < sizeof (named_keys) / sizeof (named_keys[0]); i++) {
        if (!strcmp (tok, named_keys[i].name)) {
            *sym = named_keys[i].sym;
            return TRUE;
        }
    }

    if (strlen (tok) == 1 && tok[0] > ' ' && tok[0] < 127) {
        /* X reports Shift+a as XK_A */
        *sym = (*state & ShiftMask) && tok[0] >= 'a' && tok[0] <= 'z' ? (KeySym) (tok[0] - 'a' + 'A') : (KeySym) tok[0];
        return TRUE;
    }

    return FALSE;
}

static void print_state (const char *tok, gboolean eaten, gboolean eaten_release) {
    char commit[4096] = "";
    if (output_bufferN && output_buffer)
        snprintf (commit, sizeof (commit), "%s", output_buffer);
    clear_output_buffer ();

    printf ("%-8s %s in=\"%s\" sel=\"%s\"", tok, eaten ? "eat " : "pass", rec_preedit, rec_cand);
    if (eaten_release)
        printf (" eat-release");
    if (commit[0])
        printf (" commit=\"%s\"", commit);
    if (rec_buf[0])
        printf (" buf=\"%s\"", rec_buf);
    if (rec_presel[0])
        printf (" presel=\"%s\"", rec_presel);
    if (rec_keycodes[0])
        printf (" codes=\"%s\"", rec_keycodes);
    if (rec_page[0])
        printf (" page=\"%s\"", rec_page);
    if (rec_bells)
        printf (" bell=%d", rec_bells);
    if (!rec_win_visible)
        printf (" hidden");
    printf ("\n");

    rec_bells = 0;
}

static int find_method (const char *file) {
    int i;
    for (i = 0; i < inmdN; i++)
        if (inmd[i].filename && !strcmp (inmd[i].filename, file))
            return i;
    return -1;
}

int main (int argc, char **argv) {
    if (argc != 2) {
        fprintf (stderr, "usage: %s TABLE.gtab < script\n", argv[0]);
        return 2;
    }

    init_TableDir ();
    load_settings ();
    load_gtab_list (FALSE);

    int idx = find_method (argv[1]);
    if (idx < 0) {
        fprintf (stderr, "%s is not in gtab.list\n", argv[1]);
        return 2;
    }

    static ClientState cs;
    cs.b_hime_protocol = TRUE;
    cs.input_style = InputStyleOverSpot;
    cs.b_im_enabled = TRUE;
    cs.b_chinese_mode = TRUE;
    current_CS = &cs;

    if (!init_in_method (idx)) {
        fprintf (stderr, "cannot load %s\n", argv[1]);
        return 2;
    }
    clear_output_buffer ();

    char line[1024];
    while (fgets (line, sizeof (line), stdin)) {
        char *hash = strchr (line, '#');
        if (hash) {
            char *end = hash + strcspn (hash, "\n");
            printf ("%.*s\n", (int) (end - hash), hash);
            *hash = 0;
        }

        char *save = NULL;
        char *tok;
        for (tok = strtok_r (line, " \t\n", &save); tok; tok = strtok_r (NULL, " \t\n", &save)) {
            KeySym sym;
            uint32_t state;
            if (!parse_key (tok, &sym, &state)) {
                fprintf (stderr, "bad key token: %s\n", tok);
                return 2;
            }

            gboolean eaten = ProcessKeyPress (sym, state);
            gboolean eaten_release = ProcessKeyRelease (sym, state);
            print_state (tok, eaten, eaten_release);
        }
    }

    return 0;
}
