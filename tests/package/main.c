#include <graphite/graphite.h>
#if defined(CONSUMER_MACOS)
#include <graphite/macos.h>
#elif defined(CONSUMER_GTK)
#include <graphite/gtk.h>
#elif defined(CONSUMER_WINDOWS)
#include <graphite/windows.h>
#elif defined(CONSUMER_WEB)
#include <graphite/web.h>
#endif

int main(void)
{
    gr_size size = {64, 32};
    gr_scene *scene = gr_scene_new(size);
    if (!scene || !gr_scene_get_root(scene))
        return 1;
    gr_scene_unref(scene);
#if defined(CONSUMER_MACOS)
    gr_macos_pixels pixels = {0};
    gr_macos_pixels_clear(&pixels);
#elif defined(CONSUMER_GTK)
    gr_gtk_pixels pixels = {0};
    gr_gtk_pixels_clear(&pixels);
#elif defined(CONSUMER_WINDOWS)
    gr_windows_pixels pixels = {0};
    gr_windows_pixels_clear(&pixels);
#elif defined(CONSUMER_WEB)
    /* Linking the backend needs its runtime library; attaching needs a document, which node does not have. */
    gr_scene_host *(*volatile attach)(gr_scene *, const char *) = gr_web_attach_to_element;
    if (!attach)
        return 1;
#endif
    return 0;
}
