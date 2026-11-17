#include <proto/graphics.h>

#include "screen.h"
#include "conio.h"
#include "gfx_render.h"
#include "shapes.h"
#include "vector_outline.h"
#include "data.h"

/* Stroke a closed loop as pixel lines, clipping each segment against the
 * screen. Wrapping worlds: the server sends one copy per visible wrap
 * with centres that may sit off-screen - segments are clipped with true
 * slopes, never vertex-clamped (which would draw false diagonals). */
static void draw_contour_lines(struct RastPort *rp, const vo_point *pts,
                               uint8_t len)
{
    int32_t min_x = 0, min_y = 0;
    int32_t max_x = SCREEN_PIXEL_WIDTH - 1;
    int32_t max_y = (int32_t)amiga_conio_height() - 1;
    int32_t pen_x = 0, pen_y = 0;
    uint8_t pen_down = 0;
    uint8_t i;

    if (len < 2) {
        return;
    }
    SetAPen(rp, 2); /* reserved shape pen */

    for (i = 0; i < len; i++) {
        uint8_t j = (uint8_t)((i + 1 == len) ? 0 : i + 1);
        int32_t ax = pts[i].x, ay = pts[i].y;
        int32_t bx = pts[j].x, by = pts[j].y;

        if (!vo_clip_segment(&ax, &ay, &bx, &by, min_x, min_y, max_x, max_y)) {
            pen_down = 0;
            continue;
        }
        if (!pen_down || ax != pen_x || ay != pen_y) {
            Move(rp, (LONG)ax, (LONG)ay);
        }
        Draw(rp, (LONG)bx, (LONG)by);
        pen_x = bx;
        pen_y = by;
        pen_down = 1;
    }
}

static void draw_vector(struct RastPort *rp, const vo_outline *ol,
                        int32_t base_x, int32_t base_y,
                        int32_t scale_x, int32_t height_px)
{
    static vo_point px_pts[VO_MAX_PTS]; /* static: off the 64KB stack */
    uint8_t c, i, n;

    SetAPen(rp, 2); /* reserved shape pen */

    for (c = 0; c < ol->contour_count; c++) {
        n = ol->contour_len[c];
        for (i = 0; i < n; i++) {
            const vo_point *v = &ol->pts[ol->contour_start[c] + i];

            /* Unclamped: draw_contour_lines clips each segment against
             * the screen, preserving true slopes for off-screen centres. */
            px_pts[i].x = (int16_t)(base_x + (int32_t)v->x * scale_x / 4);
            px_pts[i].y = (int16_t)(base_y +
                (int32_t)v->y * height_px / (4 * REG_WORLD_HEIGHT));
        }
        draw_contour_lines(rp, px_pts, n);
    }
}

static void draw_block(struct RastPort *rp, int32_t center_x, int32_t center_y,
                       uint8_t width)
{
    int32_t half_w, half_h, x0, y0, x1, y1;
    uint16_t h = amiga_conio_height();

    /* Shapes are proportional to the logical 40x24 world grid; scale the
     * world-unit footprint into registered screen pixels using the
     * runtime drawable height so NTSC machines stay registered. */
    half_w = ((int32_t)width * SCREEN_PIXEL_WIDTH / REG_WORLD_WIDTH) / 2;
    half_h = ((int32_t)width * h / REG_WORLD_HEIGHT) / 2;

    x0 = (int32_t)center_x - half_w;
    y0 = (int32_t)center_y - half_h;
    x1 = (int32_t)center_x + half_w - 1;
    y1 = (int32_t)center_y + half_h - 1;

    /* Bodies can transiently sit outside the registered screen while
     * wrapping. Clamp to the bitmap and drop rects that clamp away
     * entirely - a reversed RectFill (x0 > x1 or y0 > y1) is undefined
     * behaviour and crashed the guest. */
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 >= SCREEN_PIXEL_WIDTH) {
        x1 = SCREEN_PIXEL_WIDTH - 1;
    }
    if (y1 >= (int32_t)h) {
        y1 = (int32_t)h - 1;
    }
    if (x1 < x0 || y1 < y0) {
        return;
    }

    SetAPen(rp, 2); /* colour 2 is the reserved shape pen (conio palette) */
    RectFill(rp, (LONG)x0, (LONG)y0, (LONG)x1, (LONG)y1);
}

static void draw_shape_at(uint8_t shape_id, int16_t center_x, int16_t center_y,
                          int32_t base_x, int32_t base_y)
{
    struct RastPort *rp;
    const ShapeRecord *shape;
    static vo_outline outline; /* static: keeps geometry off the 64KB stack */
    uint16_t h;
    uint8_t width;

    if (shape_id >= shape_count) {
        return;
    }
    rp = (struct RastPort *)amiga_conio_draw_rp();
    if (!rp) {
        return;
    }

    shape = &shapes[shape_id];
    width = shape->shape_width;
    if (width == 0 || !shape->shape_data) {
        return;
    }

    h = amiga_conio_height();

    if (bwc_render_mode == RENDER_BLOCK) {
        draw_block(rp, center_x, center_y, width);
        return;
    }

    if (vo_authored(shape_id, &outline) != 0) return;

    if (base_x >= SCREEN_PIXEL_WIDTH || base_y >= (int32_t)h ||
        base_x + 2 * ((int32_t)width * SCREEN_PIXEL_WIDTH / REG_WORLD_WIDTH) < 0 ||
        base_y + 2 * ((int32_t)width * h / REG_WORLD_HEIGHT) < 0) {
        return;
    }

    draw_vector(rp, &outline, base_x, base_y,
                SCREEN_PIXEL_WIDTH / REG_WORLD_WIDTH, h);
}

void gfx_show_shape_px(uint8_t shape_id, int16_t center_x, int16_t center_y)
{
    int32_t base_x, base_y;
    uint16_t h;
    uint8_t width;

    if (shape_id >= shape_count) {
        return;
    }
    {
        const ShapeRecord *shape = &shapes[shape_id];

        width = shape->shape_width;
        if (width == 0 || !shape->shape_data) {
            return;
        }
    }

    h = amiga_conio_height();
    base_x = (int32_t)center_x -
        ((int32_t)width * SCREEN_PIXEL_WIDTH / REG_WORLD_WIDTH) / 2;
    base_y = (int32_t)center_y -
        ((int32_t)width * h / REG_WORLD_HEIGHT) / 2;

    /* Wrapping worlds: the server sends one copy per visible wrap, with
     * centres that may sit off-screen (e.g. -2 or 322). Draw each copy
     * as sent - draw_shape_at clips to what is actually viewable. No
     * client-side seam duplication. */
    draw_shape_at(shape_id, center_x, center_y, base_x, base_y);
}
