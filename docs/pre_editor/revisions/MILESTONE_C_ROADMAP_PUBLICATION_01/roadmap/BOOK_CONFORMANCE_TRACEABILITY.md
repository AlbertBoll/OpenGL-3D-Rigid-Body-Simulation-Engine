# Mandatory book conformance traceability register

**CONTRACT REGISTER ONLY; IMPLEMENTATION/VALIDATION EVIDENCE NOT YET ACCEPTED.** Reference: Gregory Hodges, *Game Physics: The Rest of Your Life*, Version1.08; [primary source and reference identity](BOOK_REFERENCE.md). [Owner clarification](OWNER_CLARIFICATIONS.md) controls this register. The rows are obligations, not claims that a new feature exists or that historical evidence automatically applies.

| Chapter(s) | Capability or reference role | Provider / reconciliation owner | Implementation evidence required | Validation evidence required | Present gate status |
| --- | --- | --- | --- | --- | --- |
| 1 | Overview and scope |32|Map reference scope to engine capability ledger|Review completeness; no standalone executable feature inferred|UNRECONCILED |
| 2–4 | Vector/matrix foundations |26, reviewed at32|Exact existing/new math symbols and equivalent operations; no required copy of the book classes|Focused applicable algebra/finite-state evidence or justified matching reuse|UNVERIFIED |
| 5 | Constraint solve foundations |26, reviewed at32|Exact solve method and supported-domain justification|Declared numerical cases/convergence evidence; retained contact arithmetic unchanged|UNVERIFIED |
| 6–7 | Constraint formulation/base |26|World-owned API, identities, rows, body-local conventions and lifecycle symbols|Deterministic formulation/error and ownership tests plus RBS integration|NOT_IMPLEMENTED_OR_VERIFIED_BY_THIS_REGISTER |
| 8–9 | Distance and chains |26|Provider symbols and deterministic4/8/16-link configurations|Separate case identities, admitted length/anchor/energy/contact/cost bounds and sufficient results|NOT_IMPLEMENTED_OR_VERIFIED_BY_THIS_REGISTER |
| 10–11 | Warm start and stabilization |26; affected later joints|Per-joint cache/reset and stabilization mapping; preserve approved contact path|Affected reset/step/energy/lifetime cases and input-specific accepted limits|UNVERIFIED |
| 12–13 | Contacts and caching |Existing approved providers;32 audit|Current approved source identities and technical correspondence; no rejected Candidate11 restoration|Applicable matched contact/stability/performance evidence and explicit unresolved status; no automatic historic replay|UNRECONCILED; FROZEN21 FAILURES PRESERVED |
| 14–18 | Hinge/quaternion/limit progression |28|Hinge/limit provider symbols, coordinates and supported domain|Free/limited/loaded/error cases under admitted bounds; ownership/contact/cost evidence|NOT_IMPLEMENTED_OR_VERIFIED_BY_THIS_REGISTER |
| 19 | **Constant Velocity Constraint** |**28;32 conformance**|**Actual provider or documented technically justified equivalent; explicit reference semantics and supported domain**|**Distinct deterministic RBS case and exact correctness/stability/performance evidence; generic motor/ball label is insufficient**|**MANDATORY, UNRESOLVED** |
| 20 | **Limited Constant Velocity Constraint** |**28;32 conformance**|**Actual limited provider or justified verified equivalent; map reference limitation semantics and transitions**|**Distinct limited-case evidence, boundary/transition behavior and retained correctness/stability/performance gates**|**MANDATORY, UNRESOLVED** |
| 21 | Passive articulation |29|Ragdoll bodies/joints/filters/bone mapping and transactional lifecycle|Admitted drop/settle/wake/teardown and contact/cost evidence|NOT_IMPLEMENTED_OR_VERIFIED_BY_THIS_REGISTER |
| 22 | Driven constraints |30|Bounded motor target/cap/limit-priority provider|Loaded/disabled/saturated/reversed cases with admitted work/error/cost bounds|NOT_IMPLEMENTED_OR_VERIFIED_BY_THIS_REGISTER |
| 23 | Prescribed motion |31|Typed kinematic mover and fixed-tick/presentation ownership mapping|Deterministic mover/rider/start-stop/contact/lifetime/cost evidence|NOT_IMPLEMENTED_OR_VERIFIED_BY_THIS_REGISTER |
| 24 | Integrated capabilities |32|Map all mandatory delivered providers and startup cases|Complete applicable gate ledger and final Owner RBS inspection|BLOCKED UNTIL MANDATORY GATES RESOLVED |
| 25 | Bibliography |32|Reference identification and provenance|Review references; not a separately mandated algorithm catalogue|REFERENCE_REVIEW_PENDING |

Before any conformance claim, expand each grouped capability row into the specific chapter requirements that it covers. Every mandatory row must identify:

- Exact chapter/section/page and Version1.08 reference identity; the required capability and supported domain.
- Engine source path/symbol, source/dependency hash, API/ownership mapping, and whether the implementation is direct or equivalent. For an equivalent, explain the mathematical/behavioral correspondence, intentional differences, domain limits and why those differences preserve the required capability.
- Exact deterministic RBS preset/case/configuration identity and relevant build/product identities. A BookConformance case resolves its provider before world creation; requested and resolved identities are both recorded.
- Validation check/protocol, admitted numerical bounds/units/workload/time limits, raw/summary evidence identity, actual PASS/FAIL/INCONCLUSIVE/reused outcome and reuse justification. Bind the implementation and validation to the same relevant inputs.
- Any residual limitation, its effect on required semantics, and the gate disposition. Unresolved mandatory capability or insufficient equivalence evidence remains blocking; it cannot be marked complete through an exclusion label or deferral.

The register adds no new numerical thresholds and authorizes no runtime. Existing MSV rules select the smallest sufficient affected checks; unchanged accepted evidence may be reused with identity and impact proof. Mandatory missing evidence still blocks. Phase32 cannot declare conformance while any mandatory capability, correctness, stability or performance gate is unresolved, and cannot relabel a historical FAIL/INCONCLUSIVE result as PASS. Nonalgorithmic chapters and implementation-detail adaptations must be explained explicitly; this does not permit omission of Chapters19/20.
