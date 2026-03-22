# Player System Phase 1

## Purpose

Define the first real gameplay-layer player system for the engine.

This phase moves the project from:

- "camera as the player"

to:

- "player as a first-class gameplay entity"

with:

- third-person camera follow
- grounded movement
- gravity
- jump
- robust AABB/block collision against world terrain
- runtime voxel-model rendering for the player body

This is not an ECS pass.

It is a structured gameplay architecture pass that introduces clear ownership boundaries without overbuilding the engine.

## Current State

Today the engine behaves like this:

- [CubeEngine](C:/Users/magui/source/repos/voxel_enginevk/src/game/cube_engine.cpp) creates a plain [GameObject](C:/Users/magui/source/repos/voxel_enginevk/src/components/game_object.h).
- That object owns [PlayerInputComponent](C:/Users/magui/source/repos/voxel_enginevk/src/components/player_input_component.cpp), which directly mutates `_position`, `_front`, `_yaw`, and `_pitch`.
- Collision is just a point probe against one block position.
- There is no gravity, jump state, velocity integration, or true body volume.
- [GameScene](C:/Users/magui/source/repos/voxel_enginevk/src/scenes/game_scene.cpp) copies the player transform directly into the camera every frame.
- The result is effectively a first-person free-move controller with basic block avoidance, not a gameplay player entity.

## Goals

- Make the player a first-class gameplay entity.
- Separate player simulation from camera presentation.
- Support third-person RPG-style camera follow.
- Replace fly movement with grounded motion plus gravity.
- Add jump support.
- Add stable AABB collision against solid world blocks.
- Render the player through the voxel runtime path using asset id `player`.
- Keep the architecture extensible for future NPC/entity systems.

## Non-Goals

- Full ECS.
- Animation state machines.
- Skeletal voxel assemblies in this pass.
- Combat.
- Interaction with decoration collisions like flowers.
- Network prediction/rollback.

## High-Level Architecture

## 1. Entity Layer

Introduce a gameplay entity base above raw `GameObject`.

Recommended direction:

```cpp
class Entity : public GameObject
{
public:
    using GameObject::GameObject;
    virtual void tick(float dt) = 0;
};
```

Then define:

```cpp
class PlayerEntity final : public Entity
{
};
```

Why:

- `GameObject` already exists and can still serve as the low-level component host.
- We do not need to rewrite everything to ECS.
- But we do need a gameplay-level type boundary so the player is not just "some object with an input component".

## 2. Player Entity Responsibilities

`PlayerEntity` should own:

- transform/state
- movement velocity
- grounded state
- jump state
- body bounds
- facing / desired movement direction
- voxel render identity

Suggested first-pass data:

```cpp
struct PlayerMovementState
{
    glm::vec3 velocity{0.0f};
    bool grounded{false};
    bool jumpQueued{false};
};

struct PlayerBodyDef
{
    glm::vec3 halfExtents{0.35f, 0.9f, 0.35f};
    glm::vec3 originOffset{0.0f, 0.9f, 0.0f};
};
```

Notes:

- The player position should have one explicit meaning.
- Recommended: player transform position represents the bottom-center of the feet on the ground plane.
- Then the AABB is derived from that plus body dimensions.

That gives intuitive movement, ground checks, and third-person camera targeting.

## 3. Rendering Ownership

The player should render through the runtime voxel system.

First pass:

- player uses a single `VoxelModelComponent`
- `assetId = "player"`

Later:

- player can upgrade to a `VoxelAssembly`
- body parts
- equipment
- animation

Important rule:

- gameplay player ownership should not be in `GameScene`
- render submission should still route through existing voxel runtime systems

So the player entity should own the conceptual render component, and `CubeEngine` should expose the resulting runtime state to the scene layer.

## 4. Camera Separation

The camera should stop being the player.

Instead:

- the player is simulated in gameplay/world space
- the camera follows the player from a third-person pivot

Recommended first-pass third-person camera model:

- target point: player upper-body / shoulder height
- yaw controlled by player look input
- pitch clamped
- follow distance configurable
- optional camera collision is deferred unless needed immediately

Suggested data:

```cpp
struct ThirdPersonCameraState
{
    float yaw{0.0f};
    float pitch{-18.0f};
    float distance{5.0f};
    float minDistance{2.0f};
    float maxDistance{7.0f};
    glm::vec3 targetOffset{0.0f, 1.4f, 0.0f};
};
```

First pass recommendation:

- camera orbits the player
- player movement is relative to camera yaw on the XZ plane
- player model rotates toward movement direction

This is the right base for a CubeWorld/Zelda-style feel.

## 5. Input Model

Current input is first-person:

- WASD moves along `_front`
- mouse changes the same object’s yaw/pitch directly

New input model should be:

- `WASD` produces a desired movement vector in camera-relative planar space
- mouse controls camera yaw/pitch
- player rotates based on move direction
- `Space` queues jump

Suggested input structure:

```cpp
struct PlayerInputState
{
    bool moveForward{false};
    bool moveBackward{false};
    bool moveLeft{false};
    bool moveRight{false};
    bool jumpPressed{false};
    float lookDeltaX{0.0f};
    float lookDeltaY{0.0f};
};
```

Important difference:

- input should no longer directly mutate world position
- it should feed a player movement simulation step

## 6. Physics Model

This pass needs a simple but solid character controller.

Not rigidbody simulation.

Instead:

- kinematic character movement
- explicit gravity
- explicit jump impulse
- swept/stepped axis resolution against solid blocks

Recommended first-pass rules:

- gravity always applies when not grounded
- jump sets upward velocity only when grounded
- horizontal movement is controlled velocity on XZ plane
- collision resolves axis-by-axis
- grounded state comes from vertical collision resolution

Suggested tunables:

```cpp
struct PlayerPhysicsTuning
{
    float moveSpeed{4.5f};
    float airControl{0.35f};
    float gravity{24.0f};
    float jumpVelocity{8.5f};
    float maxFallSpeed{30.0f};
    float groundSnapDistance{0.08f};
};
```

## 7. Collision Model

This is the most important correctness area.

The current point collision approach is not acceptable for the player pass.

We need:

- player AABB
- queries against solid chunk blocks
- robust movement resolution with no tunneling through normal walk speeds
- stable grounding
- no flying

Current reusable primitive:

- [AABB](C:/Users/magui/source/repos/voxel_enginevk/src/physics/aabb.h)

But it is currently minimal and not integrated into gameplay movement.

### Recommended Collision Strategy

Use discrete axis-separated movement with voxel/block overlap tests:

1. compute desired delta from velocity
2. resolve X movement against solid blocks
3. resolve Z movement against solid blocks
4. resolve Y movement against solid blocks
5. if downward Y motion is blocked, set grounded
6. if upward Y motion is blocked, clear positive Y velocity

Why this approach:

- easy to reason about
- deterministic
- appropriate for voxel worlds
- much safer than trying to be clever too early

### Collision Query Boundary

Add a dedicated world collision query layer.

Example direction:

```cpp
class WorldCollision
{
public:
    [[nodiscard]] bool intersects_solid(const AABB& bounds) const;
};
```

Better:

- helper that enumerates overlapped block coordinates for an AABB
- tests only blocks inside the AABB footprint

### Decoration Collision

Skip for now.

Rules:

- chunk decorations like flowers do not block player movement
- only solid terrain/structure blocks matter

## 8. Player Bounds

Need a clear decision on what the player body is.

Recommended first pass:

- width: `0.7`
- height: `1.8`
- depth: `0.7`
- position = feet center

Derived AABB:

```cpp
min = position + vec3(-0.35f, 0.0f, -0.35f)
max = position + vec3( 0.35f, 1.8f,  0.35f)
```

This is much easier to reason about than center-origin body placement for character motion.

## 9. Player/Camera Relationship

The player should still have facing state, but camera and player facing are no longer identical.

Recommended split:

- camera owns orbit yaw/pitch
- player movement uses camera yaw to convert input into world-space move direction
- player visual yaw rotates toward move heading
- player pitch should not drive full-body vertical tilt in this first pass

So:

- camera pitch is for viewing only
- player body stays upright

## 10. Snapshot / Scene Boundary

`GameScene` currently reads `GameSnapshot.player`.

This should evolve to expose:

```cpp
struct PlayerSnapshot
{
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec3 facing;
    bool grounded;
    float visualYaw;
};
```

And separately expose camera-follow data from gameplay or a camera controller state.

`GameScene` should not be responsible for simulating player gameplay rules.

It should:

- feed input into `CubeEngine`
- read snapshots
- update a third-person camera from gameplay-owned target state
- submit render/debug UI

## 11. Voxel Runtime Integration

The player should use the voxel runtime instance path instead of being special-cased.

Phase 1 expectation:

- one `VoxelModelComponent`
- `assetId = "player"`
- `lightingMode = SampledRuntime`
- follows entity world transform

Later:

- swap to `VoxelAssembly`
- sockets for equipment
- multi-part render instances

## 12. Files / Systems Expected To Change

Likely major touch points:

- [cube_engine.h](C:/Users/magui/source/repos/voxel_enginevk/src/game/cube_engine.h)
- [cube_engine.cpp](C:/Users/magui/source/repos/voxel_enginevk/src/game/cube_engine.cpp)
- [game_object.h](C:/Users/magui/source/repos/voxel_enginevk/src/components/game_object.h)
- [player_input_component.h](C:/Users/magui/source/repos/voxel_enginevk/src/components/player_input_component.h)
- [player_input_component.cpp](C:/Users/magui/source/repos/voxel_enginevk/src/components/player_input_component.cpp)
- [camera.h](C:/Users/magui/source/repos/voxel_enginevk/src/camera.h)
- [camera.cpp](C:/Users/magui/source/repos/voxel_enginevk/src/camera.cpp)
- [game_scene.h](C:/Users/magui/source/repos/voxel_enginevk/src/scenes/game_scene.h)
- [game_scene.cpp](C:/Users/magui/source/repos/voxel_enginevk/src/scenes/game_scene.cpp)
- [aabb.h](C:/Users/magui/source/repos/voxel_enginevk/src/physics/aabb.h)
- [aabb.cpp](C:/Users/magui/source/repos/voxel_enginevk/src/physics/aabb.cpp)

Likely new files:

- `src/game/entity.h`
- `src/game/player_entity.h`
- `src/game/player_entity.cpp`
- `src/game/world_collision.h`
- `src/game/world_collision.cpp`
- possibly `src/game/third_person_camera_controller.h/.cpp`

## 13. Order Of Implementation

Recommended implementation order:

### Phase A: Player Entity Boundary

- introduce `Entity`
- introduce `PlayerEntity`
- move player-specific simulation ownership out of raw `GameObject` usage in `CubeEngine`

Exit criteria:

- `CubeEngine` owns `PlayerEntity`, not a generic `GameObject`

### Phase B: Physics + Bounds

- define player body dimensions
- define player movement state
- define gravity/jump tuning
- compute player AABB

Exit criteria:

- player has explicit bounds and velocity state

### Phase C: World Collision Layer

- add block/AABB collision queries
- implement axis-separated movement resolution
- implement grounded detection

Exit criteria:

- no more fly movement
- player stands on terrain and collides reliably with solid blocks

### Phase D: Input Refactor

- input becomes intent, not direct teleport-like position mutation
- add jump input
- movement relative to camera yaw

Exit criteria:

- WASD + jump moves simulated player correctly

### Phase E: Third-Person Camera

- decouple camera from player transform
- add orbit/follow camera state
- make `GameScene` use third-person follow

Exit criteria:

- visible third-person camera behavior in game

### Phase F: Player Voxel Render

- attach `VoxelModelComponent` with asset id `player`
- route player render through existing voxel runtime registry

Exit criteria:

- player model is visible and follows gameplay transform

### Phase G: Tests

- AABB overlap tests
- block collision tests
- grounded/jump/gravity tests
- movement resolution regression tests

Exit criteria:

- collision/movement rules are covered by unit tests

## Risks

### 1. Camera/Player Coupling Regression

Risk:

- old first-person assumptions remain scattered in `GameScene`, `Camera`, and `CubeEngine`

Mitigation:

- define one authoritative player simulation owner
- define one authoritative third-person camera owner

### 2. Collision Jank

Risk:

- edge snagging
- floating above ground
- tunneling
- jump inconsistency

Mitigation:

- axis-separated AABB resolution
- explicit grounding rules
- tests for edge/block/ceiling cases

### 3. Render Ownership Confusion

Risk:

- `GameScene` starts owning player voxel rendering ad hoc

Mitigation:

- player should surface runtime render state through gameplay/runtime boundaries

### 4. Overbuilding

Risk:

- trying to invent a full ECS, animation system, and combat framework in one pass

Mitigation:

- keep this pass focused on:
  - entity boundary
  - movement
  - collision
  - third-person camera
  - one player voxel model

## Validation Plan

Manual validation:

- player falls onto terrain on spawn
- player can walk across uneven terrain
- player cannot pass through solid terrain or tree trunks
- player can jump
- player collides with ceilings
- player can walk through flowers/decorations
- third-person camera follows consistently
- player voxel model remains aligned with gameplay body

Unit tests to add:

- AABB/block overlap helper tests
- movement resolution against floor/wall/ceiling
- grounded-state transitions
- jump impulse only when grounded
- diagonal movement collision stability

## Live Todo

- [ ] Introduce `Entity` base abstraction on top of `GameObject`
- [ ] Add `PlayerEntity`
- [ ] Move player ownership in `CubeEngine` from generic `GameObject` to `PlayerEntity`
- [ ] Define player body bounds and movement state
- [ ] Add gravity/jump tuning
- [ ] Add world AABB-vs-solid-block collision query system
- [ ] Replace point-probe collision with axis-separated AABB resolution
- [ ] Refactor input to intent-driven RPG movement
- [ ] Add `Space` jump support
- [ ] Separate third-person camera state from player state
- [ ] Make `GameScene` camera follow the player in third-person
- [ ] Add runtime voxel render path for player asset id `player`
- [ ] Expose player snapshot data needed for camera/render/debug
- [ ] Add unit tests for movement, gravity, and collision

## Recommended First Slice

Start with:

1. `PlayerEntity`
2. player bounds + velocity state
3. world collision helper
4. gravity + grounded movement

Do not start with the camera.

Reason:

- if player simulation is still first-person/free-fly underneath, the third-person camera is just cosmetic
- the simulation model must become correct first

