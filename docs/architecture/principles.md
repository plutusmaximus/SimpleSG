# Architecture Principles

Principles guide judgment across the project. They express qualities to
preserve rather than mandate a particular class, function, or implementation.
When principles are in tension, make the tradeoff explicit.

## Make ownership and lifetime explicit

Every resource and operation with persistent state should have an identifiable
owner. Non-owning references and pointers must not outlive the objects they
refer to, and the code should make responsibility for those objects' lifetimes
clear. Prefer values and unique ownership; use shared ownership only when the
domain genuinely has shared lifetime.

## Make invalid states difficult to represent

Use construction, types, and access control to prevent invalid combinations
where practical. Factories should return usable objects, consuming operations
should be visibly consuming, and implementation-only states should not leak
into public interfaces.

Runtime checks remain appropriate at external boundaries and for invariants
that types cannot economically express.

## Keep control flow locally understandable

A reader should be able to determine how behavior is coordinated, where state
changes, and how failures propagate without reconstructing logic spread across
unrelated components. Prefer explicit transitions and responsibilities over
hidden or diffuse control flow.

## Respect architectural boundaries

Give each subsystem a clear responsibility and keep dependency direction
intentional. Interactions across subsystem, platform, resource, or execution
boundaries should occur through explicit interfaces, with relevant constraints
and assumptions made visible.

## Prefer composition and conventions over frameworks

Use small types with focused responsibilities and consistent vocabulary.
Prefer a documented convention over common inheritance or shared infrastructure
when additional machinery would neither enforce a meaningful invariant nor
remove demonstrated duplication.

## Prefer continuity across systems

Use established project patterns and analogous systems as the starting point
for new work. Consistent ownership, naming, lifecycle, and failure semantics
reduce the number of concepts needed to understand the system.

Consistency is not an end in itself. When constraints differ or an established
approach has a demonstrated architectural problem, prefer an intentional
variation and make its rationale clear rather than preserving superficial
uniformity.

## Keep public interfaces smaller than implementations

Expose the concepts consumers require while keeping internal state, platform
details, and other implementation mechanisms private. Use indirection only when
it provides a concrete benefit such as reduced coupling, stable storage, or
compilation isolation.

## Make failure policy explicit

Distinguish recoverable operational failure from violated invariants. Define
what success means, whether partial results are valid, which failures can be
recovered from, and how failures cross architectural boundaries.

## Pay for complexity when requirements demand it

Do not introduce features, ownership models, shared infrastructure, or broad
abstractions in anticipation of hypothetical needs. Do not generalize from a
single example. When a recurring problem does justify additional machinery,
record the problem, invariants, tradeoffs, and intended scope.

## Preserve diagnosability

Failures and invariant violations should provide enough context to identify the
operation and affected resource. Architectural boundaries should not discard
useful failure information unless logging is deliberately the diagnostic
contract.
