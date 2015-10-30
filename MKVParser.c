/*
 * Copyright 2014, 2015 Martin Alexander Wilhelmsen
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>

#include "MKVParser.h"

#define EBML_INTERNAL_SAFE(stmt)                                               \
  do {                                                                         \
    if (stmt == EBML_ERROR) {                                                  \
      fprintf(stderr, #stmt " FAILED AT " __FILE__ " %d\n", __LINE__);         \
      return EBML_ERROR;                                                       \
    }                                                                          \
  } while (0)

/**
 * An unsafe function for reading 8 bits from the EBMLData source
 */
static inline uint8_t ebmlRead8(EBMLData *data) {
  uint8_t v = *data->pos;
  data->pos++;
  data->left--;
  return v;
}

/**
 * Function for checking if the source includes len number of
 *  bytes to be read. This enables the use of ebmlRead8
 *  in inner loops where the length is known.
 *
 * Returns 0 if the source DOES NOT include enough bytes
 */
static inline EBMLStatus ebmlCheckLength(EBMLData *data, size_t len) {
  return (data->left >= len);
}

static inline int ebmlEmpty(EBMLData *data) { return data->left == 0; }

EBMLData ebmlInit(const void *data, size_t len) {
  struct EBMLData r;
  r.org = r.pos = data;
  r.len = len;
  r.left = len;
  return r;
}

static EBMLStatus ebmlReadIntLen(EBMLData *data, int *len) {
  EBML_INTERNAL_SAFE(ebmlCheckLength(data, 1));

  // we can not increase the pos pointer as this
  //  byte may contain more than just the length
  uint8_t d = *data->pos;

  if (d == 1) {
    *len = 8;
    return EBML_OK;
  }

  uint8_t v0 = 0xFF, v1 = 0x01;
  for (int i = 1; i <= 7; i++) {
    if ((d & (v0 << i)) == (v1 << i)) {
      *len = 8 - i;

      return EBML_OK;
    }
  }

  return EBML_ERROR;
}

static EBMLStatus ebmlUnserialize(EBMLData *data, int len, uint64_t *val) {
  EBML_INTERNAL_SAFE(ebmlCheckLength(data, len));

  uint64_t ret = 0;

  for (int i = 0; i < len; i++) {
    ret <<= 8;
    ret |= ebmlRead8(data);
  }

  *val = ret;

  return EBML_OK;
}

static EBMLStatus ebmlReadTag(EBMLData *data, EBMLTag *tag) {
  int len;

  EBML_INTERNAL_SAFE(ebmlReadIntLen(data, &len));
  return ebmlUnserialize(data, len, tag);
}

static EBMLStatus ebmlUnserailizeUnsignedInt(EBMLData *data, uint64_t *val) {
  int len;

  EBML_INTERNAL_SAFE(ebmlReadIntLen(data, &len));
  EBML_INTERNAL_SAFE(ebmlCheckLength(data, len));

  uint64_t ret = 0;

  if (len != 8) {
    ret = ebmlRead8(data);
    ret &= 0xFF >> len;
  } else {
    // skip
    ebmlRead8(data);
  }

  for (int i = 1; i < len; i++) {
    ret <<= 8;
    ret |= ((uint64_t)ebmlRead8(data));
  }

  *val = ret;

  return EBML_OK;
}

static EBMLStatus ebmlReadSubElement(EBMLData *data, EBMLTag *tag,
                                     EBMLData *subEl) {
  EBML_INTERNAL_SAFE(ebmlReadTag(data, tag));

  uint64_t elementLength;
  EBML_INTERNAL_SAFE(ebmlUnserailizeUnsignedInt(data, &elementLength));
  EBML_INTERNAL_SAFE(ebmlCheckLength(data, elementLength));

  *subEl = ebmlInit(data->pos, elementLength);
  data->pos += elementLength;
  data->left -= elementLength;

  return EBML_OK;
}

/////////////////////////////////////////////////////////////////////////////////

static EBMLStatus mkvClusterHandleTimecode(MKVCluster *cluster,
                                           EBMLData *data) {
  uint64_t size;

  EBML_INTERNAL_SAFE(ebmlUnserailizeUnsignedInt(data, &size));
  assert(size > 0 && size <= 8);

  uint64_t timecode;
  EBML_INTERNAL_SAFE(ebmlUnserialize(data, (int)size, &timecode));

  cluster->timecode = timecode;

  return EBML_OK;
}

static EBMLStatus mkvClusterHandleSimpleBlock(MKVCluster *cluster,
                                              EBMLData *data) {
  size_t i = cluster->n_blocks;
  if (i >= cluster->allocated_blocks)
    return EBML_ERROR;

  uint64_t elementLength;
  EBML_INTERNAL_SAFE(ebmlUnserailizeUnsignedInt(data, &elementLength));

  EBML_INTERNAL_SAFE(ebmlCheckLength(data, elementLength));

  uint64_t track;
  uint64_t timecode;
  uint64_t flags;

  const uint8_t *before_header = data->pos;

  EBML_INTERNAL_SAFE(ebmlUnserailizeUnsignedInt(data, &track));
  EBML_INTERNAL_SAFE(ebmlUnserialize(data, 2, &timecode));
  EBML_INTERNAL_SAFE(ebmlUnserialize(data, 1, &flags));

  const uint8_t *after_header = data->pos;
  size_t binaryLength = elementLength - (after_header - before_header);

  cluster->blocks[i].track = (unsigned int)track;
  cluster->blocks[i].timecode = (unsigned int)timecode;
  cluster->blocks[i].flags = (unsigned int)flags;
  cluster->blocks[i].data = data->pos;
  cluster->blocks[i].size = binaryLength;

  data->pos += binaryLength;
  data->left -= binaryLength;

  cluster->n_blocks++;

  return EBML_OK;
}

static EBMLStatus mkvClusterSkipSubElement(MKVCluster *cluster,
                                           EBMLData *data) {
  uint64_t elementLength;
  EBML_INTERNAL_SAFE(ebmlUnserailizeUnsignedInt(data, &elementLength));
  EBML_INTERNAL_SAFE(ebmlCheckLength(data, elementLength));

  data->pos += elementLength;
  data->left -= elementLength;

  return EBML_OK;
}

static EBMLStatus mkvClusterSkipInteger(MKVCluster *cluster, EBMLData *data) {
  uint64_t size;

  EBML_INTERNAL_SAFE(ebmlUnserailizeUnsignedInt(data, &size));
  assert(size > 0 && size <= 8);

  uint64_t dummy;
  EBML_INTERNAL_SAFE(ebmlUnserialize(data, (int)size, &dummy));

  return EBML_OK;
}

MKVCluster *mkvClusterInit(size_t maxSimpleBlocks) {
  MKVCluster *cluster =
      malloc(sizeof(MKVCluster) + maxSimpleBlocks * sizeof(MKVSimpleBlock));
  if (cluster == NULL) {
    return NULL;
  }

  cluster->allocated_blocks = maxSimpleBlocks;
  cluster->n_blocks = 0;
  cluster->timecode = 0;

  return cluster;
}

void mkvClusterDestroy(MKVCluster *cluster) { free(cluster); }

EBMLStatus mkvClusterParse(MKVCluster *cluster, EBMLData *data) {
  enum {
    TagCluster = 0x1F43B675,
    TagTimecode = 0xE7,
    TagSimpleBlock = 0xA3,
    TagSilentTracks = 0x5854,
    TagPosition = 0xA7,
    TagPrevSize = 0xAB,
    TagBlockGroup = 0xA0,
  };

  EBMLTag clusterTag;
  EBMLData subEl;
  EBML_INTERNAL_SAFE(ebmlReadSubElement(data, &clusterTag, &subEl));
  if (clusterTag != TagCluster) {
    return EBML_ERROR;
  }

  struct {
    EBMLTag tag;
    EBMLStatus (*handler)(MKVCluster *cluster, EBMLData *data);
  } actions[] = {
      {TagTimecode, mkvClusterHandleTimecode},
      {TagSimpleBlock, mkvClusterHandleSimpleBlock},
      {TagSilentTracks, mkvClusterSkipSubElement},
      {TagPosition, mkvClusterSkipInteger},
      {TagPrevSize, mkvClusterSkipInteger},
      {TagBlockGroup, mkvClusterSkipSubElement},
  };

  EBMLTag iterTag;
  int foundHandler;

  while (!ebmlEmpty(&subEl)) {
    EBML_INTERNAL_SAFE(ebmlReadTag(&subEl, &iterTag));

    foundHandler = 0;
    for (int i = 0; i < sizeof actions / sizeof actions[0]; i++) {
      if (iterTag == actions[i].tag) {
        EBML_INTERNAL_SAFE(actions[i].handler(cluster, &subEl));
        foundHandler = 1;
      }
    }

    if (foundHandler == 0) {
      fprintf(stderr, "No handler for: %llx.\n", iterTag);
      fprintf(stderr, "Left: %zu.\n", subEl.left);
      return EBML_ERROR;
    }
  }

  return EBML_OK;
}
