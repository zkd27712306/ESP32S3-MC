#include "terrain.h"
#include "game_state.h"
#include "registries.h"
#include "procedures.h"
#include <string.h>
#include <stdlib.h>
#include <Arduino.h>

uint8_t chunk_section[4096];

// ====== 获取随机世界种子 ======
uint32_t getWorldSeed() {
    static uint32_t seed = 0;
    if (seed == 0) {
        seed = esp_random();
        seed ^= (uint32_t)micros() << 16;
        seed ^= (uint32_t)millis() << 8;
        seed ^= (uint32_t)rng_seed;
        if (seed == 0) seed = 0xDEADBEEF;
    }
    return seed;
}

uint32_t getChunkHash(int16_t x, int16_t z) {
    uint8_t buf[8];
    uint32_t seed = getWorldSeed();
    memcpy(buf, &x, 2);
    memcpy(buf + 2, &z, 2);
    memcpy(buf + 4, &seed, 4);
    return (uint32_t)splitmix64(*((uint64_t *)buf));
}

uint8_t getChunkBiome(int16_t x, int16_t z) {
    x += BIOME_RADIUS;
    z += BIOME_RADIUS;
    int8_t dx = BIOME_RADIUS - mod_abs(x, BIOME_SIZE);
    int8_t dz = BIOME_RADIUS - mod_abs(z, BIOME_SIZE);
    if (dx * dx + dz * dz > BIOME_RADIUS * BIOME_RADIUS) return W_beach;
    int16_t biome_x = div_floor(x, BIOME_SIZE);
    int16_t biome_z = div_floor(z, BIOME_SIZE);
    uint8_t index = abs((biome_x & 3) + ((biome_z * 4) & 15));
    static const uint8_t biome_lookup[4] = {
        W_plains, W_mangrove_swamp, W_desert, W_snowy_plains
    };
    return biome_lookup[(getWorldSeed() >> (index * 2)) & 3];
}

static uint8_t getCornerHeight(uint32_t hash, uint8_t biome) {
  uint8_t height = TERRAIN_BASE_HEIGHT;
  switch (biome) {
    case W_mangrove_swamp:
      height += (hash % 3) + ((hash >> 4) % 3) + ((hash >> 8) % 3) + ((hash >> 12) % 3);
      if (height < 64) height -= (hash >> 24) & 3;
      break;
    case W_plains:
      height += (hash & 3) + ((hash >> 4) & 3) + ((hash >> 8) & 3) + ((hash >> 12) & 3);
      break;
    case W_desert:
      height += 4 + (hash & 3) + ((hash >> 4) & 3);
      break;
    case W_beach:
      height = 62 - ((hash & 3) + ((hash >> 4) & 3) + ((hash >> 8) & 3));
      break;
    case W_snowy_plains:
      height += (hash & 7) + ((hash >> 4) & 7);
      break;
    default: break;
  }
  return height;
}

static uint8_t interpolate(uint8_t a, uint8_t b, uint8_t c, uint8_t d, int x, int z) {
  uint16_t top    = a * (CHUNK_SIZE - x) + b * x;
  uint16_t bottom = c * (CHUNK_SIZE - x) + d * x;
  return (top * (CHUNK_SIZE - z) + bottom * z) / (CHUNK_SIZE * CHUNK_SIZE);
}

static uint8_t getPlainsDecoration(ChunkAnchor anchor, int rx, int rz, uint8_t height) {
  if (height < 64) return 0;
  uint8_t deco = (uint8_t)(
    ((anchor.hash >> ((rx + rz) & 15)) & 0xFF) ^
    ((rx & 15) << 4) ^ (rz & 15) ^ height
  );
  if ((deco & 31) == 0) return B_dandelion;
  if ((deco & 31) == 1) return B_poppy;
  if ((deco & 3) == 0) return B_short_grass;
  return 0;
}

static uint8_t getHeightAtFromAnchors(int rx, int rz, ChunkAnchor *anchor_ptr) {
  if (rx == 0 && rz == 0) {
    int height = getCornerHeight(anchor_ptr[0].hash, anchor_ptr[0].biome);
    if (height > 67) return height - 1;
  }
  return interpolate(
    getCornerHeight(anchor_ptr[0].hash, anchor_ptr[0].biome),
    getCornerHeight(anchor_ptr[1].hash, anchor_ptr[1].biome),
    getCornerHeight(anchor_ptr[16 / CHUNK_SIZE + 1].hash, anchor_ptr[16 / CHUNK_SIZE + 1].biome),
    getCornerHeight(anchor_ptr[16 / CHUNK_SIZE + 2].hash, anchor_ptr[16 / CHUNK_SIZE + 2].biome),
    rx, rz
  );
}

uint8_t getHeightAtFromHash(int rx, int rz, int _x, int _z, uint32_t chunk_hash, uint8_t biome) {
  if (rx == 0 && rz == 0) {
    int height = getCornerHeight(chunk_hash, biome);
    if (height > 67) return height - 1;
  }
  return interpolate(
    getCornerHeight(chunk_hash, biome),
    getCornerHeight(getChunkHash(_x + 1, _z), getChunkBiome(_x + 1, _z)),
    getCornerHeight(getChunkHash(_x, _z + 1), getChunkBiome(_x, _z + 1)),
    getCornerHeight(getChunkHash(_x + 1, _z + 1), getChunkBiome(_x + 1, _z + 1)),
    rx, rz
  );
}

uint8_t getHeightAt(int x, int z) {
  int _x = div_floor(x, CHUNK_SIZE);
  int _z = div_floor(z, CHUNK_SIZE);
  int rx = mod_abs(x, CHUNK_SIZE);
  int rz = mod_abs(z, CHUNK_SIZE);
  uint32_t chunk_hash = getChunkHash(_x, _z);
  uint8_t biome = getChunkBiome(_x, _z);
  return getHeightAtFromHash(rx, rz, _x, _z, chunk_hash, biome);
}

static ChunkFeature getFeatureFromAnchor(ChunkAnchor anchor) {
  ChunkFeature feature;
  uint8_t feature_position = anchor.hash % (CHUNK_SIZE * CHUNK_SIZE);
  feature.x = feature_position % CHUNK_SIZE;
  feature.z = feature_position / CHUNK_SIZE;
  uint8_t skip_feature = 0;
  if (anchor.biome != W_mangrove_swamp) {
    if (feature.x < 3 || feature.x > CHUNK_SIZE - 3) skip_feature = 1;
    else if (feature.z < 3 || feature.z > CHUNK_SIZE - 3) skip_feature = 1;
  }
  if (skip_feature) {
    feature.y = 0xFF;
  } else {
    feature.x += anchor.x * CHUNK_SIZE;
    feature.z += anchor.z * CHUNK_SIZE;
    feature.y = getHeightAtFromHash(
      mod_abs(feature.x, CHUNK_SIZE), mod_abs(feature.z, CHUNK_SIZE),
      anchor.x, anchor.z, anchor.hash, anchor.biome
    ) + 1;
    feature.variant = (anchor.hash >> (feature.x + feature.z)) & 1;
  }
  return feature;
}

static uint8_t getTerrainAtFromCache(int x, int y, int z, int rx, int rz,
                                      ChunkAnchor anchor, ChunkFeature feature, uint8_t height) {
  if (y >= 64 && y >= height && feature.y != 255) switch (anchor.biome) {
    case W_plains: {
      if (feature.y < 64) break;
      uint8_t birch_style = (((anchor.hash >> ((feature.x + feature.z) & 7)) & 3) == 0);
      uint8_t ore_tree_roll = (uint8_t)(
        ((anchor.hash >> ((feature.x ^ feature.z) & 15)) & 255) ^
        ((feature.x * 17 + feature.z * 31) & 255)
      );
      uint8_t ore_tree_type = 0;
      if (ore_tree_roll & 1) ore_tree_type = (ore_tree_roll & 2) ? 2 : 1;
      uint8_t tree_log = B_oak_log, tree_leaf = B_oak_leaves, tree_root = B_dirt;
      if (ore_tree_type == 1) { tree_log = B_iron_ore; tree_leaf = B_stone; tree_root = B_iron_ore; }
      else if (ore_tree_type == 2) { tree_log = B_diamond_ore; tree_leaf = B_stone; tree_root = B_diamond_ore; }
      int tree_top = feature.y - feature.variant + 6 + (birch_style ? 1 : 0);
      if (x == feature.x && z == feature.z) {
        if (y == feature.y - 1) return tree_root;
        if (y >= feature.y && y < tree_top) return tree_log;
      }
      uint8_t dx = x > feature.x ? x - feature.x : feature.x - x;
      uint8_t dz = z > feature.z ? z - feature.z : feature.z - z;
      if (dx < 3 && dz < 3 && y > tree_top - 4 && y < tree_top - 1) {
        if (y == tree_top - 2 && dx == 2 && dz == 2) break;
        return tree_leaf;
      }
      if (dx < 2 && dz < 2 && y >= tree_top - 1 && y <= tree_top) {
        if (y == tree_top && dx == 1 && dz == 1) break;
        return tree_leaf;
      }
      if (y == height) return B_grass_block;
      if (y == height + 1) {
        uint8_t decoration = getPlainsDecoration(anchor, rx, rz, height);
        if (decoration) return decoration;
      }
      return B_air;
    }
    case W_desert: {
      if (x != feature.x || z != feature.z) break;
      if (feature.variant == 0) {
        if (y == height + 1) return B_dead_bush;
      } else if (y > height) {
        if (height & 1 && y <= height + 3) return B_cactus;
        if (y <= height + 2) return B_cactus;
      }
      break;
    }
    case W_mangrove_swamp: {
      if (x == feature.x && z == feature.z && y == 64 && height < 63) return B_lily_pad;
      if (y == height + 1) {
        uint8_t dx = x > feature.x ? x - feature.x : feature.x - x;
        uint8_t dz = z > feature.z ? z - feature.z : feature.z - z;
        if (dx + dz < 4) return B_moss_carpet;
      }
      break;
    }
    case W_snowy_plains: {
      if (x == feature.x && z == feature.z && y == height + 1 && height >= 64) return B_short_grass;
      break;
    }
    default: break;
  }
  if (height >= 63) {
    if (y == height) {
      if (anchor.biome == W_mangrove_swamp) return B_mud;
      if (anchor.biome == W_snowy_plains) return B_snowy_grass_block;
      if (anchor.biome == W_desert) return B_sand;
      if (anchor.biome == W_beach) return B_sand;
      return B_grass_block;
    }
    if (anchor.biome == W_plains && y == height + 1) {
      uint8_t decoration = getPlainsDecoration(anchor, rx, rz, height);
      if (decoration) return decoration;
    }
    if (anchor.biome == W_snowy_plains && y == height + 1) return B_snow;
  }
  if (y <= height - 4) {
    int8_t gap = height - TERRAIN_BASE_HEIGHT;
    if (y < CAVE_BASE_DEPTH + gap && y > CAVE_BASE_DEPTH - gap) return B_air;
    uint8_t ore_y = ((rx & 15) << 4) + (rz & 15);
    ore_y ^= ore_y << 4; ore_y ^= ore_y >> 5; ore_y ^= ore_y << 1;
    ore_y &= 63;
    if (y == ore_y) {
      uint8_t ore_probability = (anchor.hash >> (ore_y % 24)) & 255;
      if (y < 15) {
        if (ore_probability < 10) return B_diamond_ore;
        if (ore_probability < 12) return B_gold_ore;
        if (ore_probability < 15) return B_redstone_ore;
      }
      if (y < 30) {
        if (ore_probability < 3) return B_gold_ore;
        if (ore_probability < 8) return B_redstone_ore;
      }
      if (y < 54) {
        if (ore_probability < 30) return B_iron_ore;
        if (ore_probability < 40) return B_copper_ore;
      }
      if (ore_probability < 60) return B_coal_ore;
      if (y < 5) return B_lava;
      return B_cobblestone;
    }
    return B_stone;
  }
  if (y <= height) {
    if (anchor.biome == W_desert) return B_sandstone;
    if (anchor.biome == W_mangrove_swamp) return B_mud;
    if (anchor.biome == W_beach && height > 64) return B_sandstone;
    return B_dirt;
  }
  if (y == 63 && anchor.biome == W_snowy_plains) return B_ice;
  if (y < 64) return B_water;
  return B_air;
}

uint8_t getTerrainAt(int x, int y, int z, ChunkAnchor anchor) {
  if (y > 80) return B_air;
  int rx = x % CHUNK_SIZE; if (rx < 0) rx += CHUNK_SIZE;
  int rz = z % CHUNK_SIZE; if (rz < 0) rz += CHUNK_SIZE;
  ChunkFeature feature = getFeatureFromAnchor(anchor);
  uint8_t height = getHeightAtFromHash(rx, rz, anchor.x, anchor.z, anchor.hash, anchor.biome);
  return getTerrainAtFromCache(x, y, z, rx, rz, anchor, feature, height);
}

uint8_t getBlockAt(int x, int y, int z) {
  if (y < 0) return B_bedrock;
  uint8_t block_change = getBlockChange(x, y, z);
  if (block_change != 0xFF) return block_change;
  int16_t anchor_x = div_floor(x, CHUNK_SIZE);
  int16_t anchor_z = div_floor(z, CHUNK_SIZE);
  ChunkAnchor anchor = { anchor_x, anchor_z, getChunkHash(anchor_x, anchor_z), getChunkBiome(anchor_x, anchor_z) };
  return getTerrainAt(x, y, z, anchor);
}

uint8_t buildChunkSectionInto(uint8_t *section_out, int cx, int cy, int cz) {
  ChunkAnchor chunk_anchors[(16 / CHUNK_SIZE + 1) * (16 / CHUNK_SIZE + 1)];
  ChunkFeature chunk_features[256 / (CHUNK_SIZE * CHUNK_SIZE)];
  uint8_t chunk_section_height[16][16];
  int anchor_index = 0, feature_index = 0;
  for (int i = cz; i < cz + 16 + CHUNK_SIZE; i += CHUNK_SIZE) {
    for (int j = cx; j < cx + 16 + CHUNK_SIZE; j += CHUNK_SIZE) {
      ChunkAnchor *anchor = chunk_anchors + anchor_index;
      anchor->x = j / CHUNK_SIZE;
      anchor->z = i / CHUNK_SIZE;
      anchor->hash = getChunkHash(anchor->x, anchor->z);
      anchor->biome = getChunkBiome(anchor->x, anchor->z);
      if (i != cz + 16 && j != cx + 16) {
        chunk_features[feature_index] = getFeatureFromAnchor(*anchor);
        feature_index++;
      }
      anchor_index++;
    }
  }
  for (int i = 0; i < 16; i++) {
    for (int j = 0; j < 16; j++) {
      anchor_index = (j / CHUNK_SIZE) + (i / CHUNK_SIZE) * (16 / CHUNK_SIZE + 1);
      ChunkAnchor *anchor_ptr = chunk_anchors + anchor_index;
      chunk_section_height[j][i] = getHeightAtFromAnchors(j % CHUNK_SIZE, i % CHUNK_SIZE, anchor_ptr);
    }
  }
  for (int j = 0; j < 4096; j += 8) {
    int y = j / 256 + cy;
    int rz = j / 16 % 16;
    int rz_mod = rz % CHUNK_SIZE;
    feature_index = (j % 16) / CHUNK_SIZE + (j / 16 % 16) / CHUNK_SIZE * (16 / CHUNK_SIZE);
    anchor_index = (j % 16) / CHUNK_SIZE + (j / 16 % 16) / CHUNK_SIZE * (16 / CHUNK_SIZE + 1);
    for (int offset = 0; offset < 8; offset++) {
      int k = j + offset;
      int rx = k % 16;
      section_out[k] = getTerrainAtFromCache(
        rx + cx, y, rz + cz,
        rx % CHUNK_SIZE, rz_mod,
        chunk_anchors[anchor_index],
        chunk_features[feature_index],
        chunk_section_height[rx][rz]
      );
    }
  }
  return chunk_anchors[0].biome;
}

uint8_t buildChunkSection(int cx, int cy, int cz) {
  return buildChunkSectionInto(chunk_section, cx, cy, cz);
}

// ============================================================
// Structures
// ============================================================

static void setBlockIfReplaceable(int16_t x, uint8_t y, int16_t z, uint8_t block) {
  uint8_t target = getBlockAt(x, y, z);
  if (!isReplaceableBlock(target) && target != B_oak_leaves) return;
  makeBlockChange(x, y, z, block);
}

void placeTreeStructure(int16_t x, uint8_t y, int16_t z) {
  uint32_t r = fast_rand();
  uint8_t birch_style = ((r >> 3) & 3) == 0;
  uint8_t log_block = B_oak_log;
  uint8_t height = (birch_style ? 5 : 4) + (r % 3);
  makeBlockChange(x, y - 1, z, B_dirt);
  makeBlockChange(x, y, z, log_block);
  for (int i = 1; i < height; i++) setBlockIfReplaceable(x, y + i, z, log_block);
  uint8_t t = 2;
  for (int i = -2; i <= 2; i++) {
    for (int j = -2; j <= 2; j++) {
      setBlockIfReplaceable(x + i, y + height - 3, z + j, B_oak_leaves);
      if ((i == 2 || i == -2) && (j == 2 || j == -2)) { t++; if ((r >> t) & 1) continue; }
      setBlockIfReplaceable(x + i, y + height - 2, z + j, B_oak_leaves);
    }
  }
  for (int i = -1; i <= 1; i++) {
    for (int j = -1; j <= 1; j++) {
      setBlockIfReplaceable(x + i, y + height - 1, z + j, B_oak_leaves);
      if ((i == 1 || i == -1) && (j == 1 || j == -1)) { t++; if ((r >> t) & 1) continue; }
      setBlockIfReplaceable(x + i, y + height, z + j, B_oak_leaves);
    }
  }
}