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

Tasks can be composed. A parent task can own child tasks and call their
`Update()` methods from its own `Update()`. The parent uses the children's
results to decide how to continue.

### General shape

```cpp
auto task = Thing::Create(...);

while(!task->IsComplete())
{
    task->Update();

    // Other work can be done here...
}

if(task->Succeeded())
{
    auto result = task->Take();
}
```

Tasks generally provide `Update()`, `IsComplete()`, and `Succeeded()`. Tasks
that produce a value also provide `Take()`, which transfers the result and
consumes the task.
