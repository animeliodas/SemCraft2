# SemCraft 2 — contract between Serious Sam and Minecraft

Two unmodified games run side by side. A Classics Patch plugin in Serious Sam (`SemCraft2.dll`) and a Fabric
mod in Minecraft (`semcraft2`) translate state. Neither game's logic is reimplemented in the other.

## Ownership

| What | Owner | The other side |
|---|---|---|
| Player position, look, movement, physics | **Sam** | MC player is a puppet: placed at Sam's eye every frame, `noPhysics`, flying |
| Camera (pose, FOV, aspect) | **Sam** (`OnRenderView` projection) | MC camera forced to it (CameraMixin) |
| Level geometry | **Sam** (brush polygons) | MC: invisible `semcraft2:shell` blocks (voxelised), so mobs/items/blocks rest on Sam floors |
| Player health | **Sam** | MC hearts mirror Sam health (100 Sam = 20 MC); MC damage to the player is forwarded to Sam |
| Blocks, MC mobs, items, redstone | **MC** | drawn into Sam's frame with depth |
| Sam monsters, projectiles, triggers, saves | **Sam** | MC sees nearby monsters as invisible proxies (later phase) |
| Input | **Sam window has focus** | Sam forwards MC-relevant input over the link; MC window is hidden |

Control never moves: Sam always drives the player. Build mode only changes which game the mouse buttons and
hotbar keys act on.

## Units and axes
- 1 Sam unit = 1 metre = 1 block. Sam and MC are both right-handed, y up, and look down −z at heading 0 / yaw 180.
- Position: `MC = Sam + offset`, offset `(ox, 0, oz)` chosen by MC per level (each level gets its own region of
  the MC world). **Only the MC mod knows the offset**: Sam always sends and receives Sam coordinates.
- Angles (degrees): `yaw = 180 − heading`, `pitch_mc = −pitch_sam`, `roll_mc = bank_sam` (sign verified in test).
- FOV: Sam sends the vertical FOV and aspect of the projection it rendered with; MC renders with exactly those.

## Channels
1. **Link** — WebSocket text frames (JSON), `127.0.0.1:25610` (`-Dsemcraft2.port`). MC is the server, the Sam
   plugin the client (reconnects every second). Unknown `t` values are ignored by both sides.
2. **Frames** — named shared memory `Local\SemCraft2Frame`, created by MC, opened read-only by Sam.
3. **Level geometry** — a binary file written by Sam (`%TEMP%\SemCraft2\level_<hash>.tri`), announced on the link.

### Link messages
Sam → MC
- `{"t":"hello","v":1,"game":"TFE"|"TSE","pid":N}` once per connection.
- `{"t":"cam","f":frame,"p":[x,y,z],"r":[heading,pitch,bank],"vfov":deg,"aspect":w/h,"w":px,"h":px,"near":m}`
  once per rendered game frame (Sam coords of the camera actually rendered).
- `{"t":"level","name":"Levels\\X.wld","file":"C:\\...\\level_<hash>.tri","hash":"<hex>","tris":N}` after a world
  loads (and again on reconnect).
- `{"t":"state","hp":f,"armor":f,"alive":b,"inGame":b,"build":b}` 5 times a second.
- `{"t":"key","k":"attack"|"use"|"pick"|"drop","down":b}`, `{"t":"slot","n":0..8}`, `{"t":"scroll","d":±1}`.
- `{"t":"build","on":b}` build mode (mouse and number keys go to MC).
- `{"t":"shot","o":[x,y,z],"d":[x,y,z],"dmg":f,"n":rays,"spread":deg[,"range":m]}` Sam hitscan fire (knife and
  chainsaw: range 3). MC hurts the first mob along each ray, or wears down the first non-shell block (cracks; breaks
  at 15 + 50 × hardness Sam damage, glass at once, TNT is lit).
- `{"t":"proj","l":[[id,x,y,z,radius,dmg,break,flame],...]}` Sam projectiles in flight, every tick.
- `{"t":"boom","p":[x,y,z],"r":radius,"dmg":f,"break":b,"src":"rocket"|"grenade"|"flame"|...}` a tracked projectile
  is gone (exploded): MC hurts mobs in the radius, explodes blocks if `break`, a flame sets fire.
- `{"t":"actors","l":[[id,x,feetY,z,height,width,health],...]}` Sam monsters within 96 m, every 2 ticks.
- `{"t":"cmd","c":"..."}`, `{"t":"status"}`, `{"t":"spawn",...}`, `{"t":"pillar",...}` dev/test hooks.

MC → Sam
- `{"t":"hello","v":1,"shm":"Local\\SemCraft2Frame","pid":N}` on connect.
- `{"t":"hurt","d":samHp,"src":"zombie","from":[x,y,z]}` MC damage to the player (Sam coords, Sam hp units); only
  damage with an entity source, explosions and fire.
- `{"t":"push","v":[x,y,z]}` a MC explosion's knockback on the player, as Sam m/s (×30 of MC blocks/tick, ≤40);
  Sam gives it to its player as an impulse.
- `{"t":"projhit","id":N}` a Sam projectile hit a MC mob and exploded there: Sam removes its copy quietly.
- `{"t":"actorhit","id":N,"d":samHp,"boom":b,"from":"zombie"}` a MC hit on a Sam monster's proxy.
- `{"t":"blocks","set":[x,y,z,...],"clear":[...]}` solid MC blocks appeared / went (Sam coords of minimum corners);
  Sam keeps invisible colliding block models for them.
- `{"t":"level_ready","hash":"<hex>","blocks":N}` when the shell for a level is built.
- `{"t":"log","m":"..."}` diagnostics printed to Sam's console.

### Frame shared memory `Local\SemCraft2Frame` (little-endian)
```
header (4096 bytes)
  0 int magic 0x32464353 ("SCF2")   4 int version 1   8 int header bytes   12 int slot count (3)
  16 long slot stride   24 int max width   28 int max height
  32 long publish counter   40 int latest slot (-1 none)   44 int MC pid
slot descriptor i at 256 + 128*i
  +0  long seq (odd while written)    +8 long MC frame   +16 long host frame (Sam "f" of the cam used)
  +24 int width  +28 int height  +32 float near  +36 float far  +40 float vfov (deg)
  +44 int flags: 1 depth in [0,1], 2 rows bottom-up, 4 reversed Z (1 near, 0 empty)
  +48 float cam x,y,z (Sam coords, as received)  +60 float heading, pitch, bank (as received)
  +72 long capture ns  +80 long publish ns  +88 float aspect
slot data at 4096 + i*stride: world RGBA8 premultiplied (w*h*4), world depth float32 (w*h*4),
  overlay RGBA8 premultiplied (hand + HUD, w*h*4)
```
Readers copy a slot only if `seq` is even and unchanged after the copy. Max size 1920×1200 (≈83 MB mapping;
Sam is a 32-bit process).

### Level geometry file `.tri`
```
0 char[4] "SCT1"   4 int version 1   8 int triangle count N   12 int flags (reserved)
16 float bbox min x,y,z, max x,y,z
40 N × { float v0[3], v1[3], v2[3]; int flags }   (40 bytes each, Sam coords)
   flags: 1 = from a moving brush (door/lift), 2 = passable (not solid)
```

## Lifecycle
- Either game can start first. MC opens/creates the void world `semcraft2` by itself and waits.
- On link connect both send `hello`; Sam resends `level` for the current world.
- MC hides its window while a host is attached (cam received within 2 s) and shows it again when Sam goes away.
- Sam's compositor draws a slot only if it is younger than 500 ms; otherwise draws nothing (no stale picture over
  menus). Nothing is drawn while Sam's menu/console covers the game or no game is running.
- Sam quits → link closes → MC keeps running (the human closes it) and stops treating itself as attached.

## Failure behaviour
- Link down: Sam renders vanilla; MC renders its own view (window shown).
- Shared memory missing / bad magic / size mismatch: compositor disabled, logged once.
- Slot torn (seq changed): skip this frame, keep the previous texture.
