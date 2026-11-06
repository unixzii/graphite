/** Graphite canvas API. Ownership contracts: ../../src/CONTRACT.md. */
#ifndef GRAPHITE_CANVAS_H
#define GRAPHITE_CANVAS_H
#include "resource.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Recording lifetime */

    /** Creates an owned empty recording canvas for exclusive access. */
    gr_canvas *gr_canvas_new(void);

    /** Acquires an owned reference and returns the same pointer; NULL returns NULL. */
    gr_canvas *gr_canvas_ref(gr_canvas *canvas);

    /** Releases an owned reference; NULL is accepted. Final release destroys the object at a safe boundary. */
    void gr_canvas_unref(gr_canvas *canvas);

    /* Graphics state */

    /** Records a graphics-state save (transform, clip and alpha). */
    void gr_canvas_save(gr_canvas *canvas);

    /** Records a graphics-state restore; ignores an unmatched restore. */
    void gr_canvas_restore(gr_canvas *canvas);

    /** Records concatenation of a finite affine transform; identity transforms may be omitted. */
    gr_result gr_canvas_concat_transform(gr_canvas *canvas, gr_affine transform);

    /** Records multiplication of global alpha by the finite input clamped to [0,1]. */
    gr_result gr_canvas_set_alpha(gr_canvas *canvas, float alpha);

    /* Clipping and drawing */

    /** Records clipping to rect in current user coordinates; rejects invalid geometry. */
    gr_result gr_canvas_clip_rect(gr_canvas *canvas, gr_rect rect);

    /** Records clipping to path using rule and retains the path; rejects invalid rule. */
    gr_result gr_canvas_clip_path(gr_canvas *canvas, gr_path *path, gr_fill_rule rule);

    /** Records a path fill and refs path/paint; rejects invalid fill rule. */
    gr_result gr_canvas_fill_path(gr_canvas *canvas, gr_path *path, gr_fill_rule rule, gr_paint *paint);

    /** Records a rectangle fill and refs paint; rejects invalid geometry. */
    gr_result gr_canvas_fill_rect(gr_canvas *canvas, gr_rect rect, gr_paint *paint);

    /** Records a path stroke and refs both path and stroke. */
    gr_result gr_canvas_stroke_path(gr_canvas *canvas, gr_path *path, gr_stroke *stroke);

    /** Records image drawing into destination and refs image; rejects invalid geometry. */
    gr_result gr_canvas_draw_image(gr_canvas *canvas, gr_image *image, gr_rect destination);

    /* Finished recordings */

    /** Owned list, balances saves, resets canvas. Does not consume the canvas ref. */
    gr_display_list *gr_canvas_finish(gr_canvas *canvas);

    /** Acquires an owned reference and returns the same pointer; NULL returns NULL. */
    gr_display_list *gr_display_list_ref(gr_display_list *list);

    /** Releases an owned reference; NULL is accepted. Final release destroys the object at a safe boundary. */
    void gr_display_list_unref(gr_display_list *list);

    /** Returns the immutable resource identity, unique for the process lifetime and never reused. */
    uint64_t gr_display_list_get_id(const gr_display_list *list);

    /** Returns whether the recording contains no operations. */
    bool gr_display_list_is_empty(const gr_display_list *list);

#ifdef __cplusplus
}
#endif
#endif
