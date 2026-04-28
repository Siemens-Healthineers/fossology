/*
 SPDX-FileCopyrightText: © 2026 Siemens AG

 SPDX-License-Identifier: GPL-2.0-only
*/
/**
 * \file
 * \brief Unicode utility functions for FOSSology agents.
 *
 * Provides helpers for reading files and converting byte offsets to
 * UTF-16 code unit offsets, used for consistent highlight positions
 * across agents.
 */

#ifndef LIBFOSSUNICODE_H
#define LIBFOSSUNICODE_H

#include <stdlib.h>
#include <stdint.h>

/**
 * Maximum file size (in bytes) for which offset conversion will be attempted.
 * Files larger than this are assumed to be binary/non-text and conversion
 * is skipped (byte offsets are used as-is).
 */
#define FO_UNICODE_MAX_FILE_SIZE (50 * 1024 * 1024)  /* 50 MB */

/**
 * Sampling interval for the offset table.
 * One sample is recorded every this many bytes during table construction.
 * Lookups scan at most this many bytes after finding the nearest sample.
 */
#define FO_UTF16_SAMPLE_INTERVAL 256

/**
 * @brief A single sample entry in the offset table.
 *
 * Each sample records the cumulative UTF-16 count at a character-aligned
 * byte position.  The bytePos is always at the start of a UTF-8 character
 * and is guaranteed to be <= the nominal sample boundary (k * INTERVAL).
 */
typedef struct {
  size_t utf16Count;   /**< Cumulative UTF-16 code unit count at bytePos */
  size_t bytePos;      /**< Character-aligned byte position (<= k * INTERVAL) */
} FoUtf16Sample;

/**
 * @brief Precomputed sampled byte→UTF-16 offset table.
 *
 * Allows O(1) conversion from byte offsets to UTF-16 code unit offsets
 * by storing cumulative UTF-16 counts at regular byte intervals.
 * A lookup for any byte offset requires scanning at most
 * FO_UTF16_SAMPLE_INTERVAL + 3 bytes from the nearest sample point.
 */
typedef struct {
  FoUtf16Sample* samples; /**< Array of sample entries */
  size_t  sampleCount;    /**< Number of entries in the samples array */
  const unsigned char* buf; /**< Reference to file content (NOT owned) */
  size_t  bufSize;        /**< Size of the file buffer */
} FoUtf16OffsetTable;

/**
 * @brief Read entire file into a malloc'd buffer.
 *
 * @param fileName  Path to file to read
 * @param outSize   Set to number of bytes read on success
 * @return Pointer to malloc'd buffer (caller must free), or NULL on failure
 */
unsigned char* fo_readFileBytes(const char* fileName, size_t* outSize);

/**
 * @brief Convert a byte length to a UTF-16 code unit count.
 *
 * Counts how many UTF-16 code units (UChar16) correspond to the first
 * @p byteLen bytes of a UTF-8 encoded buffer. Characters outside the
 * Basic Multilingual Plane (code points >= U+10000) count as 2 UTF-16
 * code units (surrogate pair).
 *
 * NOTE: For repeated conversions on the same file, use
 * fo_utf16OffsetTable_build() + fo_utf16OffsetTable_lookup() instead,
 * which is O(1) per lookup vs O(byteLen) for this function.
 *
 * @param buf       UTF-8 encoded content
 * @param byteLen   Number of bytes to process
 * @return Number of UTF-16 code units in the first byteLen bytes
 */
size_t fo_utf8ByteLenToUChar16Len(const unsigned char* buf, size_t byteLen);

/**
 * @brief Check whether a buffer is pure ASCII (all bytes < 0x80).
 *
 * When a file is pure ASCII, byte offsets and UTF-16 offsets are identical,
 * so conversion can be skipped entirely for a large performance gain.
 *
 * @param buf       File content buffer
 * @param size      Number of bytes in the buffer
 * @return 1 if all bytes are < 0x80, 0 otherwise
 */
int fo_utf8FileIsAscii(const unsigned char* buf, size_t size);

/**
 * @brief Build a sampled offset table for fast byte→UTF-16 lookups.
 *
 * Scans the file content once (O(n)) and records the cumulative UTF-16
 * count every FO_UTF16_SAMPLE_INTERVAL bytes. Subsequent lookups via
 * fo_utf16OffsetTable_lookup() scan at most FO_UTF16_SAMPLE_INTERVAL
 * bytes, making them effectively O(1).
 *
 * @param buf   UTF-8 encoded file content (must remain valid while table is in use)
 * @param size  Number of bytes in buf
 * @return Pointer to a new table (caller must free with fo_utf16OffsetTable_free),
 *         or NULL on allocation failure
 */
FoUtf16OffsetTable* fo_utf16OffsetTable_build(const unsigned char* buf, size_t size);

/**
 * @brief Look up the UTF-16 code unit offset for a given byte offset.
 *
 * Uses the precomputed sample table for O(1) lookup. Scans at most
 * FO_UTF16_SAMPLE_INTERVAL bytes from the nearest sample point.
 *
 * @param table       Precomputed offset table
 * @param byteOffset  Byte offset to convert
 * @return UTF-16 code unit offset corresponding to byteOffset
 */
size_t fo_utf16OffsetTable_lookup(const FoUtf16OffsetTable* table, size_t byteOffset);

/**
 * @brief Free a precomputed offset table.
 *
 * @param table  Table to free (may be NULL)
 */
void fo_utf16OffsetTable_free(FoUtf16OffsetTable* table);

#endif /* LIBFOSSUNICODE_H */
