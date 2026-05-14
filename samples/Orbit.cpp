#include "Compositor.h"
#include "Renderer.h"
#include "PropKit.h"
#include "ECS.h"
#include "GltfLoader.h"
#include "ImGuiRenderer.h"
#include "Level.h"
#include "MouseNav.h"
#include "PerfMetrics.h"
#include "Projection.h"
#include "Scene.h"
#include "scope_exit.h"
#include "Shapes.h"
#include "Stopwatch.h"
#include "WebgpuHelper.h"
#include "VecMath.h"

#include <algorithm>
#include <filesystem>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <random>
#include <SDL3/SDL.h>
#include <SDL3/SDL_mouse.h>
#include <thread>

static constexpr const char* APP_NAME = "Orbit";

static constexpr float PHYSICS_FPS = 100.0f;
static constexpr float RENDER_FPS = 60.0f;

namespace
{

class WorldMatrix : public Mat44f
{
public:
    using Mat44f::Mat44f;

    WorldMatrix& operator=(const Mat44& that)
    {
        this->Mat44f::operator=(that);
        return *this;
    }
};

// Used to tag entities that represent loaded models in the ECS registry.
struct ModelTag{};

}

static Result<>
Startup()
{
    Log::SetLevel(Log::Level::Trace);

    auto cwd = std::filesystem::current_path();
    MLG_INFO("Current working directory: {}", cwd.string());

    MLG_CHECK(WebgpuHelper::Startup(APP_NAME));

    return Result<>::Ok;
}

static void
Shutdown()
{
    WebgpuHelper::Shutdown();
}

static Result<> RenderGui();

static Result<>
Load(const std::filesystem::path& path,
    TextureCache& textureCache,
    PropKit& outPropKit,
    Level& outLevel)
{
    auto shape = Shapes::Ball(1.0f, 10);

    PropKitDef propKitDef //
        {
            .ModelDefs //
            {
                {
                    .Name{ "Shape" },
                    .MeshDefs //
                    {
                        {
                            .Vertices{ shape.GetVertices().begin(), shape.GetVertices().end() },
                            .Indices{ shape.GetIndices().begin(), shape.GetIndices().end() },
                        },
                    },
                },
            },
        };

    MLG_CHECK(PropKit::Create(path.parent_path(), textureCache, propKitDef, outPropKit),
        "Failed to create PropKit for {}",
        path.string());

    std::mt19937 gen(12345); // Fixed seed for reproducibility
    std::uniform_real_distribution<float> dis(-1, 1);

    constexpr int GRID_SIZE = 20;
    constexpr float MAX_RADIUS = 1.0f;
    constexpr float MIN_RADIUS = 0.1f;
    constexpr float MAX_SPEED = 0.5f;
    constexpr float MIN_SPEED = 0.1f;
#ifndef NDEBUG
    // Reduce the number of bodies in debug builds to improve performance.
    constexpr size_t NUM_BODIES = 500;
#else
    constexpr size_t NUM_BODIES = 1000;
#endif

    std::vector<LevelNodeDef> nodeDefs;
    nodeDefs.reserve(NUM_BODIES);
    for(size_t i = 0; i < nodeDefs.capacity(); ++i)
    {
        const float radius = MIN_RADIUS + std::abs(dis(gen)) * (MAX_RADIUS - MIN_RADIUS);
        const float mass = radius;
        const Vec3f position{ dis(gen) * GRID_SIZE, dis(gen) * GRID_SIZE, dis(gen) * GRID_SIZE };
        const Vec3f velocity = Vec3f{ dis(gen), dis(gen), dis(gen) }.Normalize() *
                               (MIN_SPEED + std::abs(dis(gen)) * (MAX_SPEED - MIN_SPEED));

        LevelNodeDef nodeDef//
        {
            .Name{ std::format("Body{}", i) },
            .Transform{ .T{position}, .S{ radius } },
            .Components //
            {
                .Model = ModelRef{ .Name = "Shape" },
                .Body = RigidBodyDef{ .Velocity{ velocity }, .Mass{ mass } },
                .Collider = ColliderDef{ SphereDef{ .Radius = radius } },
            },
        };

        nodeDefs.emplace_back(std::move(nodeDef));
    }

    LevelDef levelDef //
    {
        .NodeDefs = std::move(nodeDefs),
    };

    MLG_CHECK(Level::Create(levelDef, outPropKit, outLevel),
        "Failed to create Level for {}",
        path.string());

    return Result<>::Ok;
}

class BodyPair
{
public:

    BodyPair(size_t indexA, size_t indexB)
    {
        if(indexA < indexB)
        {
            m_IndexA = indexA;
            m_IndexB = indexB;
        }
        else
        {
            m_IndexA = indexB;
            m_IndexB = indexA;
        }
    }

    size_t IndexA() const { return m_IndexA; }
    size_t IndexB() const { return m_IndexB; }

    bool operator==(const BodyPair& that) const
    {
        return (m_IndexA == that.m_IndexA && m_IndexB == that.m_IndexB);
    }

    bool operator!=(const BodyPair& that) const
    {
        return !(*this == that);
    }

    auto operator<=>(const BodyPair& that) const
    {
        if(m_IndexA != that.m_IndexA)
        {
            return m_IndexA <=> that.m_IndexA;
        }

        return m_IndexB <=> that.m_IndexB;
    }

private:

    size_t m_IndexA;
    size_t m_IndexB;
};

struct ImpactResult
{
    float Alpha; // Distance along path at impact, from 0 to 1.
    float PenetrationDepth;
    Vec3f ContactPoint;
    Vec3f ContactNormal;
    Vec3f PosAtImpactA;
    Vec3f PosAtImpactB;
};

struct Simulation
{
    static Result<> Create(const Level& level, Simulation& outSim);

    Simulation() = default;
    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;
    Simulation(Simulation&& other) = default;
    Simulation& operator=(Simulation&& other) = default;

    void DoCollisions(const float dt);

    void UpdatePositions(const float dt);

    void ApplyForces(void (*ApplyForceFunc)(const Simulation& sim, std::span<Vec3f> forces))
    {
        ApplyForceFunc(*this, m_A1);
    }

    void UpdateVelocities(const float dt);

    Result<> SyncToLevel(Level& level);

private:

    Simulation(std::vector<Level::NodeHandle>&& nodeHandles,
        std::vector<TrsTransformf>&& transforms,
        std::vector<RigidBody>&& bodies,
        std::vector<Collider>&& colliders)
        : m_NodeHandles(std::move(nodeHandles)),
          m_Bodies(std::move(bodies)),
          m_Colliders(std::move(colliders))
    {
        m_TransformPool[0] = std::move(transforms);
        m_TransformPool[1] = m_TransformPool[0];
        m_APool[0].resize(m_Bodies.size(), Vec3f{ 0 });
        m_APool[1].resize(m_Bodies.size(), Vec3f{ 0 });
        m_T0 = m_TransformPool[0];
        m_T1 = m_TransformPool[1];
        m_A0 = m_APool[0];
        m_A1 = m_APool[1];
    }

    bool SphereSphereSweep(const BodyPair& pair, ImpactResult& impactResult) const;

public:

    std::vector<Level::NodeHandle> m_NodeHandles;
    std::vector<TrsTransformf> m_TransformPool[2];
    std::vector<Vec3f> m_APool[2];
    std::vector<RigidBody> m_Bodies;
    std::vector<Collider> m_Colliders;
    std::span<TrsTransformf> m_T0;
    std::span<TrsTransformf> m_T1;
    std::span<Vec3f> m_A0;
    std::span<Vec3f> m_A1;
};

Result<>
Simulation::Create(const Level& level, Simulation& outSim)
{
    std::span<const Level::NodeHandle> allHandles = level.GetAllHandles();

    size_t count = 0;
    for(const auto& handle : allHandles)
    {
        const auto& node = level.GetNode(handle);
        if(node->Components.Body)
        {
            ++count;
        }
    }

    std::vector<Level::NodeHandle> nodeHandles;
    std::vector<TrsTransformf> transforms;
    std::vector<RigidBody> bodies;
    std::vector<Collider> colliders;
    nodeHandles.reserve(count);
    transforms.reserve(count);
    bodies.reserve(count);
    colliders.reserve(count);

    for(const auto& handle : allHandles)
    {
        const auto& node = level.GetNode(handle);
        MLG_ASSERT((node->Components.Body && node->Components.Collider)
            || (!node->Components.Body && !node->Components.Collider),
            "Node {} has Body component but no Collider, or vice versa",
            node->Name);

        if(!node->Components.Body)
        {
            continue;
        }

        nodeHandles.emplace_back(handle);
        transforms.emplace_back(node->LocalTransform);
        bodies.emplace_back(*node->Components.Body);
        colliders.emplace_back(*node->Components.Collider);
    }

    Simulation sim(std::move(nodeHandles),
        std::move(transforms),
        std::move(bodies),
        std::move(colliders));

    outSim = std::move(sim);

    return Result<>::Ok;
}

void
Simulation::UpdatePositions(const float dt)
{
    for (size_t i = 0; i < m_Bodies.size(); ++i)
    {
        // Update position using velocity and acceleration from previous time step.
        // p = ∫ v dt
        // v = v0 + a * t
        // p1 = ∫ (v0 + a * t) dt = v0 * dt + (a * dt^2) / 2 + p0
        m_T1[i].T = m_T0[i].T + (m_Bodies[i].Velocity * dt) + ((m_A0[i] * dt * dt) / 2);
    }
}

void
Simulation::UpdateVelocities(const float dt)
{
    for (size_t i = 0; i < m_Bodies.size(); ++i)
    {
        // Update velocity using average of acceleration from previous and current time step.
        // dt * ((a0 + a1) / m) / 2
        m_Bodies[i].Velocity += (m_A0[i] + m_A1[i]) * 0.5f * dt;
    }

    std::swap(m_A0, m_A1);
    std::fill(m_A1.begin(), m_A1.end(), Vec3f{ 0 });
}

Result<>
Simulation::SyncToLevel(Level& level)
{
    for(size_t i = 0; i < m_NodeHandles.size(); ++i)
    {
        const auto& handle = m_NodeHandles[i];
        const TrsTransformf& xform = m_T0[i];
        MLG_CHECK(level.UpdateLocalTransform(handle, xform));
    }

    return Result<>::Ok;
}

bool
Simulation::SphereSphereSweep(const BodyPair& pair, ImpactResult& impactResult) const
{
    constexpr float EPSILON = 1e-6f;
    constexpr float EPSILON_SQ = EPSILON * EPSILON;

    // p0 = relative position at t0.
    // p1 = relative position at t1.
    // relMo = p1 - p0.  Relative motion over the time step.
    // r = radiusA + radiusB.
    // At time of impact t distance between centers is equal to sum of radii.
    // t * relMo + p0 = r
    //Equivalently:
    // (t * relMo + p0)^2 = r^2
    // t^2 * relMo.Dot(relMo) + 2 * t * relMo.Dot(p0) + p0.Dot(p0) - r^2 = 0

    // Quadratic equation terms a*t^2 + 2b*t + c = 0:
    // a = relMo.Dot(relMo)
    // b = 2 * relMo.Dot(p0)
    // c = p0.Dot(p0) - r^2
    //
    // Solve the quadratic equation for t.

    const Collider& colliderA = m_Colliders[pair.IndexA()];
    const Collider& colliderB = m_Colliders[pair.IndexB()];

    const float radiusA = std::get<SphereCollider>(colliderA.Shape).Radius;
    const float radiusB = std::get<SphereCollider>(colliderB.Shape).Radius;

    const TrsTransformf& transformA0 = m_T0[pair.IndexA()];
    const TrsTransformf& transformA1 = m_T1[pair.IndexA()];
    const TrsTransformf& transformB0 = m_T0[pair.IndexB()];
    const TrsTransformf& transformB1 = m_T1[pair.IndexB()];

    const Vec3 p0 = transformA0.T - transformB0.T;
    const Vec3 p1 = transformA1.T - transformB1.T;
    const Vec3 relMo = p1 - p0;
    const float r = radiusA + radiusB;
    const float r2 = r * r;
    const float dist0Sqr = p0.Dot(p0);

    // "c" term of the quadratic equation.
    // Square distance between centers at start of time step minus square of sum of radii.
    const float c = dist0Sqr - r2; // If <= 0, already overlapping at start of time step.

    if(c <= 0)
    {
        const float dist0 = std::sqrtf(dist0Sqr);

        //Overlapping during the time step.  We can treat this as an immediate collision at t=0.
        impactResult.Alpha = 0.0f;
        if(dist0 < EPSILON)
        {
            // Centers are extremely close.  Try setting contact normal based on relative motion.
            const float relMoLenSq = relMo.Dot(relMo);
            if (relMoLenSq > EPSILON_SQ)
            {
                // Relative motion is also extremely small.  Just pick an arbitrary contact normal.
                impactResult.ContactNormal = Vec3f{ 1, 0, 0 };
            }
            else
            {
                impactResult.ContactNormal = relMo / std::sqrtf(relMoLenSq);
            }
        }
        else
        {
            impactResult.ContactNormal = p0 / dist0;
        }

        impactResult.ContactPoint = transformB0.T + impactResult.ContactNormal * radiusB;

        impactResult.PenetrationDepth = r - dist0;
        impactResult.PosAtImpactA = transformA0.T;
        impactResult.PosAtImpactB = transformB0.T;

        return true;
    }

    // "a" term of the quadratic equation - Squared distance moved.
    const float a = relMo.Dot(relMo);
    if(a < EPSILON_SQ)
    {
        // No relative motion.  Can't collide if not already overlapping.
        return false;
    }

    // "b" term of the quadratic equation.
    // Projection of the vector from B0 to A0, which is the initial relative position, onto
    // the relative motion vector from (A0-B0) to (A1-B1).
    const float b = 2.0f * relMo.Dot(p0);
    if(b > 0)
    {
        // Moving apart.  Can't collide.
        return false;
    }

    // Quadratic formula:
    // t = -b (+/-) sqrt(b^2 - 4ac) / (2a)

    const float discriminant = b*b - 4*a*c;

    if (discriminant < EPSILON)
    {
        // No real roots, so no collision.
        return false;
    }

    // -b - sqrt(b^2 - 4ac) / 2a is the entry point.
    // -b + sqrt(b^2 - 4ac) / 2a is the exit point.
    // We want the entry point.
    const float t = (-b - std::sqrtf(discriminant)) / (2 * a);

    if(t < 0 || t > 1)
    {
        // Collision occurs outside of time step.
        return false;
    }

    // Time of impact within the timestep.
    impactResult.Alpha = t;

    // Centers at time of impact.
    const Vec3f centerA = transformA0.T + (transformA1.T - transformA0.T) * t;
    const Vec3f centerB = transformB0.T + (transformB1.T - transformB0.T) * t;

    // Vector between centers.
    const Vec3f n = centerA - centerB;
    const float nLenSq = n.Dot(n);
    const float nLen = std::sqrtf(nLenSq);
    impactResult.ContactNormal = nLen > EPSILON ? n / nLen : Vec3f{ 1.0f, 0.0f, 0.0f };
    impactResult.ContactPoint = centerB + impactResult.ContactNormal * radiusB;
    impactResult.PenetrationDepth = r - nLen;
    impactResult.PosAtImpactA = centerA;
    impactResult.PosAtImpactB = centerB;

    return true;
}

template<size_t CELL_SIZE>
struct CellCoord_T
{
    static constexpr size_t CELL_SIZE = CELL_SIZE;

    int x;
    int y;
    int z;

    static int Quantize(float value)
    {
        return static_cast<int>(std::floor(value / static_cast<float>(CELL_SIZE)));
    }

    bool operator==(const CellCoord_T& that) const
    {
        return x == that.x && y == that.y && z == that.z;
    }

    bool operator!=(const CellCoord_T& that) const
    {
        return !(*this == that);
    }

    auto operator<=>(const CellCoord_T& that) const
    {
        if(x != that.x)
        {
            return x <=> that.x;
        }

        if (y != that.y)
        {
            return y <=> that.y;
        }

        return z <=> that.z;
    }
};

using CellCoord = CellCoord_T<5>;

struct CellEntry
{
    size_t BodyIndex;
    CellCoord Cell;

    auto operator<=>(const CellEntry& that) const
    {
        if(Cell <=> that.Cell != 0)
        {
            return Cell <=> that.Cell;
        }

        return BodyIndex <=> that.BodyIndex;
    }
};

static std::vector<CellEntry> cellEntries;
static std::vector<BodyPair> potentialCollisions;

void
Simulation::DoCollisions([[maybe_unused]] const float dt)
{
    cellEntries.clear();
    potentialCollisions.clear();

    auto getCellCoords = [this](const size_t index, CellCoord& minCoord, CellCoord& maxCoord) -> void
    {
        const Vec3f p0 = m_T0[index].T;
        const Vec3f p1 = m_T1[index].T;
        const float radius = std::get<SphereCollider>(m_Colliders[index].Shape).Radius;

        const Vec3f maxExtent //
            {
                std::max(p0.x, p1.x) + radius,
                std::max(p0.y, p1.y) + radius,
                std::max(p0.z, p1.z) + radius,
            };
        const Vec3f minExtent //
            {
                std::min(p0.x, p1.x) - radius,
                std::min(p0.y, p1.y) - radius,
                std::min(p0.z, p1.z) - radius,
            };

        const int minX = CellCoord::Quantize(minExtent.x);
        const int minY = CellCoord::Quantize(minExtent.y);
        const int minZ = CellCoord::Quantize(minExtent.z);
        const int maxX = CellCoord::Quantize(maxExtent.x);
        const int maxY = CellCoord::Quantize(maxExtent.y);
        const int maxZ = CellCoord::Quantize(maxExtent.z);

        minCoord = CellCoord{ minX, minY, minZ };
        maxCoord = CellCoord{ maxX, maxY, maxZ };
    };

    for(size_t i = 0; i < m_Bodies.size(); ++i)
    {
        CellCoord minCoord, maxCoord;
        getCellCoords(i, minCoord, maxCoord);

        for(int x = minCoord.x; x <= maxCoord.x; ++x)
        {
            for(int y = minCoord.y; y <= maxCoord.y; ++y)
            {
                for(int z = minCoord.z; z <= maxCoord.z; ++z)
                {
                    cellEntries.push_back({ i, CellCoord{ x, y, z } });
                }
            }
        }
    }

    std::sort(cellEntries.begin(), cellEntries.end());

    for(size_t i = 0; i < cellEntries.size(); ++i)
    {
        const size_t indexA = cellEntries[i].BodyIndex;

        for(size_t j = i + 1; j < cellEntries.size() && cellEntries[j].Cell == cellEntries[i].Cell; ++j)
        {
            // Bodies that share a cell are potentially colliding.
            // We can add them to a list of pairs to check for collision.
            // We may have duplicates, but we'll sort and deduplicate the pairs later.

            const size_t indexB = cellEntries[j].BodyIndex;
            if(MLG_VERIFY(indexA != indexB, "Duplicate body in same cell? Body index: {}", indexA))
            {
                potentialCollisions.push_back({ indexA, indexB });
            }
        }
    }

    std::sort(potentialCollisions.begin(), potentialCollisions.end());

    /*for(size_t i = 0; i < m_Bodies.size(); ++i)
    {
        ColliderPair pair //
            {
                m_Bodies[i],
                m_Colliders[i],
                m_T0[i],
                m_T1[i],
            };

        for(size_t j = i + 1; j < m_Bodies.size(); ++j)
        {
            pair.BodyB = m_Bodies[j];
            pair.ColliderB = m_Colliders[j];
            pair.TransformB0 = m_T0[j];
            pair.TransformB1 = m_T1[j];

            ImpactResult impactResult;

            if(!SphereSphereSweep(pair, impactResult))
            {
                continue;
            }

            const BodyPair bodyPair{ i, j };
            auto it = std::find(potentialCollisions.begin(), potentialCollisions.end(), bodyPair);

            if(!MLG_VERIFY(it != potentialCollisions.end(),
                "Body pair {},{} is colliding but not in potential collisions list",
                bodyPair.IndexA, bodyPair.IndexB))
            {
                CellCoord minCoordA, maxCoordA;
                CellCoord minCoordB, maxCoordB;
                getCellCoords(i, minCoordA, maxCoordA);
                getCellCoords(j, minCoordB, maxCoordB);
                continue;
            }
        }
    }*/

    BodyPair lastBodyPair{ std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max() };

    for(size_t i = 0; i < potentialCollisions.size(); ++i)
    {
        const BodyPair& bodyPair = potentialCollisions[i];
        if(bodyPair == lastBodyPair)
        {
            // We've already checked this pair for collision.
            continue;
        }

        lastBodyPair = bodyPair;

        ImpactResult impactResult;

        if(!SphereSphereSweep(bodyPair, impactResult))
        {
            continue;
        }

        const size_t indexA = bodyPair.IndexA();
        const size_t indexB = bodyPair.IndexB();

        RigidBody& bodyA = m_Bodies[indexA];
        RigidBody& bodyB = m_Bodies[indexB];

        // Compute relative velocity along the normal
        const float vRel = (bodyA.Velocity - bodyB.Velocity).Dot(impactResult.ContactNormal);

        // Only resolve if bodies are moving towards each other
        if (vRel < 0)
        {
            const float mA = bodyA.Mass.Value();
            const float mB = bodyB.Mass.Value();

            // Impulse
            constexpr float e = 0.8f;//0.5f; // Coefficient of restitution
            const float impulse = -(1 + e) * vRel * (mA * mB) / (mA + mB);

            // Move bodies to point of impact.
            m_T1[indexA].T = impactResult.PosAtImpactA;
            m_T1[indexB].T = impactResult.PosAtImpactB;

            // Reflect velocities along the contact normal.
            bodyA.Velocity += (impulse * impactResult.ContactNormal) * bodyA.Mass.InvValue();
            bodyB.Velocity -= (impulse * impactResult.ContactNormal) * bodyB.Mass.InvValue();
        }

        constexpr float correctionSlop = 1e-3f;

        if(impactResult.PenetrationDepth > correctionSlop)
        {
            constexpr float positionalCorrectionPercent = 0.1f;

            const float correctionMagnitude =
                std::max(0.0f, impactResult.PenetrationDepth - correctionSlop) *
                positionalCorrectionPercent;

            //const float mA = bodyA.Mass.Value();
            //const float mB = bodyB.Mass.Value();
            const float invMA = bodyA.Mass.InvValue();
            const float invMB = bodyB.Mass.InvValue();
            const float invMassSum = invMA + invMB;

            // Inverse mass sum = (1/mA + 1/mB) = (mA + mB) / (mA * mB)
            //const float recipInvMassSum = (mA + mB) * (invMA * invMB);

            //const Vec3f correction = correctionMagnitude * impactResult.ContactNormal * recipInvMassSum;
            const Vec3f correction = correctionMagnitude * impactResult.ContactNormal / invMassSum;

            m_T1[indexA].T += correction * invMA;
            m_T1[indexB].T -= correction * invMB;
        }
    }

    std::swap(m_T0, m_T1);
}

static constexpr float G = 0.01f;//6.674e-11f;        // Gravitational constant (m^3 kg^-1 s^-2)

[[maybe_unused]] static void ApplyGravity(const Simulation& sim, std::span<Vec3f> accel)
{
    MLG_ASSERT(accel.size() >= sim.m_Bodies.size());

    // Compute gravitational forces between all pairs of bodies.
    for(size_t i = 0; i < sim.m_Bodies.size(); ++i)
    {
        for(size_t j = i + 1; j < sim.m_Bodies.size(); ++j)
        {
            const float radiusA = std::get<SphereCollider>(sim.m_Colliders[i].Shape).Radius;
            const float radiusB = std::get<SphereCollider>(sim.m_Colliders[j].Shape).Radius;
            const float minSeparation = radiusA + radiusB;
            const float minSeparationSq = minSeparation * minSeparation;

            // Vector from body i to body j
            const Vec3f delta = sim.m_T0[j].T - sim.m_T0[i].T;

            // Gravitational force magnitude: F = G * (M * m) / r^2
            // Direction toward source: delta / r
            // Combined vector form: F = G * M * m * delta / r^3

            // If bodies overlap clamp to minimum separation.
            const float r2 = std::max(delta.Dot(delta), minSeparationSq);
            const float massProduct = sim.m_Bodies[i].Mass.Value() * sim.m_Bodies[j].Mass.Value();
            const Vec3f F = G * massProduct * delta / (r2 * std::sqrtf(r2));

            accel[i] += F * sim.m_Bodies[i].Mass.InvValue();
            accel[j] -= F * sim.m_Bodies[j].Mass.InvValue();
        }
    }
}

static Result<>
MainLoop()
{
    bool running = true;
    bool minimized = false;

    Renderer renderer;
    Compositor compositor;
    ImGuiRenderer imGuiRenderer;
    TextureCache textureCache;
    PropKit propKit;
    Level level;
    Scene scene;
    Simulation simulation;
    EcsRegistry registry;
    WalkMouseNav mouseNav;

    MLG_CHECK(renderer.Startup());
    MLG_CHECK(compositor.Startup());
    MLG_CHECK(imGuiRenderer.Startup());
    MLG_CHECK(textureCache.Startup());

    MLG_CHECK(Load("", textureCache, propKit, level));

    MLG_CHECK(Scene::Create(level, propKit, scene));

    MLG_CHECK(Simulation::Create(level, simulation));

    Entity model = registry.CreateEntity(TrsTransformf{}, WorldMatrix{}, ModelTag{});

    Entity camera = registry.CreateEntity(TrsTransformf{.T{0,0,-40}}, WorldMatrix{}, Projection{});

    mouseNav.SetTransform(camera.Get<TrsTransformf>());

    uint64_t frameBeginTicks = SDL_GetTicksNS();

    bool mouseCaptured = true;
    SDL_SetWindowRelativeMouseMode(WebgpuHelper::GetWindow(), mouseCaptured);

    while(running)
    {
        PerfMetrics::BeginFrame();

        static PerfTimer frameTimer("Frame");
        frameTimer.Start();

        const uint64_t curTicksNs = SDL_GetTicksNS();
        const uint64_t elapsedTicksNs = curTicksNs - frameBeginTicks;
        const float elapsedSeconds = SDL_NS_TO_SECONDS(static_cast<float>(elapsedTicksNs));
        frameBeginTicks = curTicksNs;

        SDL_Event event;

        while(minimized && running && SDL_PollEvent(&event))
        {
            switch(event.type)
            {
                case SDL_EVENT_WINDOW_RESTORED:
                case SDL_EVENT_WINDOW_MAXIMIZED:
                    minimized = false;
                    break;
            }
        }

        if(minimized)
        {
            std::this_thread::yield();
            continue;
        }

        while(!minimized && running && SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_EVENT_QUIT:
                running = false;
                break;

            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                //app->OnResize(event.window.data1, event.window.data2);
                break;

            case SDL_EVENT_WINDOW_MINIMIZED:
                minimized = true;
                break;

            //case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                mouseNav.ClearButtons();
                break;

            case SDL_EVENT_WINDOW_FOCUS_LOST:
                mouseNav.ClearButtons();
                break;

            case SDL_EVENT_MOUSE_MOTION:
                if(mouseCaptured)
                {
                    mouseNav.OnMouseMove(Vec2f(event.motion.xrel, event.motion.yrel));
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                if(mouseCaptured)
                {
                    mouseNav.OnScroll(Vec2f(event.wheel.x, event.wheel.y));
                }
                break;

            case SDL_EVENT_KEY_DOWN:
                if(mouseCaptured)
                {
                    mouseNav.OnKeyDown(event.key.scancode);
                }

                if(SDL_SCANCODE_ESCAPE == event.key.scancode)
                {
                    running = false;
                }
                else if(SDL_SCANCODE_SPACE == event.key.scancode)
                {
                    mouseCaptured = !mouseCaptured;
                    SDL_SetWindowRelativeMouseMode(WebgpuHelper::GetWindow(), mouseCaptured);
                }
                break;
            case SDL_EVENT_KEY_UP:
                if(mouseCaptured)
                {
                    mouseNav.OnKeyUp(event.key.scancode);
                }
                break;
            }
        }

        constexpr float physicsTimeStep = 1.0f/60;//1.0f / PHYSICS_FPS;

        simulation.UpdatePositions(physicsTimeStep);

        simulation.DoCollisions(physicsTimeStep);

        simulation.ApplyForces(ApplyGravity);

        simulation.UpdateVelocities(physicsTimeStep);

        simulation.SyncToLevel(level);

        scene.SyncFromLevel(level);

        mouseNav.Update(elapsedSeconds);

        auto screenBounds = WebgpuHelper::GetScreenBounds();
        const float aspectRatio = screenBounds.Width / screenBounds.Height;
        camera.Get<Projection>().SetAspectRatio(aspectRatio);
        camera.Get<TrsTransformf>() = mouseNav.GetTransform();

        // Transform roots
        for(const auto& tuple : registry.GetView<TrsTransformf, WorldMatrix>())
        {
            auto [eid, xform, worldMat] = tuple;
            worldMat = xform.ToMatrix();
        }

        compositor.BeginFrame();
        imGuiRenderer.NewFrame();

        scene.SyncToGpu();

        const auto& camWorldMat = camera.Get<WorldMatrix>();
        const auto& projection = camera.Get<Projection>();
        for(const auto& tuple : registry.GetView<WorldMatrix, ModelTag>())
        {
            const auto [eid, worldMat, modelTag] = tuple;

            renderer.Render(camWorldMat, projection, scene, propKit, compositor);
        }

        RenderGui();

        imGuiRenderer.Render(compositor);

        compositor.EndFrame();

#if !defined(__EMSCRIPTEN__)
        MLG_CHECK(WebgpuHelper::GetSurface().Present(), "Failed to present backbuffer");
#endif

        WebgpuHelper::GetInstance().ProcessEvents();

        frameTimer.Stop();

        PerfMetrics::EndFrame();
    }

    MLG_CHECK(textureCache.Shutdown());
    MLG_CHECK(imGuiRenderer.Shutdown());
    MLG_CHECK(compositor.Shutdown());
    MLG_CHECK(renderer.Shutdown());

    PerfMetrics::LogTimers();

    return Result<>::Ok;
}

static Result<> RenderGui()
{
    const char* buildType;
#if defined (NDEBUG)
    buildType = "Release";
#else
    buildType = "Debug";
#endif

    constexpr const char* backend = "Dawn";

    auto title = std::format("Timers: {}/{}", buildType, backend);

    ImGui::SetNextWindowSize(ImVec2(0, 0)); // Auto-fit both width and height
    ImGui::Begin(title.c_str());
    PerfMetrics::TimerStat timers[256];
    unsigned timerCount = PerfMetrics::GetTimers(timers, std::size(timers));
    for(unsigned i = 0; i < timerCount; ++i)
    {
        ImGui::Text("%s: %.3f ms", timers[i].GetName().c_str(), timers[i].GetValue() * 1000.0f);
    }
    ImGui::End();

    return Result<>::Ok;
}

int main(int /*argc*/, char** /*argv*/)
{
    if(!Startup())
    {
        return -1;
    }

    auto shutdown = scope_exit([]()
    {
        Shutdown();
    });

    if(!MainLoop())
    {
        return -1;
    }

    return 0;
}