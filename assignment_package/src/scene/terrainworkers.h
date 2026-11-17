#pragma once
#include "chunk.h"
#include <QRunnable>
#include <deque>
#include <mutex>
#include <string>
#include <utility>

// each completion queue owns the mutex protecting its shared results
template<typename T>
class TerrainWorkQueue {
    std::mutex m_mutex;
    std::deque<T> m_results;
public:
    void push(T result) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_results.push_back(std::move(result));
    }
    bool tryPop(T &result) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_results.empty()) return false;
        result = std::move(m_results.front());
        m_results.pop_front();
        return true;
    }
};

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
    glm::ivec2 zone;
    std::vector<uPtr<Chunk>> chunks;
    std::string error;
};

struct VBOResult {
    glm::ivec2 origin;
    std::vector<ChunkVersion> versions;
    Chunk::MeshData mesh;
    std::string error;
};

class BlockTypeWorker : public QRunnable {
    glm::ivec2 m_zone;
    OpenGLContext *m_context;
    TerrainWorkQueue<BlockTypeResult> &m_completed;
public:
    BlockTypeWorker(glm::ivec2 zone, OpenGLContext *context,
                    TerrainWorkQueue<BlockTypeResult> &completed);
    void run() override;
};

class VBOWorker : public QRunnable {
    glm::ivec2 m_origin;
    std::vector<ChunkSnapshot> m_snapshots;
    std::vector<ChunkVersion> m_versions;
    TerrainWorkQueue<VBOResult> &m_completed;
public:
    VBOWorker(glm::ivec2 origin, std::vector<ChunkSnapshot> snapshots,
              std::vector<ChunkVersion> versions, TerrainWorkQueue<VBOResult> &completed);
    void run() override;
};
