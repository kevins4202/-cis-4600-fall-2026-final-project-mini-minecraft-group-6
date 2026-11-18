#pragma once
#include "chunk.h"
#include <QRunnable>
#include <mutex>
#include <string>
#include <utility>

struct ChunkSnapshot {
    glm::ivec2 origin;
    Chunk::BlockData blocks;
};

struct ChunkVersion {
    glm::ivec2 origin;
    bool present;
    uint64_t revision;
};

struct BlockTypeResult {
    BlockTypeResult() = default;
    BlockTypeResult(BlockTypeResult&&) noexcept = default;
    BlockTypeResult &operator=(BlockTypeResult&&) noexcept = default;
    BlockTypeResult(const BlockTypeResult&) = delete;
    BlockTypeResult &operator=(const BlockTypeResult&) = delete;
    glm::ivec2 zone;
    std::vector<uPtr<Chunk>> chunks;
    std::string error;
};

struct VBOResult {
    VBOResult() = default;
    VBOResult(VBOResult&&) noexcept = default;
    VBOResult &operator=(VBOResult&&) noexcept = default;
    VBOResult(const VBOResult&) = delete;
    VBOResult &operator=(const VBOResult&) = delete;
    glm::ivec2 origin;
    std::vector<ChunkVersion> versions;
    Chunk::MeshData mesh;
    std::string error;
};

// raw pointers and their ownership records are published together under one mutex
class FilledChunkCollection {
    std::mutex m_mutex;
    std::vector<Chunk*> m_filledChunks;
    std::vector<BlockTypeResult> m_zones;
public:
    void push(BlockTypeResult result);
    bool tryPop(BlockTypeResult &result, std::vector<Chunk*> &filledChunks);
};

// a separate mutex protects completed opaque and transparent mesh results
class VBOResultCollection {
    std::mutex m_mutex;
    std::vector<VBOResult> m_results;
public:
    void push(VBOResult result);
    bool tryPop(VBOResult &result);
};

class BlockTypeWorker : public QRunnable {
    glm::ivec2 m_zone;
    OpenGLContext *m_context;
    FilledChunkCollection &m_completed;
public:
    BlockTypeWorker(glm::ivec2 zone, OpenGLContext *context,
                    FilledChunkCollection &completed);
    void run() override;
};

class VBOWorker : public QRunnable {
    glm::ivec2 m_origin;
    std::vector<ChunkSnapshot> m_snapshots;
    std::vector<ChunkVersion> m_versions;
    VBOResultCollection &m_completed;
public:
    VBOWorker(glm::ivec2 origin, std::vector<ChunkSnapshot> snapshots,
              std::vector<ChunkVersion> versions, VBOResultCollection &completed);
    void run() override;
};
