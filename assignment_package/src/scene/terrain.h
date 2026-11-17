#pragma once
#include "chunk.h"
#include "glm_includes.h"
#include "shaderprogram.h"
#include "smartpointerhelp.h"
#include <array>
#include <unordered_map>
#include <future>
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

  struct ChunkSnapshot {
    glm::ivec2 origin;
    Chunk::BlockData blocks;
  };
  struct ChunkVersion {
    glm::ivec2 origin;
    bool present;
    uint64_t revision;
  };
  struct MeshUpload {
    glm::ivec2 origin;
    Chunk::MeshData mesh;
  };
  struct ChunkBuildResult {
    glm::ivec2 origin;
    Chunk::BlockData blocks;
    std::vector<MeshUpload> meshes;
  };

  // isolate one background job and track revisions to protect live block edits
  std::future<ChunkBuildResult> m_pendingChunk;
  std::vector<ChunkVersion> m_pendingVersions;
  static ChunkBuildResult buildChunk(glm::ivec2 origin,
                                    std::vector<ChunkSnapshot> snapshots);
  bool publishCompletedChunk();
  void queueChunk(glm::ivec2 origin);

  OpenGLContext *mp_context;

  //
  bool isSolidAt(const glm::ivec3 &cell) const;

public:
  static constexpr int CHUNK_SIZE = 16;
  static constexpr int ZONE_SIZE = 64;
  static constexpr int RENDER_ZONE_RADIUS = 1;

  Terrain(OpenGLContext *context);
  ~Terrain();

  // Instantiates a new Chunk and stores it in
  // our chunk map at the given coordinates.
  // Returns a pointer to the created Chunk.
  Chunk *instantiateChunkAt(int x, int z);
  // publish completed work and queue the nearest missing chunk
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
};
