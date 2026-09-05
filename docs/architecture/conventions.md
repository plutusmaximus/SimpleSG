# Coding Conventions

Conventions are concrete expectations for project code. They cover choices that
are too specific to be principles and too small to be patterns.

## Language and build

- Use C++23.
- Do not use exceptions or RTTI. The project builds with both disabled.
- Keep project code free of compiler warnings. The build treats warnings as
  errors for project targets.

## Errors and invariants

- Use `Result` for recoverable failures.
- Use assertions and the project's verification helpers for programming errors
  and violated invariants.

## Allocation and containers

- Heap allocation is acceptable during program startup.
- Small allocations are also acceptable when they are short-lived and do not
  happen repeatedly, such as once per frame or update.
- Prefer a small number of bulk allocations over many long-lived small
  allocations.
- Use `std::vector` for dynamically sized collections when contiguous storage is
  appropriate.
- Make an effort to determine a vector's eventual size and call `reserve()`
  before adding elements so the vector does not repeatedly grow.
- Avoid non-contiguous containers such as `std::list` and `std::map` unless their
  specific behavior is needed and a contiguous representation would not be a
  good fit.
- Look for a contiguous alternative before choosing a non-contiguous container.
  Prefer `std::vector` over `std::list`, and consider a sorted vector with binary
  search instead of `std::map`.

Why:

Many long-lived small allocations add allocator overhead, increase memory
fragmentation, and spread related data across memory. Repeated allocation in a
frequently run path also adds bookkeeping and can make frame time less
predictable. Occasional temporary allocations are less concerning because
their cost does not accumulate and the memory is quickly returned to the
allocator. Bulk contiguous storage uses less overhead and gives the CPU better
memory locality.

## Parameters stored by value

When a function or constructor stores a non-trivial movable value, such as a
`std::string` or `std::vector`, pass the parameter by value and move it into
storage. Pass small, trivially copyable values by value and assign them
normally.

```cpp
class Thing
{
public:
    explicit Thing(std::string name)
        : m_Name(std::move(name))
    {
    }

private:
    std::string m_Name;
};
```

Why:

- With an lvalue: copy-construct the parameter, then move-construct the member.
- With an rvalue: move-construct the parameter, then move-construct the member.
- With a temporary: construction of the parameter may be elided, followed by
  moving the parameter into the member.
- `std::move()` casts the named parameter to an rvalue so the member's move
  constructor can be used.
- A `const` value or `const` reference cannot be moved into the member, so the
  member must be copied instead.

In cases where the function/constructor only uses the value but doesn't store it
use a `const` reference.

## Style

- Format source code using the root `.clang-format` file.
- Follow the naming used by nearby project code.
