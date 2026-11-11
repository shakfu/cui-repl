/* cui-repl: a terminal in a CUI window, emulated by libghostty-vt. Runs a login
   $SHELL, or the command given; exits with its exit code when it exits. */
#include "cui.h"
#include "session.h"
#include "term_view.h"

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Faces for what Geist Mono, CUI's monospace font, lacks: box drawing, symbols,
   CJK. The first ones present are used; CUI takes 4 fallbacks per face. */
static const char *const FALLBACKS[] = {
#if defined(__APPLE__)
    "/System/Library/Fonts/Menlo.ttc",
    "/System/Library/Fonts/Apple Symbols.ttf",
    "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
#else
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
    "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono.ttf",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
#endif
};

static void load_fonts(cui_ctx *ui, const char *font) {
    if (font) {
        cui_font f = cui_font_load_file(ui, font);
        if (f < 0) fprintf(stderr, "cui-repl: cannot load %s; using the built-in font\n", font);
        else cui_font_set_role(ui, CUI_FONT_MONO, f);
    }
    int added = 0;
    for (size_t i = 0; i < sizeof FALLBACKS / sizeof *FALLBACKS && added < 4; i++) {
        if (access(FALLBACKS[i], R_OK) < 0) continue;
        cui_font f = cui_font_load_file(ui, FALLBACKS[i]);
        if (f >= 0) cui_font_add_fallback(ui, CUI_FONT_MONO, f), added++;
    }
}

static int usage(const char *argv0, int rc) {
    fprintf(rc ? stderr : stdout,
            "usage: %s [-C DIR] [-f FONT] [-s SIZE] [--light] [COMMAND [ARG...]]\n"
            "  -C DIR    start in DIR\n"
            "  -f FONT   a monospace .ttf, .otf or .ttc (first face) instead of Geist Mono\n"
            "  -s SIZE   font size in points (default 13)\n"
            "  --light   light window theme (the terminal keeps its own colors)\n"
            "Without COMMAND, runs $SHELL -l.\n",
            argv0);
    return rc;
}

int main(int argc, char **argv) {
    const char *dir = NULL, *font = NULL;
    float size = 13;
    bool light = false;
    static const struct option longs[] = {{"light", no_argument, NULL, 'l'}, {"help", no_argument, NULL, 'h'}, {0}};
    int opt;
    while ((opt = getopt_long(argc, argv, "+C:f:s:h", longs, NULL)) != -1) {
        if (opt == 'C') dir = optarg;
        else if (opt == 'f') font = optarg;
        else if (opt == 's') size = strtof(optarg, NULL);
        else if (opt == 'l') light = true;
        else if (opt == 'h') return usage(argv[0], 0);
        else return usage(argv[0], 2);
    }
    if (size < 6 || size > 72) {
        fprintf(stderr, "cui-repl: font size must be 6 to 72\n");
        return 2;
    }

    static session s;
    static term_view v;
    if (session_open(&s, optind < argc ? argv + optind : NULL, dir, 80, 24) < 0) {
        fprintf(stderr, "cui-repl: cannot start a pty: %s\n", strerror(errno));
        session_close(&s);
        return 1;
    }
    if (term_view_init(&v, &s, size) < 0) {
        fprintf(stderr, "cui-repl: out of memory\n");
        session_close(&s);
        return 1;
    }
    cui_app *app = cui_app_create(&(cui_app_desc){.title = "cui-repl", .width = 900, .height = 560,
                                                  .theme = light ? CUI_THEME_LIGHT : CUI_THEME_DARK});
    if (!app) {
        fprintf(stderr, "cui-repl: cannot open a window\n");
        term_view_free(&v);
        session_close(&s);
        return 1;
    }
    load_fonts(cui_app_ui(app), font);
    while (!s.exited && cui_app_frame(app)) {
        term_view_frame(cui_app_ui(app), &v, "terminal");
        if (s.title_changed) {
            cui_app_set_title(app, s.title[0] ? s.title : "cui-repl");
            s.title_changed = false;
        }
    }
    bool exited = s.exited;
    cui_app_destroy(app);
    term_view_free(&v);
    session_close(&s); /* a closed window hangs up the child */
    return exited ? session_exit_code(&s) : 0;
}
