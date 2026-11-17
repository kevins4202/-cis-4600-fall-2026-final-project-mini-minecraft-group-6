#include "terrain.h"
#include "noise.h"
#include <array>
#include <chrono>
#include <utility>
#include <iostream>
#include <stdexcept>

#include <cmath>
#include <limits>

Terrain::Terrain(OpenGLContext *context) : m_chunks(), mp_context(context) {}

Terrain::~Terrain() {}

// Combine two 32-bit ints into one 64-bit int
// where the upper 32 bits are X and the lower 32 bits are Z
int64_t toKey(int x, int z) {
  int64_t xz = 0xffffffffffffffff;
  int64_t x64 = x;
  int64_t z64 = z;

  // Set all lower 32 bits to 1 so we can & with Z later
  xz = (xz & (x64 << 32)) | 0x00000000ffffffff;

  // Set all upper 32 bits to 1 so we can & with XZ
  z64 = z64 | 0xffffffff00000000;

  // Combine
  xz = xz & z64;
  return xz;
}

glm::ivec2 toCoords(int64_t k) {
  // Z is lower 32 bits
  int64_t z = k & 0x00000000ffffffff;
  // If the most significant bit of Z is 1, then it's a negative number
  // so we have to set all the upper 32 bits to 1.
  // Note the 8    V
  if (z & 0x0000000080000000) {
    z = z | 0xffffffff00000000;
  }
  int64_t x = (k >> 32);

  return glm::ivec2(x, z);
}

int mountainHeight(int x, int z) {
  glm::vec2 p = glm::vec2(x, z) / 128.f;
  float noise = fbmAbsPerlin(p);
  noise = glm::clamp(noise / 0.45f, 0.f, 1.f);
  float height = 150.f + noise * 105.f;
  return glm::clamp(static_cast<int>(height), 129, 255);
}

int grasslandHeight(int x, int z) {
  glm::vec2 p = glm::vec2(x, z) / 64.f;
  float noise = worleyNoise(p);
  noise = 1.f - noise;
  noise = glm::clamp(noise, 0.f, 1.f);
  float height = 115.f + noise * 40.f;
  return glm::clamp(static_cast<int>(height), 129, 255);
}

float biomeBlend(int x, int z) {
  glm::vec2 p = glm::vec2(x, z) / 500.f;
  float noise = perlinNoise2D(p);
  noise = glm::clamp((noise + 1.f) / 2.f, 0.f, 1.f);
  return glm::smoothstep(0.25f, 0.75f, noise);
}

// Surround calls to this with try-catch if you don't know whether
// the coordinates at x, y, z have a corresponding Chunk
BlockType Terrain::getGlobalBlockAt(int x, int y, int z) const {
  if (hasChunkAt(x, z)) {
    // Just disallow action below or above min/max height,
    // but don't crash the game over it.
    if (y < 0 || y >= 256) {
      return EMPTY;
    }
    const uPtr<Chunk> &c = getChunkAt(x, z);
    glm::vec2 chunkOrigin = glm::vec2(floor(x / 16.f) * 16, floor(z / 16.f) * 16);
    return c->getLocalBlockAt(static_cast<unsigned int>(x - chunkOrigin.x), static_cast<unsigned int>(y), static_cast<unsigned int>(z - chunkOrigin.y));
  } else {
    throw std::out_of_range("Coordinates " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(z) + " have no Chunk!");
  }
}

BlockType Terrain::getGlobalBlockAt(glm::vec3 p) const { return getGlobalBlockAt(p.x, p.y, p.z); }

bool Terrain::hasChunkAt(int x, int z) const {
  // Map x and z to their nearest Chunk corner
  // By flooring x and z, then multiplying by 16,
  // we clamp (x, z) to its nearest Chunk-space corner,
  // then scale back to a world-space location.
  // Note that floor() lets us handle negative numbers
  // correctly, as floor(-1 / 16.f) gives us -1, as
  // opposed to (int)(-1 / 16.f) giving us 0 (incorrect!).
  int xFloor = static_cast<int>(glm::floor(x / 16.f));
  int zFloor = static_cast<int>(glm::floor(z / 16.f));
  return m_chunks.find(toKey(16 * xFloor, 16 * zFloor)) != m_chunks.end();
}

uPtr<Chunk> &Terrain::getChunkAt(int x, int z) {
  int xFloor = static_cast<int>(glm::floor(x / 16.f));
  int zFloor = static_cast<int>(glm::floor(z / 16.f));
  return m_chunks[toKey(16 * xFloor, 16 * zFloor)];
}

const uPtr<Chunk> &Terrain::getChunkAt(int x, int z) const {
  int xFloor = static_cast<int>(glm::floor(x / 16.f));
  int zFloor = static_cast<int>(glm::floor(z / 16.f));
  return m_chunks.at(toKey(16 * xFloor, 16 * zFloor));
}

void Terrain::setGlobalBlockAt(int x, int y, int z, BlockType t) {
  if (hasChunkAt(x, z)) {
    uPtr<Chunk> &c = getChunkAt(x, z);
    glm::vec2 chunkOrigin = glm::vec2(floor(x / 16.f) * 16, floor(z / 16.f) * 16);
    c->setLocalBlockAt(static_cast<unsigned int>(x - chunkOrigin.x), static_cast<unsigned int>(y), static_cast<unsigned int>(z - chunkOrigin.y), t);
  } else {
    throw std::out_of_range("Coordinates " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(z) + " have no Chunk!");
  }
}

Chunk *Terrain::instantiateChunkAt(int x, int z) {
  if (hasChunkAt(x, z)) {
    return getChunkAt(x, z).get();
  }

  uPtr<Chunk> chunk = mkU<Chunk>(mp_context, x, z);
  Chunk *cPtr = chunk.get();
  m_chunks[toKey(x, z)] = std::move(chunk);
  // Set the neighbor pointers of itself and its neighbors
  if (hasChunkAt(x, z + 16)) {
    auto &chunkNorth = m_chunks[toKey(x, z + 16)];
    cPtr->linkNeighbor(chunkNorth, ZPOS);
  }
  if (hasChunkAt(x, z - 16)) {
    auto &chunkSouth = m_chunks[toKey(x, z - 16)];
    cPtr->linkNeighbor(chunkSouth, ZNEG);
  }
  if (hasChunkAt(x + 16, z)) {
    auto &chunkEast = m_chunks[toKey(x + 16, z)];
    cPtr->linkNeighbor(chunkEast, XPOS);
  }
  if (hasChunkAt(x - 16, z)) {
    auto &chunkWest = m_chunks[toKey(x - 16, z)];
    cPtr->linkNeighbor(chunkWest, XNEG);
  }
  return cPtr;
}

Terrain::ChunkBuildResult Terrain::buildChunk(glm::ivec2 origin,
                                                   std::vector<ChunkSnapshot> snapshots) {
  Terrain staging(nullptr);
  for (const ChunkSnapshot &snapshot : snapshots) {
    staging.instantiateChunkAt(snapshot.origin.x, snapshot.origin.y)
        ->setBlockData(snapshot.blocks);
  }
  Chunk *generated = staging.instantiateChunkAt(origin.x, origin.y);
  staging.generateChunk(origin.x, origin.y);

  ChunkBuildResult result;
  result.origin = origin;
  result.blocks = generated->blockData();
  result.meshes.reserve(5);
  result.meshes.push_back({origin, generated->buildMeshData()});
  const std::array<glm::ivec2, 4> offsets{{{-CHUNK_SIZE, 0}, {CHUNK_SIZE, 0},
                                        {0, -CHUNK_SIZE}, {0, CHUNK_SIZE}}};
  for (const glm::ivec2 &offset : offsets) {
    const glm::ivec2 neighbor = origin + offset;
    if (staging.hasChunkAt(neighbor.x, neighbor.y)) {
      result.meshes.push_back({neighbor,
          staging.getChunkAt(neighbor.x, neighbor.y)->buildMeshData()});
    }
  }
  return result;
}

void Terrain::queueChunk(glm::ivec2 origin) {
  std::vector<ChunkSnapshot> snapshots;
  snapshots.reserve(13);
  m_pendingVersions.clear();
  m_pendingVersions.reserve(13);
  // snapshot a two-chunk radius to rebuild the new chunk and its neighbors
  for (int dx = -2; dx <= 2; ++dx) {
    for (int dz = -2; dz <= 2; ++dz) {
      if (std::abs(dx) + std::abs(dz) > 2) continue;
      const glm::ivec2 position = origin + glm::ivec2(dx, dz) * CHUNK_SIZE;
      const bool present = hasChunkAt(position.x, position.y);
      const uint64_t revision = present ? getChunkAt(position.x, position.y)->revision() : 0;
      m_pendingVersions.push_back({position, present, revision});
      if (present) {
        snapshots.push_back({position, getChunkAt(position.x, position.y)->blockData()});
      }
    }
  }
  m_pendingChunk = std::async(std::launch::async, &Terrain::buildChunk,
                             origin, std::move(snapshots));
}

bool Terrain::publishCompletedChunk() {
  if (!m_pendingChunk.valid() ||
      m_pendingChunk.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
    return false;
  }
  ChunkBuildResult result = m_pendingChunk.get();
  for (const ChunkVersion &version : m_pendingVersions) {
    const bool present = hasChunkAt(version.origin.x, version.origin.y);
    if (present != version.present ||
        (present && getChunkAt(version.origin.x, version.origin.y)->revision() != version.revision)) {
      // discard stale meshes and retry from current blocks to preserve edits
      return false;
    }
  }
  instantiateChunkAt(result.origin.x, result.origin.y)->setBlockData(result.blocks);
  for (const MeshUpload &upload : result.meshes) {
    getChunkAt(upload.origin.x, upload.origin.y)
        ->bufferVBOdata(upload.mesh.vertices, upload.mesh.indices);
  }
  return true;
}

void Terrain::expandTerrain(const glm::vec3 &playerPosition) {
  publishCompletedChunk();
  if (m_pendingChunk.valid()) return;

  const int zoneX = static_cast<int>(glm::floor(playerPosition.x / ZONE_SIZE)) * ZONE_SIZE;
  const int zoneZ = static_cast<int>(glm::floor(playerPosition.z / ZONE_SIZE)) * ZONE_SIZE;
  constexpr int radius = RENDER_ZONE_RADIUS * ZONE_SIZE;
  float nearestDistance = std::numeric_limits<float>::infinity();
  glm::ivec2 nearest;
  bool found = false;
  for (int x = zoneX - radius; x < zoneX + radius + ZONE_SIZE; x += CHUNK_SIZE) {
    for (int z = zoneZ - radius; z < zoneZ + radius + ZONE_SIZE; z += CHUNK_SIZE) {
      if (hasChunkAt(x, z)) continue;
      const glm::vec2 delta = glm::vec2(x + CHUNK_SIZE / 2.f, z + CHUNK_SIZE / 2.f)
                            - glm::vec2(playerPosition.x, playerPosition.z);
      const float distance = glm::dot(delta, delta);
      if (distance < nearestDistance) {
        nearestDistance = distance;
        nearest = glm::ivec2(x, z);
        found = true;
      }
    }
  }
  if (found) queueChunk(nearest);
}

void Terrain::draw(int minX, int maxX, int minZ, int maxZ, ShaderProgram *shaderProgram) {
  for (int x = minX; x < maxX; x += 16) {
    for (int z = minZ; z < maxZ; z += 16) {
      if (hasChunkAt(x, z)) {
        shaderProgram->drawInterleaved(*getChunkAt(x, z));
      }
    }
  }
}

void Terrain::CreateTestScene(const glm::vec3 &playerPosition) {
  const int zoneX = static_cast<int>(glm::floor(playerPosition.x / ZONE_SIZE)) * ZONE_SIZE;
  const int zoneZ = static_cast<int>(glm::floor(playerPosition.z / ZONE_SIZE)) * ZONE_SIZE;
  for (int x = zoneX; x < zoneX + ZONE_SIZE; x += CHUNK_SIZE) {
    for (int z = zoneZ; z < zoneZ + ZONE_SIZE; z += CHUNK_SIZE) {
      instantiateChunkAt(x, z);
      generateChunk(x, z);
    }
  }
  for (auto &chunk : m_chunks) {
    chunk.second->createVBOdata();
  }
}

void Terrain::generateChunk(int chunkX, int chunkZ) {
  for (int x = 0; x < 16; ++x) {
    for (int z = 0; z < 16; ++z) {
      generateColumn(chunkX + x, chunkZ + z);
    }
  }
}

void Terrain::generateColumn(int x, int z) {
  // resolve the chunk once per column to avoid repeated world-coordinate lookups
  Chunk &chunk = *getChunkAt(x, z);
  const int localX = x - static_cast<int>(glm::floor(x / 16.f)) * 16;
  const int localZ = z - static_cast<int>(glm::floor(z / 16.f)) * 16;
  const auto setBlock = [&chunk, localX, localZ](int y, BlockType type) {
    chunk.setLocalBlockAt(localX, y, localZ, type);
  };
  float t = biomeBlend(x, z);
  int h = static_cast<int>(glm::mix(float(grasslandHeight(x, z)), float(mountainHeight(x, z)), t));
  h = glm::clamp(h, 129, 255);
  for (int y = 0; y <= 128; ++y) {
    setBlock(y, STONE);
  }
  if (t < 0.5f) {
    for (int y = 129; y < h; ++y) {
      setBlock(y, DIRT);
    }
    setBlock(h, GRASS);
  } else {
    for (int y = 129; y < h; ++y) {
      setBlock(y, STONE);
    }
    setBlock(h, h > 200 ? SNOW : STONE);
  }
  for (int y = 129; y <= 138; ++y) {
    if (chunk.getLocalBlockAt(localX, y, localZ) == EMPTY) {
      setBlock(y, WATER);
    }
  }
}

// Raymarching Helpers / Code

bool Terrain::isSolidAt(const glm::ivec3 &cell) const {
  if (cell.y < 0 || cell.y >= 256) {
    return false;
  }

  // We don't want to walk into ungenerated chunks for now
  if (!hasChunkAt(cell.x, cell.z)) {
    return true;
  }

  return getGlobalBlockAt(cell.x, cell.y, cell.z) != EMPTY;
}

bool Terrain::gridMarch(glm::vec3 origin, glm::vec3 ray, RayHit &hit) const {
  const float maxDistance = glm::length(ray);

  hit = RayHit{};
  hit.distance = maxDistance;

  if (maxDistance == 0.f) {
    return false;
  }

  const glm::vec3 direction = ray / maxDistance;

  // flooring to find what block a position belongs to
  glm::ivec3 cell = glm::ivec3(glm::floor(origin));

  if (isSolidAt(cell)) {
    hit.distance = 0.f;
    hit.block = cell;
    return true;
  }

  const float infinity = std::numeric_limits<float>::infinity();

  glm::ivec3 step(0);
  glm::vec3 tMax(infinity);
  glm::vec3 tDelta(infinity);

  for (int axis = 0; axis < 3; ++axis) {
    if (direction[axis] == 0.f) {
      continue;
    }

    step[axis] = direction[axis] > 0.f ? 1 : -1;

    const float boundary = float(cell[axis] + (step[axis] > 0 ? 1 : 0));

    // distance from origin to the first boundary
    tMax[axis] = (boundary - origin[axis]) / direction[axis];

    // distance between subsequent boundaries on this axis
    tDelta[axis] = std::abs(1.f / direction[axis]);
  }

  while (true) {
    // find the nearest upcoming boundary
    int axis = 0;

    if (tMax.y < tMax[axis])
      axis = 1;
    if (tMax.z < tMax[axis])
      axis = 2;

    const float distance = tMax[axis];

    if (distance > maxDistance) {
      return false;
    }

    // enter the neighboring cell
    cell[axis] += step[axis];
    tMax[axis] += tDelta[axis];

    if (isSolidAt(cell)) {
      hit.distance = distance;
      hit.block = cell;

      hit.normal = glm::ivec3(0);
      hit.normal[axis] = -step[axis];

      return true;
    }
  }
}
