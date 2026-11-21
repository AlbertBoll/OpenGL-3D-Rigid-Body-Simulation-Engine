# Chapter 21 passive ragdoll

The reference scene follows the six boxes and five passive joints in Chapter 21 of Gregory Hodges' *Game Physics: The Rest of Your Life*, v1.08, pp. 82–84. The hinge and limited constant-velocity constraints use the existing Chapter 18 and 20 providers. The room dimensions and small box follow *Game Physics: The Next Week*, v1.07, pp. 19–21. The frozen transcription gives the PDF identities, book and engine coordinates, material settings and joint frames for each member. [Reference packet](../../logs/pre_editor/phase-29/admission-02/reference-packet.json)

| Body | Engine center at reference rest (m) | Mass (kg) | Joint to torso |
| --- | --- | ---: | --- |
| Head | (0, 5.5, 0) | 0.5 | Neck hinge, head is endpoint A |
| Torso | (0, 4, 0) | 2 | Logical root |
| Left arm | (0, 4.75, −2) | 1 | Left limited constant-velocity shoulder |
| Right arm | (0, 4.75, 2) | 1 | Right limited constant-velocity shoulder |
| Left leg | (0, 2.5, −1) | 1 | Left hip hinge |
| Right leg | (0, 2.5, 1) | 1 | Right hip hinge |

The engine uses metres, kilograms, seconds and radians. The proper rotation from book to engine coordinates maps (x, y, z) to (x, z, −y). A book body orientation becomes `r * q * inverse(r)`. A joint frame becomes `r * F` because its primary, U and V labels stay attached to the joint. The left and right shoulder primary axes consequently point along −Z and +Z. The limb pose retains the book's binary32 literal `3.1415f/2.f`. [Reference and tilted packets](../../RigidBodySimulation/src/RbsLaunchConfig.cpp)

All six bodies are independent world-space entities. The torso hierarchy in the Editor identifies the logical graph. The composition binds the Scene's existing body handles, verifies common world pivots and active constraints at rest, creates five world-owned angular joints, and checks their descriptors. The reference disables collision for five adjacent pairs and enables all ten other body pairs and all thirty body/environment pairs. [Composition provider](../../GEngine/src/Physics/PassiveRagdoll.cpp)

The ordinary Editor adds 0.5 m to the assembly height to match its shared floor. The qualification room uses one floor and four fixed boxes at the frozen coordinates. Both start with zero member velocities and total dynamic mass of 6.5 kg. The reference uses gravity of −10 m/s² and one 1/60 s step. The second qualification packet rotates the whole reference pose about its center of mass and raises it 1 m. Its scheduled opposing angular impulses exercise both ends of the joint limits. Both packets prescribe a +1 N·s world-X torso impulse at the center of mass before tick 961. [Scene construction](../../RigidBodySimulation/src/RbsScene.cpp) · [Workloads and action vectors](../../logs/pre_editor/phase-29/admission-02/workloads.json)

The qualification criteria apply to these two identified packets on Jolt and Native. Edited poses, primitive substitutions and effective backend values can be inspected at Tick 0. Runtime results for the reference packets and actual Owner observations are recorded separately. [Frozen numerical and performance criteria](../../logs/pre_editor/phase-29/admission-02/criteria.json) · [Authoring and validation design](../../logs/pre_editor/phase-29/admission-02/AUTHORING_AND_VALIDATION.md)
