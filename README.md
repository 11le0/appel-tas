# Appel TAS Maker

A tool-assisted speedrun (TAS) engine for Appel. It has three parts that share
one physics engine:

- **Web editor** – write frame-by-frame inputs, watch the player run through your
  level, inspect the physics state on every frame, and export a game replay code.
- **Randy** – a fast native stochastic explorer that finds routes to a goal over
  hundreds of frames, with a live heat map, goal picker and drawable kill zones.
- **Brute Force Solver** – an exhaustive native search over short windows of
  frames, for squeezing out the best possible result from a given state.

Everything runs locally on your computer. Nothing is uploaded anywhere.

---

## Quick start

### 1. Install

| You need | Why |
|---|---|
| A C compiler (`gcc` or `clang`) and `make` | builds the native engine |
| Node.js 18 or newer (<https://nodejs.org>) | runs the local web server |

No `npm install` is needed.

**Linux (Debian/Ubuntu):** `sudo apt install build-essential nodejs`

**macOS:** run `xcode-select --install`, then install Node.js from
<https://nodejs.org> (or `brew install node`).

**Windows:** install Node.js from <https://nodejs.org>, then get a compiler:
download **w64devkit** (a single zip, no installer) from
<https://github.com/skeeto/w64devkit/releases>, unzip it somewhere such as
`C:\w64devkit`, and add `C:\w64devkit\bin` to your PATH. (If you already use
MSYS2, `pacman -S mingw-w64-ucrt-x86_64-gcc` works too.)

### 2. Run

Linux and macOS: `./run.sh`
Windows: double-click **`run.bat`**

The first run builds Randy and the solver, then starts the server and prints
`http://localhost:8787`. Open that address in your browser. Press Ctrl+C in the
terminal to stop.

To use another port on Linux/macOS: `PORT=9000 ./run.sh`. On Windows, run `set PORT=9000` first.

### 3. Build by hand (if `run.sh` doesn't work)

```sh
make                 # builds ./bruteforce and ./randy/randy
cd webui
node server.js       # then open http://localhost:8787
```

`make -C randy` builds only Randy. On Windows, `run.bat` shows the exact `gcc`
commands if you'd rather run them yourself.

---

## The web editor

Paste your level code into the level box and the page loads the level and runs
the physics engine in your browser.

### Frame Inputs

Inputs are written as tokens, one after another:

| Token | Meaning |
|---|---|
| `D1` | hold D (right) for 1 frame |
| `WD5` | hold W + D together for 5 frames |
| `_6` or `6` | no keys for 6 frames |
| `(D1 SD1)50` | repeat the contents of the brackets 50 times |
| `break` | pause playback at this frame so you can inspect it |
| `// text` | comment, ignored to the end of the line |

Example:

```
D1 WD5        // right for 1 frame, then jump-right for 5
_6 W1 _3      // wait 6, jump 1, wait 3
(D1 _1)10     // tap D every other frame, 10 times
break
SD8           // crouch + right for 8 frames
```

The token being played is highlighted as the TAS runs.

### Player State panel

Updates every frame with the engine's internal state:

| Field | Meaning |
|---|---|
| `PLAYER_X` / `PLAYER_Y` | pixel position |
| `PLAYER_SX` / `PLAYER_SY` | horizontal / vertical speed (sub-pixels per frame) |
| `is_jumping` | frames since the last jump started |
| `is_falling` | frames spent falling |
| `player_state` | internal state code (idle, run, jump, fall, dead, ...) |
| `player_wall` | which wall the player is touching, if any |
| `flipped` | whether the sprite is mirrored (facing left) |
| `friction` | friction coefficient applied this frame |

### Timeline

A strip along the bottom shows every frame as a coloured band, one colour per key
combination. Click or drag to seek, and use the zoom slider to change how many
pixels one frame takes.

### Replay Code

The game's native replay code is shown at the bottom of the Frame Inputs tab and
is regenerated whenever your inputs change.

---

## Randy (stochastic explorer)

Randy keeps the best route found to each spot on a grid and keeps spawning
mutated runs from the frontier. It can cover long routes that the exhaustive
solver can't. The trade-off is that it is not a proof of optimality.

### Using it

1. Paste your level code.
2. Open the **Randy** tab.
3. Click the map to set the goal, or type X/Y, or press
   **GOAL = CURRENT FRAME POSITION**.
4. Optionally capture a start state from the current TAS frame
   (**SET START STATE**). Left empty, Randy starts from the level spawn.
5. Press **START SEARCH**.

The map shows a heat map of how many frames (or replay-code characters) it took
to first reach each spot, from blue (cheap) to red (near the cap). The best route
so far is drawn in magenta, the start in green and the goal in yellow. Drag to
pan and use the wheel to zoom.

When it finds something good, copy the TAS output, or press
**Load into Frame Inputs** to replace the editor contents with the best route.

### Map toolbar

| Control | What it does |
|---|---|
| **TILE: ON/OFF** | goal is a whole tile (any arrival inside it counts, fastest wins) instead of a single point (closest approach wins) |
| **DRAW KILL ZONE** | click and drag on the map to draw a circle; runs that leave it are cut off. If a zone already exists you are asked whether to replace it |
| **ON** | enable or disable the zone without deleting it |
| **CLEAR** | remove the zone |
| **HEAT** | switch the heat colours on or off |

### Search parameters

| Setting | Meaning |
|---|---|
| MUTATE RATE | chance per frame that a spawned run changes its input |
| STEP SIZE (F) | frames simulated per spawned run |
| KILL RADIUS | 0 = off; otherwise runs die or can't spawn farther than this from the best path |
| WEIGHT (W \| S), (A \| D) | how often each key is picked when mutating |
| MAX FRAMES / MAX CODE LEN | the cap on route length (`+1` raises it by one) |
| GRID | spatial cell size in world units |
| MIN CODE LEN | optimise for the shortest replay code instead of the fewest frames |
| SIM STEPS / FRAME | simulation batch size. Lower means less UI lag; 0 is adaptive |
| SPAWN WINDOW (LAST F) | 0 = spawn anywhere; otherwise only from nodes within this many frames of the cap |
| MAX SECONDS | 0 = run until stopped |

Goal, key weights, mutate rate, step size, kill radius and zone, spawn window and
the caps can all be changed while it runs. Grid size, tile size, the
optimise-for mode and the start state are read once at the start.

---

## Brute Force Solver

The solver tries every allowed key combination over a short window of frames and
reports the sequence that best satisfies your goal.

| Setting | Meaning |
|---|---|
| Player State | the start state, captured from the current TAS frame |
| Search Depth | how many frames to search. Higher is slower but can find better results |
| Optimization Goal | an expression evaluated on the final frame, maximised or minimised. Presets: Max X, Max Speed, X and SX |
| Frame Pins | lock specific frames: `1:+W` (W must be held), `2:-D` (D must not be held), `3:WD` (exactly W+D) |
| Kill Zone | an expression; any branch where it is true is pruned |
| Enabled Combos | which key combinations may be tried, with an optional cap on how many frames each may appear |

The result appears in TAS format, ready to paste into Frame Inputs.

The search is depth-first over 16 key combinations per frame. Duplicate-state
pruning is what keeps it tractable, and how well that works depends entirely on
the level. Turn depth up only as far as your level allows.

## Project layout

| Path | What it is |
|---|---|
| `physics.c/.h` | the physics engine |
| `spikes.c/.h`, `spike_correlation_table.json` | spike collision handling |
| `level.c/.h` | level code decoding |
| `playerstate_json.c/.h` | reads a player state from JSON |
| `main.c`, `hashset.c/.h`, `expr.c/.h` | the Brute Force Solver |
| `randy/` | Randy: `randy_cli.c` (headless program), `explorer.c` (search), `compat.h` (Windows/macOS/Linux differences), plus its own copy of the engine |
| `run.sh`, `run.bat` | build if needed and start the server (Linux/macOS, Windows) |
| `webui/` | the web editor and `server.js`, the local server that starts the native programs and relays their output to the page |
| `webui/physics.js` | the same physics, running in the browser for the editor and playback |
| `tools/crosscheck_physics.js` | checks the browser physics against the native engine |

## Notes

- **Do not add `-march=native` or `-ffast-math` when building Randy.** They change
  floating-point results (FMA contraction) and therefore the physics. Randy's
  Makefile already uses `-O3` only. For the same reason, use `make portable` if you
  want the Brute Force Solver built without `-march=native` too.
- One Randy run at a time: starting another stops the previous one, and closing
  the page stops it.
- If the page says it can't reach the local server, `node server.js` isn't
  running. If it says a binary wasn't found, run `make`.

### Troubleshooting

| Message | Fix |
|---|---|
| `randy binary not found` | The build failed or wasn't run. Run `make -C randy` and read the errors |
| `make: command not found` / `cc: not found` / `gcc not found` | Install the compiler (see Install above); on Windows make sure `w64devkit\bin` is on your PATH and reopen the window |
| `node: command not found` | Install Node.js |
| Port already in use | use another port (see Run above) |
