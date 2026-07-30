// Unit tests for the pure alphabetic (L1/R1) letter-jump navigation helpers.
// These have no SDL/display dependencies, so they run headless with the host
// compiler via `make test`.

#include "list_nav.h"

#include <stdio.h>

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

// convenience constructors keep the test bodies readable
#define ITEM(n) {.name = (n), .is_header = false, .unselectable = false}
#define HEADER(n) {.name = (n), .is_header = true, .unselectable = false}
#define UNSEL(n) {.name = (n), .is_header = false, .unselectable = true}

static void test_next_basic(void)
{
    struct ListNavItem items[] = {ITEM("Apple"), ITEM("Avocado"), ITEM("Banana"), ITEM("Cherry")};
    int n = 4;
    CHECK_EQ(ListNav_NextLetterIndex(items, n, 0), 2, "next: A->B from first A");
    CHECK_EQ(ListNav_NextLetterIndex(items, n, 1), 2, "next: A->B from second A");
    CHECK_EQ(ListNav_NextLetterIndex(items, n, 2), 3, "next: B->C");
}

static void test_next_lands_on_first_of_group(void)
{
    struct ListNavItem items[] = {ITEM("Apple"), ITEM("Banana"), ITEM("Berry"), ITEM("Cherry")};
    int n = 4;
    CHECK_EQ(ListNav_NextLetterIndex(items, n, 0), 1, "next: lands on first B (Banana)");
    CHECK_EQ(ListNav_NextLetterIndex(items, n, 1), 3, "next: B->C skips second B");
}

static void test_prev_basic(void)
{
    struct ListNavItem items[] = {ITEM("Apple"), ITEM("Avocado"), ITEM("Banana"), ITEM("Cherry")};
    int n = 4;
    CHECK_EQ(ListNav_PrevLetterIndex(items, n, 3), 2, "prev: C->B (first B)");
    CHECK_EQ(ListNav_PrevLetterIndex(items, n, 2), 0, "prev: B->A (first A)");
}

static void test_prev_lands_on_first_of_group(void)
{
    struct ListNavItem items[] = {ITEM("Apple"), ITEM("Ant"), ITEM("Banana"), ITEM("Cherry")};
    int n = 4;
    // from Cherry, previous group is B, whose first item is index 2
    CHECK_EQ(ListNav_PrevLetterIndex(items, n, 3), 2, "prev: C->first B");
    // from Banana, previous group is A, whose first item is index 0
    CHECK_EQ(ListNav_PrevLetterIndex(items, n, 2), 0, "prev: B->first A");
}

static void test_wrap(void)
{
    struct ListNavItem items[] = {ITEM("Apple"), ITEM("Avocado"), ITEM("Banana"), ITEM("Cherry")};
    int n = 4;
    // next past the last letter wraps to the first group
    CHECK_EQ(ListNav_NextLetterIndex(items, n, 3), 0, "next wrap: C->A");
    // prev before the first letter wraps to the last group (its first item)
    CHECK_EQ(ListNav_PrevLetterIndex(items, n, 0), 3, "prev wrap: A->C");
    CHECK_EQ(ListNav_PrevLetterIndex(items, n, 1), 3, "prev wrap: second A -> C");
}

static void test_single_letter_no_move(void)
{
    struct ListNavItem items[] = {ITEM("Apple"), ITEM("Ant"), ITEM("Axe")};
    int n = 3;
    CHECK_EQ(ListNav_NextLetterIndex(items, n, 0), 0, "next: single letter stays put");
    CHECK_EQ(ListNav_NextLetterIndex(items, n, 1), 1, "next: single letter stays put (mid)");
    CHECK_EQ(ListNav_PrevLetterIndex(items, n, 2), 2, "prev: single letter stays put");
}

static void test_case_insensitive(void)
{
    struct ListNavItem items[] = {ITEM("apple"), ITEM("Banana")};
    int n = 2;
    CHECK_EQ(ListNav_NextLetterIndex(items, n, 0), 1, "next: lowercase a -> B");
    CHECK_EQ(ListNav_PrevLetterIndex(items, n, 1), 0, "prev: B -> lowercase a");
}

static void test_skips_headers_and_unselectable(void)
{
    struct ListNavItem items[] = {HEADER("Fruits"), ITEM("Apple"), ITEM("Avocado"), ITEM("Banana")};
    int n = 4;
    CHECK_EQ(ListNav_NextLetterIndex(items, n, 1), 3, "next: A->B skipping header");
    CHECK_EQ(ListNav_PrevLetterIndex(items, n, 3), 1, "prev: B->first selectable A (header skipped)");
    // prev from the first A group wraps to B; the header is never selected
    CHECK_EQ(ListNav_PrevLetterIndex(items, n, 1), 3, "prev wrap: A->B, header never targeted");

    struct ListNavItem unsel[] = {UNSEL("Apple"), ITEM("Avocado"), ITEM("Banana")};
    CHECK_EQ(ListNav_PrevLetterIndex(unsel, 3, 2), 1, "prev: B->first selectable A (unselectable skipped)");
}

static void test_edge_inputs(void)
{
    struct ListNavItem items[] = {ITEM("Apple"), ITEM("Banana")};
    // empty list returns the selection unchanged
    CHECK_EQ(ListNav_NextLetterIndex(items, 0, 0), 0, "next: empty list no-op");
    CHECK_EQ(ListNav_PrevLetterIndex(items, 0, 5), 5, "prev: empty list returns selected");
    // NULL items returns the selection unchanged
    CHECK_EQ(ListNav_NextLetterIndex(NULL, 3, 1), 1, "next: NULL items no-op");
    // out-of-range selection returns unchanged
    CHECK_EQ(ListNav_NextLetterIndex(items, 2, 9), 9, "next: out-of-range selected no-op");
    CHECK_EQ(ListNav_PrevLetterIndex(items, 2, -1), -1, "prev: negative selected no-op");
}

int main(void)
{
    test_next_basic();
    test_next_lands_on_first_of_group();
    test_prev_basic();
    test_prev_lands_on_first_of_group();
    test_wrap();
    test_single_letter_no_move();
    test_case_insensitive();
    test_skips_headers_and_unselectable();
    test_edge_inputs();

    if (failures == 0)
    {
        printf("ok - all %d checks passed\n", checks);
        return 0;
    }

    fprintf(stderr, "not ok - %d/%d checks failed\n", failures, checks);
    return 1;
}
