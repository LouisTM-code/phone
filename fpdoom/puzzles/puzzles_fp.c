/* Simon Tatham's Portable Puzzle Collection on the phone: a drawing_api
   over the framebuffer, keypad keys for the cursor interface of the
   puzzles. The screen and keys come through pz_ui.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "puzzles.h"
#include "pz_ui.h"

#define BAR_H (PZ_FONT_H + 2)
#define TIMER_MS 40

struct frontend {
    midend *me;
    const game *game;
    uint16_t *colours;
    int ncolours;
    int ox, oy, w, h; /* puzzle area on the screen */
    int cx0, cy0, cx1, cy1; /* clip, in screen coordinates */
    int timer;
    unsigned last_ms;
    char status[128];
};

struct blitter {
    int w, h, x, y;
    uint16_t *pix;
};

static frontend fe_glob;

/* drawing */

static void fill(frontend *fe, int x, int y, int w, int h, uint16_t c)
{
    int i, j, x1 = x + w, y1 = y + h;
    if (x < fe->cx0) x = fe->cx0;
    if (y < fe->cy0) y = fe->cy0;
    if (x1 > fe->cx1) x1 = fe->cx1;
    if (y1 > fe->cy1) y1 = fe->cy1;
    for (j = y; j < y1; j++) {
        uint16_t *p = framebuf + j * scr_w;
        for (i = x; i < x1; i++) p[i] = c;
    }
}

static void plot(frontend *fe, int x, int y, uint16_t c)
{
    if (x >= fe->cx0 && x < fe->cx1 && y >= fe->cy0 && y < fe->cy1)
        framebuf[y * scr_w + x] = c;
}

static void fp_draw_text(drawing *dr, int x, int y, int fonttype,
                         int fontsize, int align, int colour, const char *text)
{
    frontend *fe = GET_HANDLE_AS_TYPE(dr, frontend);
    int s = (fontsize + 8) / 16, n = strlen(text), i, j, k;
    uint16_t c = fe->colours[colour];
    (void)fonttype;
    if (s < 1) s = 1;
    x += fe->ox;
    y += fe->oy;
    if (align & ALIGN_HCENTRE) x -= n * PZ_FONT_W * s / 2;
    else if (align & ALIGN_HRIGHT) x -= n * PZ_FONT_W * s;
    /* the font cell has the baseline at row 12 */
    if (align & ALIGN_VCENTRE) y -= PZ_FONT_H * s / 2;
    else y -= 12 * s;
    for (k = 0; k < n; k++, x += PZ_FONT_W * s) {
        const uint8_t *g = pz_glyph((uint8_t)text[k]);
        for (j = 0; j < PZ_FONT_H; j++)
            for (i = 0; i < 8; i++)
                if (g[j] << i & 0x80)
                    fill(fe, x + i * s, y + j * s, s, s, c);
    }
}

static void fp_draw_rect(drawing *dr, int x, int y, int w, int h, int colour)
{
    frontend *fe = GET_HANDLE_AS_TYPE(dr, frontend);
    fill(fe, fe->ox + x, fe->oy + y, w, h, fe->colours[colour]);
}

static void fp_draw_line(drawing *dr, int x1, int y1, int x2, int y2, int colour)
{
    frontend *fe = GET_HANDLE_AS_TYPE(dr, frontend);
    uint16_t c = fe->colours[colour];
    int dx = abs(x2 - x1), dy = -abs(y2 - y1);
    int sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1, err = dx + dy;
    for (;;) {
        plot(fe, fe->ox + x1, fe->oy + y1, c);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

static void fp_draw_circle(drawing *dr, int cx, int cy, int r,
                           int fillcolour, int outlinecolour)
{
    frontend *fe = GET_HANDLE_AS_TYPE(dr, frontend);
    int y;
    cx += fe->ox;
    cy += fe->oy;
    for (y = -r; y <= r; y++) {
        int x = 0;
        while ((x + 1) * (x + 1) + y * y <= r * r) x++;
        if (fillcolour >= 0)
            fill(fe, cx - x, cy + y, 2 * x + 1, 1, fe->colours[fillcolour]);
        if (outlinecolour >= 0) {
            int xi = 0;
            while ((xi + 1) * (xi + 1) + y * y <= (r - 1) * (r - 1)) xi++;
            if (abs(y) >= r) xi = -1;
            fill(fe, cx - x, cy + y, x - xi, 1, fe->colours[outlinecolour]);
            fill(fe, cx + xi + 1, cy + y, x - xi, 1, fe->colours[outlinecolour]);
        }
    }
}

static void fp_draw_update(drawing *dr, int x, int y, int w, int h)
{
    (void)dr; (void)x; (void)y; (void)w; (void)h;
}

static void fp_clip(drawing *dr, int x, int y, int w, int h)
{
    frontend *fe = GET_HANDLE_AS_TYPE(dr, frontend);
    fe->cx0 = max(fe->ox + x, 0);
    fe->cy0 = max(fe->oy + y, 0);
    fe->cx1 = min(fe->ox + x + w, (int)scr_w);
    fe->cy1 = min(fe->oy + y + h, (int)scr_h);
}

static void fp_unclip(drawing *dr)
{
    frontend *fe = GET_HANDLE_AS_TYPE(dr, frontend);
    fe->cx0 = fe->ox;
    fe->cy0 = fe->oy;
    fe->cx1 = fe->ox + fe->w;
    fe->cy1 = fe->oy + fe->h;
}

static void fp_start_draw(drawing *dr) { (void)dr; }

static void draw_bars(frontend *fe)
{
    int x0 = fe->cx0, y0 = fe->cy0, x1 = fe->cx1, y1 = fe->cy1;
    fe->cx0 = 0; fe->cy0 = 0; fe->cx1 = scr_w; fe->cy1 = scr_h;
    pz_fill(0, 0, scr_w, BAR_H, PZ_RGB(60, 60, 100));
    pz_text(4, 1, fe->game->name, PZ_RGB(255, 255, 255));
    pz_text(scr_w - 6 * PZ_FONT_W - 4, 1, "LSOFT", PZ_RGB(200, 200, 255));
    pz_fill(0, scr_h - BAR_H, scr_w, BAR_H, PZ_RGB(30, 30, 45));
    pz_text(4, scr_h - BAR_H + 1, fe->status, PZ_RGB(255, 255, 255));
    fe->cx0 = x0; fe->cy0 = y0; fe->cx1 = x1; fe->cy1 = y1;
}

static void fp_end_draw(drawing *dr)
{
    frontend *fe = GET_HANDLE_AS_TYPE(dr, frontend);
    draw_bars(fe);
    pz_present();
}

static void fp_status_bar(drawing *dr, const char *text)
{
    frontend *fe = GET_HANDLE_AS_TYPE(dr, frontend);
    strncpy(fe->status, text, sizeof(fe->status) - 1);
    fe->status[sizeof(fe->status) - 1] = 0;
}

static blitter *fp_blitter_new(drawing *dr, int w, int h)
{
    blitter *bl = snew(blitter);
    (void)dr;
    bl->w = w;
    bl->h = h;
    bl->pix = snewn(w * h, uint16_t);
    return bl;
}

static void fp_blitter_free(drawing *dr, blitter *bl)
{
    (void)dr;
    sfree(bl->pix);
    sfree(bl);
}

static void fp_blitter_save(drawing *dr, blitter *bl, int x, int y)
{
    frontend *fe = GET_HANDLE_AS_TYPE(dr, frontend);
    int i, j;
    bl->x = x;
    bl->y = y;
    for (j = 0; j < bl->h; j++)
        for (i = 0; i < bl->w; i++) {
            int sx = fe->ox + x + i, sy = fe->oy + y + j;
            bl->pix[j * bl->w + i] = sx >= 0 && sx < (int)scr_w && sy >= 0 && sy < (int)scr_h
                ? framebuf[sy * scr_w + sx] : 0;
        }
}

static void fp_blitter_load(drawing *dr, blitter *bl, int x, int y)
{
    frontend *fe = GET_HANDLE_AS_TYPE(dr, frontend);
    int i, j;
    for (j = 0; j < bl->h; j++)
        for (i = 0; i < bl->w; i++)
            plot(fe, fe->ox + x + i, fe->oy + y + j, bl->pix[j * bl->w + i]);
}

static const drawing_api fp_drawing = {
    1,
    fp_draw_text, fp_draw_rect, fp_draw_line, draw_polygon_fallback,
    fp_draw_circle, fp_draw_update, fp_clip, fp_unclip,
    fp_start_draw, fp_end_draw, fp_status_bar,
    fp_blitter_new, fp_blitter_free, fp_blitter_save, fp_blitter_load,
    NULL, NULL, NULL, NULL, NULL, NULL, /* printing */
    NULL, NULL, NULL, NULL,
};

/* frontend functions the midend calls */

void fatal(const char *fmt, ...)
{
    char buf[256];
    va_list va;
    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);
    pz_note(buf);
    exit(1);
}

void get_random_seed(void **randseed, int *randseedsize)
{
    unsigned *p = snew(unsigned);
    *p = pz_time_ms() * 2654435761u;
    *randseed = p;
    *randseedsize = sizeof(unsigned);
}

void frontend_default_colour(frontend *fe, float *output)
{
    (void)fe;
    output[0] = output[1] = output[2] = 0.8F;
}

void activate_timer(frontend *fe)
{
    if (!fe->timer) fe->last_ms = pz_time_ms();
    fe->timer = 1;
}

void deactivate_timer(frontend *fe)
{
    fe->timer = 0;
}

/* game setup */

static void start_game(frontend *fe, int new_game)
{
    float *rgb;
    int i, w = scr_w, h = scr_h - 2 * BAR_H;
    if (new_game) midend_new_game(fe->me);
    midend_size(fe->me, &w, &h, false, 1.0);
    fe->w = w;
    fe->h = h;
    fe->ox = (scr_w - w) / 2;
    fe->oy = BAR_H + (scr_h - 2 * BAR_H - h) / 2;
    rgb = midend_colours(fe->me, &fe->ncolours);
    sfree(fe->colours);
    fe->colours = snewn(fe->ncolours, uint16_t);
    for (i = 0; i < fe->ncolours; i++)
        fe->colours[i] = PZ_RGB((int)(rgb[i * 3] * 255), (int)(rgb[i * 3 + 1] * 255),
                (int)(rgb[i * 3 + 2] * 255));
    sfree(rgb);
    fe->status[0] = 0;
    fe->cx0 = 0; fe->cy0 = 0; fe->cx1 = scr_w; fe->cy1 = scr_h;
    fill(fe, 0, 0, scr_w, scr_h, fe->colours[0]);
    fe->cx0 = fe->ox; fe->cy0 = fe->oy; fe->cx1 = fe->ox + w; fe->cy1 = fe->oy + h;
    midend_force_redraw(fe->me);
}

/* the preset tree as a list of names, depth first */
static int presets(struct preset_menu *menu, game_params **params,
        const char **names, int n, int max)
{
    int i;
    for (i = 0; i < menu->n_entries && n < max; i++) {
        struct preset_menu_entry *e = &menu->entries[i];
        if (e->submenu) {
            n = presets(e->submenu, params, names, n, max);
        } else {
            params[n] = e->params;
            names[n++] = e->title;
        }
    }
    return n;
}

static int pick_game(void)
{
    const char *names[64];
    int i;
    for (i = 0; i < gamecount && i < 64; i++) names[i] = gamelist[i]->name;
    return pz_pick("Головоломки", names, i);
}

enum { M_NEW, M_RESTART, M_UNDO, M_REDO, M_SOLVE, M_SIZE, M_GAME, M_EXIT, M_N };

/* returns 0 to go on, 1 to change the game, -1 to exit */
static int game_menu(frontend *fe)
{
    static const char *const items[M_N] = {
        "Новая игра", "Начать заново", "Отменить ход", "Вернуть ход",
        "Показать решение", "Размер и сложность", "Другая головоломка", "Выход",
    };
    int i = pz_pick(fe->game->name, items, M_N);
    const char *err;
    switch (i) {
    case M_NEW: start_game(fe, 1); break;
    case M_RESTART: midend_restart_game(fe->me); midend_force_redraw(fe->me); break;
    case M_UNDO: midend_process_key(fe->me, 0, 0, UI_UNDO); midend_force_redraw(fe->me); break;
    case M_REDO: midend_process_key(fe->me, 0, 0, UI_REDO); midend_force_redraw(fe->me); break;
    case M_SOLVE:
        err = midend_solve(fe->me);
        if (err) pz_note(err);
        midend_force_redraw(fe->me);
        break;
    case M_SIZE: {
        game_params *params[64];
        const char *names[64];
        int n = presets(midend_get_presets(fe->me, NULL), params, names, 0, 64), k;
        k = n ? pz_pick("Размер", names, n) : -1;
        if (k >= 0) {
            midend_set_params(fe->me, params[k]);
            start_game(fe, 1);
        } else {
            midend_force_redraw(fe->me);
        }
        break;
    }
    case M_GAME: return 1;
    case M_EXIT: case -2: return -1;
    default: midend_force_redraw(fe->me);
    }
    return 0;
}

static int map_key(int k)
{
    switch (k) {
    case PZ_UP: return CURSOR_UP;
    case PZ_DOWN: return CURSOR_DOWN;
    case PZ_LEFT: return CURSOR_LEFT;
    case PZ_RIGHT: return CURSOR_RIGHT;
    case PZ_CENTER: return CURSOR_SELECT;
    case PZ_STAR: return CURSOR_SELECT2;
    case PZ_HASH: return '\b';
    case PZ_RSOFT: return UI_UNDO;
    }
    if (k >= PZ_DIGIT0 && k <= PZ_DIGIT0 + 9) return '0' + k - PZ_DIGIT0;
    return 0;
}

/* plays one game; returns 1 to pick another game, 0 to exit */
static int play(frontend *fe)
{
    for (;;) {
        int k = pz_poll_key();
        if (k < 0) return 0;
        if (k == PZ_LSOFT) {
            int r = game_menu(fe);
            if (r) return r > 0;
            continue;
        }
        if (k) {
            int key = map_key(k);
            if (key && midend_process_key(fe->me, 0, 0, key) == PKR_QUIT) return 0;
            continue;
        }
        if (fe->timer) {
            unsigned now = pz_time_ms();
            midend_timer(fe->me, (now - fe->last_ms) / 1000.0F);
            fe->last_ms = now;
        }
        pz_wait_ms(fe->timer ? TIMER_MS : 10);
    }
}

int main(int argc, char **argv)
{
    frontend *fe = &fe_glob;
    int i;
    (void)argc; (void)argv;
    pz_init();
    for (;;) {
        i = pick_game();
        if (i < 0) break;
        fe->game = gamelist[i];
        fe->me = midend_new(fe, fe->game, &fp_drawing, fe);
        start_game(fe, 1);
        i = play(fe);
        midend_free(fe->me);
        fe->timer = 0;
        if (!i) break;
    }
    return 0;
}
