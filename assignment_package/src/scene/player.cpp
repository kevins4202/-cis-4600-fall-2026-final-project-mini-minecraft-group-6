#include "player.h"
#include <QString>

#include <algorithm>
#include <cmath>

static constexpr float HALF_WIDTH = 0.3f;

Player::Player(glm::vec3 pos, const Terrain &terrain)
    : Entity(pos), m_velocity(0,0,0), m_acceleration(0,0,0),
      m_camera(pos + glm::vec3(0, 1.5f, 0)), mcr_terrain(terrain),
      m_pitch(0.f), mcr_camera(m_camera)
{}

Player::~Player()
{}

void Player::tick(float dT, InputBundle &input) {
    processInputs(input);
    computePhysics(dT, mcr_terrain);
}

void Player::processInputs(InputBundle &inputs) {
    m_acceleration = glm::vec3(0.f);

    m_flightMode = inputs.flightMode;

    glm::vec3 forward = m_forward;
    glm::vec3 right = m_right;

    if (!m_flightMode) {
        forward.y = 0;
        right.y = 0;

        if (glm::length(forward) > 0.0001f) {
            forward = glm::normalize(forward);
        } else {
            forward = glm::vec3(0.f);
        }

        if (glm::length(right) > 0.0001f) {
            right = glm::normalize(right);
        } else {
            right = glm::vec3(0.f);
        }
    }

    // MOUSE MOVEMENT

    const float sensitivity = 0.15f;

    rotateOnUpGlobal(-inputs.mouseX * sensitivity);

    float newPitch = glm::clamp(m_pitch - inputs.mouseY * sensitivity, -89.f, 89.f);

    rotateOnRightLocal(newPitch - m_pitch);
    m_pitch = newPitch;

    // Consume and reset the movement
    inputs.mouseX = 0.f;
    inputs.mouseY = 0.f;

    const float acceleration = m_flightMode ? 25.f : 60.f;

    if (inputs.wPressed) m_acceleration += acceleration * forward;
    if (inputs.sPressed) m_acceleration -= acceleration * forward;
    if (inputs.dPressed) m_acceleration += acceleration * right;
    if (inputs.aPressed) m_acceleration -= acceleration * right;

    // e and q don't function outside of flight mode
    if (m_flightMode) {
        if (inputs.ePressed) m_acceleration += acceleration * m_up;
        if (inputs.qPressed) m_acceleration -= acceleration * m_up;
    }

    // When on the ground, and when space is pressed we can jump
    if (!m_flightMode && inputs.spacePressed && m_grounded) {
        m_velocity.y = 10.f;
        m_grounded = false;
    }

}

void Player::computePhysics(float dT, const Terrain &terrain) {
    if (!m_flightMode) {
        m_acceleration.y -= 25.f;
    }

    const float horizontalDrag = m_flightMode ? 3.f : 10.f;

    const float horizontalDamping =
        std::exp(-horizontalDrag * dT);

    m_velocity.x *= horizontalDamping;
    m_velocity.z *= horizontalDamping;
    m_velocity.y *= std::exp(-3.f * dT);

    m_velocity += m_acceleration * dT;

    glm::vec3 displacement = m_velocity * dT;

    if (m_flightMode) {
        m_grounded = false;
        moveAlongVector(displacement);
        return;
    }

    // recompute grounding later
    m_grounded = false;

    const int axes[] = {0, 2, 1};

    for (int axis : axes) {
        bool collided = false;

        const float allowed = allowedMovementOnAxis(axis, displacement[axis], terrain, collided);

        glm::vec3 movement(0.f);
        movement[axis] = allowed;

        moveAlongVector(movement);

        if (collided) {
            m_velocity[axis] = 0.f;

            // only a downward collision means standing on ground
            if (axis == 1 && displacement.y < 0.f) {
                m_grounded = true;
            }
        }
    }
}

void Player::setCameraWidthHeight(unsigned int w, unsigned int h) {
    m_camera.setWidthHeight(w, h);
}

void Player::moveAlongVector(glm::vec3 dir) {
    Entity::moveAlongVector(dir);
    m_camera.moveAlongVector(dir);
}
void Player::moveForwardLocal(float amount) {
    Entity::moveForwardLocal(amount);
    m_camera.moveForwardLocal(amount);
}
void Player::moveRightLocal(float amount) {
    Entity::moveRightLocal(amount);
    m_camera.moveRightLocal(amount);
}
void Player::moveUpLocal(float amount) {
    Entity::moveUpLocal(amount);
    m_camera.moveUpLocal(amount);
}
void Player::moveForwardGlobal(float amount) {
    Entity::moveForwardGlobal(amount);
    m_camera.moveForwardGlobal(amount);
}
void Player::moveRightGlobal(float amount) {
    Entity::moveRightGlobal(amount);
    m_camera.moveRightGlobal(amount);
}
void Player::moveUpGlobal(float amount) {
    Entity::moveUpGlobal(amount);
    m_camera.moveUpGlobal(amount);
}
void Player::rotateOnForwardLocal(float degrees) {
    Entity::rotateOnForwardLocal(degrees);
    m_camera.rotateOnForwardLocal(degrees);
}
void Player::rotateOnRightLocal(float degrees) {
    Entity::rotateOnRightLocal(degrees);
    m_camera.rotateOnRightLocal(degrees);
}
void Player::rotateOnUpLocal(float degrees) {
    Entity::rotateOnUpLocal(degrees);
    m_camera.rotateOnUpLocal(degrees);
}
void Player::rotateOnForwardGlobal(float degrees) {
    Entity::rotateOnForwardGlobal(degrees);
    m_camera.rotateOnForwardGlobal(degrees);
}
void Player::rotateOnRightGlobal(float degrees) {
    Entity::rotateOnRightGlobal(degrees);
    m_camera.rotateOnRightGlobal(degrees);
}
void Player::rotateOnUpGlobal(float degrees) {
    Entity::rotateOnUpGlobal(degrees);
    m_camera.rotateOnUpGlobal(degrees);
}

QString Player::posAsQString() const {
    std::string str("( " + std::to_string(m_position.x) + ", " + std::to_string(m_position.y) + ", " + std::to_string(m_position.z) + ")");
    return QString::fromStdString(str);
}
QString Player::velAsQString() const {
    std::string str("( " + std::to_string(m_velocity.x) + ", " + std::to_string(m_velocity.y) + ", " + std::to_string(m_velocity.z) + ")");
    return QString::fromStdString(str);
}
QString Player::accAsQString() const {
    std::string str("( " + std::to_string(m_acceleration.x) + ", " + std::to_string(m_acceleration.y) + ", " + std::to_string(m_acceleration.z) + ")");
    return QString::fromStdString(str);
}
QString Player::lookAsQString() const {
    std::string str("( " + std::to_string(m_forward.x) + ", " + std::to_string(m_forward.y) + ", " + std::to_string(m_forward.z) + ")");
    return QString::fromStdString(str);
}

// Collision helper

float Player::allowedMovementOnAxis(int axis, float amount, const Terrain &terrain, bool &collided) const
{
    collided = false;

    if (amount == 0.f) {
        return 0.f;
    }

    constexpr float EPS = 0.001f;

    const float sign = amount > 0.f ? 1.f : -1.f;
    float allowed = std::abs(amount);

    glm::vec3 ray(0.f);

    // Epsilon for contact margin
    // Don't return the full distance so we can't
    // clip into walls
    ray[axis] = sign * (allowed + EPS);

    const float ys[] = {EPS, 1.f, 2.f - EPS};
    const float xs[] = {-HALF_WIDTH + EPS, HALF_WIDTH - EPS};
    const float zs[] = {-HALF_WIDTH + EPS, HALF_WIDTH - EPS};

    for (float x : xs) {
        for (float y : ys) {
            for (float z : zs) {
                glm::vec3 origin =
                    m_position + glm::vec3(x, y, z);

                if (axis == 1) {
                    origin.y = m_position.y
                               + (sign > 0.f ? 2.f : 0.f);
                } else {
                    origin[axis] = m_position[axis] + sign * HALF_WIDTH;
                }

                RayHit hit;

                if (terrain.gridMarch(origin, ray, hit)) {
                    const float clearance =
                        std::max(0.f, hit.distance - EPS);

                    allowed = std::min(allowed, clearance);
                    collided = true;
                }
            }
        }
    }

    return sign * allowed;
}

// Break / place

bool Player::getTargetBlock(RayHit &hit) const
{
    return mcr_terrain.gridMarch(m_camera.mcr_position, glm::normalize(m_forward) * 3.f, hit);
}

bool Player::overlapsBlock(const glm::ivec3 &block) const
{
    const glm::vec3 playerMin = m_position + glm::vec3(-HALF_WIDTH, 0.f, -HALF_WIDTH);

    const glm::vec3 playerMax = m_position + glm::vec3(HALF_WIDTH, 2.f, HALF_WIDTH);

    const glm::vec3 blockMin = glm::vec3(block);
    const glm::vec3 blockMax = blockMin + glm::vec3(1.f);

    return playerMin.x < blockMax.x
           && playerMax.x > blockMin.x
           && playerMin.y < blockMax.y
           && playerMax.y > blockMin.y
           && playerMin.z < blockMax.z
           && playerMax.z > blockMin.z;
}