#include "internal.h"
#include "test.h"

static void crossing_interpolates(void)
{
    sample samples[] = {{0, {0, 0, 0, 1}}, {1, {0, 0, 0, 0.5}}, {2, {0, 0, 0, 0}}};
    profile p = {samples, 3};
    CHECK_NEAR(profile_crossing(p, 3, 0.75), 0.5);
    CHECK_NEAR(profile_crossing(p, 3, 0.25), 1.5);
    CHECK_NEAR(profile_coverage(p, 3, 2), 0.75);
}

static void names_are_unique(void)
{
    CHECK(gr_conf_case_count == 55);
    for (size_t i = 0; i < gr_conf_case_count; ++i)
        for (size_t j = 0; j < i; ++j)
            CHECK(strcmp(gr_conf_cases[i].name, gr_conf_cases[j].name) != 0);
}

static void probes_and_profiles(void)
{
    uint8_t pixels[] = {255, 0, 0, 255, 0, 0, 255, 128};
    frame f = {{2, 1, pixels}, 2};
    rgba8 got;
    CHECK(frame_at(&f, POINT(0.1, 0.1), &got) && got.v[0] == 255);
    CHECK(frame_at(&f, POINT(0.9, 0.1), &got) && got.v[2] == 255);
    CHECK(!frame_at(&f, POINT(-0.1, 0), &got));
    CHECK(!frame_at(&f, POINT(1, 0), &got));
    profile p = frame_profile(&f, POINT(0, 0.1), POINT(1, 0.1));
    CHECK(p.count == 2);
    CHECK_NEAR(p.data[0].distance, 0.25);
    CHECK_NEAR(p.data[1].distance, 0.75);
    free(p.data);
}

static void context_storage_grows(void)
{
    ctx c = {.scale = 1.5};
    frame *first = NULL;
    for (size_t i = 0; i < 40; ++i)
    {
        gr_scene *scene = new_scene(&c);
        for (size_t j = 0; j < 3; ++j)
            new_layer(&c, scene, GR_LAYER_CONTAINER);
        frame *f = new_frame(&c);
        f->pixels = (gr_conf_pixels){1, 1, malloc(4)};
        CHECK(f->pixels.data);
        memcpy(f->pixels.data, RED.v, 4);
        if (!first)
            first = f;
        CHECK(first == c.frames[0]);
        CHECK(first->pixels.width == 1 && first->pixels.data[0] == 255);
    }
    CHECK(c.scene_count == 40 && c.layer_count == 120 && c.frame_count == 40);
    context_clear(&c);
    CHECK(!c.frames && !c.layers && !c.scenes && c.frame_count == 0);
}

int main(int argc, char **argv)
{
    const gr_test_case cases[] = {GR_TEST_CASE(crossing_interpolates), GR_TEST_CASE(names_are_unique),
                                  GR_TEST_CASE(probes_and_profiles), GR_TEST_CASE(context_storage_grows)};
    return gr_test_run("conformance", cases, sizeof(cases) / sizeof(cases[0]), argc, argv);
}
