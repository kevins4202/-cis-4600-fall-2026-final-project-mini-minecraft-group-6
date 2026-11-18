#pragma once
#include "chunk.h"
#include "glm_includes.h"
#include "shaderprogram.h"
#include "smartpointerhelp.h"
#include <array>
#include <unordered_map>
#include "terrainworkers.h"
#include <QThreadPool>
#include <unordered_set>
#include <vector>

// using namespace std;

// Helper functions to convert (x, z) to and from hash map key
int64_t toKey(int x, int z);
glm::ivec2 toCoords(int64_t k);

int mountainHeight(int x, int z);
int grasslandHeight(int x, int z);
float biomeBlend(int x, int z);

// The container class for all of the Chunks in the game.
// Ultimately, while Terrain will always store all Chunks,
// not all Chunks will be drawn at any given time as the world
// expands.

// contains info about how far a ray travelled,
// the coordinates of the block hit by the ray
// and the surface normal of the surface that is hit
struct RayHit {
  float distance = 0.f;
  glm::ivec3 block = glm::ivec3(0);
  glm::ivec3 normal = glm::ivec3(0);
};

class Terrain {
private:
  // Stores every Chunk according to the location of its lower-left corner
  // in world space.
  // We combine the X and Z coordinates of the Chunk's corner into one 64-bit int
  // so that we can use them as a key for the map, as objects like std::pairs or
  // glm::ivec2s are not hashable by default, so they cannot be used as keys.
  std::unordered_map<int64_t, uPtr<Chunk>> m_chunks;

  std::unordered_set<int64_t> m_generatedTerrain;
  std::unordered_set<int64_t> m_generatingZones;
  std::unordered_set<int64_t> m_meshingChunks;
  std::unordered_set<int64_t> m_dirtyChunks;

  // workers only share these mutex-protected queues with the main thread
  FilledChunkCollection m_completedBlocks;
  VBOResultCollection m_completedMeshes;
  QThreadPool m_workerPool;

  void linkChunkNeighbors(Chunk &chunk, int x, int z);
  void markChunkAndNeighborsDirty(int x, int z);
  void publishGeneratedZone();
  void uploadCompletedMeshes();
  void queueVBOWorker(glm::ivec2 origin);

  OpenGLContext *mp_context;

  //
  bool isSolidAt(const glm::ivec3 &cell) const;

public:
  static constexpr int CHUNK_SIZE = 16;
  static constexpr int ZONE_SIZE = 64;
  static constexpr int RENDER_ZONE_RADIUS = 1;
  static constexpr int GENERATION_ZONE_RADIUS = 2;

  Terrain(OpenGLContext *context);
  ~Terrain();

  // Instantiates a new Chunk and stores it in
  // our chunk map at the given coordinates.
  // Returns a pointer to the created Chunk.
  Chunk *instantiateChunkAt(int x, int z);
  // scan nearby zones and advance block generation, meshing, and uploads
  void expandTerrain(const glm::vec3 &playerPosition);
  // Do these world-space coordinates lie within
  // a Chunk that exists?
  bool hasChunkAt(int x, int z) const;
  // Assuming a Chunk exists at these coords,
  // return a mutable reference to it
  uPtr<Chunk> &getChunkAt(int x, int z);
  // Assuming a Chunk exists at these coords,
  // return a const reference to it
  const uPtr<Chunk> &getChunkAt(int x, int z) const;
  // Given a world-space coordinate (which may have negative
  // values) return the block stored at that point in space.
  BlockType getGlobalBlockAt(int x, int y, int z) const;
  BlockType getGlobalBlockAt(glm::vec3 p) const;
  // Given a world-space coordinate (which may have negative
  // values) set the block at that point in space to the
  // given type.
  void setGlobalBlockAt(int x, int y, int z, BlockType t);

  // Draws every Chunk that falls within the bounding box
  // described by the min and max coords, using the provided
  // ShaderProgram
  void draw(int minX, int maxX, int minZ, int maxZ, ShaderProgram *shaderProgram);

  // populate the spawn zone before physics starts and stream other chunks
  void CreateTestScene(const glm::vec3 &playerPosition);

  // grid march helper
  bool gridMarch(glm::vec3 origin, glm::vec3 ray, RayHit &hit) const;
  // Fills column at world (x, z) with procedurally generated
  // blocks.
  void generateColumn(int x, int z);

  // Fills every column of the Chunk whose corner is at (chunkX, chunkZ).
  void generateChunk(int chunkX, int chunkZ);
  static void fillChunkBlocks(Chunk &chunk, int chunkX, int chunkZ);
};
