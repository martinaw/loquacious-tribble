#ifndef MKVPARSER_H
#define MKVPARSER_H

#include <stddef.h>
#include <stdint.h>

typedef struct EBMLData {
  const uint8_t *pos;
  const uint8_t *org;
  size_t len;
  size_t left;
} EBMLData;

typedef struct MKVSimpleBlock {
  unsigned int track;
  unsigned int timecode;
  unsigned int flags;

  size_t size;
  const uint8_t *data;
} MKVSimpleBlock;

typedef struct MKVCluster {
  struct EBMLData *ebml;
  uint64_t timecode;

  size_t allocated_blocks;
  size_t n_blocks;
  struct MKVSimpleBlock blocks[];
} MKVCluster;

typedef uint64_t EBMLTag;

typedef enum EBMLStatus { EBML_ERROR = 0, EBML_OK = 1 } EBMLStatus;

EBMLData ebmlInit(const void *data, size_t len);
MKVCluster *mkvClusterInit(size_t maxSimpleBlocks);
void mkvClusterDestroy(MKVCluster *cluster);
EBMLStatus mkvClusterParse(MKVCluster *cluster, EBMLData *data);

#endif
