#include "scene/terrain.h"
#include "scene/terrainworkers.h"
#include <QCoreApplication>
#include <QThreadPool>
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        require(toCoords(toKey(-64, -128)) == glm::ivec2(-64, -128), "negative coordinate key failed");
        require(toCoords(toKey(64, -128)) == glm::ivec2(64, -128), "mixed coordinate key failed");
        FilledChunkCollection blocks;
        QThreadPool pool;
        pool.setMaxThreadCount(2);
        pool.start(new BlockTypeWorker(glm::ivec2(-64, -64), nullptr, blocks));
        pool.start(new BlockTypeWorker(glm::ivec2(0, 0), nullptr, blocks));
        pool.waitForDone();
        int zones = 0;
        BlockTypeResult zone;
        std::vector<Chunk*> filledChunks;
        while (blocks.tryPop(zone, filledChunks)) {
            require(filledChunks.size() == 16, "shared chunk pointer collection lost a chunk");
            for (size_t i = 0; i < filledChunks.size(); ++i) {
                require(filledChunks[i] == zone.chunks[i].get(), "published pointer does not match its owner");
            }
            require(zone.error.empty(), "block worker failed");
            require(zone.chunks.size() == 16, "zone did not contain sixteen chunks");
            for (const auto &chunk : zone.chunks) {
                const glm::ivec2 origin = chunk->origin();
                require(origin.x >= zone.zone.x && origin.x < zone.zone.x + 64 &&
                        origin.y >= zone.zone.y && origin.y < zone.zone.y + 64,
                        "chunk was generated in the wrong zone");
                require(chunk->elemCount(INDEX) == -1, "block worker uploaded gpu data");
                for (int x = 0; x < 16; ++x) {
                    for (int z = 0; z < 16; ++z) {
                        require(chunk->getLocalBlockAt(x, 0, z) == STONE, "missing stone foundation");
                        require(chunk->getLocalBlockAt(x, 128, z) == STONE, "missing upper foundation");
                        const int wx = origin.x + x, wz = origin.y + z;
                        const float blend = biomeBlend(wx, wz);
                        const int h = int(glm::mix(float(grasslandHeight(wx, wz)), float(mountainHeight(wx, wz)), blend));
                        const BlockType top = blend < .5f ? GRASS : (h > 200 ? SNOW : STONE);
                        require(chunk->getLocalBlockAt(x, h, z) == top, "incorrect surface material");
                    }
                }
            }
            ++zones;
        }
        require(zones == 2, "completion queue lost or duplicated a zone");

        Chunk center(nullptr, 0, 0), neighbor(nullptr, 16, 0);
        center.setLocalBlockAt(15, 10, 0, STONE);
        neighbor.setLocalBlockAt(0, 10, 0, STONE);
        std::vector<ChunkSnapshot> snapshots{{{0, 0}, center.blockData()}, {{16, 0}, neighbor.blockData()}};
        std::vector<ChunkVersion> versions{{{0, 0}, true, center.revision()}, {{16, 0}, true, neighbor.revision()}};
        VBOResultCollection meshes;
        auto worker = new VBOWorker(glm::ivec2(0, 0), std::move(snapshots), versions, meshes);
        neighbor.setLocalBlockAt(0, 10, 0, EMPTY);
        pool.start(worker);
        pool.waitForDone();
        VBOResult mesh;
        require(meshes.tryPop(mesh), "missing mesh result");
        require(mesh.error.empty(), "mesh worker failed");
        require(mesh.mesh.opaqueIndices.size() == 30, "shared boundary face was not removed");
        require(mesh.mesh.opaqueVertices.size() == 60, "incorrect interleaved vertex layout");
        require(mesh.versions[1].revision != neighbor.revision(), "snapshot revision did not detect the edit");
        require(center.elemCount(INDEX) == -1, "mesh worker uploaded gpu data");
        require(!meshes.tryPop(mesh), "completion queue duplicated a mesh");

        Chunk mixed(nullptr, 0, 0);
        mixed.setLocalBlockAt(1, 10, 1, STONE);
        mixed.setLocalBlockAt(2, 10, 1, WATER);
        auto split = mixed.buildMeshData();
        require(split.opaqueIndices.size() == 36, "opaque face beneath water was removed");
        require(split.transparentIndices.size() == 30, "incorrect transparent face count");
        require(split.opaqueVertices.size() == 72 && split.transparentVertices.size() == 60,
                "opaque and transparent vertex data were not separated");
        for (size_t i = 2; i < split.transparentVertices.size(); i += 3) {
            require(split.transparentVertices[i].a < 1.f, "water color has no transparency");
        }
        mixed.setLocalBlockAt(1, 10, 1, WATER);
        split = mixed.buildMeshData();
        require(split.opaqueIndices.empty() && split.opaqueVertices.empty(), "water entered the opaque buffer");
        require(split.transparentIndices.size() == 60, "internal water faces were not removed");

        VBOResultCollection errors;
        pool.start(new VBOWorker(glm::ivec2(0, 0), {}, {}, errors));
        pool.waitForDone();
        require(errors.tryPop(mesh) && !mesh.error.empty(), "worker failure did not reach the main thread");
        for (int i = 0; i < 3; ++i) {
            Terrain terrain(nullptr);
            terrain.expandTerrain(glm::vec3(0, 150, 0));
        }
        std::cout << "passed: shared chunk pointers, zone workers, four mesh vectors, boundary faces, snapshot isolation, errors, and shutdown\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
