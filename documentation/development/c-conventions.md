# C implementation conventions

[Documentation index](../README.md)

These naming and formatting conventions were accepted by the user. C implements neo; C types do not define a class hierarchy in the language.

## Names and layout

| Element | Convention | Example |
| --- | --- | --- |
| Functions | neo_ prefix and snake_case | neo_object_copy |
| Types and struct tags | neo_ prefix and snake_case | neo_vm, neo_object_id |
| Variables and fields | snake_case | message_count |
| Constants and enum members | NEO_UPPER_SNAKE_CASE | NEO_MESSAGE_ACCEPTED |
| Macros | NEO_UPPER_SNAKE_CASE | NEO_ARRAY_COUNT |
| Files | snake_case | object.c, object.h |

Use four spaces, no indentation tabs, and same-line opening braces. Put the pointer star with the declarator: `neo_vm *vm`. Use explicit blocks for control flow. Keep formatting consistent rather than introducing a formatter dependency for the first milestone.

## Types and interfaces

- Prefer opaque structs at module boundaries: `typedef struct neo_vm neo_vm;`.
- Do not hide pointers inside typedefs or use project `_t` suffixes.
- Use size_t for allocation sizes, indexes, and counts; check size arithmetic before allocation.
- Use fixed-width integers when representation width is part of the contract.
- Use bool for booleans, enums for states, and const for read-only inputs.
- File-private functions are static. Prefer functions over macros where possible.
- Fallible operations return explicit neo_status results. Return other results through named out parameters, such as out_copy.

```c
neo_status neo_object_copy(
    neo_vm *vm,
    neo_object_id source,
    neo_object_id destination,
    neo_object_id *out_copy
);
```

This is a naming illustration, not a final authorized-operation signature. The implementation must carry or derive the authenticated principal and check its authority; accepting a bare ID is not itself authorization.

## Engineering baseline

Use the architecture's proposed portable C17 baseline. Keep runtime code standard-library-only initially. Document ownership of allocated memory, borrowed pointers, output parameters, and cleanup obligations in headers. Define outputs on failure consistently. Never expose raw C addresses as neo identities or serialize native layouts or function pointers.

Use strict compiler warnings and debug builds. Run address and undefined-behavior sanitizers where the toolchain supports them. Assertions express internal programmer invariants; malformed images and denied operations return failures rather than relying on assertions.

Keep host error reporting separate from neo's configurable failure policy. A C status return does not prescribe whether the image retries, repairs, or propagates a language-level failure.
