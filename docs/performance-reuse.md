# Performance priorities and projectile computation reuse

Performance is the primary goal of this fork. Reduced numerical precision,
animation fidelity and cosmetic effects are acceptable where they improve frame
times. Preserve damage as much as possible, and measure damage retention along
with presentation rate and frame-time tails. Normal world speed and usable aiming
remain requirements.

Computation reuse must retain separate projectiles. Each projectile keeps its own
position, rotation, scale, native collision and damage. Replacing several attacks
with one representative projectile and adding their damage together is outside
the requested design.

## Shared rendering calculations

`ReuseSpriteRotation=1` reuses native sine/cosine pairs for repeated sprite angles
at two guarded J374 ANM2 call sites. Each instance still runs the original pivot
and vertex arithmetic. This is a small rendering optimization, not a replacement
for the game's projectile simulation or a guarantee of 90 FPS under every load.
Set it to `0` and restart to compare against the original rotation calls.

The cache uses a fixed index and exact angle bits. It owns no entities, images or
game pointers. Unsupported floating-point modes and angles use the native path;
poor reuse triggers a bounded native interval before another trial. The supported
path preserves computed coordinates; transient floating-point exception-status
flags are not replayed. Heavy work stays in the native DLL, while the optional Lua
companion provides controls for the separate workload budgets.

## Copying sources

Coverage follows the common native rendering path rather than a familiar-name
whitelist. The native source audit includes the player, Incubus, Twisted Pair,
Cain's Other Eye, Gello, all Sumptorium clot types and Lil Clot, Sprinkler, Fate's
Reward and Multidimensional Baby. Mongo Baby is not classified as a player-shot
copying source; its presence in the diagnostic fixture is an ordinary-familiar
control. Fate's Reward inherits some tear properties, not special weapon forms.
Temporary familiar duplication,
multishot and mod-created native tears can use the same path as well.

Their attack recipes are not interchangeable. Blood-clot subtypes modify flags
and damage; Cain's Other Eye chooses its own directions; Gello and Sprinkler have
separate weapon paths. Sprinkler and Multidimensional Baby also preserve player or
original-shot ownership, so direct familiar-spawner counts alone miss them.

Sprites using these guarded ANM2 calls participate regardless of their source.
Other rendering paths, including some laser and knife geometry, remain native.
An ordinary-tear source-coverage test does not establish every weapon/mod variant.

## Validation requirements

Use actual native-code differential tests privately and distributable synthetic
ABI/cache tests separately. Benchmark the complete adapter, including guards and
cache misses, before enabling it. Isolated live comparisons must retain matching
save/mod/fixture manifests, silent audio, independent projectiles and normal
cleanup. Report single-function timing separately from whole-game improvement.

The more general position-independent quad-prefix cache and separate scalar
trigonometry caches were rejected after their lookup overhead exceeded the work
saved. They are not part of the enabled implementation.

The measured comparisons, executable/source hashes and remaining limits are in
[the validation record](validation/sprite-rotation-reuse.json). In the matched
default-item pair, both modes presented 90 FPS and cumulative damage was 1783
with reuse disabled versus 1787 enabled. Four-target triple-item combat remained
below stable 90 FPS; this small optimization does not remove that bottleneck.
