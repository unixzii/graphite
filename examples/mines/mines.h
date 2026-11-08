/* Mines: a field-size picker and a board with springy, physical feedback. Every animation is a spring or a timed
 * schedule advanced in fixed steps on the Scene thread. */
#ifndef MINES_H
#define MINES_H

#include "example.h"

#define STEP (1.0 / 240)
#define MAX_SIDE 64
#define MAX_CELLS (MAX_SIDE * MAX_SIDE)
#define RINGS 2
#define BITS 160
#define FLAKES 260
#define PRESETS 3
#define FIELDS 3 /**< Columns, rows, mines. */
#define START FIELDS
#define FOCUSABLES (FIELDS + 1 + PRESETS)

/* Palette: warm paper, sage accents and pastel number tiles. */
#define C_PAGE 0xF3F1EC
#define C_PANEL 0xFBFAF7
#define C_LINE 0xE2DED6
#define C_INK 0x3B3F3D
#define C_MUTED 0x8E918C
#define C_ACCENT 0x6F9D8D
#define C_ACCENT_DEEP 0x557F71
#define C_HIDDEN 0xCBD1C8
#define C_HIDDEN_HOVER 0xDCE2D8
#define C_OPEN 0xECEAE4
#define C_MINE_OPEN 0xF0D6CF
#define C_BOOM 0xE07F6E
#define C_FLAG 0xDB7461
#define C_SLATE 0x434A48

/* Springs */

typedef struct
{
    double response, damping; /**< Period of the undamped oscillation in seconds, and damping ratio. */
} spring_params;

typedef struct
{
    double value, velocity, target;
    bool active;
} spring;

void spring_to(spring *s, double target);
void spring_set(spring *s, double value, double target);
void spring_kick(spring *s, double velocity);
void spring_step(spring *s, spring_params p, double dt);
void spring_run(spring *s, spring_params p, int steps);

gr_color mix(gr_color a, gr_color b, double t);
double clampd(double v, double lo, double hi);
gr_rect inset(gr_rect r, double d);
bool inside(gr_rect r, gr_point p);
double ease_out(double t);

/* Single-line text that lays itself out on each draw. */
typedef struct
{
    gr_text_system *text;
    gr_layer *layer;
    char value[96];
    double size, spacing;
    uint16_t weight;
    gr_color color;
    gr_text_align align;
} label;

label *label_new(example *ex, gr_layer *parent, double size, uint16_t weight, gr_color color, gr_text_align align,
                 const char *value);
void label_set(label *l, const char *format, ...);
void label_color(label *l, gr_color color);
void label_frame(label *l, gr_rect rect);
double text_width(gr_text_system *text, const char *value, double size, uint16_t weight);
/** Draws value centered in box; without a text system, draws one rounded dot per character. */
void draw_centered_text(gr_canvas *c, gr_text_system *text, const char *value, gr_rect box, double size,
                        uint16_t weight, gr_color color);

/* Icons drawn with paths. */
typedef enum
{
    ICON_FLAG,
    ICON_MINE,
    ICON_CLOCK,
    ICON_WRONG
} icon_kind;

void draw_icon(gr_canvas *c, icon_kind kind, gr_rect box, gr_color color);
gr_layer *icon_new(example *ex, gr_layer *parent, icon_kind kind, gr_color color);

/* Push buttons with hover and press springs. */
typedef struct
{
    gr_layer *layer;
    label *title, *detail;
    gr_rect rect; /**< Page coordinates, for hit testing. */
    spring hover, press;
    bool primary, held;
} button;

void button_init(example *ex, button *b, gr_layer *parent, const char *title, const char *detail, bool primary);
void button_layout(button *b, gr_rect rect);
void button_update(button *b, int steps);
bool button_hit(const button *b, gr_point p);
void button_hover(button *b, bool hovered);

/* Size picker */

typedef struct
{
    char text[6];
    int value, min, max; /**< value is the last valid one, within [min, max]. */
    double digits_size;
    bool fresh; /**< The next digit replaces the text, shown as a selection. */
    double text_w;
    gr_layer *box, *selection, *caret;
    label *digits, *caption;
    gr_rect rect;
    spring shake, error, pop, lit;
} field;

typedef struct
{
    gr_layer *page, *content, *ring, *preview, *rule_left, *rule_right, *rule_mid;
    double scale; /**< content is laid out at 1/scale of the page and scaled to fit small windows. */
    label *title, *subtitle, *times, *hint, *divider;
    field fields[FIELDS];
    button start, presets[PRESETS];
    int focus; /**< Fields, then START, then presets; -1 for none. Buttons take focus only from the keyboard. */
    bool window_focused;
    bool custom_mines; /**< The mine count was typed in; otherwise it follows the suggested count. */
    double blink_epoch;
    spring rx, ry, rw, rh, rr, ring_alpha, pw, ph;
    gr_rect preview_area;
} setup_page;

/* Board */

enum
{
    HIDDEN,
    FLAGGED,
    OPEN
};

enum
{
    SHOW_NONE,
    SHOW_NUMBER,
    SHOW_FLAG,
    SHOW_MINE,
    SHOW_WRONG
};

typedef enum
{
    PLAY_READY,
    PLAY_RUNNING,
    PLAY_LOST,
    PLAY_WON
} play_state;

typedef struct game_page game_page;

typedef struct
{
    game_page *game;
    gr_layer *tile, *glyph;
    uint8_t mine, adjacent, state;
    uint8_t shown, digit; /**< What the glyph draws; set only by show(), so resetting the rules state between
                           * games cannot change a glyph that is still fading out. */
    bool active, first_boom;
    uint16_t rank;                                             /**< Explosion order; later explosions hit softer. */
    double intro_at, reset_at, reveal_at, explode_at, wave_at; /**< Scheduled times, or -1. */
    spring hover, pop, open, boom, gscale, galpha, kx, ky;
} cell;

typedef struct
{
    gr_layer *layer;
    double start, size;
    bool live;
} ring;

typedef struct
{
    gr_layer *layer;
    gr_point p, v;
    double angle, spin, start, life, gravity, drag;
    bool live;
} bit;

struct game_page
{
    example *ex;
    gr_layer *page, *backdrop, *panel, *board, *tiles, *fx, *veil, *card, *card_flag, *card_mine, *sky;
    /* backdrop holds the board and the header; it blurs behind the result card. */
    cell *cells;
    int *active, *queue;
    int pool, active_count;
    int cols, rows, count, flags, opened, last;
    play_state play;
    double started, ended, card_at, now;
    uint64_t seed;
    gr_rect board_rect; /**< Page coordinates. */
    double pitch, tile;
    int hover;
    bool pressing;
    spring depth, cx, cy;
    double shake;
    ring rings[RINGS];
    bit bits[BITS];     /**< Debris, in board space. */
    bit flakes[FLAKES]; /**< Win confetti over the whole window, in page space. */
    int next_ring, next_bit, next_flake;
    gr_size view;
    double card_scale;
    button back, restart, again, sizes;
    gr_layer *flag_chip, *time_chip, *flag_icon, *clock_icon;
    label *flag_text, *time_text, *card_title, *card_detail;
    spring flag_pop, card_t;
    gr_rect card_rect;
};

typedef struct mines
{
    example *ex;
    double now, carry;
    gr_size view;
    int page; /**< 0 size picker, 1 board. */
    spring page_t;
    gr_cursor cursor;
    setup_page setup;
    game_page game;
} mines;

void setup_build(mines *m);
void setup_layout(mines *m);
void setup_update(mines *m, int steps);
bool setup_input(mines *m, const gr_event *e);
void setup_reset_pointer(mines *m);

void game_build(mines *m);
void game_layout(mines *m);
void game_update(mines *m, int steps);
bool game_input(mines *m, const gr_event *e);
void game_new(mines *m, int cols, int rows, int count, bool intro);
void game_reset_pointer(mines *m);

void go_game(mines *m, int cols, int rows, int count);
void go_setup(mines *m);
void set_cursor(mines *m, gr_cursor cursor);
/** Suggested mines for a field: classic densities. */
int mine_count(int cols, int rows);
/** Most mines a field can hold while the first click still opens a free 3 × 3 area (or at least its own cell). */
int max_mines(int cols, int rows);

#endif
