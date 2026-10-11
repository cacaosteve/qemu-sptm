#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/arm/darwin_iomfb_swap.h"

static void native_contract(void)
{
    uint8_t input[DARWIN_IOMFB_SWAP_INPUT_SIZE] = { 0 };
    uint8_t output[DARWIN_IOMFB_SWAP_COMPLETION_SIZE + 8];
    uint32_t id;
    /* A nonzero identifier catches accidentally hardcoded first-frame IDs. */
    stl_le_p(input + 0x98, 0xfedcba98);
    g_assert_true(darwin_iomfb_swap_id(input, sizeof(input), 12, &id));
    g_assert_cmphex(id, ==, 0xfedcba98);
    memset(output, 0xa5, sizeof(output));
    darwin_iomfb_swap_completion(output, id);
    g_assert_cmphex((uint32_t)ldl_le_p(output), ==, id);
    for (size_t i = 4; i < DARWIN_IOMFB_SWAP_COMPLETION_SIZE; i++) {
        g_assert_cmpuint(output[i], ==, i == 0x72c ? 1 : 0);
    }
    for (size_t i = DARWIN_IOMFB_SWAP_COMPLETION_SIZE; i < sizeof(output); i++) {
        g_assert_cmpuint(output[i], ==, 0xa5);
    }
}

static void a406_reply_contract(void)
{
    uint8_t out[16];
    memset(out, 0xa5, sizeof(out));
    g_assert_false(darwin_iomfb_a406_reply(out, 8, 0, 0));
    g_assert_false(darwin_iomfb_a406_reply(out, 7, 1, 0));
    g_assert_false(darwin_iomfb_a406_reply(NULL, 8, 1, 0));
    g_assert_cmpuint(out[0], ==, 0xa5);
    g_assert_true(darwin_iomfb_a406_reply(out, 8, 0x42, 0));
    g_assert_cmphex((uint32_t)ldl_le_p(out), ==, 0x42);
    g_assert_cmphex((uint32_t)ldl_le_p(out + 4), ==, 0);
    for (size_t i = 8; i < sizeof(out); i++) {
        g_assert_cmpuint(out[i], ==, 0xa5);
    }
    g_assert_true(darwin_iomfb_a406_reply(out, sizeof(out), 7, 0x11));
    g_assert_cmphex((uint32_t)ldl_le_p(out), ==, 7);
    g_assert_cmphex((uint32_t)ldl_le_p(out + 4), ==, 0x11);
}

static void a406_object_a407_lookup(void)
{
    DarwinIOMFBSwapObjects objects = { 0 };
    uint8_t input[DARWIN_IOMFB_A407_INPUT_SIZE] = { 0 };
    uint8_t out[8] = { 0 };
    uint32_t id = UINT32_MAX;

    g_assert_true(darwin_iomfb_a406_reply(out, sizeof(out), 1, 0));
    g_assert_cmpuint(ldl_le_p(out), ==, 1);
    g_assert_cmpuint(ldl_le_p(out + 4), ==, 0);
    stl_le_p(input + 0x98, ldl_le_p(out));

    g_assert_cmpint(darwin_iomfb_a407_object_lookup(
        &objects, input, sizeof(input), &id), ==, DARWIN_IOMFB_SWAP_LOOKUP_MISS);
    g_assert_cmpuint(id, ==, 1);
    g_assert_false(darwin_iomfb_swap_objects_retain(&objects, 0));
    g_assert_true(darwin_iomfb_swap_objects_retain(&objects, 1));
    g_assert_false(darwin_iomfb_swap_objects_retain(&objects, 1));
    g_assert_cmpint(darwin_iomfb_a407_object_lookup(
        &objects, input, sizeof(input), &id), ==, DARWIN_IOMFB_SWAP_LOOKUP_HIT);
    g_assert_cmpuint(id, ==, 1);

    stl_le_p(input + 0x98, 2);
    g_assert_cmpint(darwin_iomfb_a407_object_lookup(
        &objects, input, sizeof(input), &id), ==, DARWIN_IOMFB_SWAP_LOOKUP_MISS);
    g_assert_cmpuint(id, ==, 2);
    g_assert_true(darwin_iomfb_swap_objects_retain(&objects, 2));
    g_assert_cmpint(darwin_iomfb_a407_object_lookup(
        &objects, input, sizeof(input), &id), ==, DARWIN_IOMFB_SWAP_LOOKUP_HIT);
    g_assert_cmpuint(id, ==, 2);
    g_assert_cmpint(darwin_iomfb_a407_object_lookup(
        &objects, input, sizeof(input) - 1, &id), ==,
        DARWIN_IOMFB_SWAP_LOOKUP_INVALID);
    g_assert_cmpint(darwin_iomfb_a407_object_lookup(
        &objects, NULL, sizeof(input), &id), ==,
        DARWIN_IOMFB_SWAP_LOOKUP_INVALID);
    g_assert_cmpint(darwin_iomfb_a407_object_lookup(
        &objects, input, sizeof(input), NULL), ==,
        DARWIN_IOMFB_SWAP_LOOKUP_INVALID);

    darwin_iomfb_swap_objects_clear(&objects);
    stl_le_p(input + 0x98, 1);
    g_assert_cmpint(darwin_iomfb_a407_object_lookup(
        &objects, input, sizeof(input), &id), ==, DARWIN_IOMFB_SWAP_LOOKUP_MISS);
}

static void a407_object_state_and_assumed_transfer(void)
{
    DarwinIOMFBSwapObjects objects = { 0 };
    uint8_t input[DARWIN_IOMFB_A407_INPUT_SIZE] = { 0 };
    uint32_t id = 0;
    uint32_t state = UINT32_MAX;
    bool queued = true;

    g_assert_true(darwin_iomfb_swap_objects_retain(&objects, 1));
    stl_le_p(input + 0x98, 1);
    stl_le_p(input + 0x17c, 0x15);
    g_assert_cmpint(darwin_iomfb_a407_object_update(
        &objects, input, sizeof(input), false, &id, &state, &queued), ==,
        DARWIN_IOMFB_SWAP_LOOKUP_HIT);
    g_assert_cmpuint(id, ==, 1);
    g_assert_cmphex(state, ==, 0x15);
    g_assert_false(queued);

    stl_le_p(input + 0x17c, 0);
    g_assert_cmpint(darwin_iomfb_a407_object_update(
        &objects, input, sizeof(input), true, &id, &state, &queued), ==,
        DARWIN_IOMFB_SWAP_LOOKUP_HIT);
    g_assert_cmpuint(id, ==, 1);
    g_assert_cmphex(state, ==, 0);
    g_assert_true(queued);

    stl_le_p(input + 0x98, 2);
    state = UINT32_MAX;
    queued = false;
    g_assert_cmpint(darwin_iomfb_a407_object_update(
        &objects, input, sizeof(input), true, &id, &state, &queued), ==,
        DARWIN_IOMFB_SWAP_LOOKUP_MISS);
    g_assert_cmpuint(id, ==, 2);
    g_assert_cmpuint(state, ==, UINT32_MAX);
    g_assert_false(queued);

    stl_le_p(input + 0x98, 1);
    g_assert_cmpint(darwin_iomfb_a407_object_update(
        &objects, input, sizeof(input), false, &id, &state, &queued), ==,
        DARWIN_IOMFB_SWAP_LOOKUP_HIT);
    g_assert_true(queued);
    g_assert_cmpint(darwin_iomfb_a407_object_update(
        &objects, input, sizeof(input) - 1, true, &id, &state, &queued), ==,
        DARWIN_IOMFB_SWAP_LOOKUP_INVALID);
    darwin_iomfb_swap_objects_clear(&objects);
}

static void a407_hypothesis_payloads_and_gate(void)
{
    DarwinIOMFBSwapObjects objects = { 0 };
    uint8_t request[DARWIN_IOMFB_A407_INPUT_SIZE] = { 0 };
    uint8_t d582[DARWIN_IOMFB_D582_INPUT_SIZE + 4];
    uint8_t d595[DARWIN_IOMFB_D595_INPUT_SIZE + 4];
    uint32_t id = 0, state = UINT32_MAX;
    bool queued = false;

    memset(d582, 0xa5, sizeof(d582));
    darwin_iomfb_d582_completion(d582, 0x1234);
    g_assert_cmpuint(ldl_le_p(d582), ==, 0x1234);
    g_assert_cmpuint(d582[4], ==, 1);
    for (size_t i = 5; i < DARWIN_IOMFB_D582_INPUT_SIZE; i++) {
        g_assert_cmpuint(d582[i], ==, 0);
    }
    for (size_t i = DARWIN_IOMFB_D582_INPUT_SIZE; i < sizeof(d582); i++) {
        g_assert_cmpuint(d582[i], ==, 0xa5);
    }

    memset(d595, 0xa5, sizeof(d595));
    darwin_iomfb_d595_completion(d595, 0x1234);
    g_assert_cmpuint(ldl_le_p(d595), ==, 0x1234);
    g_assert_cmpuint(ldl_le_p(d595 + 0x400), ==, 1);
    for (size_t i = 4; i < DARWIN_IOMFB_D595_INPUT_SIZE; i++) {
        if (i >= 0x400 && i < 0x404) {
            continue;
        }
        g_assert_cmpuint(d595[i], ==, 0);
    }
    for (size_t i = DARWIN_IOMFB_D595_INPUT_SIZE; i < sizeof(d595); i++) {
        g_assert_cmpuint(d595[i], ==, 0xa5);
    }

    g_assert_true(darwin_iomfb_swap_objects_retain(&objects, 1));
    g_assert_false(darwin_iomfb_swap_objects_completion_claim(&objects, 1,
                                                               false));
    stl_le_p(request + 0x98, 1);
    g_assert_cmpint(darwin_iomfb_a407_object_update(
        &objects, request, sizeof(request), true, &id, &state, &queued), ==,
        DARWIN_IOMFB_SWAP_LOOKUP_HIT);
    g_assert_true(queued);
    g_assert_false(darwin_iomfb_swap_objects_completion_claim(&objects, 2,
                                                               false));
    g_assert_true(darwin_iomfb_swap_objects_completion_claim(&objects, 1,
                                                              false));
    g_assert_false(darwin_iomfb_swap_objects_completion_claim(&objects, 1,
                                                               true));
    darwin_iomfb_swap_objects_completion_abort(&objects, 1);
    g_assert_true(darwin_iomfb_swap_objects_completion_claim(&objects, 1,
                                                              true));
    darwin_iomfb_swap_objects_clear(&objects);
}

static void reject_unsupported(void)
{
    uint8_t input[DARWIN_IOMFB_SWAP_INPUT_SIZE + 1] = { 0 };
    uint32_t id = 0xabcdef;
    for (size_t n = 0; n <= sizeof(input); n++) {
        if (n != DARWIN_IOMFB_SWAP_INPUT_SIZE) {
            g_assert_false(darwin_iomfb_swap_id(input, n, 12, &id));
        }
    }
    input[0xfea] = 1;
    g_assert_false(darwin_iomfb_swap_id(input, DARWIN_IOMFB_SWAP_INPUT_SIZE, 12, &id));
    input[0xfea] = 0;
    g_assert_false(darwin_iomfb_swap_id(input, DARWIN_IOMFB_SWAP_INPUT_SIZE, 0, &id));
    g_assert_false(darwin_iomfb_swap_id(NULL, DARWIN_IOMFB_SWAP_INPUT_SIZE, 12, &id));
    g_assert_false(darwin_iomfb_swap_id(input, DARWIN_IOMFB_SWAP_INPUT_SIZE, 12, NULL));
    g_assert_cmphex(id, ==, 0xabcdef);
}

static void scanout_contract(void)
{
    uint8_t input[DARWIN_IOMFB_SWAP_INPUT_SIZE] = { 0 };
    DarwinIOMFBSurface v;
    uint8_t *d = input + 0x6e0;
    input[0xfec] = input[0xfed] = input[0xfee] = 1;
    stl_le_p(d + 0xb, 0x42475241);
    stl_le_p(d + 0x15, 4864);
    stl_le_p(d + 0x21, 1179);
    stl_le_p(d + 0x25, 2556);
    stl_le_p(d + 0x29, 12435456);
    stq_le_p(input + 0xf90, 0x10000000000ULL);
    g_assert_true(darwin_iomfb_swap_surface(input, sizeof(input), &v));
    g_assert_cmpuint(v.size, ==, 12432384);
    g_assert_cmpuint(v.width, ==, 1179);
    g_assert_cmphex(v.dva, ==, 0x10000000000ULL);
    g_assert_false(darwin_iomfb_swap_surface(input, sizeof(input) - 1, &v));
    input[0xfec] = 0;
    g_assert_false(darwin_iomfb_swap_surface(input, sizeof(input), &v));
    input[0xfec] = 1;
    stl_le_p(d + 0xb, 0x41524742);
    g_assert_false(darwin_iomfb_swap_surface(input, sizeof(input), &v));
    stl_le_p(d + 0xb, 0x42475241);
    stl_le_p(d + 0x29, 1);
    g_assert_false(darwin_iomfb_swap_surface(input, sizeof(input), &v));
    stl_le_p(d + 0x29, UINT32_MAX);
    stl_le_p(d + 0x15, UINT32_MAX - 3);
    g_assert_false(darwin_iomfb_swap_surface(input, sizeof(input), &v));
    stl_le_p(d + 0x15, 4864);
    stq_le_p(input + 0xf90, UINT64_MAX - 16);
    g_assert_false(darwin_iomfb_swap_surface(input, sizeof(input), &v));
}

static void a407_mapped_surface_contract(void)
{
    const char *path = g_test_get_filename(
        G_TEST_DIST, "fixtures", "a407-mapped-full2-input.bin", NULL);
    g_autofree char *data = NULL;
    size_t len = 0;
    DarwinIOMFBSurface surface;
    GError *error = NULL;

    g_assert_true(g_file_get_contents(path, &data, &len, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(len, ==, DARWIN_IOMFB_A407_INPUT_SIZE);
    g_assert_true(darwin_iomfb_a407_surface((uint8_t *)data, len, &surface));
    g_assert_cmpuint(surface.width, ==, 1179);
    g_assert_cmpuint(surface.height, ==, 2556);
    g_assert_cmpuint(surface.stride, ==, 4864);
    g_assert_cmpuint(surface.size, ==, 12432384);
    g_assert_cmphex(surface.dva, ==, 0x10000000000ULL);
    g_assert_false(darwin_iomfb_a407_surface((uint8_t *)data, len - 1,
                                               &surface));

    data[0x1d23] = 1;
    g_assert_false(darwin_iomfb_a407_surface((uint8_t *)data, len, &surface));
    data[0x1d23] = 0;
    data[0x1d24] = 0;
    g_assert_false(darwin_iomfb_a407_surface((uint8_t *)data, len, &surface));
    data[0x1d24] = 1;
    stl_le_p(data + 0x14c, 3);
    g_assert_false(darwin_iomfb_a407_surface((uint8_t *)data, len, &surface));
    stl_le_p(data + 0x14c, 1);
    stl_le_p(data + 0x9c, 0);
    g_assert_false(darwin_iomfb_a407_surface((uint8_t *)data, len, &surface));
    stl_le_p(data + 0x9c, 1);
    stq_le_p(data + 0xf90, 0);
    g_assert_false(darwin_iomfb_a407_surface((uint8_t *)data, len, &surface));
    stq_le_p(data + 0xf90, 0x10000000000ULL);
    stl_le_p(data + 0x6e0 + 0x29, 1);
    g_assert_false(darwin_iomfb_a407_surface((uint8_t *)data, len, &surface));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/darwin-iomfb/swap/native-contract", native_contract);
    g_test_add_func("/darwin-iomfb/swap/a406-reply-contract", a406_reply_contract);
    g_test_add_func("/darwin-iomfb/swap/a406-object-a407-lookup",
                    a406_object_a407_lookup);
    g_test_add_func("/darwin-iomfb/swap/a407-object-state-transfer",
                    a407_object_state_and_assumed_transfer);
    g_test_add_func("/darwin-iomfb/swap/a407-hypothesis-payloads-gate",
                    a407_hypothesis_payloads_and_gate);
    g_test_add_func("/darwin-iomfb/swap/reject-unsupported", reject_unsupported);
    g_test_add_func("/darwin-iomfb/swap/scanout-contract", scanout_contract);
    g_test_add_func("/darwin-iomfb/swap/a407-mapped-surface-contract",
                    a407_mapped_surface_contract);
    return g_test_run();
}
