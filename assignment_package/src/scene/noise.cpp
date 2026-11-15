#include "noise.h"

glm::vec2 random2(glm::vec2 p) {
    return glm::fract(glm::sin(glm::vec2(glm::dot(p, glm::vec2(127.1f, 311.7f)),
                                         glm::dot(p, glm::vec2(269.5f, 183.3f))))
                      * 43758.5453f);
}

static float surflet(glm::vec2 P, glm::vec2 gridPoint) {
    float distX = glm::abs(P.x - gridPoint.x);
    float distY = glm::abs(P.y - gridPoint.y);
    float tX = 1.f - 6.f * distX*distX*distX*distX*distX + 15.f * distX*distX*distX*distX - 10.f * distX*distX*distX;
    float tY = 1.f - 6.f * distY*distY*distY*distY*distY + 15.f * distY*distY*distY*distY - 10.f * distY*distY*distY;
    glm::vec2 gradient = 2.f * random2(gridPoint) - glm::vec2(1.f);
    glm::vec2 diff = P - gridPoint;
    float height = glm::dot(diff, gradient);
    return height * tX * tY;
}

float perlinNoise2D(glm::vec2 uv) {
    float surfletSum = 0.f;
    for (int dx = 0; dx <= 1; ++dx) {
        for (int dy = 0; dy <= 1; ++dy) {
            surfletSum += surflet(uv, glm::floor(uv) + glm::vec2(dx, dy));
        }
    }
    return surfletSum;
}

float fbmAbsPerlin(glm::vec2 p, int octaves) {
    float total = 0.f;
    float persistence = 0.5f;
    float freq = 2.f;
    float amp = 0.5f;
    for (int i = 1; i <= octaves; ++i) {
        total += glm::abs(perlinNoise2D(p * freq)) * amp;
        freq *= 2.f;
        amp *= persistence;
    }
    return total;
}

float worleyNoise(glm::vec2 uv) {
    glm::vec2 uvInt = glm::floor(uv);
    glm::vec2 uvFract = glm::fract(uv);
    float minDist = 1.f;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            glm::vec2 neighbor = glm::vec2(float(x), float(y));
            glm::vec2 point = random2(uvInt + neighbor);
            glm::vec2 diff = neighbor + point - uvFract;
            float dist = glm::length(diff);
            minDist = glm::min(minDist, dist);
        }
    }
    return minDist;
}
