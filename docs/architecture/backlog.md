# Architecture Backlog

This file tracks architectural questions that still need a decision. It is not
a backlog of coding tasks.

When an item is resolved, record the outcome in the appropriate architecture
document and remove it from this file.

## Failure classification and handling

The project currently uses `Result` and `MLG_CHECK` for failures that may be
recoverable as well as failures that prevent the program from continuing. The
difference between these cases is not yet defined.

Questions to answer:

- How do we distinguish programming errors, unrecoverable operational failures,
  and recoverable failures?
- Who decides whether an operational failure is recoverable?
- How should each kind of failure be represented?
- Where should unrecoverable failures terminate the current operation or the
  program?
- When should code use assertions, verification, returned errors, logging, or
  termination?
- What error context should be preserved as a failure passes through systems?
- Should `MLG_CHECK` keep its current role or be replaced by more specific
  helpers?

This item is resolved when the failure categories, their handling, and the
responsibilities of callers and subsystems are documented well enough to review
existing error paths consistently.
