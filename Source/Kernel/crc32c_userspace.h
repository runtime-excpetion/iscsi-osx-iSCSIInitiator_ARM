/*!
 * @author		Nareg Sinenian (implementation in crc32c.c by Mark Adler)
 * @file		crc32c.h (userspace version)
 * @date		July 28, 2026
 * @version		1.0
 * @copyright	(c) 2014 Nareg Sinenian. All rights reserved.
 *
 * Userspace-compatible header for Mark Adler's CRC32C computation (crc32c.c)
 * The implementation in crc32c.c is pure C and compiles for both kext and userspace.
 * This header replaces the IOKit-based one (crc32c.h) for userspace targets.
 */

#ifndef __ISCSI_CRC32C_USERSPACE_H__
#define __ISCSI_CRC32C_USERSPACE_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*! Call once to initialize CRC32C. */
void crc32c_init(void);

/*! Computes the CRC32C checksum of data.
 *  @param crc the existing crc for prior data, if any.
 *  @param buffer the buffer to compute
 *  @param length the length of the buffer
 *  @return the new CRC32C checksum. */
uint32_t crc32c(uint32_t crc, const void * buffer, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* __ISCSI_CRC32C_USERSPACE_H__ */
