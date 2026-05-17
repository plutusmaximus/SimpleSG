#include "PhysicsSolver.h"

#include "PerfMetrics.h"
#include "ThreadPool.h"

#include <thread>

static constexpr float RESTING_VELOCITY_THRESHOLD = 0.01f;
static constexpr float COEFF_OF_RESTITUTION = 0.8f;

Result<>
PhysicsSolver::Create(const Level& level, PhysicsSolver& outSolver)
{
    std::span allHandles = level.GetAllHandles();

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

    PhysicsSolver solver(std::move(nodeHandles),
        std::move(transforms),
        std::move(bodies),
        std::move(colliders));

    outSolver = std::move(solver);

    return Result<>::Ok;
}

void
PhysicsSolver::AddForce(size_t bodyIndex, const Vec3f& force)
{
    MLG_ASSERT(bodyIndex < m_Bodies.size(), "Body index out of range");

    // Accumulate accelerations for the current frame.
    m_A0[bodyIndex] += force * m_Bodies[bodyIndex].Mass.InvValue();
}

void
PhysicsSolver::Update(const float timeStep)
{
    MLG_SCOPED_TIMER("Physics.Update");

    std::swap(m_Trs0, m_Trs1);
    // Update velocities based on accelerations accumulated this frame.
    UpdateVelocities(timeStep);
    PredictPositions(timeStep);
    FindAndResolveAllImpacts();
    std::swap(m_Am1, m_A0);
    std::fill(m_A0.begin(), m_A0.end(), Vec3f{ 0 });
}

Result<>
PhysicsSolver::SyncToLevel(Level& level)
{
    for(size_t i = 0; i < m_NodeHandles.size(); ++i)
    {
        MLG_CHECK(level.UpdateLocalTransform( m_NodeHandles[i], m_Trs0[i]));
    }

    return Result<>::Ok;
}

float
PhysicsSolver::ComputeKineticEnergy() const
{
    float totalEnergy = 0.0f;

    for(size_t i = 0; i < m_Bodies.size(); ++i)
    {
        const float mass = m_Bodies[i].Mass.Value();
        const float speedSq = m_Bodies[i].LinearVelocity.Dot(m_Bodies[i].LinearVelocity);
        totalEnergy += 0.5f * mass * speedSq;
    }

    return totalEnergy;
}

// private:

void
PhysicsSolver::UpdateVelocities(const float dt)
{
    MLG_SCOPED_TIMER("Physics.UpdateVelocities");

    // FIXME(KB) - caller should account for dt not being the entire timestep,
    // but a substep due to collision events.
    for (size_t i = 0; i < m_Bodies.size(); ++i)
    {
        // Acceleration can change over the timestep due to, e.g., gravity.
        // So it's incorrect to use a single acceleration over the timestep.
        // Instead we approximate the integral of the acceleration function
        // using the trapezoidal rule:
        // integral from t0 to t1 of a(t) dt ~= (t1 - t0) * (a(t0) + a(t1)) / 2

        // m_Am1 is from the previous frame, and m_A0 is from the current frame.
        m_Bodies[i].LinearVelocity += (m_Am1[i] + m_A0[i]) * dt * 0.5f;
    }
}

void
PhysicsSolver::PredictPositions(const float dt)
{
    MLG_SCOPED_TIMER("Physics.PredictPositions");

    for (size_t i = 0; i < m_Bodies.size(); ++i)
    {
        // Update position using velocity and acceleration from previous time step.
        // p = ∫ v dt
        // v = v0 + a * t
        // p1 = ∫ (v0 + a * t) dt = v0 * dt + (a * dt^2) / 2 + p0

        // Use velocity/acceleration from current frame.
        // Note that this frame's velocity was computed from
        // the previous and current frame's acceleration.
        m_Trs1[i].T = (m_Bodies[i].LinearVelocity * dt) + ((m_A0[i] * dt * dt) / 2) + m_Trs0[i].T;
    }
}

#define FIND_AND_RESOLVE_ALL_IMPACTS_MULTITHREADED 1

void
PhysicsSolver::ResolveImpact(const ImpactRecord& impact)
{
    //FIXME(KB) - make perf timers MT safe.
    MLG_SCOPED_TIMER("Physics.ResolveImpact");

    const BodyPair& bodyPair = impact.Bodies;
    const ImpactResult& impactResult = impact.Result;

    const size_t indexA = bodyPair.IndexA();
    const size_t indexB = bodyPair.IndexB();

    RigidBody& bodyA = m_Bodies[indexA];
    RigidBody& bodyB = m_Bodies[indexB];

    // Compute relative velocity along the normal
    const float vRel = (bodyA.LinearVelocity - bodyB.LinearVelocity).Dot(impactResult.ContactNormalBtoA);

    // Only resolve if bodies are moving towards each other
    if (vRel < -RESTING_VELOCITY_THRESHOLD)
    {
        // Move bodies to point of impact.
        m_Trs1[indexA].T = impactResult.PosAtImpactA;
        m_Trs1[indexB].T = impactResult.PosAtImpactB;

        // Impulse

        // n = contact normal
        // vrel ​= (vA​−vB​)⋅n
        // j = -(1 + e) * vrel / (1/mA + 1/mB)
        // vA' += n * (j / mA)
        // vA' += n * -(1 + e) * vRel * (1/mA) * (1 / (1/mA + 1/mB))
        // vA' += n * -(1 + e) * vRel * (1/(mA * (mA + mB)/(mA * mB)))
        // vA' += n * -(1 + e) * vRel * ((mA * mB)/(mA * (mA + mB))
        // vA' += n * -(1 + e) * vRel * mB/(mA + mB))

        const float k = -(1 + COEFF_OF_RESTITUTION) * vRel / (bodyA.Mass.Value() + bodyB.Mass.Value());
        const Vec3f u = k * impactResult.ContactNormalBtoA;

        bodyA.LinearVelocity += u * bodyB.Mass.Value();
        bodyB.LinearVelocity -= u * bodyA.Mass.Value();
    }
    else if(vRel < 0)
    {
        // Bodies are moving towards each other but below the resting velocity threshold, so we
        // treat this as a resting contact.

        const float k = -vRel / (bodyA.Mass.Value() + bodyB.Mass.Value());
        const Vec3f u = k * impactResult.ContactNormalBtoA;

        bodyA.LinearVelocity += u * bodyB.Mass.Value();
        bodyB.LinearVelocity -= u * bodyA.Mass.Value();
    }

    // FIXME(KB) - parameterize this.
    constexpr float correctionSlop = 1e-3f;

    if(impactResult.PenetrationDepth > correctionSlop)
    {
        // FIXME(KB) - parameterize this.
        constexpr float positionalCorrectionPercent = 0.1f;

        const float invMA = bodyA.Mass.InvValue();
        const float invMB = bodyB.Mass.InvValue();
        const float invMassSum = invMA + invMB;

        const float correctionMagnitude =
            std::max(0.0f, impactResult.PenetrationDepth - correctionSlop) *
            positionalCorrectionPercent;

        const Vec3f correction = correctionMagnitude * impactResult.ContactNormalBtoA / invMassSum;

        m_Trs1[indexA].T += correction * invMA;
        m_Trs1[indexB].T -= correction * invMB;
    }
}

void
PhysicsSolver::FindAndResolveAllImpacts()
{
    MLG_SCOPED_TIMER("Physics.FindAndResolveAllImpacts");

    m_GridHash.Clear();
    m_ImpactRecords.clear();

    for(size_t i = 0; i < m_Bodies.size(); ++i)
    {
        const Vec3f bbMin //
            {
                std::min(m_Trs0[i].T.x, m_Trs1[i].T.x),
                std::min(m_Trs0[i].T.y, m_Trs1[i].T.y),
                std::min(m_Trs0[i].T.z, m_Trs1[i].T.z),
            };
        const Vec3f bbMax //
            {
                std::max(m_Trs0[i].T.x, m_Trs1[i].T.x),
                std::max(m_Trs0[i].T.y, m_Trs1[i].T.y),
                std::max(m_Trs0[i].T.z, m_Trs1[i].T.z),
            };
        m_GridHash.Add(bbMin, bbMax, m_Colliders[i], i);
    }

    const size_t potentialCollisionCount = m_GridHash.PotentialCollisionCount();

    m_ImpactRecords.reserve(potentialCollisionCount);

    struct SweepTestBatch
    {
        std::span<ImpactRecord> Records;
        std::atomic<size_t>* FinishCounter{nullptr};

        void Enqueue()
        {
#if FIND_AND_RESOLVE_ALL_IMPACTS_MULTITHREADED
            ThreadPool::Enqueue<SweepTestBatch::Process>(this);
#else
            Process(this);
#endif
        }

        static void Process(SweepTestBatch* batch)
        {
            for(ImpactRecord& impactRecord : batch->Records)
            {
                impactRecord.ImpactFound =
                    impactRecord.Solver->SphereSphereSweep(impactRecord.Bodies,
                        impactRecord.Result);
            }

            batch->FinishCounter->fetch_add(1, std::memory_order_release);
        }
    };

    // Prepare process potential collisions in parallel.
    // Batches of BodyPairs will be offloaded to worker threads.

    // Calculate maximum batch size to max out worker threads.
    const size_t workerCount = ThreadPool::GetWorkerCount();
    const size_t batchSize = (potentialCollisionCount + workerCount - 1) / workerCount;
    const size_t batchCount = (potentialCollisionCount + batchSize - 1) / batchSize;

    std::vector<SweepTestBatch> batches;
    batches.reserve(batchCount);

    size_t pairCount = 0;
    size_t subspanStart = 0;

    // Counter used to determine when all batches have finished processing.
    std::atomic<size_t> finishCounter;

    // Collect impact records into batches and enqueue for processing.
    for(const BodyPair& bodyPair : m_GridHash)
    {
        m_ImpactRecords.emplace_back(this, bodyPair);
        ++pairCount;

        if(pairCount >= batchSize)
        {
            MLG_ASSERT(batches.size() < batchCount, "Batch count exceeded expected count");

            std::span batchSpan = std::span(m_ImpactRecords).subspan(subspanStart, pairCount);
            SweepTestBatch& batch = batches.emplace_back(batchSpan, &finishCounter);

            // Enqueue the batch for processing.
            batch.Enqueue();

            subspanStart += pairCount;
            pairCount = 0;
        }
    }

    // Enqueue any remaining pairs that didn't fill an entire batch.
    if(pairCount > 0)
    {
        std::span batchSpan = std::span(m_ImpactRecords).subspan(subspanStart, pairCount);
        SweepTestBatch& batch = batches.emplace_back(batchSpan, &finishCounter);

        batch.Enqueue();
    }

    MLG_ASSERT(batches.size() == batchCount);

    // Wait for all batches to finish.
    while(finishCounter.load(std::memory_order_acquire) < batches.size())
    {
        std::this_thread::yield();
    }

    // Cull non-impacts, and resolve impacts in order.
    size_t dst = 0;
    for(size_t src = 0; src < m_ImpactRecords.size(); ++src)
    {
        if(m_ImpactRecords[src].ImpactFound)
        {
            if(dst != src)
            {
                m_ImpactRecords[dst] = m_ImpactRecords[src];
            }
            ++dst;
        }
    }

    m_ImpactRecords.erase(m_ImpactRecords.begin() + dst, m_ImpactRecords.end());

    // Sort impact records by time of impact, and resolve in that order.
    // This isn't actually correct, but better than resolving out of order.
    // Substepping will make this better.
    std::sort(m_ImpactRecords.begin(), m_ImpactRecords.end());

    for(auto& impactRecord : m_ImpactRecords)
    {
        ResolveImpact(impactRecord);
    }
}

bool
PhysicsSolver::SphereSphereSweep(const BodyPair& bodyPair, ImpactResult& impactResult) const
{
#if !FIND_AND_RESOLVE_ALL_IMPACTS_MULTITHREADED
    //FIXME(KB) - make perf timers MT safe.
    MLG_SCOPED_TIMER("Physics.SphereSphereSweep");
#endif

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

    const Collider& colliderA = m_Colliders[bodyPair.IndexA()];
    const Collider& colliderB = m_Colliders[bodyPair.IndexB()];

    const float radiusA = colliderA.GetSphereRadius();
    const float radiusB = colliderB.GetSphereRadius();

    const TrsTransformf& transformA0 = m_Trs0[bodyPair.IndexA()];
    const TrsTransformf& transformA1 = m_Trs1[bodyPair.IndexA()];
    const TrsTransformf& transformB0 = m_Trs0[bodyPair.IndexB()];
    const TrsTransformf& transformB1 = m_Trs1[bodyPair.IndexB()];

    const Vec3 relP0 = transformA0.T - transformB0.T;
    const Vec3 relP1 = transformA1.T - transformB1.T;
    const Vec3 relMo = relP1 - relP0;
    const float r = radiusA + radiusB;
    const float r2 = r * r;
    const float dist0Sqr = relP0.Dot(relP0);

    // "c" term of the quadratic equation.
    // Square distance between centers at start of time step minus square of sum of radii.
    const float c = dist0Sqr - r2;

    if(c <= 0)
    {
        // Already overlapping at time t0.

        const float dist0 = std::sqrtf(dist0Sqr);

        //Treat this as an immediate collision at t0.
        impactResult.Alpha = 0.0f;

        if(dist0 < EPSILON)
        {
            // Centers are extremely close.  Try setting contact normal based on relative motion.
            const float relMoLenSq = relMo.Dot(relMo);
            if (relMoLenSq >= EPSILON_SQ)
            {
                impactResult.ContactNormalBtoA = relMo / std::sqrtf(relMoLenSq);
            }
            else
            {
                // Relative motion is also extremely small.  Just pick an arbitrary contact normal.
                impactResult.ContactNormalBtoA = Vec3f{ 1, 0, 0 };
            }
        }
        else
        {
            // Spheres overlapping so contact normal is direction from one center to the other
            // at time t0.
            impactResult.ContactNormalBtoA = relP0 / dist0;
        }

        impactResult.ContactPoint = transformB0.T + impactResult.ContactNormalBtoA * radiusB;
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
    const float b = 2.0f * relMo.Dot(relP0);
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
    impactResult.PosAtImpactA = transformA0.T + (transformA1.T - transformA0.T) * t;
    impactResult.PosAtImpactB = transformB0.T + (transformB1.T - transformB0.T) * t;

    // Vector between centers.
    impactResult.ContactNormalBtoA = impactResult.PosAtImpactA - impactResult.PosAtImpactB;
    // Normalize by sum of radii, since at impact distance between centers is equal to sum of radii.
    // Saves a sqrt operation.
    impactResult.ContactNormalBtoA /= r;
    impactResult.ContactPoint =
        impactResult.PosAtImpactB + (impactResult.ContactNormalBtoA * radiusB);
    impactResult.PenetrationDepth = 0;

    return true;
}