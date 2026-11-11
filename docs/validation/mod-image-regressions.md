# GoodTrip and Regret image regressions

These reports bind results to the exact tested DLL, fixture and target-mod Lua hashes. R1/R2 are working-tree candidate labels, not source commits or claims about a later release binary. Only authored fixtures are included; third-party mod code/resources, game binaries, captures and saves are excluded.

## GoodTrip: missing map restored in R1

[Machine-readable results](goodtrip-image-regression.json) compare native60, the earlier0.13.0-j374.4 at90FPS, and R1 at90FPS.

| Measured image subset | Native60 | Earlier90 | R1 at90 |
|---|---:|---:|---:|
| Map visible / frames independently showing the native Tab timer | 242/242 | 0/184 | 180/180 |
| Independent Sprite control visible / captured frames | 360/360 | 0/360 | 360/360 |
| Map-region median/p95 RGB error versus native reference | 0/0 | 58.725/58.725 | 0/0 |

All complete fixtures reached540 world ticks and1080 main Lua render callbacks, with720 calls each to the real GoodTrip map, UI and Tab paths. R1 restored the drawing without increasing those side effects. Input was real Tab in the verified isolated display: synthetic MC_INPUT_ACTION did not drive this J374 direct Input query.

The R1 DLL is `0221770be17b98e339bc7fb8de0ab322406d93f2fe9d721c4a3a640bc74785d1`. Its hold-interior native present timestamps have median11.111ms, p99 11.230ms and maximum12.049ms across990 intervals. Complete logged buckets show zero main-cache misses and shader rejects, compared with1620 misses and1080 shader rejects in the earlier build; these bucket totals include startup/trailing activity.

Each capture contains only360 consecutive samples: about6 seconds native or4 seconds at90FPS. The independently confirmed Tab subsets cover about4.016/2.033/1.990 seconds and lie within the first hold. This is sustained restoration in those captured windows, not proof of every frame of both holds. Teleport, cursor navigation, moving gameplay and physical VRR remain outside this evidence. R4 graphics were subsequently tested at90 and80Hz as described below; R5 affected-path verification at80Hz is completed below.

## GoodTrip R4 graphics at90 and80Hz

R4's exact DLL is `62d364bf708b6470bae3b813dbbb152da47f7fdf5b48500dbc546c072457cccc`. At90Hz, the map is visible in185/185 independently confirmed Tab frames and the Sprite control in360/360 captures. At80Hz, those counts are159/159 and320/320. Map-region median and p95 RGB error against the native reference are zero in both cases. All logged buckets have zero main-cache misses, shader rejects and unsafe-operation entries.

Native present p99/maximum are11.215/11.302ms at90Hz and12.608/12.825ms at80Hz. Both complete runs still have1080 main Lua callbacks over540 world ticks. Their Map/UI/Tab counters are721, versus720 in the earlier native/R1 runs. That one-call difference is retained in the report: real OS key delivery is not a frame-perfect input schedule, and its precise cause is not established here. Captures cover about4 seconds, with only about2 seconds in the confirmed first hold. This validates the measured R4 graphics path; it is not an overall release or R5 input-cache verification claim.

## Regret: first ghost state recovered in R2

[Machine-readable results](regret-image-regression.json) use actual decoded RGB image crops. Consecutive duplicate presentations are collapsed; every remaining image-state hash is compared in order.

| Direct-removal ghost | Native / R2 image states | Result |
|---|---:|---|
| First left icon | 60/60 | Exact sequence, including the first state missing in R1 |
| Second right icon | 60/60 | Exact sequence |
| Simultaneous left icon | 60/60 | Exact sequence |
| Simultaneous right icon | 61/61 | Exact sequence, including native's brief tail blink |

R1 omitted the first ghost state, extending the initial empty interval from33.668ms to54.475ms. R2 restores that state: removal→first display is33.668/33.072ms native/R2, and first display→clear is1000.353/1000.015ms. Across all matched states, maximum phase difference is7.253ms. Both distinct item icons are preserved when drawn together.

The R2 DLL is `c3344f15c61a2c377ef3da1496ff7fcabc94abe14deaa722e0d7883c022d723c`. Its810 captured-frame intervals have median11.048ms and maximum13.405ms. These are capture timestamps, not physical-display timing.

The two-ghost sequence has one brief right-icon blank followed by its final faint state in both native and R2. The original mod removes an array element while iterating, skipping the shifted next element for one canonical render. This is inherited behavior, not a new high-FPS flicker.

The base fixture directly removes known-id pedestals. A separate R4 native Blind option-choice supplement is described below; shops and arbitrary texture churn remain separate tests. Spawn InitSeeds differed despite equal start seed, fixture and executable; this is image equivalence for the measured scope, not whole-RNG equivalence.

Authored reproduction fixtures and their exact hashes are under [tests/fixtures/mod-compat](../../tests/fixtures/mod-compat/README.md). They are never deployed or run automatically.

## Native Blind option-choice supplement (R4)

A separate fixture constructs two native option pedestals under the real Blind curse, then walks into the left one. Actual images show two question marks before selection, the acquired Sad Onion, and the unpicked Inner Eye ghost after the engine removes the right pedestal. The fixture does not call Remove. All45 recorded state rows match native exactly; inventory increments at tick121 after the animation, while the unpicked pedestal already vanishes before tick90.

Alignment uses actual image presence. R4 contains90 ghost samples with a continuous fade and first-visible→first-clear duration998.59ms. Native capture undersampled the fade to43 images:23 have exact R4 region matches, all43 have mean RGB error at most0.172/255 against their best corresponding R4 sample, with capture-relative time differences at most5.78ms. The region includes the native removal poof, so this is not a claim of all60 native fade states or whole-image equality. Native onset and disappearance brackets support the same approximately one-second duration. This is constructed native option-choice behavior, not naturally rolled treasure-room generation. Exact cases, hashes, sample ranges and limits are in the [Regret JSON](regret-image-regression.json).

## R4 direct-removal repeat

The unchanged direct-removal fixture was repeated on the R4 DLL `62d364bf708b6470bae3b813dbbb152da47f7fdf5b48500dbc546c072457cccc`. All four icon-region sequences again match native exactly:60,60,60 and61 states, including the first ghost state and the inherited two-ghost tail blink. Their common native reference also establishes equality to the measured R2 sequences. Single-ghost fade durations are999.947/1000.067ms; simultaneous left is1000.000ms. Maximum matched-state onset difference from native is8.924ms. The810-frame capture has median11.099ms and maximum22.194ms intervals; no interval exceeds25ms. Logged cache-miss, pure-state, pure-Lua and fatal diagnostic lines are all zero. These R4 graphics results remain bound to their actual DLL hash, not an untested overall release.

## R5 GoodTrip affected-path verification at80Hz

The final tested R5 DLL is `1a2b77b5cc0ba4fc63890f9d08e09a43674d7c4cf32506f9ef446cb46af8c139`. GoodTrip's map is visible in162/162 independently confirmed Tab frames, and the Sprite control in320/320 captures. Map-region median/p95 RGB error versus native is zero; after the first four UI-reveal samples, all158 remaining selected map regions match the settled native reference exactly. No main/input-cache misses, shader rejects, unsafe-operation entries, pure-Lua/pure-state warnings or fatal lines are logged.

The complete run retains540 elapsed world ticks and1080 main Lua callbacks. Its Map/UI/Tab counters are721, matching R4; the earlier native/R1 value720 is retained as a real-input boundary difference, not silently normalized away. Across880 hold-interior native present intervals, median/p99/maximum are12.499/12.626/12.757ms. The320 captures span3.986 seconds, with2.012 seconds in the confirmed first hold. This completes the planned R5 GoodTrip affected-path check; teleport, cursor navigation, physical VRR and unseen frames remain outside its scope.
