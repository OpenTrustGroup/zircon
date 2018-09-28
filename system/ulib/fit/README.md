libfit
======

FIT is a lean library of portable C++ abstractions for control flow and
memory management beyond what is offered by the C++ 14 standard library.

FIT is intended to facilitate the implementation of Fuchsia SDK libraries and
their clients.  Some of its features are intended to simplify asynchronous
event-driven programs as the C++ 14 standard library has some limitations in
that regard.  Such programs are very common on Fuchsia due to the prevalence of
asynchronous inter-process communication throughout the operating system.

FIT only depends on the C++ language and standard library.  It offers essential
enhancements to the C++ standard library rather than attempting to replace it or
become a framework for writing applications.  FIT can be thought of as an
"annex" that expresses a few ideas we wish the C++ standard library might itself
implement someday.

FIT is lean.

## What Belongs in FIT

Several Fuchsia SDK libraries, such as *libfidl*, depend on FIT and on the C++
standard library.  As these libraries are broadly used, we must take care in
deciding what features to include in FIT to avoid burdening developers with
unnecessary code or dependencies.

In general, the goal is to identify specific abstractions that make sense to
generalize across the entire ecosystem of Fuchsia C++ applications.  These will
necessarily be somewhat low-level but high impact.  We don't want to add code to
FIT simply because we think it's cool.  We need evidence that it is a common
idiom and that a broad audience of developers will significantly benefit from
its promotion.

Here are a few criteria to consider:

- Is the feature lightweight, general-purpose, and platform-independent?
- Is the feature not well served by other means, particularly by the C++
  standard library?
- Is the feature needed by a Fuchsia SDK library?
- Does the feature embody a beneficial idiom that clients of the Fuchsia SDK
  commonly use?
- Has the feature been re-implemented many times already leading to code
  fragmentation that we would like to eliminate?

If in doubt, leave it out.  See [Justifications] below.

## What Doesn't Belong in FIT

FIT is not intended to become a catch-all class library.

Specifically prohibited features:

- Features that introduce dependencies on libraries other than the C and C++
  standard library.
- Features that only work on certain operating systems.
- Collection classes where the C++ 14 standard library already offers an
  adequate (if not perfect) alternative.
- Classes that impose an implementation burden on clients such as event loops,
  dispatchers, frameworks, and other glue code.

## Implementation Considerations

FIT is not exception safe (but could be made to be in the future).

## Style Conventions

FIT's API style follows C++ standard library conventions.

In brief:

- Class, method, field, and variable identifiers are `snake_case`.
- Template parameters are `CamelCase`.
- Preprocessor macros are `UPPER_SNAKE_CASE`.
- Whenever a FIT API mimics a C++ standard library API, it should have a
  similar structure.  For example, `fit::function` offers the same methods
  as `std::function` except where necessary to diverge due to its move-only
  semantics.

Rule of thumb: Using FIT should feel like using the C++ standard library.

## Justifications

These sections explain why certain features are in FIT.

### fit::function

- *libfidl*'s API needs a callable function wrapper with move semantics but
  C++ 14's `std::function` only supports copyable function objects which forces
  FIDL to allocate callback state on the heap making programs less efficient
  and harder to write.
- Lots of other C++ code uses callbacks extensively and would benefit from move
  semantics for similar reasons.
- So we should create a move-only function wrapper to use everywhere.

### fit::defer

- When writing asynchronous event-driven programs, it can become challenging
  to ensure that resources remain in scope for the duration of an operation
  in progress and are subsequently released.
- The C++ 14 standard library offers several classes with RAII semantics, such
  as `std::unique_ptr`, which are helpful in these situations.  Unfortunately the
  C++ 14 standard library does not offer affordances for easily invoking a
  function when a block or object goes out of scope short of implementing a
  new class from scratch.
- We have observed several re-implementations of the same idea throughout the
  system.
- So we should create a simple way to invoke a function on scope exit.
