// Unit tests for the pure per-item image helpers. These have no SDL/display
// dependencies, so they run headless with the host compiler via `make test`.

#include "list_image.h"

#include <stdio.h>
#include <string.h>

static int checks = 0;
static int failures = 0;

#define CHECK_EQ(actual, expected, msg)                                         \
    do                                                                          \
    {                                                                           \
        checks++;                                                               \
        int _a = (actual);                                                      \
        int _e = (expected);                                                    \
        if (_a != _e)                                                           \
        {                                                                       \
            failures++;                                                         \
            fprintf(stderr, "FAIL: %s (expected %d, got %d)\n", (msg), _e, _a); \
        }                                                                       \
    } while (0)

// CHECK_FIT scales src into the box and asserts the returned flag and the two
// output dimensions in a single, readable statement.
#define CHECK_FIT(sw, sh, mw, mh, exp_ok, exp_w, exp_h, msg)                    \
    do                                                                          \
    {                                                                           \
        int _w = -1;                                                            \
        int _h = -1;                                                            \
        bool _ok = ImageFit_Scale((sw), (sh), (mw), (mh), &_w, &_h);            \
        CHECK_EQ(_ok ? 1 : 0, (exp_ok) ? 1 : 0, msg " (ok)");                   \
        CHECK_EQ(_w, (exp_w), msg " (w)");                                      \
        CHECK_EQ(_h, (exp_h), msg " (h)");                                      \
    } while (0)

static void test_fit_downscale_only(void)
{
    // an image already within the box keeps its native size
    CHECK_FIT(30, 30, 213, 54, true, 30, 30, "fit: small stays native");
    CHECK_FIT(213, 54, 213, 54, true, 213, 54, "fit: exact box");
    CHECK_FIT(100, 20, 213, 54, true, 100, 20, "fit: fits both dims");
}

static void test_fit_downscale(void)
{
    // 640x480 into 213x54 -> height-constrained -> 72x54
    CHECK_FIT(640, 480, 213, 54, true, 72, 54, "fit: landscape height-bound");
    // 500x40 into 213x54 -> width-constrained -> 213x17
    CHECK_FIT(500, 40, 213, 54, true, 213, 17, "fit: wide width-bound");
    // 100x100 into 213x54 -> height-constrained -> 54x54
    CHECK_FIT(100, 100, 213, 54, true, 54, 54, "fit: square height-bound");
    // 40x500 into 213x54 (taller than the row) -> height-constrained -> 4x54
    CHECK_FIT(40, 500, 213, 54, true, 4, 54, "fit: tall portrait");
    // width already fits but far too tall -> height-constrained
    CHECK_FIT(213, 600, 213, 54, true, 19, 54, "fit: exact width, too tall");
}

static void test_fit_rounding_and_guards(void)
{
    // extreme downscale rounds up to a floor of 1px on the constrained axis
    CHECK_FIT(1000, 10, 5, 5, true, 5, 1, "fit: rounds up to 1px");
    // non-positive inputs fail and leave dst at 0
    CHECK_FIT(0, 50, 213, 54, false, 0, 0, "fit: zero src width");
    CHECK_FIT(50, 0, 213, 54, false, 0, 0, "fit: zero src height");
    CHECK_FIT(50, 50, 0, 54, false, 0, 0, "fit: zero max width");
    CHECK_FIT(50, 50, 213, 0, false, 0, 0, "fit: zero max height");
    CHECK_FIT(-10, 50, 213, 54, false, 0, 0, "fit: negative src width");

    // NULL out-pointers must not crash
    checks++;
    if (!ImageFit_Scale(640, 480, 213, 54, NULL, NULL))
    {
        failures++;
        fprintf(stderr, "FAIL: fit: NULL out-pointers should still succeed\n");
    }
}

static void test_variant_select(void)
{
    struct ImageVariant with_default[] = {
        {"default", "/d.png"},
        {"1280x720", "/hd.png"},
    };
    int n = (int)(sizeof(with_default) / sizeof(with_default[0]));

    CHECK_EQ(ImageVariant_SelectIndex(with_default, n, "1280x720"), 1, "select: exact match");
    CHECK_EQ(ImageVariant_SelectIndex(with_default, n, "640x480"), 0, "select: falls back to default");
    CHECK_EQ(ImageVariant_SelectIndex(with_default, n, "1024x768"), 0, "select: unknown -> default");
    CHECK_EQ(ImageVariant_SelectIndex(with_default, n, "default"), 0, "select: literal default key");
    CHECK_EQ(ImageVariant_SelectIndex(with_default, n, NULL), 0, "select: NULL resolution -> default");

    struct ImageVariant no_default[] = {
        {"1280x720", "/hd.png"},
    };
    CHECK_EQ(ImageVariant_SelectIndex(no_default, 1, "1280x720"), 0, "select: no-default exact");
    CHECK_EQ(ImageVariant_SelectIndex(no_default, 1, "640x480"), -1, "select: no-default no-match");
    CHECK_EQ(ImageVariant_SelectIndex(no_default, 1, NULL), -1, "select: no-default NULL");

    CHECK_EQ(ImageVariant_SelectIndex(NULL, 0, "1280x720"), -1, "select: NULL array");
    CHECK_EQ(ImageVariant_SelectIndex(with_default, 0, "1280x720"), -1, "select: zero count");
}

int main(void)
{
    test_fit_downscale_only();
    test_fit_downscale();
    test_fit_rounding_and_guards();
    test_variant_select();

    if (failures == 0)
    {
        printf("ok - all %d checks passed\n", checks);
        return 0;
    }

    fprintf(stderr, "not ok - %d/%d checks failed\n", failures, checks);
    return 1;
}
