#include "PhysicsLevel.h"

#include "PerfMetrics.h"
#include "ThreadPool.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <ranges>
#include <thread>

static constexpr float RESTING_VELOCITY_THRESHOLD = 1.0f/128;
static constexpr float COEFF_OF_RESTITUTION = 0.8f;

// FIXME(KB) - Calculate world space pos for collision detection.

Result<PhysicsLevel>
PhysicsLevel::Create(const Level& level, ThreadPool& threadPool)
{
    size_t count = 0;
    for(const auto& node : level.GetAllNodes())
    {
        if(node.Components.Body)
        {
            ++count;
        }
    }

    //FIXME(KB) - use center of bounding volume as position

    std::vector<const Level::Node*> nodes;
    std::vector<Vec3f> positions;
    std::vector<RigidBody> bodies;
    nodes.reserve(count);
    positions.reserve(count);
    bodies.reserve(count);

    for(const auto& node : level.GetAllNodes())
    {
        const std::optional<RigidBody>& optBody = node.Components.Body;

        if(optBody)
        {
            nodes.emplace_back(&node);
            positions.emplace_back(node.LocalTransform.T);
            bodies.emplace_back(*optBody);
        }
    }

    PhysicsLevel physLevel(std::move(nodes),
        positions,
        std::move(bodies),
        threadPool);
        
    return std::move(physLevel);
}

void
PhysicsLevel::PredictPositions(const float dt)
{
    MLG_SCOPED_TIMER("Physics.PredictPositions");

    const auto range = std::views::zip(m_ActiveBodies,
        m_LinearVelocities.X,
        m_LinearVelocities.Y,
        m_LinearVelocities.Z,
        m_A0,
        m_P0.X,
        m_P0.Y,
        m_P0.Z,
        m_P1.X,
        m_P1.Y,
        m_P1.Z);

    for(auto&& [isActive, v0x, v0y, v0z, a0, p0x, p0y, p0z, p1x, p1y, p1z] : range)
    {
        if(!isActive)
        {
            continue;
        }
        // Update position using velocity and acceleration from previous time step.
        // p = ∫ v dt
        // v = v0 + a * t
        // p1 = ∫ (v0 + a * t) dt
        // p1 = p0 + v0*dt + 0.5 * a0 * dt^2

        const float ascale = 0.5f * dt * dt;

        p1x = p0x + (v0x * dt) + (ascale * a0.x);
        p1y = p0y + (v0y * dt) + (ascale * a0.y);
        p1z = p0z + (v0z * dt) + (ascale * a0.z);
    }
}

void
PhysicsLevel::Resolve()
{
    MLG_SCOPED_TIMER("Physics.Resolve");

    FindAndResolveAllImpacts();
    std::swap(m_P0, m_P1);
    std::swap(m_A0, m_A1);
    std::ranges::fill(m_A1, Vec3f{ 0 });
}

void
PhysicsLevel::AddForce(const size_t bodyIndex, const Vec3f& force)
{
    MLG_ASSERT(bodyIndex < m_Bodies.size(), "Body index out of range");

    m_A1[bodyIndex] += force * m_Bodies[bodyIndex].GetMass().InvValue();
}

void
PhysicsLevel::UpdateVelocities(const float dt)
{
    MLG_SCOPED_TIMER("Physics.UpdateVelocities");

    // FIXME(KB) - caller should account for dt not being the entire timestep,
    // but a substep due to collision events.
    const auto range = std::views::zip(m_ActiveBodies,
        m_LinearVelocities.X,
        m_LinearVelocities.Y,
        m_LinearVelocities.Z,
        m_A0,
        m_A1);

    for(auto&& [isActive, v0x, v0y, v0z, a0, a1] : range)
    {
        if(!isActive)
        {
            continue;
        }

        // Acceleration can change over the timestep due to, e.g., gravity.
        // So it's incorrect to use a single acceleration over the timestep.
        // Instead we approximate the integral of the acceleration function
        // using the trapezoidal rule:
        // integral from t0 to t1 of a(t) dt ~= (t1 - t0) * (a(t0) + a(t1)) / 2

        const float scale = dt * 0.5f;

        v0x += (a0.x + a1.x) * scale;
        v0y += (a0.y + a1.y) * scale;
        v0z += (a0.z + a1.z) * scale;
    }
}

Result<>
PhysicsLevel::SyncToLevel(Level& level)
{
    auto view = std::views::zip(m_Nodes, m_P0.X, m_P0.Y, m_P0.Z, m_ActiveBodies);

    for(const auto&& [node, posX, posY, posZ, isActive] : view)
    {
        isActive = node->IsActive();
        TrsTransformf trs = node->LocalTransform;
        trs.T = Vec3f{ posX, posY, posZ };
        MLG_CHECK(level.UpdateLocalTransform(*node, trs));
    }

    return Result<>::Ok;
}

// private:

#define FIND_AND_RESOLVE_ALL_IMPACTS_MULTITHREADED 0

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

    Vec3f velA{ m_LinearVelocities.X[indexA], m_LinearVelocities.Y[indexA], m_LinearVelocities.Z[indexA] };
    Vec3f velB{ m_LinearVelocities.X[indexB], m_LinearVelocities.Y[indexB], m_LinearVelocities.Z[indexB] };

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

        m_LinearVelocities.X[indexA] = velA.x;
        m_LinearVelocities.Y[indexA] = velA.y;
        m_LinearVelocities.Z[indexA] = velA.z;
        m_LinearVelocities.X[indexB] = velB.x;
        m_LinearVelocities.Y[indexB] = velB.y;
        m_LinearVelocities.Z[indexB] = velB.z;
    }

    // FIXME(KB) - parameterize this.
    constexpr float kCorrectionSlop = 1e-3f;

    MLG_ASSERT(impactResult.PenetrationDepth >= 0, "Penetration depth should be non-negative");

    if(0 == impactResult.PenetrationDepth)
    {
        // Move bodies to point of impact.
        m_P1.X[indexA] = impactResult.PosAtImpactA.x;
        m_P1.Y[indexA] = impactResult.PosAtImpactA.y;
        m_P1.Z[indexA] = impactResult.PosAtImpactA.z;
        m_P1.X[indexB] = impactResult.PosAtImpactB.x;
        m_P1.Y[indexB] = impactResult.PosAtImpactB.y;
        m_P1.Z[indexB] = impactResult.PosAtImpactB.z;
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

        m_P1.X[indexA] += correction.x * invMA;
        m_P1.Y[indexA] += correction.y * invMA;
        m_P1.Z[indexA] += correction.z * invMA;
        m_P1.X[indexB] -= correction.x * invMB;
        m_P1.Y[indexB] -= correction.y * invMB;
        m_P1.Z[indexB] -= correction.z * invMB;
    }
}

void
PhysicsLevel::FindAndResolveAllImpacts()
{
    MLG_SCOPED_TIMER("Physics.FindAndResolveAllImpacts");

    m_GridHash.Clear();
    m_ImpactRecords.clear();

    MLG_ABORTIF(m_ActiveBodies.size() > std::numeric_limits<uint32_t>::max(),
        "PhysicsLevel supports a maximum of {} active bodies, but {} are active.",
        std::numeric_limits<uint32_t>::max(),
        m_ActiveBodies.size());

    const auto indices = std::views::iota(0u, static_cast<uint32_t>(m_ActiveBodies.size()));
    const auto range = std::views::zip(m_Bodies,
        m_ActiveBodies,
        m_P0.X,
        m_P0.Y,
        m_P0.Z,
        m_P1.X,
        m_P1.Y,
        m_P1.Z,
        indices);

    size_t potentialCollisionCount = 0;

    {
        MLG_SCOPED_TIMER("Physics.FindAndResolveAllImpacts.GridHash");

        // Bodies will be added to all cells of the grid overlapped by the bounding box
        // defined by the current and predicted position.
        for(auto&& [body, isActive, p0x, p0y, p0z, p1x, p1y, p1z, index] : range)
        {
            if(!isActive)
            {
                continue;
            }

            // Bodies will be added to all cells of the grid overlapped by the bounding box
            // defined by the current and predicted position.

            //FIXME(KB) - transform bounding spher to world space and use its position.
            m_GridHash.Add({ p0x, p0y, p0z }, { p1x, p1y, p1z }, body.GetBoundingSphere(), index);
        }

        potentialCollisionCount = m_GridHash.PotentialCollisionCount();

        if(potentialCollisionCount == 0)
        {
            return;
        }

        m_ImpactRecords.reserve(potentialCollisionCount);
    }

    {
        MLG_SCOPED_TIMER("Physics.FindAndResolveAllImpacts.SweepTests");
        
        // Prepare to process potential collisions in parallel.
        // Batches of BodyPairs will be offloaded to worker threads.

        // Calculate optimal batch size to max out worker threads.
        const size_t workerCount = m_ThreadPool->GetWorkerCount();
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
                        .StartPosA{m_P0.X[bodyPair.IndexA()], m_P0.Y[bodyPair.IndexA()], m_P0.Z[bodyPair.IndexA()]},
                        .EndPosA{m_P1.X[bodyPair.IndexA()], m_P1.Y[bodyPair.IndexA()], m_P1.Z[bodyPair.IndexA()]},
                        .SphereA = m_Bodies[bodyPair.IndexA()].GetBoundingSphere(),
                        .StartPosB{m_P0.X[bodyPair.IndexB()], m_P0.Y[bodyPair.IndexB()], m_P0.Z[bodyPair.IndexB()]},
                        .EndPosB{m_P1.X[bodyPair.IndexB()], m_P1.Y[bodyPair.IndexB()], m_P1.Z[bodyPair.IndexB()]},
                        .SphereB = m_Bodies[bodyPair.IndexB()].GetBoundingSphere(),
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
                EnqueueSweepTests(&batch);

                subspanStart += pairCount;
                pairCount = 0;
            }
        }

        // Enqueue any remaining pairs that didn't fill an entire batch.
        if(pairCount > 0)
        {
            const std::span batchSpan = std::span(m_ImpactRecords).subspan(subspanStart, pairCount);
            SweepTestBatch& batch = m_SweepTestBatches.emplace_back(batchSpan, &finishCounter);

            EnqueueSweepTests(&batch);
        }

        MLG_ASSERT(m_SweepTestBatches.size() == batchCount);

        MLG_SCOPED_TIMER("Physics.FindAndResolveAllImpacts.SweepTests.Wait");

        size_t finishCount = finishCounter.load();
        while(finishCount < m_SweepTestBatches.size())
        {
            finishCounter.wait(finishCount);
            finishCount = finishCounter.load();
        }
    }

    static PerfCounter pcPotentialImpacts({ .Name = "Physics.Collision.PotentialImpacts", });
    pcPotentialImpacts.Increment(m_ImpactRecords.size());

    // Cull non-impacts, and resolve impacts in order.
    auto removed = std::ranges::remove_if(m_ImpactRecords, [](const ImpactRecord& impactRecord)
    {
        return !impactRecord.ImpactFound;
    });

    m_ImpactRecords.erase(removed.begin(), removed.end());

    static PerfCounter pcActualImpacts({ .Name = "Physics.Collision.ActualImpacts", });
    pcActualImpacts.Increment(m_ImpactRecords.size());

    // Sort impact records by time of impact, and resolve in that order.
    // This isn't actually correct, but better than resolving out of order.
    // Substepping will make this better.
    std::ranges::sort(m_ImpactRecords);

    for(auto& impactRecord : m_ImpactRecords)
    {
        ResolveImpact(impactRecord);
    }
}

void
PhysicsLevel::EnqueueSweepTests(SweepTestBatch* batch) // NOLINT(readability-convert-member-functions-to-static,-warnings-as-errors)
{
#if FIND_AND_RESOLVE_ALL_IMPACTS_MULTITHREADED
    m_ThreadPool->Enqueue<SweepTestBatch::Process>(batch);
#else
    SweepTestBatch::Process(batch);
#endif
}

bool
PhysicsLevel::SphereSphereSweep(const SphereSweepParams& params, ImpactResult& impactResult)
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

    const BoundingSphere& sphereA = params.SphereA;
    const BoundingSphere& sphereB = params.SphereB;

    const Vec3f pA0 = params.StartPosA + sphereA.GetCenter();
    const Vec3f pA1 = params.EndPosA + sphereA.GetCenter();
    const Vec3f pB0 = params.StartPosB + sphereB.GetCenter();
    const Vec3f pB1 = params.EndPosB + sphereB.GetCenter();

    const Vec3f relP0 = pA0 - pB0;
    const Vec3f relP1 = pA1 - pB1;
    const Vec3f relMo = relP1 - relP0;
    const float r = sphereA.GetRadius() + sphereB.GetRadius();
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

        impactResult.ContactPoint = pB0 + impactResult.ContactNormalBtoA * sphereB.GetRadius();
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
        impactResult.PosAtImpactB + (impactResult.ContactNormalBtoA * sphereB.GetRadius());
    impactResult.PenetrationDepth = 0;

    return true;
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
    batch->FinishCounter->notify_all();
}