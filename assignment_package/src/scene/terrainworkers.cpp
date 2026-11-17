#include "terrainworkers.h"
#include "terrain.h"
#include <exception>
#include <unordered_map>

BlockTypeWorker::BlockTypeWorker(glm::ivec2 zone, OpenGLContext *context,
                                 TerrainWorkQueue<BlockTypeResult> &completed)
    : m_zone(zone), m_context(context), m_completed(completed) {}

void BlockTypeWorker::run() {
    BlockTypeResult result;
    result.zone = m_zone;
    try {
        result.chunks.reserve(16);
        for (int x = m_zone.x; x < m_zone.x + Terrain::ZONE_SIZE; x += Terrain::CHUNK_SIZE) {
            for (int z = m_zone.y; z < m_zone.y + Terrain::ZONE_SIZE; z += Terrain::CHUNK_SIZE) {
                // the context pointer is only used later by the main thread
                auto chunk = mkU<Chunk>(m_context, x, z);
                Terrain::fillChunkBlocks(*chunk, x, z);
                result.chunks.push_back(std::move(chunk));
            }
        }
    } catch (const std::exception &error) {
        result.error = error.what();
        result.chunks.clear();
    }
    m_completed.push(std::move(result));
}

VBOWorker::VBOWorker(glm::ivec2 origin, std::vector<ChunkSnapshot> snapshots,
                     std::vector<ChunkVersion> versions, TerrainWorkQueue<VBOResult> &completed)
    : m_origin(origin), m_snapshots(std::move(snapshots)),
      m_versions(std::move(versions)), m_completed(completed) {}

void VBOWorker::run() {
    VBOResult result;
    result.origin = m_origin;
    result.versions = std::move(m_versions);
    try {
        // isolated copies allow edits and generation to continue without sharing live blocks
        std::unordered_map<int64_t, uPtr<Chunk>> chunks;
        for (const ChunkSnapshot &snapshot : m_snapshots) {
            auto chunk = mkU<Chunk>(nullptr, snapshot.origin.x, snapshot.origin.y);
            chunk->setBlockData(snapshot.blocks);
            chunks.emplace(toKey(snapshot.origin.x, snapshot.origin.y), std::move(chunk));
        }
        Chunk &center = *chunks.at(toKey(m_origin.x, m_origin.y));
        const std::array<std::pair<glm::ivec2, Direction>, 4> neighbors{{
            {{-Terrain::CHUNK_SIZE, 0}, XNEG}, {{Terrain::CHUNK_SIZE, 0}, XPOS},
            {{0, -Terrain::CHUNK_SIZE}, ZNEG}, {{0, Terrain::CHUNK_SIZE}, ZPOS}
        }};
        for (const auto &neighbor : neighbors) {
            const glm::ivec2 origin = m_origin + neighbor.first;
            auto found = chunks.find(toKey(origin.x, origin.y));
            if (found != chunks.end()) center.linkNeighbor(found->second, neighbor.second);
        }
        result.mesh = center.buildMeshData();
    } catch (const std::exception &error) {
        result.error = error.what();
    }
    m_completed.push(std::move(result));
}
