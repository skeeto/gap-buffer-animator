#include <math.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define MAX(a, b) ((b) > (a) ? (b) : (a))
#define MIN(a, b) ((b) < (a) ? (b) : (a))

struct image {
    int w;
    int h;
    unsigned char rgb[];
};

struct image *
image_create(int w, int h)
{
    struct image *m = calloc(1, sizeof(*m) + (size_t)3 * w * h);
    m->w = w;
    m->h = h;
    return m;
}

struct image *
image_load(FILE *in)
{
    char c;
    int w, h;
    struct image *m = 0;
    if (fscanf(in, "P6 %d %d 255%c", &w, &h, &c) == 3 && c == '\n') {
        m = image_create(w, h);
        fread(m->rgb, h * 3, w, in);
    }
    return m;
}

void
image_set(struct image *m, int x, int y, unsigned long rgb)
{
    size_t i = (size_t)3 * m->w * y + 3 * x;
    m->rgb[i + 0] = rgb >> 16;
    m->rgb[i + 1] = rgb >>  8;
    m->rgb[i + 2] = rgb >>  0;
}

unsigned long
image_get(const struct image *m, int x, int y)
{
    size_t i = (size_t)3 * m->w * y + 3 * x;
    unsigned long r = m->rgb[i + 0];
    unsigned long g = m->rgb[i + 1];
    unsigned long b = m->rgb[i + 2];
    return (r << 16) | (g << 8) | b;
}

void
image_rect(struct image *m, int x0, int y0, int x1, int y1, unsigned long rgb)
{
    int w = abs(x1 - x0);
    int h = abs(y1 - y0);
    int bx = MIN(x0, x1);
    int by = MIN(y0, y1);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            image_set(m, bx + x, by + y, rgb);
}

void
image_write(const struct image *m, FILE *out)
{
    fprintf(out, "P6\n%d %d\n255\n", m->w, m->h);
    fwrite(m->rgb, m->h, 3 * m->w, out);
    fflush(out);
}

struct gapbuf {
    char *buf;
    size_t total;
    size_t front;
    size_t gap;
};

void
gapbuf_init(struct gapbuf *b, size_t init)
{
    b->total = b->gap = init + !init;
    b->front = 0;
    b->buf = malloc(init);
}

void
gapbuf_destroy(struct gapbuf *b)
{
    free(b->buf);
    b->buf = 0;
}

void
gapbuf_insert(struct gapbuf *b, int c)
{
    if (!b->gap) {
        size_t back = b->total - b->front;
        b->gap = b->total;
        b->total *= 2;
        b->buf = realloc(b->buf, b->total);
        memmove(b->buf + b->front + b->gap, b->buf + b->front, back);
    }
    b->buf[b->front] = c;
    b->front++;
    b->gap--;
}

void
gapbuf_inserts(struct gapbuf *b, const char *s)
{
    size_t len = strlen(s);
    while (b->gap < len) {
        b->gap = 0;
        gapbuf_insert(b, 0);
        b->front--;
    }
    memcpy(b->buf + b->front, s, len);
    b->front += len;
    b->gap -= len;
}

void
gapbuf_move(struct gapbuf *b, ptrdiff_t amt)
{
    size_t len;
    char *dst, *src;
    if (amt < 0) {
        len = -amt;
        if (len > b->front)
            len = b->front;
        dst = b->buf + b->front + b->gap - len;
        src = b->buf + b->front - len;
        b->front -= len;
    } else {
        size_t back = b->total - b->front - b->gap;
        len = amt;
        if (len > back)
            len = back;
        dst = b->buf + b->front;
        src = b->buf + b->front + b->gap;
        b->front += len;
    }
    memmove(dst, src, len);
}

void
gapbuf_backward(struct gapbuf *b)
{
    if (b->front > 0) {
        b->buf[b->front + b->gap - 1] = b->buf[b->front - 1];
        b->front--;
    }
}

void
gapbuf_forward(struct gapbuf *b)
{
    size_t back = b->total - b->front - b->gap;
    if (back > 0) {
        b->buf[b->front] = b->buf[b->front + b->gap];
        b->front++;
    }
}

void
gapbuf_delete(struct gapbuf *b)
{
    if (b->total > b->front + b->gap)
        b->gap++;
}

void
gapbuf_backspace(struct gapbuf *b)
{
    if (b->front) {
        b->front--;
        b->gap++;
    }
}

void
gapbuf_write(const struct gapbuf *b, FILE *out)
{
    fwrite(b->buf, 1, b->front, out);
    char *back_start = b->buf + b->front + b->gap;
    size_t back_len = b->total - b->front - b->gap;
    fwrite(back_start, 1, back_len, out);
}

#define GAPBUF_SCALE      16
#define GAPBUF_FONTSCALE  32
#define GAPBUF_BG         0xffffffUL
#define GAPBUF_FG         0x7f7f7fUL

static void
draw_block(struct image *m, int i)
{
    int x0 = i * GAPBUF_SCALE + GAPBUF_SCALE * 1 / 8;
    int y0 = GAPBUF_FONTSCALE + GAPBUF_SCALE * 1 / 8;
    int x1 = i * GAPBUF_SCALE + GAPBUF_SCALE * 7 / 8;
    int y1 = GAPBUF_FONTSCALE + GAPBUF_SCALE * 7 / 8;
    image_rect(m, x0, y0, x1, y1, GAPBUF_FG);
}

static void
draw_char(struct image *m, int i, int c, const struct image *font, int invert)
{
    if (c < ' ' || c > '~')
        c = ' ';
    int fx = c % 16;
    int fy = c / 16 - 2;
    int fw = font->w / 16;
    int fh = font->h / 6;
    int h = GAPBUF_FONTSCALE;
    int w = fw * h / fh;
    int bx = w * i;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float sx = fx * fw + (float)x * fw / w;
            float sy = fy * fh + (float)y * fh / h;
            unsigned long rgb = image_get(font, sx, sy);
            image_set(m, bx + x, y, invert ? -1UL ^ rgb : rgb);
        }
    }
}

struct image *
gapbuf_draw(const struct gapbuf *b, const struct image *font)
{
    int w = b->total * GAPBUF_SCALE;
    int h = GAPBUF_FONTSCALE + GAPBUF_SCALE;
    struct image *m = image_create(w, h);
    image_rect(m, 0, 0, w, h, GAPBUF_BG);

    for (size_t i = 0; i < b->front; i++) {
        draw_block(m, i);
        draw_char(m, i, b->buf[i], font, 0);
    }
    for (size_t i = b->front + b->gap; i < b->total; i++) {
        draw_block(m, i);
        draw_char(m, i - b->gap, b->buf[i], font, i == b->front + b->gap);
    }
    /* Always draw the cursor */
    if (b->total == b->front + b->gap)
        draw_char(m, b->front, 0, font, 1);
    return m;
}

enum opcode {
    C_HALT,
    C_WAIT,
    C_FORWARD,
    C_BACKWARD,
    C_QMOVE,
    C_INSERT,
    C_QINSERT,
    C_STRING,
    C_QSTRING,
    C_DELETE,
    C_BACKSPACE,
};

struct command {
    enum opcode op;
    union {
        char *s;
        int v;
    } arg;
};

#define FRAME() \
    do { \
        image = gapbuf_draw(buf, font); \
        image_write(image, imgout); \
        free(image); \
    } while (0)

void
animate(const struct command *p, size_t z, FILE *imgout)
{
    struct gapbuf buf[1];
    gapbuf_init(buf, z);

    FILE *fontfile = fopen("font32.ppm", "rb");
    struct image *font = image_load(fontfile);
    fclose(fontfile);

    struct image *image;
    for (; p->op; p++) {
        switch (p->op) {
            case C_HALT: {
            } break;
            case C_WAIT: {
                int v = p->arg.v;
                while (v--)
                    FRAME();
            } break;
            case C_FORWARD: {
                int v = p->arg.v;
                while (v--) {
                    gapbuf_forward(buf);
                    FRAME();
                }
            } break;
            case C_BACKWARD: {
                int v = p->arg.v;
                while (v--) {
                    gapbuf_backward(buf);
                    FRAME();
                }
            } break;
            case C_QMOVE: {
                gapbuf_move(buf, p->arg.v);
            } break;
            case C_INSERT: {
                gapbuf_insert(buf, p->arg.v);
                FRAME();
            } break;
            case C_QINSERT: {
                gapbuf_insert(buf, p->arg.v);
            } break;
            case C_QSTRING: {
                gapbuf_inserts(buf, p->arg.s);
            } break;
            case C_STRING: {
                for (const char *s = p->arg.s; *s; s++) {
                    gapbuf_insert(buf, *s);
                    FRAME();
                }
            } break;
            case C_DELETE: {
                int v = p->arg.v;
                while (v--) {
                    gapbuf_delete(buf);
                    FRAME();
                }
            } break;
            case C_BACKSPACE: {
                int v = p->arg.v;
                while (v--) {
                    gapbuf_backspace(buf);
                    FRAME();
                }
            } break;
        }
    }

    gapbuf_destroy(buf);
    free(font);
}

int
main(void)
{
    FILE *f;

    #define FPS 10 
    static const struct command intro[] = {
        {C_WAIT,      .arg.v = FPS},
        {C_STRING,    .arg.s = "This is a buffer."},
        /* "This is a buffer." */
        {C_WAIT,      .arg.v = FPS},
        {C_BACKWARD,  .arg.v = 7},
        {C_STRING,    .arg.s = "gap "},
        /* "This is a gap buffer. */
        {C_WAIT,      .arg.v = FPS},
        {C_BACKWARD,  .arg.v = 5},
        {C_WAIT,      .arg.v = FPS / 2},
        {C_BACKSPACE, .arg.v = 9},
        /* " gap buffer. */
        {C_INSERT,    .arg.v = 'A'},
        /* "A gap buffer. */
        {C_FORWARD,   .arg.v = 11},
        {C_WAIT,      .arg.v = FPS / 2},
        {C_STRING,    .arg.s = " is for clustered edits"},
        /* "A gap buffer is good at clustered edits." */
        {C_WAIT,      .arg.v = FPS / 2},
        {C_FORWARD,   .arg.v = 1},
        {C_WAIT,      .arg.v = FPS},
        {C_BACKWARD,  .arg.v = 16},
        {C_BACKSPACE, .arg.v = 24},
        {C_DELETE,    .arg.v = 1},
        {C_INSERT,    .arg.v = 'C'}, /* "Clustered edits.*/
        {C_FORWARD ,  .arg.v = 14},
        {C_STRING,    .arg.s = " are most efficient!"},
        {C_DELETE,    .arg.v = 1},
        /* "Clustered edits are most efficient." */
        {C_WAIT,      .arg.v = FPS},
        {C_BACKSPACE, .arg.v = 35},
        /* "" */
        {C_WAIT,      .arg.v = FPS},
        {C_HALT},
    };
    f = fopen("intro.ppm", "wb");
    animate(intro, 38, f);
    fclose(f);

    static const struct command multicursors[] = {
        {C_QSTRING,   .arg.s = "foo(); bar(); baz();"},
        {C_QMOVE,     .arg.v = -16},
        {C_WAIT,      .arg.v = FPS},

        {C_INSERT,    .arg.v = 'x'},
        {C_WAIT,      .arg.v = FPS / 4},
        {C_FORWARD,   .arg.v = 7},
        {C_INSERT,    .arg.v = 'x'},
        {C_WAIT,      .arg.v = FPS / 4},
        {C_FORWARD,   .arg.v = 7},
        {C_INSERT,    .arg.v = 'x'},
        {C_WAIT,      .arg.v = FPS / 4},
        {C_BACKWARD,  .arg.v = 8 * 2},

        {C_INSERT,    .arg.v = ','},
        {C_WAIT,      .arg.v = FPS / 4},
        {C_FORWARD,   .arg.v = 8},
        {C_INSERT,    .arg.v = ','},
        {C_WAIT,      .arg.v = FPS / 4},
        {C_FORWARD,   .arg.v = 8},
        {C_INSERT,    .arg.v = ','},
        {C_WAIT,      .arg.v = FPS / 4},
        {C_BACKWARD,  .arg.v = 9 * 2},

        {C_INSERT,    .arg.v = ' '},
        {C_WAIT,      .arg.v = FPS / 4},
        {C_FORWARD,   .arg.v = 9},
        {C_INSERT,    .arg.v = ' '},
        {C_WAIT,      .arg.v = FPS / 4},
        {C_FORWARD,   .arg.v = 9},
        {C_INSERT,    .arg.v = ' '},
        {C_WAIT,      .arg.v = FPS / 4},
        {C_BACKWARD,  .arg.v = 10 * 2},

        {C_INSERT,    .arg.v = 'y'},
        {C_WAIT,      .arg.v = FPS / 4},
        {C_FORWARD,   .arg.v = 10},
        {C_INSERT,    .arg.v = 'y'},
        {C_WAIT,      .arg.v = FPS / 4},
        {C_FORWARD,   .arg.v = 10},
        {C_INSERT,    .arg.v = 'y'},
        {C_WAIT,      .arg.v = FPS / 4},
        {C_BACKWARD,  .arg.v = 11 * 2},

        {C_WAIT,      .arg.v = FPS * 2},
        {C_HALT},
    };
    f = fopen("multicursors.ppm", "wb");
    animate(multicursors, 38, f);
    fclose(f);

    static const struct command macros[] = {
        {C_QSTRING,   .arg.s = "foo(); bar(); baz();"},
        {C_QMOVE,     .arg.v = -16},
        {C_WAIT,      .arg.v = FPS},

        {C_STRING,    .arg.s = "x, y"},
        {C_WAIT,      .arg.v = FPS / 4},
        {C_FORWARD,   .arg.v = 7},
        {C_STRING,    .arg.s = "x, y"},
        {C_WAIT,      .arg.v = FPS / 4},
        {C_FORWARD,   .arg.v = 7},
        {C_STRING,    .arg.s = "x, y"},
        {C_WAIT,      .arg.v = FPS / 4},

        {C_WAIT,      .arg.v = FPS * 2},
        {C_HALT},
    };
    f = fopen("macros.ppm", "wb");
    animate(macros, 38, f);
    fclose(f);

    static const struct command illusion[] = {
        {C_QSTRING,   .arg.s = "foo(); bar(); baz();"},
        {C_QMOVE,     .arg.v = -16},
        {C_WAIT,      .arg.v = FPS},

        {C_QINSERT,   .arg.v = 'x'},
        {C_QMOVE,     .arg.v = 7},
        {C_QINSERT,   .arg.v = 'x'},
        {C_QMOVE,     .arg.v = 7},
        {C_QINSERT,   .arg.v = 'x'},
        {C_QMOVE,     .arg.v = -8 * 2},
        {C_WAIT,      .arg.v = FPS / 4},

        {C_QINSERT,   .arg.v = ','},
        {C_QMOVE,     .arg.v = 8},
        {C_QINSERT,   .arg.v = ','},
        {C_QMOVE,     .arg.v = 8},
        {C_QINSERT,   .arg.v = ','},
        {C_QMOVE,     .arg.v = -9 * 2},
        {C_WAIT,      .arg.v = FPS / 4},

        {C_QINSERT,   .arg.v = ' '},
        {C_QMOVE,     .arg.v = 9},
        {C_QINSERT,   .arg.v = ' '},
        {C_QMOVE,     .arg.v = 9},
        {C_QINSERT,   .arg.v = ' '},
        {C_QMOVE,     .arg.v = -10 * 2},
        {C_WAIT,      .arg.v = FPS / 4},

        {C_QINSERT,   .arg.v = 'y'},
        {C_QMOVE,     .arg.v = 10},
        {C_QINSERT,   .arg.v = 'y'},
        {C_QMOVE,     .arg.v = 10},
        {C_QINSERT,   .arg.v = 'y'},
        {C_QMOVE,     .arg.v = -11 * 2},
        {C_WAIT,      .arg.v = FPS / 4},

        {C_WAIT,      .arg.v = FPS * 2},
        {C_HALT},
    };
    f = fopen("illusion.ppm", "wb");
    animate(illusion, 38, f);
    fclose(f);
}
