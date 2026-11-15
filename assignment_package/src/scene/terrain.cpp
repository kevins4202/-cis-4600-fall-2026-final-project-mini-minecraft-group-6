#include "terrain.h"
#include "noise.h"
#include <array>
#include <stdexcept>
#include <iostream>

Terrain::Terrain(OpenGLContext *context)
    : m_chunks(), m_generatedTerrain(), mp_context(context)
{}

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
    if(z & 0x0000000080000000) {
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
    }
    else {
        throw std::out_of_range("Coordinates " + std::to_string(x) +
                                " " + std::to_string(y) + " " +
                                std::to_string(z) + " have no Chunk!");
    }
}

Chunk* Terrain::instantiateChunkAt(int x, int z) {
    if(hasChunkAt(x, z)) {
        return getChunkAt(x, z).get();
    }

    uPtr<Chunk> chunk = mkU<Chunk>(mp_context, x, z);
    Chunk *cPtr = chunk.get();
    m_chunks[toKey(x, z)] = std::move(chunk);
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
}

void Terrain::expandTerrain(const glm::vec3 &playerPosition) {
    constexpr int CHUNK_SIZE = 16;
    constexpr float EXPANSION_DISTANCE = 16.f;

    int chunkX = static_cast<int>(glm::floor(playerPosition.x / CHUNK_SIZE)) * CHUNK_SIZE;
    int chunkZ = static_cast<int>(glm::floor(playerPosition.z / CHUNK_SIZE)) * CHUNK_SIZE;

    if(!hasChunkAt(chunkX, chunkZ)) {
        Chunk *currentChunk = instantiateChunkAt(chunkX, chunkZ);
        generateChunk(chunkX, chunkZ);
        currentChunk->createVBOdata();
    }

    struct ExpansionCandidate {
        int offsetX;
        int offsetZ;
        float distanceToEdge;
    };

    std::array<ExpansionCandidate, 4> candidates {{
        {-CHUNK_SIZE, 0, playerPosition.x - chunkX},
        {CHUNK_SIZE, 0, chunkX + CHUNK_SIZE - playerPosition.x},
        {0, -CHUNK_SIZE, playerPosition.z - chunkZ},
        {0, CHUNK_SIZE, chunkZ + CHUNK_SIZE - playerPosition.z}
    }};

    for(const ExpansionCandidate &candidate : candidates) {
        int newChunkX = chunkX + candidate.offsetX;
        int newChunkZ = chunkZ + candidate.offsetZ;
        if(candidate.distanceToEdge <= EXPANSION_DISTANCE &&
           !hasChunkAt(newChunkX, newChunkZ)) {
            Chunk *newChunk = instantiateChunkAt(newChunkX, newChunkZ);
            generateChunk(newChunkX, newChunkZ);
            newChunk->createVBOdata();
        }
    }
}

void Terrain::draw(int minX, int maxX, int minZ, int maxZ, ShaderProgram *shaderProgram) {
    for(int x = minX; x < maxX; x += 16) {
        for(int z = minZ; z < maxZ; z += 16) {
            if(hasChunkAt(x, z)) {
                shaderProgram->drawInterleaved(*getChunkAt(x, z));
            }
        }
    }
}

void Terrain::CreateTestScene()
{
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

    for(int x = 0; x < 64; ++x) {
        for(int z = 0; z < 64; ++z) {
            generateColumn(x, z);
        }
    }

    for(auto &chunk : m_chunks) {
        chunk.second->createVBOdata();
    }
}

void Terrain::generateChunk(int chunkX, int chunkZ) {
    for(int x = 0; x < 16; ++x) {
        for(int z = 0; z < 16; ++z) {
            generateColumn(chunkX + x, chunkZ + z);
        }
    }
}

void Terrain::generateColumn(int x, int z) {
    float t = biomeBlend(x, z);
    int h = static_cast<int>(glm::mix(float(grasslandHeight(x, z)),
                                      float(mountainHeight(x, z)), t));
    h = glm::clamp(h, 129, 255);
    for(int y = 0; y <= 128; ++y) {
        setGlobalBlockAt(x, y, z, STONE);
    }
    if(t < 0.5f) {
        for(int y = 129; y < h; ++y) {
            setGlobalBlockAt(x, y, z, DIRT);
        }
        setGlobalBlockAt(x, h, z, GRASS);
    } else {
        for(int y = 129; y < h; ++y) {
            setGlobalBlockAt(x, y, z, STONE);
        }
        setGlobalBlockAt(x, h, z, h > 200 ? SNOW : STONE);
    }
    for(int y = 129; y <= 138; ++y) {
        if(getGlobalBlockAt(x, y, z) == EMPTY) {
            setGlobalBlockAt(x, y, z, WATER);
        }
    }
}
