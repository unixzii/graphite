#include "platform.h"

int example_main(int argc, char **argv, const example_desc *desc)
{
    bool selftest = false, fps = false;
    const char *snapshot = NULL, *error = NULL;
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--selftest") == 0)
            selftest = true;
        else if (strcmp(argv[i], "--fps") == 0)
            fps = true;
        else if (strcmp(argv[i], "--snapshot") == 0)
        {
            if (++i == argc)
            {
                error = "--snapshot needs a path";
                break;
            }
            snapshot = argv[i];
        }
        else
        {
            error = "unknown argument";
            break;
        }
    }
    if (error)
    {
        fprintf(stderr, "%s\nusage: %s [--selftest | --fps | --snapshot <out.png>]\n", error, desc->name);
        return 2;
    }
    gr_text_system *text = example_platform_text_system();
    example *ex = example_new(desc, text);
    gr_text_system_unref(text);
    int result;
    if (snapshot)
    {
        example_update(ex, 2.4);
        result = example_platform_snapshot(ex, snapshot) ? 0 : 1;
    }
    else
        result = example_platform_run(ex, selftest, fps);
    example_free(ex);
    return result;
}
