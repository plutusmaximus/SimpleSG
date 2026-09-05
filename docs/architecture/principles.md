# Architecture Principles

These are the main ideas we want to keep in mind when designing or changing
the code. They capture choices that come up across multiple systems; they are
not meant to be a complete list of good programming practices.

## Share the concept, not necessarily the code

When systems solve the same kind of problem, use consistent names, lifecycles,
and rules. They do not need a shared implementation, base class, or framework
just to look alike. Share machinery when it prevents real duplication or is
needed to keep an important rule intact.

## Prefer composition over inheritance

Build larger behavior by putting focused components together. Use inheritance
when different types genuinely need to be used through the same interface, not
just to reuse code or give several classes the same shape.

## Keep abstractions thin and boundaries strong

- Use a class when it owns something, enforces a rule, or hides a meaningful
  implementation detail.
- Use simple value types for data that has no identity, ownership, or special
  lifetime requirements.
- Prefer free functions when an operation does not need an object's identity or
  private state.
- A concrete implementation can be well encapsulated without having an
  interface in front of it.
- Do not add an interface, factory, or forwarding layer merely because the
  implementation might be replaced someday.
- If multiple implementations become necessary, shape their shared interface
  around what callers need instead of copying one implementation's API.

## Make it clear who is responsible

When work involves several components, it should be clear who owns the state,
moves the work forward, and decides when it is finished. Other components
should provide capabilities and return results without quietly taking control
through side effects or incidental call order.
