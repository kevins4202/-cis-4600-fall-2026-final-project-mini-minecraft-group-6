#pragma once
#include "glm_includes.h"

// Hashes a 2D point to a pseudo-random 2D vector
glm::vec2 random2(glm::vec2 p);

// Perlin noise
float perlinNoise2D(glm::vec2 p);

// Fractal Brownian motion
float fbmAbsPerlin(glm::vec2 p, int octaves = 8);

// Worley noise
float worleyNoise(glm::vec2 uv);
