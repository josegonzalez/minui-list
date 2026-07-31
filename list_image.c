#include "list_image.h"

#include <string.h>

bool ImageFit_Scale(int src_w, int src_h, int max_w, int max_h,
                    int *dst_w, int *dst_h)
{
    if (dst_w != NULL)
        *dst_w = 0;
    if (dst_h != NULL)
        *dst_h = 0;

    if (src_w <= 0 || src_h <= 0 || max_w <= 0 || max_h <= 0)
        return false;

    int w;
    int h;

    // downscale-only: leave an image that already fits at its native size
    if (src_w <= max_w && src_h <= max_h)
    {
        w = src_w;
        h = src_h;
    }
    // pick the constraining dimension and scale the other with integer math so
    // the result is deterministic (and easy to unit test)
    else if ((long)src_w * max_h >= (long)src_h * max_w)
    {
        // width-constrained
        w = max_w;
        h = (int)((long)src_h * max_w / src_w);
    }
    else
    {
        // height-constrained
        h = max_h;
        w = (int)((long)src_w * max_h / src_h);
    }

    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;

    if (dst_w != NULL)
        *dst_w = w;
    if (dst_h != NULL)
        *dst_h = h;

    return true;
}

int ImageVariant_SelectIndex(const struct ImageVariant *variants, int count,
                             const char *resolution)
{
    if (variants == NULL || count <= 0)
        return -1;

    int default_index = -1;
    for (int i = 0; i < count; i++)
    {
        if (resolution != NULL && strcmp(variants[i].resolution, resolution) == 0)
            return i;
        if (strcmp(variants[i].resolution, "default") == 0)
            default_index = i;
    }

    return default_index;
}
