# Future Core Editor Object Groups: Owner design requirement

Recorded 2026-11-01 from the Owner's Candidate 04 Revision 05 directive. Planning only; no phase activation or implementation authorization. This addendum preserves the existing [Core Editor handoff plan](../phases/PHASE_55.md) and all historical evidence.

Selection is transient and uniformly uses the existing Phase24 Gold mesh-silhouette highlight. Active Entity determines Inspector editing context, without changing selection color. Persistent Object Groups have independent user-defined names, membership and colors; selected members remain Gold regardless of their groups.

Group colors primarily identify membership in the Scene Hierarchy / Outliner. Any viewport group visualization must be explicitly enabled. Deselecting restores applicable group visualization without changing membership. Physics Debug colors, collider previews and collision-risk indicators remain independent. Group membership must never be inferred from render colors or selection state, and visualization must not alter authored material colors.

The separately scoped future design must address:

- Stable GroupID, persistent membership, user-defined names and colors.
- Group selection, visibility and locking.
- Entity deletion, scene serialization, duplication and undo/redo.
- Compatibility with multi-object Transform, Material, Mesh and Collider editing, with separately defined batch-operation and atomic transaction guarantees where required.

These requirements belong in future Core Editor authoring and handoff planning, including the existing hierarchy, identity, serialization, transaction, undo/redo and overlay contracts. Group overlap and precedence rules remain future design work.

Candidate 04 implements none of this: no GroupID, persistent metadata, group serialization, group UI or new renderer passes. Batch Collider Replacement remains a separate future feature. Phase21 remains FROZEN_UNAPPROVED, Phase24's accepted performance INCONCLUSIVE disposition is unchanged, and Phase25/OD07 Revision02 remain stopped/inactive. This document grants no integration, publication or subsequent-phase authorization.
