#include "PhysicsSolver.h"

#include "PerfMetrics.h"

static constexpr float RESTING_VELOCITY_THRESHOLD = 0.01f;
static constexpr float COEFF_OF_RESTITUTION = 0.8f;

Result<>
PhysicsSolver::Create(const Level& level, PhysicsSolver& outSolver)
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

    m_A1[bodyIndex] += force * m_Bodies[bodyIndex].Mass.InvValue();
}

void
PhysicsSolver::Update(const float timeStep)
{
    static PerfTimer perfTimer("Physics.Update");
    auto scopedTimer = perfTimer.StartScoped();

    static constexpr int MAX_SUBSTEPS = 4;

    float t0 = 0.0f;
    float dt = timeStep;

    std::swap(m_Trs0, m_Trs1);
    PredictPositions(dt);
    FindImpacts();

    int count = 0;

    while(count++ < MAX_SUBSTEPS && !m_ImpactRecords.empty())
    {
        size_t numResting;

        for(numResting = 0; numResting < m_ImpactRecords.size() &&
                       m_ImpactRecords[numResting].GetResult().Alpha <= RESTING_VELOCITY_THRESHOLD;
            ++numResting)
        {
        }

        if(numResting < m_ImpactRecords.size())
        {
            ++numResting; // Include the first non-resting impact as well.
        }

        const float t1 = t0 + m_ImpactRecords[numResting - 1].GetResult().Alpha * dt;

        // Back up to time of impact.
        PredictPositions(t1);

        // Resolve impacts.
        for(size_t index = 0; index < numResting; ++index)
        {
            ResolveImpact(m_ImpactRecords[index]);
        }

        t0 = t1;
        dt = timeStep - t0;

        std::swap(m_Trs0, m_Trs1);
        PredictPositions(dt);
        FindImpacts();
    }

    if(!m_ImpactRecords.empty())
    {
        ResolveAllImpacts();
    }

    UpdateVelocities(timeStep);
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
PhysicsSolver::PredictPositions(const float dt)
{
    static PerfTimer perfTimer("Physics.PredictPositions");
    auto scopedTimer = perfTimer.StartScoped();

    for (size_t i = 0; i < m_Bodies.size(); ++i)
    {
        // Update position using velocity and acceleration from previous time step.
        // p = ∫ v dt
        // v = v0 + a * t
        // p1 = ∫ (v0 + a * t) dt = v0 * dt + (a * dt^2) / 2 + p0
        m_Trs1[i].T = (m_Bodies[i].LinearVelocity * dt) + ((m_A0[i] * dt * dt) / 2) + m_Trs0[i].T;
    }
}

void
PhysicsSolver::UpdateVelocities(const float dt)
{
    static PerfTimer perfTimer("Physics.UpdateVelocities");
    auto scopedTimer = perfTimer.StartScoped();

    // FIXME(KB) - caller should account for dt not being the entire timestep,
    // but a substep due to collision events.
    for (size_t i = 0; i < m_Bodies.size(); ++i)
    {
        // Acceleration can change over the timestep due to, e.g., gravity.
        // So it's incorrect to use a single acceleration over the timestep.
        // Instead we approximate the integral of the acceleration function
        // using the trapezoidal rule:
        // integral from t0 to t1 of a(t) dt ~= (t1 - t0) * (a(t0) + a(t1)) / 2
        m_Bodies[i].LinearVelocity += (m_A0[i] + m_A1[i]) * dt * 0.5f;
    }

    std::swap(m_A0, m_A1);
    std::fill(m_A1.begin(), m_A1.end(), Vec3f{ 0 });
}

void
PhysicsSolver::ResolveAllImpacts()
{
    static PerfTimer perfTimer("Physics.DoCollisions");
    auto scopedTimer = perfTimer.StartScoped();

    for(const ImpactRecord& impact : m_ImpactRecords)
    {
        ResolveImpact(impact);
    }
}

void
PhysicsSolver::FindImpacts()
{
    static PerfTimer perfTimer("Physics.FindImpacts");
    auto scopedTimer = perfTimer.StartScoped();

    m_GridHash.Clear();
    m_ImpactRecords.clear();

    for(size_t i = 0; i < m_Bodies.size(); ++i)
    {
        m_GridHash.Add(m_Trs0[i].T, m_Trs1[i].T, m_Colliders[i], i);
    }

    for(const BodyPair& bodyPair : m_GridHash)
    {
        ImpactResult impactResult;

        if(!SphereSphereSweep(bodyPair, impactResult))
        {
            continue;
        }

        m_ImpactRecords.emplace_back(bodyPair, impactResult);
    }

    std::sort(m_ImpactRecords.begin(), m_ImpactRecords.end());
}

bool
PhysicsSolver::SphereSphereSweep(const BodyPair& pair, ImpactResult& impactResult) const
{
    static PerfTimer perfTimer("Physics.SphereSphereSweep");
    auto scopedTimer = perfTimer.StartScoped();

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

    const float radiusA = colliderA.GetSphereRadius();
    const float radiusB = colliderB.GetSphereRadius();

    const TrsTransformf& transformA0 = m_Trs0[pair.IndexA()];
    const TrsTransformf& transformA1 = m_Trs1[pair.IndexA()];
    const TrsTransformf& transformB0 = m_Trs0[pair.IndexB()];
    const TrsTransformf& transformB1 = m_Trs1[pair.IndexB()];

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

void
PhysicsSolver::ResolveImpact(const ImpactRecord& impact)
{
    const BodyPair& bodyPair = impact.GetBodies();
    const ImpactResult& impactResult = impact.GetResult();

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