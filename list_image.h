#ifndef LIST_IMAGE_H
#define LIST_IMAGE_H

#include <stdbool.h>

// list_image provides the SDL-free math and lookups behind per-item images
// drawn on the right-hand side of a list row. It only computes scaled
// dimensions and selects a path for the active resolution; the caller loads,
// scales, and blits the actual surface. Keeping it display-free means it can be
// unit tested with the host compiler (see tests/list_image_test.c).

// ImageVariant maps a resolution key to an image path. The key is either
// "default" or a "WIDTHxHEIGHT" string (e.g. "1280x720"). The path is a full
// filesystem path to the image.
struct ImageVariant
{
    // the resolution key ("default" or "WIDTHxHEIGHT")
    char resolution[32];
    // the full path to the image for this resolution
    char path[1024];
};

// ImageFit_Scale computes aspect-fit ("contain") dimensions for an image of
// src_w x src_h drawn into a box of max_w x max_h, preserving the aspect ratio.
// It is downscale-only: an image already within the box keeps its native size
// and is never enlarged. The result is written to *dst_w / *dst_h (each at
// least 1 when the call succeeds).
//
// Returns false (and sets *dst_w / *dst_h to 0) for any non-positive input.
bool ImageFit_Scale(int src_w, int src_h, int max_w, int max_h,
                    int *dst_w, int *dst_h);

// ImageVariant_SelectIndex returns the index of the best matching variant for
// the given resolution: an exact resolution match if present, otherwise the
// "default" entry, otherwise -1. Passing a NULL array, a non-positive count, or
// a NULL resolution never matches an exact key (but a "default" entry, if any,
// is still returned).
int ImageVariant_SelectIndex(const struct ImageVariant *variants, int count,
                             const char *resolution);

#endif // LIST_IMAGE_H
