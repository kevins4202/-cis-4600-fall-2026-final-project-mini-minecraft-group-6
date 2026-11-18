#include "terrain.h"
#include "noise.h"
#include <array>
#include <algorithm>
#include <QThread>
#include <QDebug>
#include <chrono>
#include <utility>
#include <iostream>
#include <stdexcept>

#include <cmath>
#include <limits>

Terrain::Terrain(OpenGLContext *context) : m_chunks(), mp_context(context) {
  m_workerPool.setMaxThreadCount(std::max(2, std::min(4, QThread::idealThreadCount() - 1)));
}

Terrain::~Terrain() {
  // finish workers before destroying the completion queues they reference
  m_workerPool.clear();
  m_workerPool.waitForDone();
}

int64_t toKey(int x, int z) {
  const uint64_t high = static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32;
  return static_cast<int64_t>(high | static_cast<uint32_t>(z));
}

glm::ivec2 toCoords(int64_t k) {
  const uint64_t bits = static_cast<uint64_t>(k);
  return glm::ivec2(static_cast<int32_t>(bits >> 32), static_cast<int32_t>(bits));
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
    if (y < 0 || y >= 256) return;
    c->setLocalBlockAt(static_cast<unsigned int>(x - chunkOrigin.x), static_cast<unsigned int>(y), static_cast<unsigned int>(z - chunkOrigin.y), t);
    const int originX = static_cast<int>(chunkOrigin.x);
    const int originZ = static_cast<int>(chunkOrigin.y);
    m_dirtyChunks.insert(toKey(originX, originZ));
    if (x == originX && hasChunkAt(x - 1, z)) m_dirtyChunks.insert(toKey(originX - CHUNK_SIZE, originZ));
    if (x == originX + CHUNK_SIZE - 1 && hasChunkAt(x + 1, z)) m_dirtyChunks.insert(toKey(originX + CHUNK_SIZE, originZ));
    if (z == originZ && hasChunkAt(x, z - 1)) m_dirtyChunks.insert(toKey(originX, originZ - CHUNK_SIZE));
    if (z == originZ + CHUNK_SIZE - 1 && hasChunkAt(x, z + 1)) m_dirtyChunks.insert(toKey(originX, originZ + CHUNK_SIZE));
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
  linkChunkNeighbors(*cPtr, x, z);
  return cPtr;
}

void Terrain::linkChunkNeighbors(Chunk &chunk, int x, int z) {
  if (hasChunkAt(x, z + CHUNK_SIZE)) chunk.linkNeighbor(getChunkAt(x, z + CHUNK_SIZE), ZPOS);
  if (hasChunkAt(x, z - CHUNK_SIZE)) chunk.linkNeighbor(getChunkAt(x, z - CHUNK_SIZE), ZNEG);
  if (hasChunkAt(x + CHUNK_SIZE, z)) chunk.linkNeighbor(getChunkAt(x + CHUNK_SIZE, z), XPOS);
  if (hasChunkAt(x - CHUNK_SIZE, z)) chunk.linkNeighbor(getChunkAt(x - CHUNK_SIZE, z), XNEG);
}

void Terrain::markChunkAndNeighborsDirty(int x, int z) {
  const std::array<glm::ivec2, 5> offsets{{{0, 0}, {-CHUNK_SIZE, 0}, {CHUNK_SIZE, 0},
                                         {0, -CHUNK_SIZE}, {0, CHUNK_SIZE}}};
  for (const glm::ivec2 &offset : offsets) {
    if (hasChunkAt(x + offset.x, z + offset.y)) {
      m_dirtyChunks.insert(toKey(x + offset.x, z + offset.y));
    }
  }
}

void Terrain::publishGeneratedZone() {
  BlockTypeResult result;
  std::vector<Chunk*> filledChunks;
  if (!m_completedBlocks.tryPop(result, filledChunks)) return;
  const int64_t zoneKey = toKey(result.zone.x, result.zone.y);
  m_generatingZones.erase(zoneKey);
  if (!result.error.empty()) {
    qWarning("terrain block generation failed: %s", result.error.c_str());
    return;
  }
  // adopt the complete zone before connecting neighbors or scheduling its meshes
  for (auto &chunk : result.chunks) {
    const glm::ivec2 origin = chunk->origin();
    if (!hasChunkAt(origin.x, origin.y)) {
      m_chunks.emplace(toKey(origin.x, origin.y), std::move(chunk));
    }
  }
  for (Chunk *chunk : filledChunks) {
    const glm::ivec2 origin = chunk->origin();
    linkChunkNeighbors(*getChunkAt(origin.x, origin.y), origin.x, origin.y);
    markChunkAndNeighborsDirty(origin.x, origin.y);
  }
  m_generatedTerrain.insert(zoneKey);
  for (Chunk *chunk : filledChunks) {
    if (m_meshingChunks.size() >= 8) break;
    const glm::ivec2 origin = chunk->origin();
    if (m_meshingChunks.count(toKey(origin.x, origin.y)) == 0) queueVBOWorker(origin);
  }
}

void Terrain::uploadCompletedMeshes() {
  const auto start = std::chrono::steady_clock::now();
  for (int count = 0; count < 2; ++count) {
    VBOResult result;
    if (!m_completedMeshes.tryPop(result)) break;
    const int64_t key = toKey(result.origin.x, result.origin.y);
    m_meshingChunks.erase(key);
    bool current = result.error.empty();
    for (const ChunkVersion &version : result.versions) {
      const bool present = hasChunkAt(version.origin.x, version.origin.y);
      if (present != version.present ||
          (present && getChunkAt(version.origin.x, version.origin.y)->revision() != version.revision)) {
        current = false;
        break;
      }
    }
    if (current) {
      getChunkAt(result.origin.x, result.origin.y)->bufferVBOdata(result.mesh);
      m_dirtyChunks.erase(key);
    } else {
      // retry meshes invalidated by edits or newly generated neighbors
      m_dirtyChunks.insert(key);
      if (!result.error.empty()) qWarning("terrain meshing failed: %s", result.error.c_str());
    }
    if (std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(3)) break;
  }
}

void Terrain::queueVBOWorker(glm::ivec2 origin) {
  std::vector<ChunkSnapshot> snapshots;
  std::vector<ChunkVersion> versions;
  snapshots.reserve(5);
  versions.reserve(5);
  const std::array<glm::ivec2, 5> offsets{{{0, 0}, {-CHUNK_SIZE, 0}, {CHUNK_SIZE, 0},
                                         {0, -CHUNK_SIZE}, {0, CHUNK_SIZE}}};
  for (const glm::ivec2 &offset : offsets) {
    const glm::ivec2 position = origin + offset;
    const bool present = hasChunkAt(position.x, position.y);
    const uint64_t revision = present ? getChunkAt(position.x, position.y)->revision() : 0;
    versions.push_back({position, present, revision});
    if (present) snapshots.push_back({position, getChunkAt(position.x, position.y)->blockData()});
  }
  const int64_t key = toKey(origin.x, origin.y);
  m_meshingChunks.insert(key);
  m_dirtyChunks.erase(key);
  m_workerPool.start(new VBOWorker(origin, std::move(snapshots), std::move(versions), m_completedMeshes), 1);
}

void Terrain::expandTerrain(const glm::vec3 &playerPosition) {
  const int zoneX = static_cast<int>(glm::floor(playerPosition.x / ZONE_SIZE)) * ZONE_SIZE;
  const int zoneZ = static_cast<int>(glm::floor(playerPosition.z / ZONE_SIZE)) * ZONE_SIZE;
  const auto distance = [&playerPosition](glm::ivec2 origin, float size) {
    const glm::vec2 delta = glm::vec2(origin) + glm::vec2(size * 0.5f)
                           - glm::vec2(playerPosition.x, playerPosition.z);
    return glm::dot(delta, delta);
  };
  std::vector<glm::ivec2> missingZones;
  std::vector<glm::ivec2> meshes;
  for (int dx = -GENERATION_ZONE_RADIUS; dx <= GENERATION_ZONE_RADIUS; ++dx) {
    for (int dz = -GENERATION_ZONE_RADIUS; dz <= GENERATION_ZONE_RADIUS; ++dz) {
      const glm::ivec2 zone(zoneX + dx * ZONE_SIZE, zoneZ + dz * ZONE_SIZE);
      const int64_t key = toKey(zone.x, zone.y);
      if (m_generatedTerrain.count(key) == 0) {
        if (m_generatingZones.count(key) == 0) missingZones.push_back(zone);
        continue;
      }
      for (int x = zone.x; x < zone.x + ZONE_SIZE; x += CHUNK_SIZE) {
        for (int z = zone.y; z < zone.y + ZONE_SIZE; z += CHUNK_SIZE) {
          const int64_t chunkKey = toKey(x, z);
          if (m_meshingChunks.count(chunkKey) == 0 &&
              (getChunkAt(x, z)->elemCount(INDEX) < 0 || m_dirtyChunks.count(chunkKey) != 0)) {
            meshes.emplace_back(x, z);
          }
        }
      }
    }
  }
  std::sort(missingZones.begin(), missingZones.end(), [&](glm::ivec2 a, glm::ivec2 b) {
    return distance(a, ZONE_SIZE) < distance(b, ZONE_SIZE);
  });
  std::sort(meshes.begin(), meshes.end(), [&](glm::ivec2 a, glm::ivec2 b) {
    return distance(a, CHUNK_SIZE) < distance(b, CHUNK_SIZE);
  });
  // queue missing zones while bounding snapshot-heavy mesh jobs
  constexpr size_t maxMeshJobs = 8;
  for (const glm::ivec2 &zone : missingZones) {
    m_generatingZones.insert(toKey(zone.x, zone.y));
    m_workerPool.start(new BlockTypeWorker(zone, mp_context, m_completedBlocks));
  }
  for (const glm::ivec2 &origin : meshes) {
    if (m_meshingChunks.size() >= maxMeshJobs) break;
    queueVBOWorker(origin);
  }
  // consume shared completions after scheduling this tick's worker jobs
  publishGeneratedZone();
  uploadCompletedMeshes();
}

void Terrain::draw(int minX, int maxX, int minZ, int maxZ, ShaderProgram *shaderProgram) {
  // draw every opaque mesh before any transparent mesh
  for (bool transparent : {false, true}) {
    mp_context->glDepthMask(transparent ? GL_FALSE : GL_TRUE);
    const BufferType indexBuffer = transparent ? TRANSPARENT_INDEX : INDEX;
    for (int x = minX; x < maxX; x += CHUNK_SIZE) {
      for (int z = minZ; z < maxZ; z += CHUNK_SIZE) {
        if (hasChunkAt(x, z) && getChunkAt(x, z)->elemCount(indexBuffer) > 0) {
          shaderProgram->drawInterleaved(*getChunkAt(x, z), transparent);
        }
      }
    }
  }
  mp_context->glDepthMask(GL_TRUE);
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
  m_generatedTerrain.insert(toKey(zoneX, zoneZ));
}

namespace {
void fillColumn(Chunk &chunk, int localX, int localZ, int x, int z) {
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
}

void Terrain::fillChunkBlocks(Chunk &chunk, int chunkX, int chunkZ) {
  for (int x = 0; x < CHUNK_SIZE; ++x) {
    for (int z = 0; z < CHUNK_SIZE; ++z) {
      fillColumn(chunk, x, z, chunkX + x, chunkZ + z);
    }
  }
}

void Terrain::generateChunk(int chunkX, int chunkZ) {
  fillChunkBlocks(*getChunkAt(chunkX, chunkZ), chunkX, chunkZ);
}

void Terrain::generateColumn(int x, int z) {
  const int localX = x - static_cast<int>(glm::floor(x / 16.f)) * 16;
  const int localZ = z - static_cast<int>(glm::floor(z / 16.f)) * 16;
  fillColumn(*getChunkAt(x, z), localX, localZ, x, z);
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
