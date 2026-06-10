#include "PhysicsLevel.h"

#include "PerfMetrics.h"
#include "ThreadPool.h"

#include <thread>

static constexpr float RESTING_VELOCITY_THRESHOLD = 1.0f/128;
static constexpr float COEFF_OF_RESTITUTION = 0.8f;

Result<>
PhysicsLevel::Create(const Level& level, PhysicsLevel& outPhysLevel)
{
    const std::span allHandles = level.GetAllHandles();

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
        const std::optional<RigidBody>& optBody = node->Components.Body;
        const std::optional<Collider>& optCollider = node->Components.Collider;

        if(optBody && optCollider)
        {
            nodeHandles.emplace_back(handle);
            transforms.emplace_back(node->LocalTransform);
            bodies.emplace_back(*optBody);
            colliders.emplace_back(*optCollider);
        }
        else
        {
            MLG_ASSERT(!optBody && !optCollider,
                "Node {} has Body component but no Collider, or vice versa",
                node->Name);
        }
    }

    PhysicsLevel physLevel(std::move(nodeHandles),
        std::move(transforms),
        std::move(bodies),
        std::move(colliders));

    outPhysLevel = std::move(physLevel);

    return Result<>::Ok;
}

void
PhysicsLevel::AddForce(size_t bodyIndex, const Vec3f& force)
{
    MLG_ASSERT(bodyIndex < m_Bodies.size(), "Body index out of range");

    // Accumulate accelerations for the current frame.
    m_AccelCur[bodyIndex] += force * m_Bodies[bodyIndex].GetMass().InvValue();
}

void
PhysicsLevel::Update(const float timeStep)
{
    MLG_SCOPED_TIMER("Physics.Update");

    std::swap(m_TrsCur, m_TrsNext);
    // Update velocities based on accelerations accumulated this frame.
    UpdateVelocities(timeStep);
    PredictPositions(timeStep);
    FindAndResolveAllImpacts();
    std::swap(m_AccelPrev, m_AccelCur);
    std::ranges::fill(m_AccelCur, Vec3f{ 0 });
}

Result<>
PhysicsLevel::SyncToLevel(Level& level)
{
    for(size_t i = 0; i < m_NodeHandles.size(); ++i)
    {
        m_ActiveBodies[i] = level.IsActive(m_NodeHandles[i]);

        MLG_CHECK(level.UpdateLocalTransform( m_NodeHandles[i], m_TrsCur[i]));
    }

    return Result<>::Ok;
}

// private:

// Regulare velocity verlet is like this:
// p1 = p0 + v0*dt + 0.5 * a0 * dt^2
// a1 = some function of p1
// v1 = v0 + (a0 + a1) * 0.5 * dt
//
// Ours works like this:
//
// a0 = some function of p0
// v0 = v(-1) + (a(-1) + a0) * 0.5 * dt
// p1 = (v0 * dt) + (a0 * dt^2 * 0.5) + p0

void
PhysicsLevel::UpdateVelocities(const float dt)
{
    MLG_SCOPED_TIMER("Physics.UpdateVelocities");

    // FIXME(KB) - caller should account for dt not being the entire timestep,
    // but a substep due to collision events.
    for (size_t i = 0; i < m_Bodies.size(); ++i)
    {
        if(!m_ActiveBodies[i])
        {
            continue;
        }

        // Acceleration can change over the timestep due to, e.g., gravity.
        // So it's incorrect to use a single acceleration over the timestep.
        // Instead we approximate the integral of the acceleration function
        // using the trapezoidal rule:
        // integral from t0 to t1 of a(t) dt ~= (t1 - t0) * (a(t0) + a(t1)) / 2

        // m_AccelPrev is from the previous frame, and m_A0 is from the current frame.
        m_LinearVelocities[i] += (m_AccelPrev[i] + m_AccelCur[i]) * dt * 0.5f;
    }
}

void
PhysicsLevel::PredictPositions(const float dt)
{
    MLG_SCOPED_TIMER("Physics.PredictPositions");

    for (size_t i = 0; i < m_Bodies.size(); ++i)
    {
        if(!m_ActiveBodies[i])
        {
            continue;
        }

        // Update position using velocity and acceleration from previous time step.
        // p = ∫ v dt
        // v = v0 + a * t
        // p1 = ∫ (v0 + a * t) dt = v0 * dt + (a * dt^2) / 2 + p0

        // Use velocity/acceleration from current frame.
        // Note that this frame's velocity was computed from
        // the previous and current frame's acceleration.
        const Vec3f& vCur = m_LinearVelocities[i];
        m_TrsNext[i].T = (vCur * dt) + ((m_AccelCur[i] * dt * dt) / 2) + m_TrsCur[i].T;
    }
}

#define FIND_AND_RESOLVE_ALL_IMPACTS_MULTITHREADED 1

void
PhysicsLevel::ResolveImpact(const ImpactRecord& impact)
{
    MLG_SCOPED_TIMER("Physics.ResolveImpact");

    const BodyPair& bodyPair = impact.Bodies;
    const ImpactResult& impactResult = impact.Result;

    const size_t indexA = bodyPair.IndexA();
    const size_t indexB = bodyPair.IndexB();

    const RigidBody& bodyA = m_Bodies[indexA];
    const RigidBody& bodyB = m_Bodies[indexB];

    Vec3f& velA = m_LinearVelocities[indexA];
    Vec3f& velB = m_LinearVelocities[indexB];

    // Compute relative velocity along the normal
    const float vRel = (velA - velB).Dot(impactResult.ContactNormalBtoA);

    // Only resolve if bodies are moving towards each other
    if(vRel < 0)
    {
        // Impulse

        // n = contact normal
        // vrel ​= (vA​−vB​)⋅n
        // j = -(1 + e) * vrel / (1/mA + 1/mB)
        // vA' += n * (j / mA)
        // vA' += n * -(1 + e) * vRel * (1/mA) * (1 / (1/mA + 1/mB))
        // vA' += n * -(1 + e) * vRel * (1/(mA * (mA + mB)/(mA * mB)))
        // vA' += n * -(1 + e) * vRel * ((mA * mB)/(mA * (mA + mB))
        // vA' += n * -(1 + e) * vRel * mB/(mA + mB)

        const float e =
            (vRel < -RESTING_VELOCITY_THRESHOLD)
            // When closing velocity is above the resting velocity threshold
            // treat as a dynamic collision with restitution.
            ? COEFF_OF_RESTITUTION
            // When closing velocity is below the resting velocity threshold
            // treat as a resting contact.
            : 0.0f;

        const float k = -(1 + e) * vRel / (bodyA.GetMass().Value() + bodyB.GetMass().Value());
        const Vec3f u = k * impactResult.ContactNormalBtoA;

        velA += u * bodyB.GetMass().Value();
        velB -= u * bodyA.GetMass().Value();
    }

    // FIXME(KB) - parameterize this.
    constexpr float kCorrectionSlop = 1e-3f;

    MLG_ASSERT(impactResult.PenetrationDepth >= 0, "Penetration depth should be non-negative");

    if(0 == impactResult.PenetrationDepth)
    {
        // Move bodies to point of impact.
        m_TrsNext[indexA].T = impactResult.PosAtImpactA;
        m_TrsNext[indexB].T = impactResult.PosAtImpactB;
    }
    else if(impactResult.PenetrationDepth > kCorrectionSlop)
    {
        // mA = mass of A
        // mB = mass of B
        // n  = unit normal from B to A
        // d  = penetration depth
        // s  = slop
        // p  = correction percent
        // Magnitude of correction:
        // C = max(d - s, 0) * p
        // moveA + moveB = C
        // Weight the movement by the inverse of the mass (i.e., more massive bodies move less):
        // moveA = C * mB / (mA + mB)
        // moveB = C * mA / (mA + mB)
        // posA' = posA + n * C * mB / (mA + mB)
        // posB' = posB - n * C * mA / (mA + mB)
        // Equivalent to:
        // posA' = posA + n * C * (invMA) / ((invMA) + (invMB))
        // posB' = posB - n * C * (invMB) / ((invMA) + (invMB))
        // Where invMA = 1/mA and invMB = 1/mB are the inverse masses of A and B, respectively.
        // Inverse masses are used because bodies with infinite mass (i.e., immovable bodies) will
        // have an inverse mass of zero, which correctly results in them not moving.

        // FIXME(KB) - parameterize this.
        constexpr float kCorrectionPercent = 0.1f;

        const float invMA = bodyA.GetMass().InvValue();
        const float invMB = bodyB.GetMass().InvValue();
        const float invMassSum = invMA + invMB;

        const float C =
            std::max(0.0f, impactResult.PenetrationDepth - kCorrectionSlop) * kCorrectionPercent;

        const Vec3f correction = C * impactResult.ContactNormalBtoA / invMassSum;

        m_TrsNext[indexA].T += correction * invMA;
        m_TrsNext[indexB].T -= correction * invMB;
    }
}

void
PhysicsLevel::FindAndResolveAllImpacts()
{
    MLG_SCOPED_TIMER("Physics.FindAndResolveAllImpacts");

    m_GridHash.Clear();
    m_ImpactRecords.clear();

    // Bodies will be added to all cells of the grid overlapped by the bounding box
    // defined by the current and predicted position.
    for(size_t i = 0; i < m_Bodies.size(); ++i)
    {
        if(!m_ActiveBodies[i])
        {
            continue;
        }

        const Vec3f bbMin //
            {
                std::min(m_TrsCur[i].T.x, m_TrsNext[i].T.x),
                std::min(m_TrsCur[i].T.y, m_TrsNext[i].T.y),
                std::min(m_TrsCur[i].T.z, m_TrsNext[i].T.z),
            };
        const Vec3f bbMax //
            {
                std::max(m_TrsCur[i].T.x, m_TrsNext[i].T.x),
                std::max(m_TrsCur[i].T.y, m_TrsNext[i].T.y),
                std::max(m_TrsCur[i].T.z, m_TrsNext[i].T.z),
            };

        m_GridHash.Add(bbMin, bbMax, m_Colliders[i], i);
    }

    const size_t potentialCollisionCount = m_GridHash.PotentialCollisionCount();

    if(potentialCollisionCount == 0)
    {
        return;
    }

    m_ImpactRecords.reserve(potentialCollisionCount);

    // Prepare to process potential collisions in parallel.
    // Batches of BodyPairs will be offloaded to worker threads.

    // Calculate optimal batch size to max out worker threads.
    const size_t workerCount = ThreadPool::GetWorkerCount();
    const size_t batchSize = (potentialCollisionCount + workerCount - 1) / workerCount;
    const size_t batchCount = (potentialCollisionCount + batchSize - 1) / batchSize;

    m_SweepTestBatches.clear();
    m_SweepTestBatches.reserve(batchCount);

    size_t pairCount = 0;
    size_t subspanStart = 0;

    // Counter used to determine when all batches have finished processing.
    std::atomic<size_t> finishCounter;

    // Collect impact records into batches and enqueue for processing.
    for(const BodyPair& bodyPair : m_GridHash)
    {
        const ImpactRecord impactRecord //
            {
                .Bodies = bodyPair,
                .SweepParams //
                {
                    .StartPosA = m_TrsCur[bodyPair.IndexA()].T,
                    .EndPosA = m_TrsNext[bodyPair.IndexA()].T,
                    .ColliderA = m_Colliders[bodyPair.IndexA()],
                    .StartPosB = m_TrsCur[bodyPair.IndexB()].T,
                    .EndPosB = m_TrsNext[bodyPair.IndexB()].T,
                    .ColliderB = m_Colliders[bodyPair.IndexB()],
                },
            };

        m_ImpactRecords.emplace_back(impactRecord);
        ++pairCount;

        if(pairCount >= batchSize)
        {
            MLG_ASSERT(m_SweepTestBatches.size() < batchCount, "Batch count exceeded expected count");

            const std::span batchSpan = std::span(m_ImpactRecords).subspan(subspanStart, pairCount);
            SweepTestBatch& batch = m_SweepTestBatches.emplace_back(batchSpan, &finishCounter);

            // Enqueue the batch for processing.
            batch.Enqueue();

            subspanStart += pairCount;
            pairCount = 0;
        }
    }

    // Enqueue any remaining pairs that didn't fill an entire batch.
    if(pairCount > 0)
    {
        const std::span batchSpan = std::span(m_ImpactRecords).subspan(subspanStart, pairCount);
        SweepTestBatch& batch = m_SweepTestBatches.emplace_back(batchSpan, &finishCounter);

        batch.Enqueue();
    }

    MLG_ASSERT(m_SweepTestBatches.size() == batchCount);

    // Wait for all batches to finish.
    while(finishCounter.load(std::memory_order_acquire) < m_SweepTestBatches.size())
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

    const auto newEnd = m_ImpactRecords.begin() + static_cast<std::ptrdiff_t>(dst);
    m_ImpactRecords.erase(newEnd, m_ImpactRecords.end());

    // Sort impact records by time of impact, and resolve in that order.
    // This isn't actually correct, but better than resolving out of order.
    // Substepping will make this better.
    std::ranges::sort(m_ImpactRecords);

    for(auto& impactRecord : m_ImpactRecords)
    {
        ResolveImpact(impactRecord);
    }
}

bool
PhysicsLevel::SphereSphereSweep(const ColliderSweepParams& params, ImpactResult& impactResult)
{
    MLG_SCOPED_TIMER("Physics.SphereSphereSweep");

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

    const float radiusA = params.ColliderA.GetSphereRadius();
    const float radiusB = params.ColliderB.GetSphereRadius();

    const Vec3f& pA0 = params.StartPosA;
    const Vec3f& pA1 = params.EndPosA;
    const Vec3f& pB0 = params.StartPosB;
    const Vec3f& pB1 = params.EndPosB;

    const Vec3 relP0 = pA0 - pB0;
    const Vec3 relP1 = pA1 - pB1;
    const Vec3 relMo = relP1 - relP0;
    const float r = radiusA + radiusB;
    const float r2 = r * r;
    const float dist0Sqr = relP0.Dot(relP0);

    // "c" term of the quadratic equation.
    // Square distance between centers at start of time step minus square of sum of radii.
    const float c = dist0Sqr - r2;

    if(c < 0)
    {
        // Already overlapping at time t0.

        //Treat this as an immediate collision at t0.
        impactResult.Alpha = 0.0f;

        if(dist0Sqr < EPSILON_SQ)
        {
            // Centers are extremely close.  Try setting contact normal based on relative motion.
            const float relMoLenSq = relMo.Dot(relMo);
            if (relMoLenSq >= EPSILON_SQ)
            {
                impactResult.ContactNormalBtoA = relMo / std::sqrt(relMoLenSq);
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
            impactResult.ContactNormalBtoA = relP0 / std::sqrt(dist0Sqr);
        }

        impactResult.ContactPoint = pB0 + impactResult.ContactNormalBtoA * radiusB;
        impactResult.PosAtImpactA = pA0;
        impactResult.PosAtImpactB = pB0;

        // Penetration depth = r - d.
        // Where:
        // r = sum of radii, d = distance between centers.
        // (r^2 - d^2) = (r - d)(r + d)
        // So: (r - d) = (r^2 - d^2) / (r + d)
        // Using this formula avoids catastrophic cancellation when r and d are close, which can
        // happen with shallow penetrations.
        // https://en.wikipedia.org/wiki/Catastrophic_cancellation
        impactResult.PenetrationDepth = ((r * r) - dist0Sqr) / (r + std::sqrt(dist0Sqr));

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

    float discriminant = (b * b) - (4 * a * c);

    if (discriminant < -EPSILON)
    {
        // No real roots, so no collision.
        return false;
    }

    discriminant = std::max(0.0f, discriminant);// clamp tiny negative roundoff to tangent

    // -b - sqrt(b^2 - 4ac) / 2a is the entry point.
    // -b + sqrt(b^2 - 4ac) / 2a is the exit point.
    // We want the entry point.
    const float t = (-b - std::sqrt(discriminant)) / (2 * a);

    if(t < 0 || t > 1)
    {
        // Collision occurs outside of time step.
        return false;
    }

    // Time of impact within the timestep.
    impactResult.Alpha = t;

    // Centers at time of impact.
    impactResult.PosAtImpactA = pA0 + (pA1 - pA0) * t;
    impactResult.PosAtImpactB = pB0 + (pB1 - pB0) * t;

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

void
PhysicsLevel::SweepTestBatch::Enqueue()
{
#if FIND_AND_RESOLVE_ALL_IMPACTS_MULTITHREADED
    ThreadPool::Enqueue<SweepTestBatch::Process>(this);
#else
    Process(this);
#endif
}

void
PhysicsLevel::SweepTestBatch::Process(SweepTestBatch* batch)
{
    for(ImpactRecord& impactRecord : batch->PotentialImpacts)
    {
        impactRecord.ImpactFound =
            SphereSphereSweep(impactRecord.SweepParams, impactRecord.Result);
    }

    batch->FinishCounter->fetch_add(1, std::memory_order_release);
}