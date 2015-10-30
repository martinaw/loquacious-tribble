#include "MKVParser.h"

#define VIDEOTRACK 1

void parse(void *data, size_t len) {
  EBMLData ebml = ebmlInit(data, len);
  MKVCluster *cluster = mkvClusterInit(8);

  if (mkvClusterParse(cluster, &ebml) == EBML_OK) {
    for (int i = 0; i < cluster->n_blocks; i++) {
      if (cluster->blocks[i].track == VIDEOTRACK) {
        // cluster->blocks[i].data
        // cluster->blocks[i].size];
      }
    }
  }

  mkvClusterDestroy(cluster);
}

int main() {}
