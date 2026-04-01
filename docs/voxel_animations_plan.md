# Voxel Animations Plan

## Summary
- Extend the voxel assembly system with authored clips, controllers, runtime pose evaluation, optional root-motion extraction, and `AnimationEditorScene`.
- Keep the assembly graph as the skeleton. Animations target stable `partId`s while slot-based swapping remains runtime-driven through `VoxelAssemblyComponent`.
- Support both character and non-character animation, including locomotion, dodge rolls, weapon poses, flowers, bats, and chests.
- Default movement stays gameplay-driven, but controller states may opt into extracted root motion for authored displacement.

## Core Types
- `VoxelAssemblyPose` and `VoxelAssemblyPosePart` are runtime local pose overrides keyed by `partId`.
- `VoxelAnimationClipAsset` lives under `models/voxel_animations/*.vxanim.json`.
- `VoxelAnimationControllerAsset` lives under `models/voxel_animation_controllers/*.vxanimc.json`.
- `RootMotionMode` supports `Ignore`, `ExtractPlanar`, and `ExtractFull`.
- `VoxelAnimationRootMotionSample` carries extracted local translation and rotation deltas.
- `VoxelAnimationComponent` owns controller state, evaluated pose, pending events, and extracted root motion.

## Runtime Notes
- Animation layers on top of authored assembly binding states instead of replacing the assembly model.
- Attachment chains remain automatic because child placement still resolves from the posed parent attachment transform.
- The base controller layer is the only layer allowed to contribute root motion in v1.
- Event tracks are authored on clips and fire once when playback crosses the marker range, including loop wrap.
- Most locomotion remains gameplay-driven; special authored moves such as dodge rolls can consume extracted planar root motion.

## Editor Notes
- `AnimationEditorScene` reuses the existing editor orbit camera, zoom, ImGui workflow, and orientation gizmo patterns.
- The editor supports clip and controller modes, mouse-picked part selection, timeline scrubbing, and basic save/load authoring flows.
- Clip preview can evaluate unsaved clip edits directly; controller preview runs through the same runtime animation path used in gameplay.

## Validation Targets
- Repository round-trips for clips and controllers.
- Pose evaluation for attachment following and hot-swapped slot inheritance.
- Layering, blend-space, event, and root-motion behavior.
- Editor smoke coverage for locomotion, dodge roll, flower sway, bat flap, and chest open/close authoring flows.
