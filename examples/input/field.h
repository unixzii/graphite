#ifndef GRAPHITE_EXAMPLE_FIELD_H
#define GRAPHITE_EXAMPLE_FIELD_H

#include "example.h"

/* A single-line text field on the host's text input: typing, input method compositions, platform key bindings,
 * selection by pointer and keyboard. Its content layer owns it, so it lives as long as the Scene. */
typedef struct text_field text_field;

/** Adds a field filling frame (parent coordinates); host_origin is frame's origin in host space. */
text_field *text_field_new(example *ex, gr_layer *parent, gr_rect frame, gr_point host_origin, const char *placeholder);
/** Blinks the caret. */
void text_field_update(text_field *f, double time);
/** Handles a pointer event at a host-space position: presses inside focus the field and place the caret, drags
 * select, presses outside blur it. Returns whether the field consumed the event. */
bool text_field_pointer(text_field *f, const gr_event *event);
/** Handles TEXT_EDIT and TEXT_COMMAND events, and shortcuts among KEY_DOWN events while focused. */
bool text_field_event(text_field *f, const gr_event *event);
bool text_field_focused(const text_field *f);
/** Whether a host-space point lies in the field. */
bool text_field_contains(const text_field *f, gr_point point);

#endif
