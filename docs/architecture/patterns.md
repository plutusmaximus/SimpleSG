# Architecture Patterns

Patterns describe approaches we use repeatedly in this project. They explain
when an approach applies, the rules that matter, and its tradeoffs. A pattern
is not a rule for every situation, but differences should be intentional.

## Cooperative Tasks

Some work cannot be finished in one call. For that work, create a task and call
`Update()` until it is complete.

`Update()` does whatever work it can and then returns. It never waits or blocks,
and the task always runs on the thread that called `Update()`. The task may
check on work happening elsewhere, such as a file read or worker job, but that
work is separate from the task itself.

A task advances through a series of stages, usually defined with an enum. Its
`Update()` method typically uses a `switch` on the current stage, does the work
for that stage, and moves to the next stage when it can.

Cooperative tasks implement `ICoopTask`, which provides `Begin()`, `IsPending()`,
and `Update()`. A task that produces a result also provides a typed `Take()`
method. `Take()` is not part of `ICoopTask` because different tasks produce
different result types.

Construct a task with the dependencies and inputs it needs, then call `Begin()`
once. Calling `IsPending()` before `Begin()` is a contract violation. After a
task reaches a terminal stage, `IsPending()` returns false. Destroying an
unstarted or terminal task is valid, but destroying a pending task violates the
task's lifetime invariant.

Tasks can be composed directly. A parent task can own child tasks and call their
`Update()` methods from its own `Update()`. The parent uses the children's
results to decide how to continue.

Use `CoopTaskBatch` when several heterogeneous tasks can run concurrently. The
batch borrows its tasks through `ICoopTask` pointers. Every pointer must be
non-null, and every task must remain at a stable address and outlive the batch.
The batch begins and updates its children and completes after every child has
reached a terminal stage.

Batch completion does not mean that every child succeeded. The batch coordinates
lifetime and progress, while the owner remains responsible for taking each
child's result and handling its failure. If a child fails to begin, the batch
removes it from the pending set so that successfully started children can still
run to completion.

### General shape

```cpp
ThingTask task(...);
MLG_CHECK(task.Begin());

while(task.IsPending())
{
    task.Update();

    // Other work can be done here...
}

auto result = task.Take();
MLG_CHECK(result);
```

`Take()` transfers the result and consumes it, so it can only succeed once.

## Valid Construction

An object should be valid and ready to use after it is constructed. Delete the
default constructor when default construction cannot produce a valid object.

When object creation can fail, instead of exposing a constructor, use a static
`Create()` function that returns a `Result<T>`. Keep the constructor private so
callers cannot bypass the creation path.

### General shape

```cpp
class Thing
{
public:
    Thing() = delete;

    static Result<Thing> Create(...);

private:
    Thing(...);
};
```
