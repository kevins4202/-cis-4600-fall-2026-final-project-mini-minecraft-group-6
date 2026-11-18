#include "chunk.h"
#include <algorithm>
#include <array>
#include <vector>

namespace {
struct FaceData {
  glm::ivec3 neighborOffset;
  glm::vec4 normal;
  std::array<glm::vec4, 4> positions;
};

const std::array<FaceData, 6> FACE_DATA{{
    {glm::ivec3(1, 0, 0), glm::vec4(1, 0, 0, 0),
     {glm::vec4(1, 1, 0, 1), glm::vec4(1, 0, 0, 1),
      glm::vec4(1, 0, 1, 1), glm::vec4(1, 1, 1, 1)}},
    {glm::ivec3(-1, 0, 0), glm::vec4(-1, 0, 0, 0),
     {glm::vec4(0, 1, 1, 1), glm::vec4(0, 0, 1, 1),
      glm::vec4(0, 0, 0, 1), glm::vec4(0, 1, 0, 1)}},
    {glm::ivec3(0, 1, 0), glm::vec4(0, 1, 0, 0),
     {glm::vec4(1, 1, 0, 1), glm::vec4(1, 1, 1, 1),
      glm::vec4(0, 1, 1, 1), glm::vec4(0, 1, 0, 1)}},
    {glm::ivec3(0, -1, 0), glm::vec4(0, -1, 0, 0),
     {glm::vec4(1, 0, 1, 1), glm::vec4(1, 0, 0, 1),
      glm::vec4(0, 0, 0, 1), glm::vec4(0, 0, 1, 1)}},
    {glm::ivec3(0, 0, 1), glm::vec4(0, 0, 1, 0),
     {glm::vec4(1, 1, 1, 1), glm::vec4(1, 0, 1, 1),
      glm::vec4(0, 0, 1, 1), glm::vec4(0, 1, 1, 1)}},
    {glm::ivec3(0, 0, -1), glm::vec4(0, 0, -1, 0),
     {glm::vec4(0, 1, 0, 1), glm::vec4(0, 0, 0, 1),
      glm::vec4(1, 0, 0, 1), glm::vec4(1, 1, 0, 1)}}
}};

glm::vec4 blockColor(BlockType type) {
  switch (type) {
  case GRASS:
    return glm::vec4(glm::vec3(95.f, 159.f, 53.f) / 255.f, 1.f);
  case DIRT:
    return glm::vec4(glm::vec3(121.f, 85.f, 58.f) / 255.f, 1.f);
  case STONE:
    return glm::vec4(0.5f, 0.5f, 0.5f, 1.f);
  case WATER:
    return glm::vec4(0.f, 0.f, 0.75f, 0.65f);
  case SNOW:
    return glm::vec4(1.f, 1.f, 1.f, 1.f);
  default:
    return glm::vec4(1.f, 0.f, 1.f, 1.f);
  }
}
}

Chunk::Chunk(OpenGLContext *context, int x, int z)
    : Drawable(context), m_blocks(), minX(x), minZ(z),
      m_neighbors{{XPOS, nullptr}, {XNEG, nullptr},
                  {ZPOS, nullptr}, {ZNEG, nullptr}} {
  std::fill_n(m_blocks.begin(), 65536, EMPTY);
}

BlockType Chunk::getAdjacentBlockAt(int x, int y, int z) const {
  if (y < 0 || y >= 256) {
    return EMPTY;
  }

  if (x < 0) {
    Chunk *neighbor = m_neighbors.at(XNEG);
    return neighbor == nullptr ? EMPTY : neighbor->getLocalBlockAt(15, y, z);
  }
  if (x >= 16) {
    Chunk *neighbor = m_neighbors.at(XPOS);
    return neighbor == nullptr ? EMPTY : neighbor->getLocalBlockAt(0, y, z);
  }
  if (z < 0) {
    Chunk *neighbor = m_neighbors.at(ZNEG);
    return neighbor == nullptr ? EMPTY : neighbor->getLocalBlockAt(x, y, 15);
  }
  if (z >= 16) {
    Chunk *neighbor = m_neighbors.at(ZPOS);
    return neighbor == nullptr ? EMPTY : neighbor->getLocalBlockAt(x, y, 0);
  }

  return getLocalBlockAt(x, y, z);
}

void Chunk::createVBOdata() {
  bufferVBOdata(buildMeshData());
}

Chunk::MeshData Chunk::buildMeshData() const {
  MeshData mesh;

  for (int z = 0; z < 16; ++z) {
    for (int y = 0; y < 256; ++y) {
      for (int x = 0; x < 16; ++x) {
        BlockType block = getLocalBlockAt(x, y, z);
        if (block == EMPTY) {
          continue;
        }

        const bool transparent = block == WATER;
        auto &vertexData = transparent ? mesh.transparentVertices : mesh.opaqueVertices;
        auto &indices = transparent ? mesh.transparentIndices : mesh.opaqueIndices;
        glm::vec4 blockOffset(minX + x, y, minZ + z, 0.f);
        glm::vec4 color = blockColor(block);

        for (const FaceData &face : FACE_DATA) {
          glm::ivec3 adjacent = glm::ivec3(x, y, z) + face.neighborOffset;
          const BlockType neighbor = getAdjacentBlockAt(adjacent.x, adjacent.y, adjacent.z);
          if (neighbor != EMPTY && (transparent || neighbor != WATER)) {
            continue;
          }

          GLuint firstVertex = static_cast<GLuint>(vertexData.size() / 3);
          for (const glm::vec4 &position : face.positions) {
            vertexData.push_back(position + blockOffset);
            vertexData.push_back(face.normal);
            vertexData.push_back(color);
          }

          indices.push_back(firstVertex);
          indices.push_back(firstVertex + 1);
          indices.push_back(firstVertex + 2);
          indices.push_back(firstVertex);
          indices.push_back(firstVertex + 2);
          indices.push_back(firstVertex + 3);
        }
      }
    }
  }

  return mesh;
}

void Chunk::bufferVBOdata(const MeshData &mesh) {
  bufferMeshPart(mesh.opaqueVertices, mesh.opaqueIndices, INTERLEAVED, INDEX);
  bufferMeshPart(mesh.transparentVertices, mesh.transparentIndices,
                 TRANSPARENT_INTERLEAVED, TRANSPARENT_INDEX);
}

void Chunk::bufferMeshPart(const std::vector<glm::vec4> &vertices,
                           const std::vector<GLuint> &indices,
                           BufferType vertexBuffer, BufferType indexBuffer) {
  indexCounts[indexBuffer] = static_cast<int>(indices.size());
  if (!bufGenerated[indexBuffer]) generateBuffer(indexBuffer);
  bindBuffer(indexBuffer);
  mp_context->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                           indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW);
  if (!bufGenerated[vertexBuffer]) generateBuffer(vertexBuffer);
  bindBuffer(vertexBuffer);
  mp_context->glBufferData(GL_ARRAY_BUFFER,
                           vertices.size() * sizeof(glm::vec4), vertices.data(), GL_STATIC_DRAW);
}

BlockType Chunk::getLocalBlockAt(unsigned int x, unsigned int y, unsigned int z) const {
  unsigned int idx = x + 16 * y + 16 * 256 * z;
  assert(idx < m_blocks.size());   // Optimized away when compiled in release mode
  return m_blocks[idx];
}

// Exists to get rid of compiler warnings about int -> unsigned int implicit conversion
BlockType Chunk::getLocalBlockAt(int x, int y, int z) const { return getLocalBlockAt(static_cast<unsigned int>(x), static_cast<unsigned int>(y), static_cast<unsigned int>(z)); }

void Chunk::setLocalBlockAt(unsigned int x, unsigned int y, unsigned int z, BlockType t) {
  unsigned int idx = x + 16 * y + 16 * 256 * z;
  assert(idx < m_blocks.size());   // Optimized away when compiled in release mode
  m_blocks[idx] = t;
  ++m_revision;
}

const static std::unordered_map<Direction, Direction, EnumHash> oppositeDirection{{XPOS, XNEG}, {XNEG, XPOS}, {YPOS, YNEG}, {YNEG, YPOS}, {ZPOS, ZNEG}, {ZNEG, ZPOS}};

void Chunk::linkNeighbor(uPtr<Chunk> &neighbor, Direction dir) {
  if (neighbor != nullptr) {
    this->m_neighbors[dir] = neighbor.get();
    neighbor->m_neighbors[oppositeDirection.at(dir)] = this;
  }
}
