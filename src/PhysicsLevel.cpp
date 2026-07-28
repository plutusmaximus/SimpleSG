#include "PhysicsLevel.h"

#include "PerfMetrics.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>

static constexpr float RESTING_VELOCITY_THRESHOLD = 1.0f/128;
static constexpr float COEFF_OF_RESTITUTION = 0.8f;

// Distance under which penetration is ignored, to avoid jittering due to numerical error.
static constexpr float kPenetrationSlop = 1e-3f;
// Percentage of penetration depth to correct per timestep.
static constexpr float kCorrectionPercent = 0.1f;
// Maximum number of contact solver iterations to find convergence.
static constexpr size_t kContactSolverMaxIterations = 10;
// Closing speed threshold below which we consider all contacts to have converged.
static constexpr float kContactSolverVelocityThreshold = 1e-3f;

// FIXME(KB) - Calculate world space pos for collision detection.

Result<PhysicsLevel>
PhysicsLevel::Create(const std::span<const Level::Node>& nodes)
{
    return PhysicsLevel(nodes);
}

void
PhysicsLevel::PredictPositions(const float dt)
{
    MLG_SCOPED_TIMER("Physics.PredictPositions");

    const float* __restrict p0x = m_P0.X.data();
    const float* __restrict p0y = m_P0.Y.data();
    const float* __restrict p0z = m_P0.Z.data();
    float* __restrict p1x = m_P1.X.data();
    float* __restrict p1y = m_P1.Y.data();
    float* __restrict p1z = m_P1.Z.data();
    const float* __restrict v0x = m_LinearVelocities.X.data();
    const float* __restrict v0y = m_LinearVelocities.Y.data();
    const float* __restrict v0z = m_LinearVelocities.Z.data();
    const float* __restrict a0x = m_A0.X.data();
    const float* __restrict a0y = m_A0.Y.data();
    const float* __restrict a0z = m_A0.Z.data();

    const float ascale = 0.5f * dt * dt;
    const size_t count = m_ActiveBodies.size();

    for(size_t i = 0; i < count; ++i)
    {
        if(!m_ActiveBodies[i])
        {
            continue;
        }

        // p = ∫ v dt
        // v = v0 + a * t
        // p1 = ∫ (v0 + a * t) dt
        // p1 = p0 + v0*dt + 0.5 * a0 * dt^2

        // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        p1x[i] = p0x[i] + (v0x[i] * dt) + (ascale * a0x[i]);
        p1y[i] = p0y[i] + (v0y[i] * dt) + (ascale * a0y[i]);
        p1z[i] = p0z[i] + (v0z[i] * dt) + (ascale * a0z[i]);
        // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    }
}

void
PhysicsLevel::Resolve()
{
    FindAndResolveAllImpacts();
    std::swap(m_P0, m_P1);
    std::swap(m_A0, m_A1);
    std::ranges::fill(m_A1.X, 0);
    std::ranges::fill(m_A1.Y, 0);
    std::ranges::fill(m_A1.Z, 0);
}

void
PhysicsLevel::ApplyImpulse(const Level::Node* node, const Vec3f& impulse)
{
    const size_t index = GetNodeIndex(node);
    if(MLG_VERIFY(index != NodeAndIndex::kInvalidIndex, "Node not found in PhysicsLevel"))
    {
        // dv = J / m
        const Vec3 dv = impulse * m_InvMasses[index];
        m_LinearVelocities.X[index] += dv.x;
        m_LinearVelocities.Y[index] += dv.y;
        m_LinearVelocities.Z[index] += dv.z;
    }
}

void
PhysicsLevel::AddForce(const Level::Node* node, const Vec3f& force)
{
    const size_t index = GetNodeIndex(node);
    if(MLG_VERIFY(index != NodeAndIndex::kInvalidIndex, "Node not found in PhysicsLevel"))
    {
        const Vec3 accel = force * m_InvMasses[index];
        m_A1.X[index] += accel.x;
        m_A1.Y[index] += accel.y;
        m_A1.Z[index] += accel.z;
    }
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
        m_A0.X,
        m_A0.Y,
        m_A0.Z,
        m_A1.X,
        m_A1.Y,
        m_A1.Z);

    for(auto&& [isActive, v0x, v0y, v0z, a0x, a0y, a0z, a1x, a1y, a1z] : range)
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

        v0x += (a0x + a1x) * scale;
        v0y += (a0y + a1y) * scale;
        v0z += (a0z + a1z) * scale;
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

Vec3f
PhysicsLevel::GetPosition(const Level::Node* node) const
{
    const size_t index = GetNodeIndex(node);
    if(MLG_VERIFY(index != NodeAndIndex::kInvalidIndex, "Node not found in PhysicsLevel"))
    {
        return Vec3f{ m_P0.X[index], m_P0.Y[index], m_P0.Z[index] };
    }
    return Vec3f{ 0 };
}

Vec3f
PhysicsLevel::GetLinearVelocity(const Level::Node* node) const
{
    const size_t index = GetNodeIndex(node);
    if(MLG_VERIFY(index != NodeAndIndex::kInvalidIndex, "Node not found in PhysicsLevel"))
    {
        return Vec3f{ m_LinearVelocities.X[index], m_LinearVelocities.Y[index], m_LinearVelocities.Z[index] };
    }
    return Vec3f{ 0 };
}

float
PhysicsLevel::GetRadius(const Level::Node* node) const
{
    const size_t index = GetNodeIndex(node);
    if(MLG_VERIFY(index != NodeAndIndex::kInvalidIndex, "Node not found in PhysicsLevel"))
    {
        return m_Radii[index];
    }
    return 0.0f;
}

float
PhysicsLevel::GetInverseMass(const Level::Node* node) const
{
    const size_t index = GetNodeIndex(node);
    if(MLG_VERIFY(index != NodeAndIndex::kInvalidIndex, "Node not found in PhysicsLevel"))
    {
        return m_InvMasses[index];
    }
    return 0.0f;
}

void
PhysicsLevel::GetPositions(VVec3& positions) const
{
    MLG_ASSERT(positions.X.size() == m_P0.X.size(), "Size mismatch");
    MLG_ASSERT(positions.Y.size() == m_P0.Y.size(), "Size mismatch");
    MLG_ASSERT(positions.Z.size() == m_P0.Z.size(), "Size mismatch");

    std::ranges::copy(m_P0.X, positions.X.begin());
    std::ranges::copy(m_P0.Y, positions.Y.begin());
    std::ranges::copy(m_P0.Z, positions.Z.begin());
}
void
PhysicsLevel::GetLinearVelocities(VVec3& linearVelocities) const
{
    MLG_ASSERT(linearVelocities.X.size() == m_LinearVelocities.X.size(), "Size mismatch");
    MLG_ASSERT(linearVelocities.Y.size() == m_LinearVelocities.Y.size(), "Size mismatch");
    MLG_ASSERT(linearVelocities.Z.size() == m_LinearVelocities.Z.size(), "Size mismatch");

    std::ranges::copy(m_LinearVelocities.X, linearVelocities.X.begin());
    std::ranges::copy(m_LinearVelocities.Y, linearVelocities.Y.begin());
    std::ranges::copy(m_LinearVelocities.Z, linearVelocities.Z.begin());
}

void
PhysicsLevel::GetInverseMasses(std::span<float>& invMasses) const
{
    MLG_ASSERT(invMasses.size() == m_InvMasses.size(), "Size mismatch");
    std::ranges::copy(m_InvMasses, invMasses.begin());
}

void
PhysicsLevel::SetLinearVelocity(const Level::Node* node, const Vec3f& velocity)
{
    const size_t index = GetNodeIndex(node);
    if(MLG_VERIFY(index != NodeAndIndex::kInvalidIndex, "Node not found in PhysicsLevel"))
    {
        m_LinearVelocities.X[index] = velocity.x;
        m_LinearVelocities.Y[index] = velocity.y;
        m_LinearVelocities.Z[index] = velocity.z;
    }
}

void
PhysicsLevel::SetAngularVelocity(const Level::Node* node, const Vec3f& /*angularVelocity*/)
{
    const size_t index = GetNodeIndex(node);
    if(MLG_VERIFY(index != NodeAndIndex::kInvalidIndex, "Node not found in PhysicsLevel"))
    {
        //m_AngularVelocities.X[index] = angularVelocity.x;
        //m_AngularVelocities.Y[index] = angularVelocity.y;
        //m_AngularVelocities.Z[index] = angularVelocity.z;
    }
}

// private:

PhysicsLevel::PhysicsLevel(const std::span<const Level::Node>& nodes)
{
    size_t bodyCount = 0;
    for(const auto& node : nodes)
    {
        if(node.Components.Body)
        {
            ++bodyCount;
        }
    }

    m_NodeIndexMap.reserve(bodyCount);
    m_Nodes.reserve(bodyCount);
    m_PosPool[0][0].reserve(bodyCount);
    m_PosPool[0][1].reserve(bodyCount);
    m_PosPool[0][2].reserve(bodyCount);
    m_Radii.reserve(bodyCount);
    m_InvMasses.reserve(bodyCount);
    m_ActiveBodies.reserve(bodyCount);

    for(const auto& node : nodes)
    {
        const std::optional<RigidBody>& optBody = node.Components.Body;

        if(!optBody)
        {
            continue;
        }

        // m_NodeIndexMap is ordered by node pointer, so we can use binary search to find the index
        // of a node.
        m_NodeIndexMap.emplace_back(&node, m_Nodes.size());

        m_Nodes.emplace_back(&node);

        const BoundingSphere& sphere = node.WorldTransform * optBody->GetBoundingSphere();

        m_PosPool[0][0].emplace_back(sphere.GetCenter().x);
        m_PosPool[0][1].emplace_back(sphere.GetCenter().y);
        m_PosPool[0][2].emplace_back(sphere.GetCenter().z);
        m_Radii.emplace_back(sphere.GetRadius());
        m_InvMasses.emplace_back(optBody->GetMass().InvValue());
        m_ActiveBodies.emplace_back(node.IsActive());
    }

    m_PosPool[1][0] = m_PosPool[0][0]; // Make a copy
    m_PosPool[1][1] = m_PosPool[0][1]; // Make a copy
    m_PosPool[1][2] = m_PosPool[0][2]; // Make a copy

    m_LinearVelocitiesPool[0].resize(m_Nodes.size(), 0);
    m_LinearVelocitiesPool[1] = m_LinearVelocitiesPool[0]; // Make a copy
    m_LinearVelocitiesPool[2] = m_LinearVelocitiesPool[0]; // Make a copy

    m_AccelerationPool[0][0] = m_LinearVelocitiesPool[0]; // Make a copy
    m_AccelerationPool[0][1] = m_LinearVelocitiesPool[0]; // Make a copy
    m_AccelerationPool[0][2] = m_LinearVelocitiesPool[0]; // Make a copy

    m_AccelerationPool[1][0] = m_AccelerationPool[0][0]; // Make a copy
    m_AccelerationPool[1][1] = m_AccelerationPool[0][1]; // Make a copy
    m_AccelerationPool[1][2] = m_AccelerationPool[0][2]; // Make a copy

    m_P0 = VVec3 //
        {
            .X = m_PosPool[0][0],
            .Y = m_PosPool[0][1],
            .Z = m_PosPool[0][2],
        };

    m_P1 = VVec3 //
        {
            .X = m_PosPool[1][0],
            .Y = m_PosPool[1][1],
            .Z = m_PosPool[1][2],
        };

    m_A0 = VVec3 //
        {
            .X = m_AccelerationPool[0][0],
            .Y = m_AccelerationPool[0][1],
            .Z = m_AccelerationPool[0][2],
        };

    m_A1 = VVec3 //
        {
            .X = m_AccelerationPool[1][0],
            .Y = m_AccelerationPool[1][1],
            .Z = m_AccelerationPool[1][2],
        };

    m_LinearVelocities = VVec3 //
        {
            .X = m_LinearVelocitiesPool[0],
            .Y = m_LinearVelocitiesPool[1],
            .Z = m_LinearVelocitiesPool[2],
        };
}
    
size_t
PhysicsLevel::GetNodeIndex(const Level::Node* node) const
{
    MLG_ASSERT(node, "Node pointer is null");

    const size_t offset = static_cast<size_t>(node - m_NodeIndexMap.front().GetNode());
    if(offset < m_NodeIndexMap.size() && MLG_VERIFY(m_NodeIndexMap[offset].GetNode() == node))
    {
        return m_NodeIndexMap[offset].GetIndex();
    }
    
    return NodeAndIndex::kInvalidIndex;
}

void
PhysicsLevel::ResolveImpact(const ImpactRecord& impact)
{
    const ImpactResult& impactResult = impact.Result;

    const size_t indexA = impact.BodyIndexA;
    const size_t indexB = impact.BodyIndexB;

    Vec3f velA //
        {
            m_LinearVelocities.X[indexA],
            m_LinearVelocities.Y[indexA],
            m_LinearVelocities.Z[indexA],
        };
    Vec3f velB //
        {
            m_LinearVelocities.X[indexB],
            m_LinearVelocities.Y[indexB],
            m_LinearVelocities.Z[indexB],
        };

    // Compute relative velocity along the normal
    const float vRel = (velA - velB).Dot(impactResult.ContactNormalBtoA);

    const float invMA = m_InvMasses[indexA];
    const float invMB = m_InvMasses[indexB];

    MLG_ASSERT(impact.InvMassSum > 0.0f, "Both bodies have infinite mass, so we can't move them.");

    // Only resolve if bodies are moving towards each other and at least one body has finite mass.
    if(vRel < 0)
    {
        // Impulse

        // n      = contact normal from B to A
        // vRel   = (vA - vB) dot n
        // invMA  = 1 / mA
        // invMB  = 1 / mB
        //
        // j = -(1 + e) * vRel / (invMA + invMB)
        //
        // deltaVA =  j * invMA * n
        // deltaVB = -j * invMB * n

        const float e =
            (vRel < -RESTING_VELOCITY_THRESHOLD)
            // When closing velocity is above the resting velocity threshold
            // treat as a dynamic collision with restitution.
            ? COEFF_OF_RESTITUTION
            // When closing velocity is below the resting velocity threshold
            // treat as a resting contact.
            : 0.0f;

        const float impulseMagnitude = -(1.0f + e) * vRel * impact.RecipInvMassSum;
        const Vec3f impulse = impulseMagnitude * impactResult.ContactNormalBtoA;

        velA += impulse * invMA;
        velB -= impulse * invMB;

        m_LinearVelocities.X[indexA] = velA.x;
        m_LinearVelocities.Y[indexA] = velA.y;
        m_LinearVelocities.Z[indexA] = velA.z;
        m_LinearVelocities.X[indexB] = velB.x;
        m_LinearVelocities.Y[indexB] = velB.y;
        m_LinearVelocities.Z[indexB] = velB.z;
    }

    // Move bodies to point of impact.
    m_P1.X[indexA] = impactResult.PosAtImpactA.x;
    m_P1.Y[indexA] = impactResult.PosAtImpactA.y;
    m_P1.Z[indexA] = impactResult.PosAtImpactA.z;
    m_P1.X[indexB] = impactResult.PosAtImpactB.x;
    m_P1.Y[indexB] = impactResult.PosAtImpactB.y;
    m_P1.Z[indexB] = impactResult.PosAtImpactB.z;
}

void
PhysicsLevel::ResolveContactVelocities(const std::span<ImpactRecord>& contacts)
{
    for(const ImpactRecord& contact : contacts)
    {
        const ImpactResult& impactResult = contact.Result;
        const size_t indexA = contact.BodyIndexA;
        const size_t indexB = contact.BodyIndexB;

        const float invMA = m_InvMasses[indexA];
        const float invMB = m_InvMasses[indexB];

        MLG_ASSERT(contact.InvMassSum > 0.0f, "Both bodies have infinite mass, so we can't move them.");

        const Vec3f vRel //
            {
                m_LinearVelocities.X[indexA] - m_LinearVelocities.X[indexB],
                m_LinearVelocities.Y[indexA] - m_LinearVelocities.Y[indexB],
                m_LinearVelocities.Z[indexA] - m_LinearVelocities.Z[indexB],
            };

        const float closingSpeed = vRel.Dot(impactResult.ContactNormalBtoA);

        if(closingSpeed < 0)
        {
            // Impulse

            // n      = contact normal from B to A
            // vn     = (vA - vB) dot n (closing speed)
            // invMA  = 1 / mA
            // invMB  = 1 / mB
            //
            // j = -vn / (invMA + invMB)
            //
            // deltaVA =  j * invMA * n
            // deltaVB = -j * invMB * n

            const float j = -closingSpeed * contact.RecipInvMassSum;
            const Vec3f impulse = j * impactResult.ContactNormalBtoA;

            m_LinearVelocities.X[indexA] += impulse.x * invMA;
            m_LinearVelocities.Y[indexA] += impulse.y * invMA;
            m_LinearVelocities.Z[indexA] += impulse.z * invMA;
            m_LinearVelocities.X[indexB] -= impulse.x * invMB;
            m_LinearVelocities.Y[indexB] -= impulse.y * invMB;
            m_LinearVelocities.Z[indexB] -= impulse.z * invMB;
        }
    }
}

float
PhysicsLevel::ComputeMaxClosingSpeed(const std::span<ImpactRecord>& contacts) const
{
    float maxClosingSpeed = 0.0f;

    for(const ImpactRecord& contact : contacts)
    {
        const ImpactResult& impactResult = contact.Result;
        const size_t indexA = contact.BodyIndexA;
        const size_t indexB = contact.BodyIndexB;

        const Vec3f vRel //
            {
                m_LinearVelocities.X[indexA] - m_LinearVelocities.X[indexB],
                m_LinearVelocities.Y[indexA] - m_LinearVelocities.Y[indexB],
                m_LinearVelocities.Z[indexA] - m_LinearVelocities.Z[indexB],
            };

        const float closingSpeed = vRel.Dot(impactResult.ContactNormalBtoA);

        if(closingSpeed < 0)
        {
            maxClosingSpeed = std::max(maxClosingSpeed, -closingSpeed);
        }
    }

    return maxClosingSpeed;
}

void
PhysicsLevel::ResolveContactPenetrations(const std::span<ImpactRecord>& contacts)
{
    for(const ImpactRecord& contact : contacts)
    {
        const ImpactResult& impactResult = contact.Result;
        const size_t indexA = contact.BodyIndexA;
        const size_t indexB = contact.BodyIndexB;

        // Calculate penetration depth based on current positions.
        const Vec3f posA //
            {
                m_P1.X[indexA],
                m_P1.Y[indexA],
                m_P1.Z[indexA],
            };

        const Vec3f posB //
            {
                m_P1.X[indexB],
                m_P1.Y[indexB],
                m_P1.Z[indexB],
            };

        const Vec3f delta = posA - posB;
        const float distanceSq = delta.Dot(delta);
        const float minDistance = m_Radii[indexA] + m_Radii[indexB];

        if(distanceSq < minDistance * minDistance)
        {
            const float distance = std::sqrt(distanceSq);

            const Vec3f contactNormal =
                distance > 1e-6f ? delta / distance : impactResult.ContactNormalBtoA;

            const float penetration = minDistance - distance;

            if(penetration > kPenetrationSlop)
            {
                const float C = (penetration - kPenetrationSlop) * kCorrectionPercent;

                const float invMA = m_InvMasses[indexA];
                const float invMB = m_InvMasses[indexB];

                MLG_ASSERT(contact.InvMassSum > 0.0f,
                    "Both bodies have infinite mass, so we can't move them.");

                const Vec3f correction = C * contactNormal * contact.RecipInvMassSum;

                m_P1.X[indexA] += correction.x * invMA;
                m_P1.Y[indexA] += correction.y * invMA;
                m_P1.Z[indexA] += correction.z * invMA;

                m_P1.X[indexB] -= correction.x * invMB;
                m_P1.Y[indexB] -= correction.y * invMB;
                m_P1.Z[indexB] -= correction.z * invMB;
            }
        }
    }
}

void
PhysicsLevel::FindAndResolveAllImpacts()
{
    MLG_SCOPED_TIMER("Physics.FindAndResolveAllImpacts");

    m_GridHash.Clear();
    m_ImpactRecords.clear();
    m_ContactRecords.clear();

    MLG_ABORTIF(m_ActiveBodies.size() > std::numeric_limits<uint32_t>::max(),
        "PhysicsLevel supports a maximum of {} active bodies, but {} are active.",
        std::numeric_limits<uint32_t>::max(),
        m_ActiveBodies.size());

    const auto indices = std::views::iota(0u, static_cast<uint32_t>(m_ActiveBodies.size()));
    const auto range = std::views::zip(m_Radii,
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
        for(auto&& [radius, isActive, p0x, p0y, p0z, p1x, p1y, p1z, index] : range)
        {
            if(!isActive)
            {
                continue;
            }

            // Bodies will be added to all cells of the grid overlapped by the bounding box
            // defined by the current and predicted position.

            //FIXME(KB) - transform bounding spher to world space and use its position.
            m_GridHash.Add({ p0x, p0y, p0z }, { p1x, p1y, p1z }, radius, index);
        }

        potentialCollisionCount = m_GridHash.PotentialCollisionCount();

        if(potentialCollisionCount == 0)
        {
            return;
        }

        m_ImpactRecords.reserve(potentialCollisionCount);
        m_ContactRecords.reserve(potentialCollisionCount);
    }

    static PerfCounter pcPotentialImpacts({ .Name = "Physics.Collision.PotentialImpacts", });
    pcPotentialImpacts.Increment(potentialCollisionCount);

    {
        MLG_SCOPED_TIMER("Physics.FindAndResolveAllImpacts.SweepTests");

        // Collect impact records.
        for(const BodyPair& bodyPair : m_GridHash)
        {
            const size_t indexA = bodyPair.IndexA();
            const size_t indexB = bodyPair.IndexB();

            const float invMA = m_InvMasses[indexA];
            const float invMB = m_InvMasses[indexB];
            const float invMassSum = invMA + invMB;

            if(invMassSum <= 0.0f)
            {
                // Both bodies have infinite mass, so we can't move them.
                continue;
            }

            ImpactRecord impactRecord //
                {
                    .BodyIndexA = indexA,
                    .BodyIndexB = indexB,
                    .InvMassSum = invMassSum,
                    .RecipInvMassSum = 1.0f / invMassSum,
                };

            impactRecord.ImpactFound =
                SphereSphereSweep(bodyPair, impactRecord.Result);

            if(impactRecord.ImpactFound)
            {
                if(0 == impactRecord.Result.Alpha)
                {
                    m_ContactRecords.emplace_back(impactRecord);
                }
                else
                {
                    m_ImpactRecords.emplace_back(impactRecord);
                }
            }
        }
    }

    static PerfCounter pcContacts({ .Name = "Physics.Collision.Contacts", });
    pcContacts.Increment(m_ContactRecords.size());

    static PerfCounter pcImpacts({ .Name = "Physics.Collision.Impacts", });
    pcImpacts.Increment(m_ImpactRecords.size());

    size_t contactSolverIterations = 0;
    if(!m_ContactRecords.empty())
    {
        MLG_SCOPED_TIMER("Physics.FindAndResolveAllImpacts.ResolveContactVelocities");

        float maxClosingSpeed = kContactSolverVelocityThreshold;
        for(contactSolverIterations = 0; contactSolverIterations < kContactSolverMaxIterations
            && maxClosingSpeed >= kContactSolverVelocityThreshold;
            ++contactSolverIterations)
        {
            ResolveContactVelocities(m_ContactRecords);
            maxClosingSpeed = ComputeMaxClosingSpeed(m_ContactRecords);
        }
    }

    static PerfCounter pcContactSolverIterations({
        .Name = "Physics.Collision.ContactSolverIterations",
    });
    pcContactSolverIterations.Increment(contactSolverIterations);

    {
        MLG_SCOPED_TIMER("Physics.FindAndResolveAllImpacts.ResolveContactPenetrations");
        ResolveContactPenetrations(m_ContactRecords);
    }

    {
        MLG_SCOPED_TIMER("Physics.FindAndResolveAllImpacts.ResolveImpacts");
        
        // Sort impact records by time of impact, and resolve in that order.
        // This isn't actually correct, but better than resolving out of order.
        // Substepping will make this better.
        std::ranges::sort(m_ImpactRecords);

        for(auto& impactRecord : m_ImpactRecords)
        {
            ResolveImpact(impactRecord);
        }
    }
}

bool
PhysicsLevel::SphereSphereSweep(const BodyPair& bodies, ImpactResult& impactResult) const
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

    const size_t indexA = bodies.IndexA();
    const size_t indexB = bodies.IndexB();

    const Vec3f pA0{m_P0.X[indexA], m_P0.Y[indexA], m_P0.Z[indexA]};
    const Vec3f pA1{m_P1.X[indexA], m_P1.Y[indexA], m_P1.Z[indexA]};
    const Vec3f pB0{m_P0.X[indexB], m_P0.Y[indexB], m_P0.Z[indexB]};
    const Vec3f pB1{m_P1.X[indexB], m_P1.Y[indexB], m_P1.Z[indexB]};

    const Vec3f relP0 = pA0 - pB0;
    const Vec3f relP1 = pA1 - pB1;
    const Vec3f relMo = relP1 - relP0;
    const float r = m_Radii[indexA] + m_Radii[indexB];
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

        impactResult.ContactPoint = pB0 + impactResult.ContactNormalBtoA * m_Radii[indexB];
        impactResult.PosAtImpactA = pA0;
        impactResult.PosAtImpactB = pB0;

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
        impactResult.PosAtImpactB + (impactResult.ContactNormalBtoA * m_Radii[indexB]);

    return true;
}