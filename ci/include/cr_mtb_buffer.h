/* CI-only equivalent of the IDE's trace-buffer declaration macros.
 * Preserve the configured RAM reservation without requiring Redlib headers.
 * No trace driver or production source is replaced.
 */
#ifndef CI_CR_MTB_BUFFER_H
#define CI_CR_MTB_BUFFER_H
#define __CR_MTB_BUFFER(size) \
    unsigned char ci_mtb_buffer[(size)] __attribute__((section(".mtb"), aligned(size), used))
#define __CR_MTB_BUFFER_EXT(size, bank) __CR_MTB_BUFFER(size)
#endif
