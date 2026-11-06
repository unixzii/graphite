#include "demo.h"

int main(int argc, char **argv)
{
    demo_kind kind = DEMO_SHOWCASE;
    bool selftest = false, fps = false;
    const char *snapshot = NULL, *error = NULL;
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--scene") == 0)
        {
            if (++i == argc)
            {
                error = "--scene needs a name";
                break;
            }
            if (strcmp(argv[i], "showcase") == 0)
                kind = DEMO_SHOWCASE;
            else if (strcmp(argv[i], "ui") == 0)
                kind = DEMO_UI;
            else
            {
                error = "unknown scene";
                break;
            }
        }
        else if (strcmp(argv[i], "--selftest") == 0)
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
        fprintf(stderr, "%s\nusage: demo [--scene showcase|ui] [--selftest | --fps | --snapshot <out.png>]\n", error);
        return 2;
    }
    demo *d = demo_new(kind);
    int result;
    if (snapshot)
    {
        demo_update(d, 2.4);
        result = demo_platform_snapshot(d, snapshot) ? 0 : 1;
    }
    else
        result = demo_platform_run(d, selftest, fps);
    demo_free(d);
    return result;
}
