#ifndef TERRAIN_H
#define TERRAIN_H

#include "game_types.h"

uint32_t getWorldSeed();
uint32_t getChunkHash(int16_t x, int16_t z);
uint8_t getChunkBiome(int16_t x, int16_t z);
uint8_t getHeightAtFromHash(int rx, int rz, int _x, int _z, uint32_t chunk_hash, uint8_t biome);
uint8_t getHeightAt(int x, int z);
uint8_t getTerrainAt(int x, int y, int z, ChunkAnchor anchor);
uint8_t getBlockAt(int x, int y, int z);

extern uint8_t chunk_section[4096];

uint8_t buildChunkSectionInto(uint8_t *section_out, int cx, int cy, int cz);
uint8_t buildChunkSection(int cx, int cy, int cz);

void placeTreeStructure(int16_t x, uint8_t y, int16_t z);

#endif