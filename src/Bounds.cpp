#include "Bounds.h"

#include <algorithm>
#include <array>
#include <random>

static std::vector<Vec3f>
Shuffle(std::span<const Vertex> input)
{
    std::vector<Vec3f> result;
    result.reserve(input.size());
    for(const auto& vertex : input)
    {
        result.push_back(vertex.pos);
    }

    static thread_local std::mt19937 rng(std::random_device{}());

    std::shuffle(result.begin(), result.end(), rng);

    return result;
}

inline static bool
Contains(const BoundingSphere& s, const Vec3f& p)
{
    const Vec3f v(p - s.GetCenter());
    return v.Dot(v) <= (s.GetRadius() * s.GetRadius()) + 1e-12;
}

static inline BoundingSphere
SphereFrom_1(const Vec3f& A)
{
    return { A, 0.0 };
}

static inline BoundingSphere
SphereFrom_2(const Vec3f& A, const Vec3f& B)
{
    const Vec3f c = (A + B) * 0.5;
    const float r = (A - c).Length();
    return { c, r };
}

static inline BoundingSphere
SphereFrom_3(const Vec3f& A, const Vec3f& B, const Vec3f& C)
{
    // Check 2-point spheres (minimality condition)
    BoundingSphere s;

    s = SphereFrom_2(A, B);
    if(Contains(s, C))
        return s;

    s = SphereFrom_2(A, C);
    if(Contains(s, B))
        return s;

    s = SphereFrom_2(B, C);
    if(Contains(s, A))
        return s;

    // True circumcircle case

    const Vec3 a = B - A;
    const Vec3 b = C - A;

    const Vec3 axb = a.Cross(b);
    const double denom = 2.0 * axb.Dot(axb);

    // Degenerate (collinear fallback)
    if(denom == 0.0)
    {
        const Vec3f AB = A - B;
        const Vec3f AC = A - C;
        const Vec3f BC = B - C;

        // fallback to max pair
        const double dAB = AB.Dot(AB);
        const double dAC = AC.Dot(AC);
        const double dBC = BC.Dot(BC);

        if(dAB >= dAC && dAB >= dBC)
            return SphereFrom_2(A, B);
        if(dAC >= dAB && dAC >= dBC)
            return SphereFrom_2(A, C);
        return SphereFrom_2(B, C);
    }

    // Derived from solving in-plane system:
    // c = A + (|a|^2 (b x (a x b)) + |b|^2 ((a x b) x a)) / (2|a x b|^2)

    const Vec3f term1 = axb.Cross(a) * b.Dot(b);
    const Vec3f term2 = b.Cross(axb) * a.Dot(a);

    const Vec3f c = A + (term1 + term2) / static_cast<float>(denom);
    const float r = (c - A).Length();
    return { c, r };
}

static BoundingSphere
SphereFrom_4(const Vec3f& A, const Vec3f& B, const Vec3f& C, const Vec3f& D)
{
    BoundingSphere s;

    // Try all 3-point spheres
    s = SphereFrom_3(A, B, C);
    if(Contains(s, D))
        return s;

    s = SphereFrom_3(A, B, D);
    if(Contains(s, C))
        return s;

    s = SphereFrom_3(A, C, D);
    if(Contains(s, B))
        return s;

    s = SphereFrom_3(B, C, D);
    if(Contains(s, A))
        return s;

    // Solve linear system:
    // 2 c·(B-A) = |B|^2 - |A|^2
    // 2 c·(C-A) = |C|^2 - |A|^2
    // 2 c·(D-A) = |D|^2 - |A|^2

    const Vec3f ba = B - A;
    const Vec3f ca = C - A;
    const Vec3f da = D - A;

    const double len2A = A.Dot(A);
    const double len2B = B.Dot(B);
    const double len2C = C.Dot(C);
    const double len2D = D.Dot(D);

    const double rhs1 = len2B - len2A;
    const double rhs2 = len2C - len2A;
    const double rhs3 = len2D - len2A;

    // Matrix rows
    const double M[3][3] = { { 2 * ba.x, 2 * ba.y, 2 * ba.z },
        { 2 * ca.x, 2 * ca.y, 2 * ca.z },
        { 2 * da.x, 2 * da.y, 2 * da.z } };

    const double bvec[3] = { rhs1, rhs2, rhs3 };

    // Solve via Cramer's rule (explicit, stable for small system)

    auto det3 = [](const double m[3][3]) -> double
    {
        return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
               m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
               m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    };

    const double detM = det3(M);

    // Degenerate (coplanar fallback)
    if(std::abs(detM) < 1e-12)
    {
        // Should not happen if subset checks are correct,
        // but fallback anyway
        return SphereFrom_3(A, B, C);
    }

    double Mx[3][3], My[3][3], Mz[3][3];

    for(int i = 0; i < 3; ++i)
    {
        Mx[i][0] = bvec[i];
        Mx[i][1] = M[i][1];
        Mx[i][2] = M[i][2];

        My[i][0] = M[i][0];
        My[i][1] = bvec[i];
        My[i][2] = M[i][2];

        Mz[i][0] = M[i][0];
        Mz[i][1] = M[i][1];
        Mz[i][2] = bvec[i];
    }

    const Vec3f c = { static_cast<float>(det3(Mx) / detM),
        static_cast<float>(det3(My) / detM),
        static_cast<float>(det3(Mz) / detM) };

    const float r = (c - A).Length();

    return { c, r };
}

static BoundingSphere
SphereFrom(const std::array<Vec3f, 4>& R, int n)
{
    if(n == 0)
        return { { 0, 0, 0 }, 0 };
    if(n == 1)
        return SphereFrom_1(R[0]);
    if(n == 2)
        return SphereFrom_2(R[0], R[1]);
    if(n == 3)
        return SphereFrom_3(R[0], R[1], R[2]);
    return SphereFrom_4(R[0], R[1], R[2], R[3]);
}

static BoundingSphere
Welzl_Recursive(std::vector<Vec3f>& P, const size_t n, std::array<Vec3f, 4>& R, int r)
{
    // Base case: no points left OR boundary full
    if(n == 0 || r == 4)
    {
        return SphereFrom(R, r);
    }

    // Pick last point (randomized order ensures expected linear time)
    Vec3f p = P[n - 1];

    // Compute sphere without p
    BoundingSphere s = Welzl_Recursive(P, n - 1, R, r);

    // If p is inside, sphere unchanged
    if(Contains(s, p))
    {
        return s;
    }

    // Otherwise p must lie on boundary → add to R
    R[r] = p;

    return Welzl_Recursive(P, n - 1, R, r + 1);
}

BoundingSphere
BoundingSphere::FromVertices(std::span<const Vertex> vertices)
{
    std::vector<Vec3f> shuffledPoints = Shuffle(vertices);

    std::array<Vec3f, 4> R;
    return Welzl_Recursive(shuffledPoints, shuffledPoints.size(), R, 0);
}