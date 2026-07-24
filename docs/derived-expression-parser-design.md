# CPU-Only Derived-Expression Parser

Status: Implemented

## Summary

Amrvis2 will replace its external `amrex-parser` dependency with a small,
Amrvis2-owned parser for single-line scalar algebraic expressions. The parser
will support numeric literals, scalar field references, parentheses, unary
signs, the four basic arithmetic operators, exponentiation, and the following
functions:

```text
sqrt pow exp log exp10 log10
```

Both `pow(a, b)` and `a**b` will compile to the same exponentiation operation.
The compiled representation will be immutable and safe to share between
concurrent block requests. Evaluation will be CPU-only and will not allocate
memory per cell.

This change removes a configure-time network dependency and a much broader
expression language than the application needs. It does not change how
derived fields participate in the block cache, slice queries, line queries,
or derived-field chaining.

## Motivation

The current derived-field implementation fetches a pinned revision of the
standalone `amrex-parser` project by default. Amrvis2 uses only a narrow part
of that library:

1. Parse an expression once.
2. Discover the scalar field names referenced by the expression.
3. Bind those names to input positions.
4. Evaluate the compiled expression repeatedly on CPU.

The dependency additionally contains a generated Flex/Bison parser,
algebraic optimizations, assignment and conditional syntax, many unused
functions, and executors for several GPU programming models. Carrying that
surface makes offline builds and maintenance more complicated without
providing functionality required by Amrvis2.

An application-owned parser also lets the user interface describe the exact
language accepted by the application instead of referring users to the
larger AMReX parser language.

## Goals

- Support the complete expression language specified in this document.
- Preserve existing derived expressions that use `**` for exponentiation.
- Parse and bind an expression once when a derived field is added.
- Make compiled expressions immutable and safe for concurrent evaluation.
- Avoid allocation and parsing in the per-cell evaluation loop.
- Preserve constant expressions, derived-field chaining, scalar-field
  validation, dashed field names, and the current 16-input limit.
- Produce errors that identify the unexpected token and its byte offset.
- Remove all build-time and runtime dependencies on `amrex-parser`.

## Non-goals

- GPU execution.
- Multiple statements, assignments, or local variables.
- Multiline expressions or line continuations.
- Comparisons, conditionals, or Boolean operators.
- Implicit multiplication such as `2x` or `2(x + 1)`.
- User-defined functions or constants.
- General symbolic optimization or just-in-time compilation.
- Expanding the current derived-field model beyond scalar, cell-centered
  fields.

## Language

### Lexical rules

An expression is one non-empty line. Space and horizontal tab characters may
appear between tokens. Carriage returns and newlines are rejected, including
embedded newlines pasted into the dialog.

Numeric literals use decimal C-style syntax with an optional exponent:

```text
1
1.
.5
3.25
1e6
2.5E-3
```

Non-dashed parser identifiers use the existing spelling:

```text
[A-Za-z_][A-Za-z0-9_.]*
```

This preserves field names containing underscores and dots. Resolution of
known field names containing `-` is described separately below.

The following punctuation is recognized:

```text
+ - * / ** ( ) ,
```

Every other character is rejected. In particular, `^`, `;`, `=`, comments,
comparison operators, and brackets are not aliases for supported syntax.

### Grammar

The grammar is:

```text
expression  := additive
additive    := multiplicative (("+" | "-") multiplicative)*
multiplicative
            := unary (("*" | "/") unary)*
unary       := ("+" | "-") unary | power
power       := primary ("**" unary)?
primary     := number
             | field
             | "(" expression ")"
             | unary-function "(" expression ")"
             | "pow" "(" expression "," expression ")"
unary-function
            := "sqrt" | "exp" | "log" | "exp10" | "log10"
```

The recursive right operand in `power` makes `**` right-associative and
permits signed exponents:

```text
a**b**c == pow(a, pow(b, c))
a**-b   == pow(a, -b)
-a**2   == -(pow(a, 2))
```

`pow(a, b)` and `a**b` have identical numerical semantics and compile to the
same bytecode instruction. `**` is therefore a syntax compatibility alias,
not a separate operation.

Function names are recognized as functions only when followed by `(`.
Consequently, a field named `log` may be referenced as `log`, while
`log(x)` invokes the natural logarithm.

### Operations

| Syntax | Arity | Evaluation |
| --- | ---: | --- |
| `+a`, `-a` | 1 | Unary sign |
| `a+b`, `a-b` | 2 | Addition and subtraction |
| `a*b`, `a/b` | 2 | Multiplication and division |
| `a**b`, `pow(a,b)` | 2 | `std::pow(a, b)` |
| `sqrt(a)` | 1 | `std::sqrt(a)` |
| `exp(a)` | 1 | `std::exp(a)` |
| `log(a)` | 1 | `std::log(a)` |
| `exp10(a)` | 1 | `std::pow(10.0, a)` |
| `log10(a)` | 1 | `std::log10(a)` |

The parser will not add domain checks or replace the platform's ordinary
floating-point behavior. Division by zero, overflow, and invalid function
domains therefore produce the same infinities or NaNs as the corresponding
C++ floating-point operations on the supported platform.

### Field names containing dashes

Amrvis2 plotfiles may contain field names such as `second-field`. A bare
lexer cannot distinguish that name from subtraction, so field-name
resolution will preserve the current longest-known-name rule:

1. Before parsing, consider the scalar fields currently published by the
   dataset, including previously added derived fields.
2. Match known dashed names longest first at identifier boundaries.
3. Replace each match with an internal identifier that cannot collide with
   a dataset field or text already present in the expression.
4. Parse and bind the rewritten expression.

If a dataset contains fields named `a`, `b`, and `a-b`, the text `a-b`
refers to the dashed field. Users may write `a - b` to request subtraction.
This is the existing behavior and must have an explicit regression test.

## Architecture

### Component boundary

The parser will live in an independent, non-Qt library:

```text
include/amrvis/expression/Expression.hpp
src/expression/CMakeLists.txt
src/expression/Expression.cpp
```

The target will be named `Amrvis::expression`. `Amrvis::io` will depend on
it privately. Keeping the component independent of plotfile metadata makes
the grammar and evaluator directly unit-testable; dashed-name rewriting and
field-ID binding remain in the plotfile dataset layer because they require
dataset metadata.

The public expression API will have this conceptual shape:

```cpp
class CompiledExpression {
public:
    static CompiledExpression compile(std::string_view source);

    std::span<const std::string> symbols() const noexcept;
    ExpressionEvaluator makeEvaluator() const;
};

class ExpressionEvaluator {
public:
    double evaluate(std::span<const double> variables);
};
```

The exact ownership types may change during implementation, but the
following contracts must remain:

- A compiled expression owns all bytecode, constants, and symbol names.
- A compiled expression is immutable after construction and is safe to
  access concurrently.
- An evaluator references one compiled expression and owns reusable scratch
  storage.
- An evaluator is confined to one block-evaluation call and is not shared
  between threads.
- The variable span order matches `CompiledExpression::symbols()`.

### Parsing

A hand-written recursive-descent parser will implement the grammar directly.
It needs no generated sources or parser-generator dependency. Parsing emits
bytecode in postfix order rather than retaining a pointer-heavy abstract
syntax tree.

Symbol slots are assigned deterministically in first-appearance order.
Repeated references to the same identifier reuse the same slot.

The parser computes the required evaluation-stack depth while emitting
bytecode. It rejects malformed input before a derived field is published.
It does not perform algebraic rewriting or constant folding in the initial
implementation.

### Bytecode

The initial instruction set is:

```text
PushConstant
PushVariable
Negate
Add
Subtract
Multiply
Divide
Sqrt
Pow
Exp
Log
Exp10
Log10
```

Each instruction contains only its opcode and, where applicable, a constant
or variable-table index. Unary `+` emits no instruction. Both `**` and
`pow()` emit `Pow`.

Compilation validates stack effects. A well-formed program must finish with
exactly one value. This validation is an internal invariant in addition to
the grammar checks.

### Evaluation

`PlotfileDataset::DerivedField` will own the compiled expression and the
ordered input `FieldId` values. `readDerivedBlock()` will:

1. Read and pin each input block as it does today.
2. Create one evaluator for the current block request.
3. Reuse the evaluator's stack allocation for every cell.
4. Fill the existing output vector and retain the current cancellation
   checks.

No mutable parser or evaluator state is stored on the shared derived field.
Concurrent requests for the same derived field therefore share immutable
bytecode but use separate evaluation stacks.

Constant expressions have no input blocks and still use the requested grid
box to determine their output extent, preserving current behavior.

## Validation and errors

Parser errors will derive from `std::invalid_argument` and contain:

- A short description.
- The zero-based byte offset of the offending token or end of input.
- The token spelling when one is available.

Examples include:

```text
unexpected token '^' at byte 3
unknown function 'sin' at byte 0
expected ',' after first argument to pow at byte 7
newlines are not allowed at byte 12
expected expression at byte 9
```

Parsing distinguishes an unknown function call from a field reference.
Dataset binding remains responsible for reporting an unknown field,
non-scalar input, duplicate derived-field name, or more than 16 referenced
fields.

The UI should display the parser or binding error without terminating the
application. The derived field must not be added to metadata or saved UI
state unless parsing and binding both succeed.

## User interface

The derived-field dialog will use a single-line editor rather than
`QPlainTextEdit`. Its example will remain familiar:

```text
sqrt(x_velocity**2 + y_velocity**2)
```

The help text will list the supported operators and functions directly:

```text
Operators: + - * / **
Functions: sqrt, pow, exp, log, exp10, log10
```

It will no longer refer to "AMReX parser syntax."

Expressions restored from application state pass through the same parser as
new expressions. An invalid restored expression is skipped without changing
the dataset metadata, and Amrvis2 reports the field name and positional
parser error in Diagnostics. Later valid derived fields are still restored
when their inputs are available.

## Build and dependency changes

The implementation will:

- Remove the `FetchContent` declaration for `amrex-parser`.
- Remove the `AMRVIS_USE_SYSTEM_AMREXPR` option and installed-package path.
- Remove the `amrexpr::amrexpr` link and include-directory handling.
- Add and link the internal `Amrvis::expression` target.
- Remove `amrex-parser` instructions from `INSTALL.md`.

`AMRVIS_ENABLE_DERIVED_FIELDS` will remain during this change. Retaining the
feature option keeps the migration focused and avoids unnecessarily breaking
packager configurations. Whether a dependency-free derived-field feature
still needs a build-time switch can be decided separately.

The new parser will be written against the language contract in this
document rather than copied from `amrex-parser`. No third-party parser source
or generated code will be vendored, so the parser component will use the
Amrvis2 project license and will not require a new third-party notice.

## Compatibility

The supported subset includes all derived-expression forms currently covered
by Amrvis2's tests:

- Constants such as `3.5`.
- Scalar references such as `phi`.
- Arithmetic such as `2*phi`.
- Derived-field chaining.
- Dashed input names.
- Exponentiation written as `**`.
- `sqrt`.

The current example

```text
sqrt(first**2 + second-field**2)
```

continues to work unchanged.

The following `amrex-parser` features intentionally become unsupported:

- `^` as exponentiation.
- Assignments and semicolon-separated statements.
- Comparisons, `if`, `and`, and `or`.
- Trigonometric, hyperbolic, rounding, error, Bessel, elliptic-integral,
  minimum, maximum, modulus, and Heaviside functions.
- Comments and multiline input.

There is no automatic migration for those forms because Amrvis2 has never
documented or tested them as an application compatibility contract. Rejected
expressions receive a positional error so users can rewrite them.

## Testing

### Parser unit tests

The expression component will have table-driven unit tests for:

- Every numeric-literal form.
- Every unary and binary operation.
- Every supported function.
- Whitespace between tokens.
- Standard arithmetic precedence.
- Parentheses overriding precedence.
- Right-associative exponentiation.
- `a**-b` and `-a**2`.
- Numerical equivalence of `a**b` and `pow(a,b)`.
- Repeated symbols and deterministic first-appearance ordering.
- Constants and zero-variable evaluation.
- Evaluation of the same compiled expression with multiple evaluators.
- Concurrent evaluation of one shared compiled expression.

Rejection tests will cover:

- Empty input and incomplete expressions.
- Newlines and carriage returns.
- `^`, assignments, semicolons, and comments.
- Unknown functions, including formerly supported AMReX functions.
- Wrong function arity.
- Missing parentheses or commas.
- Implicit multiplication.
- Trailing tokens.
- Non-finite or out-of-range numeric literals that the selected numeric
  conversion routine cannot represent.
- Variable-count and evaluator-input-count mismatches.

Each rejection test will verify both the error category and byte offset.

### Dataset integration tests

Existing derived-field integration coverage will remain and be extended with:

- `pow(first,2)` producing the same values as `first**2`.
- `exp`, `log`, `exp10`, and `log10`.
- A dashed field taking precedence in `a-b`, with `a - b` remaining
  subtraction.
- A chained derived field evaluated through an ordinary block and slice
  request.
- A malformed restored expression being skipped and reported without
  changing metadata.
- A derived field referencing 16 inputs succeeding and 17 inputs failing.

### Build validation

Validation must include:

- A normal Qt build and the full test suite.
- A headless build and the full headless test suite.
- A build with `AMRVIS_ENABLE_DERIVED_FIELDS=OFF`.
- Configuration without network access, demonstrating that derived fields
  no longer trigger dependency downloads.
- `git diff --check`.

## Implementation sequence

1. Add the independent expression target, parser, bytecode evaluator, and
   parser unit tests.
2. Replace `amrexpr::Parser` ownership and the templated evaluator ladder in
   `PlotfileDataset` with `CompiledExpression`.
3. Extend dataset integration tests before removing the external dependency.
4. Update the dialog widget, examples, and help text.
5. Remove `amrex-parser` CMake and installation documentation.
6. Run the normal, headless, derived-fields-disabled, and offline validation
   matrix.

Keeping dependency removal until the new parser passes the existing
integration tests makes the behavioral handoff explicit and reviewable.

## Review decisions

- Retain `AMRVIS_ENABLE_DERIVED_FIELDS` temporarily. A later cleanup should
  make the dependency-free feature unconditional.
- Preserve the 16-input limit temporarily. A later compatibility cleanup
  should remove it from the index-based evaluator and dataset binding path.
- Skip invalid derived fields restored from saved state and report each
  field name and parser error. Do not fail the entire dataset load.
