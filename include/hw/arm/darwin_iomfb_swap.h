/* iOS 27 (24A5430a) A408/D594 wire contract; see surface-cache-and-completion.md. */
#ifndef HW_ARM_DARWIN_IOMFB_SWAP_H
#define HW_ARM_DARWIN_IOMFB_SWAP_H
#include "qemu/osdep.h"
#define DARWIN_IOMFB_SWAP_INPUT_SIZE 0xff4
#define DARWIN_IOMFB_A407_INPUT_SIZE 0x1d30
#define DARWIN_IOMFB_SWAP_COMPLETION_SIZE 0x730
#define DARWIN_IOMFB_D582_INPUT_SIZE 0x0c
#define DARWIN_IOMFB_D595_INPUT_SIZE 0x2708
#define DARWIN_IOMFB_D595_OUTPUT_SIZE 0x400
bool darwin_iomfb_swap_id(const uint8_t *input, size_t size,
                         size_t output_size, uint32_t *id);
void darwin_iomfb_swap_completion(uint8_t output[DARWIN_IOMFB_SWAP_COMPLETION_SIZE],
                                 uint32_t id);
void darwin_iomfb_d582_completion(uint8_t output[DARWIN_IOMFB_D582_INPUT_SIZE],
                                  uint32_t id);
void darwin_iomfb_d595_completion(uint8_t output[DARWIN_IOMFB_D595_INPUT_SIZE],
                                  uint32_t id);
/* A406 out: u32 id @0, u32 status @4. Rejects id==0; probe allocates >=1. */
bool darwin_iomfb_a406_reply(uint8_t *out, size_t out_len,
                             uint32_t id, uint32_t status);
/* A406/A407 object-membership experiment; no firmware policy is executed. */
typedef struct DarwinIOMFBSwapObjects {
    GHashTable *by_id;
} DarwinIOMFBSwapObjects;
typedef enum DarwinIOMFBSwapLookup {
    DARWIN_IOMFB_SWAP_LOOKUP_INVALID,
    DARWIN_IOMFB_SWAP_LOOKUP_MISS,
    DARWIN_IOMFB_SWAP_LOOKUP_HIT,
} DarwinIOMFBSwapLookup;
bool darwin_iomfb_swap_objects_retain(DarwinIOMFBSwapObjects *objects,
                                     uint32_t id);
DarwinIOMFBSwapLookup darwin_iomfb_a407_object_lookup(
    const DarwinIOMFBSwapObjects *objects, const uint8_t *input,
    size_t input_size, uint32_t *id);
/* On a retained HIT only, copy request+0x17c to minimal object state.
 * assume_transfer records an unmeasured w21=0 assumption; it never completes
 * the swap or sends a D-series callback. */
DarwinIOMFBSwapLookup darwin_iomfb_a407_object_update(
    DarwinIOMFBSwapObjects *objects, const uint8_t *input,
    size_t input_size, bool assume_transfer, uint32_t *id,
    uint32_t *state, bool *queued);
/* Hypothesis-only emit gate: requires retained queued ID, once per object.
 * active_assumed is a QEMU mark, not a measured firmware promotion. */
bool darwin_iomfb_swap_objects_completion_claim(DarwinIOMFBSwapObjects *objects,
                                                 uint32_t id, bool active_assumed);
void darwin_iomfb_swap_objects_completion_abort(DarwinIOMFBSwapObjects *objects,
                                                 uint32_t id);
void darwin_iomfb_swap_objects_clear(DarwinIOMFBSwapObjects *objects);
typedef struct DarwinIOMFBSurface {
    uint32_t width, height, stride, size;
    uint64_t dva;
} DarwinIOMFBSurface;
bool darwin_iomfb_swap_surface(const uint8_t *input, size_t size,
                               DarwinIOMFBSurface *surface);
/* Measured single-primary A407 body from A407_MAPPED_FULL2. This parser only
 * extracts a QEMU scanout candidate; it does not evaluate firmware w25/w21. */
bool darwin_iomfb_a407_surface(const uint8_t *input, size_t size,
                              DarwinIOMFBSurface *surface);
#endif
