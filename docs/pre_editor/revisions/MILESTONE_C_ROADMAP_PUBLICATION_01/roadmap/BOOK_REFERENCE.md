# Primary reference and conformance boundary

Verified primary source: Gregory Hodges, *Game Physics: The Rest of Your Life*, Version1.08, copyright2020,101 PDF pages, linked by the [author's series site](https://gamephysicsweekend.github.io/) and read from its [official PDF](https://gamephysicsweekend.github.io/pdfs/GamePhysicsTheRestOfYourLife.pdf) on2026-11-04. Printed page numbers below. No book code or large quotation is copied.

| Reference | Accepted roadmap use |
| --- | --- |
| Chapters2-5, pp5-13 | Math/solve foundations traceability |
| Chapters6-11, pp14-39 | Phase26 constraint/distance/chain foundations |
| Chapters12-13, pp40-53 | Preserve and audit existing contacts/cache |
| Chapters14-20, pp54-81 | Phase28 hinge/quaternion/limit mapping |
| Chapter21,p82 | Phase29 ragdoll |
| Chapter22,p86 | Phase30 velocity-driven constraints |
| Chapter23,p91 | Phase31 prescribed platform motion |
| Chapter24,p93 | Phase32 coverage review |

Ball-and-socket is an engine adaptation, not a separately named book chapter. Chapter19 covers Constant Velocity Constraints and Chapter20 covers Limited Constant Velocity Constraints: Both capabilities are mandatory under the Owner clarification. Phase28 must supply their implementation or technically justified, sufficiently verified engine equivalents; Phase32 must trace each to implementation and validation evidence. Exclusion is not conformance. The book mover prescribes velocity on an infinite-mass body; typed kinematic ownership is the proposed engine adaptation. Book conformance means reviewed capability correspondence, not copying internal classes, accepting unsafe lifetimes, or changing approved solver behavior.

Applied authority: [Owner clarification](OWNER_CLARIFICATIONS.md). See the [mandatory chapter traceability register](BOOK_CONFORMANCE_TRACEABILITY.md); no current implementation/validation PASS is implied by its rows.
