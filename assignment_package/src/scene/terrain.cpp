#include "terrain.h"
#include "cube.h"
#include <stdexcept>
#include <iostream>

#include <cmath>
#include <limits>

Terrain::Terrain(OpenGLContext *context)
    : m_chunks(), m_generatedTerrain(), m_geomCube(context),
      m_chunkVBOsNeedUpdating(true), mp_context(context)
{}

Terrain::~Terrain() {
    m_geomCube.destroyVBOdata();
}

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
    if(z & 0x0000000080000000) {
        z = z | 0xffffffff00000000;
    }
    int64_t x = (k >> 32);

    return glm::ivec2(x, z);
}

// Surround calls to this with try-catch if you don't know whether
// the coordinates at x, y, z have a corresponding Chunk
BlockType Terrain::getGlobalBlockAt(int x, int y, int z) const
{
    if(hasChunkAt(x, z)) {
        // Just disallow action below or above min/max height,
        // but don't crash the game over it.
        if(y < 0 || y >= 256) {
            return EMPTY;
        }
        const uPtr<Chunk> &c = getChunkAt(x, z);
        glm::vec2 chunkOrigin = glm::vec2(floor(x / 16.f) * 16, floor(z / 16.f) * 16);
        return c->getLocalBlockAt(static_cast<unsigned int>(x - chunkOrigin.x),
                                  static_cast<unsigned int>(y),
                                  static_cast<unsigned int>(z - chunkOrigin.y));
    }
    else {
        throw std::out_of_range("Coordinates " + std::to_string(x) +
                                " " + std::to_string(y) + " " +
                                std::to_string(z) + " have no Chunk!");
    }
}

BlockType Terrain::getGlobalBlockAt(glm::vec3 p) const {
    return getGlobalBlockAt(p.x, p.y, p.z);
}

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


uPtr<Chunk>& Terrain::getChunkAt(int x, int z) {
    int xFloor = static_cast<int>(glm::floor(x / 16.f));
    int zFloor = static_cast<int>(glm::floor(z / 16.f));
    return m_chunks[toKey(16 * xFloor, 16 * zFloor)];
}


const uPtr<Chunk>& Terrain::getChunkAt(int x, int z) const {
    int xFloor = static_cast<int>(glm::floor(x / 16.f));
    int zFloor = static_cast<int>(glm::floor(z / 16.f));
    return m_chunks.at(toKey(16 * xFloor, 16 * zFloor));
}

void Terrain::setGlobalBlockAt(int x, int y, int z, BlockType t)
{
    if(hasChunkAt(x, z)) {
        uPtr<Chunk> &c = getChunkAt(x, z);
        glm::vec2 chunkOrigin = glm::vec2(floor(x / 16.f) * 16, floor(z / 16.f) * 16);
        c->setLocalBlockAt(static_cast<unsigned int>(x - chunkOrigin.x),
                           static_cast<unsigned int>(y),
                           static_cast<unsigned int>(z - chunkOrigin.y),
                           t);

        m_chunkVBOsNeedUpdating = true;
    }
    else {
        throw std::out_of_range("Coordinates " + std::to_string(x) +
                                " " + std::to_string(y) + " " +
                                std::to_string(z) + " have no Chunk!");
    }
}

Chunk* Terrain::instantiateChunkAt(int x, int z) {
    uPtr<Chunk> chunk = mkU<Chunk>(x, z);
    Chunk *cPtr = chunk.get();
    m_chunks[toKey(x, z)] = move(chunk);
    // Set the neighbor pointers of itself and its neighbors
    if(hasChunkAt(x, z + 16)) {
        auto &chunkNorth = m_chunks[toKey(x, z + 16)];
        cPtr->linkNeighbor(chunkNorth, ZPOS);
    }
    if(hasChunkAt(x, z - 16)) {
        auto &chunkSouth = m_chunks[toKey(x, z - 16)];
        cPtr->linkNeighbor(chunkSouth, ZNEG);
    }
    if(hasChunkAt(x + 16, z)) {
        auto &chunkEast = m_chunks[toKey(x + 16, z)];
        cPtr->linkNeighbor(chunkEast, XPOS);
    }
    if(hasChunkAt(x - 16, z)) {
        auto &chunkWest = m_chunks[toKey(x - 16, z)];
        cPtr->linkNeighbor(chunkWest, XNEG);
    }
    return cPtr;
    return cPtr;
}

// TODO: When you make Chunk inherit from Drawable, change this code so
// it draws each Chunk with the given ShaderProgram
void Terrain::draw(int minX, int maxX, int minZ, int maxZ, ShaderProgram *shaderProgram) {

    if(m_chunkVBOsNeedUpdating) {
        m_geomCube.clearOffsetBuf();
        m_geomCube.clearColorBuf();

        std::vector<glm::vec3> offsets, colors;

        for(int x = minX; x < maxX; x += 16) {
            for(int z = minZ; z < maxZ; z += 16) {
                const uPtr<Chunk> &chunk = getChunkAt(x, z);
                for(int i = 0; i < 16; ++i) {
                    for(int j = 0; j < 256; ++j) {
                        for(int k = 0; k < 16; ++k) {
                            BlockType t = chunk->getLocalBlockAt(i, j, k);

                            if(t != EMPTY) {
                                offsets.push_back(glm::vec3(i+x, j, k+z));
                                switch(t) {
                                case GRASS:
                                    colors.push_back(glm::vec3(95.f, 159.f, 53.f) / 255.f);
                                    break;
                                case DIRT:
                                    colors.push_back(glm::vec3(121.f, 85.f, 58.f) / 255.f);
                                    break;
                                case STONE:
                                    colors.push_back(glm::vec3(0.5f));
                                    break;
                                case WATER:
                                    colors.push_back(glm::vec3(0.f, 0.f, 0.75f));
                                    break;
                                default:
                                    // Other block types are not yet handled, so we default to debug purple
                                    colors.push_back(glm::vec3(1.f, 0.f, 1.f));
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        m_geomCube.createInstancedVBOdata(offsets, colors);
        m_chunkVBOsNeedUpdating = false;
    }
    shaderProgram->drawInstanced(m_geomCube);
}

void Terrain::CreateTestScene()
{
    // TODO: DELETE THIS LINE WHEN YOU DELETE m_geomCube!
    m_geomCube.createVBOdata();

    // Create the Chunks that will
    // store the blocks for our
    // initial world space
    for(int x = 0; x < 64; x += 16) {
        for(int z = 0; z < 64; z += 16) {
            instantiateChunkAt(x, z);
        }
    }
    // Tell our existing terrain set that
    // the "generated terrain zone" at (0,0)
    // now exists.
    m_generatedTerrain.insert(toKey(0, 0));

    // Create the basic terrain floor
    for(int x = 0; x < 64; ++x) {
        for(int z = 0; z < 64; ++z) {
            if((x + z) % 2 == 0) {
                setGlobalBlockAt(x, 128, z, STONE);
            }
            else {
                setGlobalBlockAt(x, 128, z, DIRT);
            }
        }
    }
    // Add "walls" for collision testing
    for(int x = 0; x < 64; ++x) {
        setGlobalBlockAt(x, 129, 16, GRASS);
        setGlobalBlockAt(x, 130, 16, GRASS);
        setGlobalBlockAt(x, 129, 48, GRASS);
        setGlobalBlockAt(16, 130, x, GRASS);
    }
    // Add a central column
    for(int y = 129; y < 140; ++y) {
        setGlobalBlockAt(32, y, 32, GRASS);
    }
}

// Raymarching Helpers / Code

bool Terrain::isSolidAt(const glm::ivec3 &cell) const
{
    if (cell.y < 0 || cell.y >= 256) {
        return false;
    }

    // We don't want to walk into ungenerated chunks for now
    if (!hasChunkAt(cell.x, cell.z)) {
        return true;
    }

    return getGlobalBlockAt(cell.x, cell.y, cell.z) != EMPTY;
}

bool Terrain::gridMarch(glm::vec3 origin, glm::vec3 ray, RayHit &hit) const
{
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

    const float infinity =
        std::numeric_limits<float>::infinity();

    glm::ivec3 step(0);
    glm::vec3 tMax(infinity);
    glm::vec3 tDelta(infinity);

    for (int axis = 0; axis < 3; ++axis) {
        if (direction[axis] == 0.f) {
            continue;
        }

        step[axis] = direction[axis] > 0.f ? 1 : -1;

        const float boundary = float(
            cell[axis] + (step[axis] > 0 ? 1 : 0)
            );

        // distance from origin to the first boundary
        tMax[axis] =
            (boundary - origin[axis]) / direction[axis];

        // distance between subsequent boundaries on this axis
        tDelta[axis] = std::abs(1.f / direction[axis]);
    }

    while (true) {
        // find the nearest upcoming boundary
        int axis = 0;

        if (tMax.y < tMax[axis]) axis = 1;
        if (tMax.z < tMax[axis]) axis = 2;

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
