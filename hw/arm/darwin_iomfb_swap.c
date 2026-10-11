#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/arm/darwin_iomfb_swap.h"

bool darwin_iomfb_swap_id(const uint8_t *input, size_t size,
                         size_t output_size, uint32_t *id)
{
    /* a0c3730 -> queue-item+0x3d4; a0c9088 copies the record verbatim.
     * a0c90d4 encodes its optional/null flag at wire +0xfea. */
    if (!input || !id || size != DARWIN_IOMFB_SWAP_INPUT_SIZE ||
        output_size != 12 || input[0xfea] != 0) {
        return false;
    }
    *id = ldl_le_p(input + 0x98);
    return true;
}

void darwin_iomfb_swap_completion(uint8_t output[DARWIN_IOMFB_SWAP_COMPLETION_SIZE],
                                 uint32_t id)
{
    /* a0db034 decodes D594, then a0c58bc removes the matching pending ID.
     * Not cancelled, no optional record, zero timing records (count +0x727),
     * and null swap-info (+0x72c). No invented timestamp or refresh rate.
     * The same packet completed a real pending swap in DISPLAY_COMPLETE_R25. */
    memset(output, 0, DARWIN_IOMFB_SWAP_COMPLETION_SIZE);
    stl_le_p(output, id);
    output[0x72c] = 1;
}

void darwin_iomfb_d582_completion(uint8_t output[DARWIN_IOMFB_D582_INPUT_SIZE],
                                  uint32_t id)
{
    /* Current firmware 0x042976a4..0x042976d8: ID at +0, byte +4=1,
     * a four-byte auxiliary value at +5 and byte +9=0. The auxiliary
     * value is not known for this QEMU swap; zero is a labeled hypothesis. */
    memset(output, 0, DARWIN_IOMFB_D582_INPUT_SIZE);
    stl_le_p(output, id);
    output[4] = 1;
}

void darwin_iomfb_d595_completion(uint8_t output[DARWIN_IOMFB_D595_INPUT_SIZE],
                                  uint32_t id)
{
    /* AP 0xa0db26c/0xa0db304: up to 256 IDs at +0, count at +0x400.
     * Firmware 0x0420ddec..0x0420de34 appends active ID to this batch;
     * remaining optional flags/data are zero in this one-ID hypothesis. */
    memset(output, 0, DARWIN_IOMFB_D595_INPUT_SIZE);
    stl_le_p(output, id);
    stl_le_p(output + 0x400, 1);
}

bool darwin_iomfb_a406_reply(uint8_t *out, size_t out_len,
                             uint32_t id, uint32_t status)
{
    /* Firmware A406 (docs/re/iomfb-dseries.md): id@0, status@4, out_len=8.
     * Successful allocation returns a nonzero ID; the generic model zeroes. */
    if (!out || out_len < 8 || id == 0) {
        return false;
    }
    stl_le_p(out, id);
    stl_le_p(out + 4, status);
    return true;
}

/* The recovered A406 path inserts a newly ID'd object into fb+0x3e30
 * (0x04217080, 0x04217144..0x04217198). This opt-in model keeps only the
 * membership/ID needed for the A407 request+0x98 lookup; it does not emulate
 * the firmware object's layout, transfer gate, or completion. */
typedef struct DarwinIOMFBSwapObject {
    uint32_t id;
    uint32_t state; /* request+0x17c -> firmware object+0x3ec at 0x04215e3c */
    bool queued;    /* opt-in assumed transfer, not firmware queue state */
    bool active_assumed;
    bool completion_claimed;
} DarwinIOMFBSwapObject;

bool darwin_iomfb_swap_objects_retain(DarwinIOMFBSwapObjects *objects,
                                     uint32_t id)
{
    DarwinIOMFBSwapObject *object;

    if (!objects || !id) {
        return false;
    }
    if (!objects->by_id) {
        objects->by_id = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                               NULL, g_free);
    }
    if (g_hash_table_contains(objects->by_id, GUINT_TO_POINTER(id))) {
        return false;
    }
    object = g_new0(DarwinIOMFBSwapObject, 1);
    object->id = id;
    g_hash_table_insert(objects->by_id, GUINT_TO_POINTER(id), object);
    return true;
}

bool darwin_iomfb_swap_objects_completion_claim(DarwinIOMFBSwapObjects *objects,
                                                 uint32_t id, bool active_assumed)
{
    DarwinIOMFBSwapObject *object = objects && objects->by_id ?
        g_hash_table_lookup(objects->by_id, GUINT_TO_POINTER(id)) : NULL;

    if (!object || !object->queued || object->completion_claimed) {
        return false;
    }
    object->active_assumed = active_assumed;
    object->completion_claimed = true;
    return true;
}

void darwin_iomfb_swap_objects_completion_abort(DarwinIOMFBSwapObjects *objects,
                                                 uint32_t id)
{
    DarwinIOMFBSwapObject *object = objects && objects->by_id ?
        g_hash_table_lookup(objects->by_id, GUINT_TO_POINTER(id)) : NULL;

    if (object) {
        object->completion_claimed = false;
        object->active_assumed = false;
    }
}

DarwinIOMFBSwapLookup darwin_iomfb_a407_object_lookup(
    const DarwinIOMFBSwapObjects *objects, const uint8_t *input,
    size_t input_size, uint32_t *id)
{
    DarwinIOMFBSwapObject *object;
    uint32_t requested;

    if (!objects || !input || !id || input_size != DARWIN_IOMFB_A407_INPUT_SIZE) {
        return DARWIN_IOMFB_SWAP_LOOKUP_INVALID;
    }
    requested = ldl_le_p(input + 0x98);
    *id = requested;
    object = objects->by_id ?
        g_hash_table_lookup(objects->by_id, GUINT_TO_POINTER(requested)) : NULL;
    return object && object->id == requested ? DARWIN_IOMFB_SWAP_LOOKUP_HIT :
                                                 DARWIN_IOMFB_SWAP_LOOKUP_MISS;
}

DarwinIOMFBSwapLookup darwin_iomfb_a407_object_update(
    DarwinIOMFBSwapObjects *objects, const uint8_t *input,
    size_t input_size, bool assume_transfer, uint32_t *id,
    uint32_t *state, bool *queued)
{
    DarwinIOMFBSwapLookup lookup;
    DarwinIOMFBSwapObject *object;

    if (!state || !queued) {
        return DARWIN_IOMFB_SWAP_LOOKUP_INVALID;
    }
    lookup = darwin_iomfb_a407_object_lookup(objects, input, input_size, id);
    if (lookup != DARWIN_IOMFB_SWAP_LOOKUP_HIT) {
        return lookup;
    }
    object = g_hash_table_lookup(objects->by_id, GUINT_TO_POINTER(*id));
    /* H17P A407 copies this word only after its lookup succeeds. This model
     * deliberately stops before the w25/w21 gate and fb queue machinery. */
    object->state = ldl_le_p(input + 0x17c);
    if (assume_transfer) {
        object->queued = true;
    }
    *state = object->state;
    *queued = object->queued;
    return DARWIN_IOMFB_SWAP_LOOKUP_HIT;
}

void darwin_iomfb_swap_objects_clear(DarwinIOMFBSwapObjects *objects)
{
    if (objects && objects->by_id) {
        g_hash_table_unref(objects->by_id);
        objects->by_id = NULL;
    }
}

/* Shared packed primary descriptor. A408's +0xf90 DVA was byte-matched
 * through disp0/SID0 in VISIBLE_R6; A407_MAPPED_FULL2 has the same descriptor
 * and DVA offsets, but its DVA still needs live translation validation. */
static bool iomfb_primary_surface(const uint8_t *input,
                                  DarwinIOMFBSurface *surface)
{
    const uint8_t *d;
    DarwinIOMFBSurface v;
    uint64_t bytes;
    d = input + 0x6e0;
    if ((uint32_t)ldl_le_p(d + 0xb) != 0x42475241) {
        return false;
    }
    v.width = ldl_le_p(d + 0x21);
    v.height = ldl_le_p(d + 0x25);
    v.stride = ldl_le_p(d + 0x15);
    v.dva = ldq_le_p(input + 0xf90);
    bytes = (uint64_t)v.stride * v.height;
    if (!v.width || !v.height || v.width > 8192 || v.height > 8192 ||
        v.stride < (uint64_t)v.width * 4 || (v.stride & 3) ||
        bytes > 64 * 1024 * 1024 || bytes > (uint32_t)ldl_le_p(d + 0x29) ||
        !v.dva || v.dva > UINT64_MAX - bytes) {
        return false;
    }
    v.size = bytes;
    *surface = v;
    return true;
}

/* Measured packed primary BGRA profile from DISPLAY_SMP6_COMPLETE_R5 A408.
 * This is deliberately not a general multi-plane compositor. */
bool darwin_iomfb_swap_surface(const uint8_t *input, size_t size,
                              DarwinIOMFBSurface *surface)
{
    if (!input || !surface || size != DARWIN_IOMFB_SWAP_INPUT_SIZE ||
        input[0xfea] || input[0xfeb] || input[0xfec] != 1 ||
        input[0xfed] != 1 || input[0xfee] != 1) {
        return false;
    }
    return iomfb_primary_surface(input, surface);
}

bool darwin_iomfb_a407_surface(const uint8_t *input, size_t size,
                              DarwinIOMFBSurface *surface)
{
    /* A407_MAPPED_FULL2 RPC #574: ID 11, selected mask 1, slot-0 word 1,
     * flags 00 01 01 01 at +0x1d23. Reject other selected slots and all
     * null/poisoned profiles rather than inventing a layer compositor. */
    if (!input || !surface || size != DARWIN_IOMFB_A407_INPUT_SIZE ||
        !ldl_le_p(input + 0x98) || ldl_le_p(input + 0x14c) != 1 ||
        !ldl_le_p(input + 0x9c) || ldl_le_p(input + 0xa0) ||
        ldl_le_p(input + 0xa4) || ldl_le_p(input + 0xa8) ||
        input[0x1d23] || input[0x1d24] != 1 ||
        input[0x1d25] != 1 || input[0x1d26] != 1) {
        return false;
    }
    return iomfb_primary_surface(input, surface);
}
