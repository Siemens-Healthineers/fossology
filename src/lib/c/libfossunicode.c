/*
 SPDX-FileCopyrightText: © 2026 Siemens AG

 SPDX-License-Identifier: GPL-2.0-only
*/
/**
 * \file
 * \brief Unicode utility functions for FOSSology agents.
 */

#include "libfossunicode.h"

#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/* Forward declaration of internal helper */
static size_t countUtf16Units(const unsigned char* buf, size_t len);

unsigned char* fo_readFileBytes(const char* fileName, size_t* outSize)
{
  *outSize = 0;

  if (!fileName)
    return NULL;

  int fd = open(fileName, O_RDONLY);
  if (fd < 0)
    return NULL;

  struct stat st;
  if (fstat(fd, &st) != 0 || st.st_size <= 0)
  {
    close(fd);
    return NULL;
  }

  size_t fileSize = (size_t)st.st_size;
  unsigned char* buf = (unsigned char*)malloc(fileSize);
  if (!buf)
  {
    close(fd);
    return NULL;
  }

  size_t totalRead = 0;
  while (totalRead < fileSize)
  {
    ssize_t n = read(fd, buf + totalRead, fileSize - totalRead);
    if (n <= 0)
      break;
    totalRead += (size_t)n;
  }
  close(fd);

  if (totalRead != fileSize)
  {
    free(buf);
    return NULL;
  }

  *outSize = fileSize;
  return buf;
}

size_t fo_utf8ByteLenToUChar16Len(const unsigned char* buf, size_t byteLen)
{
  return countUtf16Units(buf, byteLen);
}

int fo_utf8FileIsAscii(const unsigned char* buf, size_t size)
{
  for (size_t i = 0; i < size; i++)
  {
    if (buf[i] >= 0x80)
      return 0;
  }
  return 1;
}

/**
 * @brief Internal: count UTF-16 code units for a slice of UTF-8 data.
 *
 * Scans buf[0..len) and returns the number of UTF-16 code units.
 * Used by both the one-shot function and the table builder.
 */
static size_t countUtf16Units(const unsigned char* buf, size_t len)
{
  size_t uchar16Count = 0;
  size_t i = 0;

  while (i < len)
  {
    unsigned char byte = buf[i];
    int seqLen;
    uint32_t codePoint;

    if (byte < 0x80)
    {
      seqLen = 1;
      codePoint = byte;
    }
    else if ((byte & 0xE0) == 0xC0)
    {
      seqLen = 2;
      codePoint = byte & 0x1F;
    }
    else if ((byte & 0xF0) == 0xE0)
    {
      seqLen = 3;
      codePoint = byte & 0x0F;
    }
    else if ((byte & 0xF8) == 0xF0)
    {
      seqLen = 4;
      codePoint = byte & 0x07;
    }
    else
    {
      i++;
      uchar16Count++;
      continue;
    }

    if (i + (size_t)seqLen > len)
      break;

    for (int j = 1; j < seqLen; j++)
    {
      if ((buf[i + j] & 0xC0) != 0x80)
      {
        seqLen = 1;
        codePoint = byte;
        break;
      }
      codePoint = (codePoint << 6) | (buf[i + j] & 0x3F);
    }

    i += (size_t)seqLen;

    if (codePoint >= 0x10000)
      uchar16Count += 2;
    else
      uchar16Count += 1;
  }

  return uchar16Count;
}

FoUtf16OffsetTable* fo_utf16OffsetTable_build(const unsigned char* buf, size_t size)
{
  FoUtf16OffsetTable* table = (FoUtf16OffsetTable*)malloc(sizeof(FoUtf16OffsetTable));
  if (!table)
    return NULL;

  size_t numSamples = (size / FO_UTF16_SAMPLE_INTERVAL) + 1;
  table->samples = (FoUtf16Sample*)malloc(numSamples * sizeof(FoUtf16Sample));
  if (!table->samples)
  {
    free(table);
    return NULL;
  }

  table->sampleCount = numSamples;
  table->buf = buf;
  table->bufSize = size;

  /* Build the table: scan the file once, recording cumulative UTF-16 count
   * at character-aligned byte positions near each sample interval boundary.
   *
   * Key invariant: samples[k].bytePos is always at the start of a UTF-8
   * character (or at EOF), so the lookup can safely scan forward from there.
   * samples[k].bytePos <= k * INTERVAL always holds. */
  size_t uchar16Count = 0;
  size_t sampleIdx = 0;
  size_t nextSampleByte = 0;
  size_t i = 0;

  while (i < size)
  {
    /* At the start of each character: emit samples for boundaries <= i */
    while (sampleIdx < numSamples && nextSampleByte <= i)
    {
      table->samples[sampleIdx].utf16Count = uchar16Count;
      table->samples[sampleIdx].bytePos = i;
      sampleIdx++;
      nextSampleByte += FO_UTF16_SAMPLE_INTERVAL;
    }

    unsigned char byte = buf[i];
    int seqLen;
    uint32_t codePoint;

    if (byte < 0x80)
    {
      seqLen = 1;
      codePoint = byte;
    }
    else if ((byte & 0xE0) == 0xC0)
    {
      seqLen = 2;
      codePoint = byte & 0x1F;
    }
    else if ((byte & 0xF0) == 0xE0)
    {
      seqLen = 3;
      codePoint = byte & 0x0F;
    }
    else if ((byte & 0xF8) == 0xF0)
    {
      seqLen = 4;
      codePoint = byte & 0x07;
    }
    else
    {
      /* Invalid lead byte (e.g. continuation byte without lead): treat as 1 unit */
      i++;
      uchar16Count++;
      continue;
    }

    /* Check if full sequence fits in the buffer */
    if (i + (size_t)seqLen > size)
      break;

    /* Validate continuation bytes — downgrade to 1-byte invalid if any fail */
    for (int j = 1; j < seqLen; j++)
    {
      if ((buf[i + j] & 0xC0) != 0x80)
      {
        seqLen = 1;
        codePoint = byte;
        break;
      }
      codePoint = (codePoint << 6) | (buf[i + j] & 0x3F);
    }

    /* After validation, seqLen is final. Emit samples for boundaries that
     * fall strictly inside this character (the char straddles them, so it
     * is NOT counted for those boundaries). */
    while (sampleIdx < numSamples && nextSampleByte < i + (size_t)seqLen)
    {
      table->samples[sampleIdx].utf16Count = uchar16Count;
      table->samples[sampleIdx].bytePos = i;
      sampleIdx++;
      nextSampleByte += FO_UTF16_SAMPLE_INTERVAL;
    }

    /* Advance past the character */
    i += (size_t)seqLen;

    if (codePoint >= 0x10000)
      uchar16Count += 2;
    else
      uchar16Count += 1;
  }

  /* Fill remaining samples at EOF position */
  while (sampleIdx < numSamples)
  {
    table->samples[sampleIdx].utf16Count = uchar16Count;
    table->samples[sampleIdx].bytePos = i;
    sampleIdx++;
  }

  return table;
}

size_t fo_utf16OffsetTable_lookup(const FoUtf16OffsetTable* table, size_t byteOffset)
{
  if (!table || !table->samples)
    return byteOffset; /* fallback: identity */

  if (byteOffset >= table->bufSize)
    byteOffset = table->bufSize;

  /* Find the nearest sample at or before byteOffset */
  size_t sampleIdx = byteOffset / FO_UTF16_SAMPLE_INTERVAL;
  if (sampleIdx >= table->sampleCount)
    sampleIdx = table->sampleCount - 1;

  size_t baseBytePos = table->samples[sampleIdx].bytePos;
  size_t baseUtf16   = table->samples[sampleIdx].utf16Count;

  /* Scan from the character-aligned sample position to byteOffset.
   * Since baseBytePos is always at a character start, countUtf16Units
   * will correctly parse multi-byte sequences from there. */
  if (byteOffset > baseBytePos)
  {
    baseUtf16 += countUtf16Units(table->buf + baseBytePos, byteOffset - baseBytePos);
  }

  return baseUtf16;
}

void fo_utf16OffsetTable_free(FoUtf16OffsetTable* table)
{
  if (table)
  {
    free(table->samples);
    free(table);
  }
}
