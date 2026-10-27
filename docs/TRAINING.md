# Training and player evolution

How PC FÚTBOL 2001 v3.6 changes players' ratings: training, the season end and the ageing. Addresses are VAs in `manager.exe` (file offset + `0x400000`). Learned from the code and from rating traces over two seasons of a career with Inter (the method is in [Ageing undone at managed clubs](GAME-INTERNALS.md#ageing-undone-at-managed-clubs)). What isn't confirmed says so.

## The ratings

A player has ten ratings, in the database's order:

| Field | Rating | Base | In the average |
|---|---|---|---|
| `+0xF5` | speed (*velocidad*) | `+0x102` | yes |
| `+0xF6` | stamina (*resistencia*) | `+0x103` | yes |
| `+0xF7` | aggressiveness (*agresividad*) | `+0x104` | yes |
| `+0xF8` | quality (*calidad*) | `+0x105` | yes |
| `+0xFD` | headshot (*remate*) | `+0x10A` | |
| `+0xFC` | dribbling (*regate*) | `+0x109` | |
| `+0xFB` | pass (*pase*) | `+0x108` | |
| `+0xFE` | shot (*tiro*) | `+0x10B` | |
| `+0xFA` | tackling (*entrada*) | `+0x107` | |
| `+0xF9` | goalkeeper (*portero*) | `+0x106` | |

The names are those of the *Editor PCF 2001*, which reads the same database records in the same order. The average the game shows (*ME*) is that of the first four.

Each rating has a **base**, `0x0D` further on. The database loaders (`0x4ADCD0`, `0x4AE200`) set the rating and its base to the same value, and only the ageing changes the base afterwards (with the patch, see [Ageing](#ageing-once-a-season-every-club)). Saved games keep the bases.

Other values training changes:

| Field | Value |
|---|---|
| `+0x138` | penalty |
| `+0x139` | left corner |
| `+0x13A` | right corner |
| `+0x13B` | left free kick |
| `+0x13C` | right free kick |
| `+0x137` | a flag that picks which free kick training changes (not identified; probably the foot) |
| `+0xFF` | the *EF* column of *Alineación* |
| `+0x100` | energy (the *EN* column) |

The set-piece fields are the loader's order; their names follow the editor's order (penalty, then the corners, then the free kicks). They have no base.

## Who trains

Only the clubs people manage. Their club state (the team record's `+0x11C`) has a training object at `+0x601C`; the other clubs have none, so nothing below happens to their players except the ageing.

The training object holds:

| Field | Contents |
|---|---|
| `+0x1C` onwards | the schedule: 5 × 21 lists of entries (player ID, exercise ID at `+4`), 8 bytes each. 21 is probably 7 days × 3 sessions; what the 5 are isn't known |
| `+0x360`, `+0x364` | one record per player (pointers, and their count) |
| `+0x368` | days since the last session |

A player's record (`0x3C` bytes, created by `0x4CA220`) has his ID and 14 **balances**, each starting at 1000:

| Record | Balance of |
|---|---|
| `+0x04` | quality |
| `+0x08` | EF |
| `+0x0C` | energy |
| `+0x10` | dribbling |
| `+0x14` | tackling |
| `+0x18` | speed |
| `+0x1C` | stamina |
| `+0x20` | pass |
| `+0x24` | shot |
| `+0x28` | headshot |
| `+0x2C` | a free kick (`+0x13C`, or `+0x13B` when `+0x137` is set) |
| `+0x30` | penalty |
| `+0x34` | aggressiveness |
| `+0x38` | goalkeeper |

The schedule set in *Entrenamiento* (by hand or with *Auto*) lasts until the season end, which takes every player out of it ([below](#the-season-end-managed-clubs-before-the-ageing)): it has to be set again every season.

## Every day: drift and sessions

`0x4C94C0`, every day, for every managed club (from `0x4B40E0` in the day loop):

**Drift.** For each player of the squad (his record is created if he has none), a random balance goes down:

- the amount is 4, or a random number up to (days since the last session) / 4 when that's larger: the longer without training, the bigger;
- which balance is random among 35 choices, of which 21 are balances (`0x4CA260`), so a player loses something on about 60% of days. Quality, speed, stamina and energy are each twice as likely as the others, EF three times. Corners have no balance and never change;
- a player with `+0xB0` set (probably injured) loses 2 to 9 from a random balance on almost every day instead.

**Sessions.** For each of the day's 3 slots, each scheduled player who isn't marked at `+0xB0` does the slot's exercise (`0x4CA3A0`). The amount *A* of the session is random: `rand() mod d`, where *d* depends on the coach in charge of that exercise (`0x4C91D0` picks him, `0x4C8900` gives his value, compared with a required level from `0x4C9470`; clubs with ID 10000 or more use 10):

| Coach | *d* | *A* |
|---|---|---|
| at least the required level minus 3 | 4 | 0 to 3 |
| below that | 3 | 0 to 2 |

The code has branches for *d* = 2 and 1 (more than 6 and more than 10 below), but its comparisons never reach them; it looks like a mistake (unconfirmed in play). So the coach's quality barely matters. After the sessions, the days since the last session go back to 0.

## The exercises

Their IDs are the order of the two lists on the *Entrenamiento* screen (`TX_NAMETRAINING1` and `TX_NAMETRAINING2` of `tx_pcfwinneoentrenamiento_cpp`, set by `0x9AF7A0` and `0x9B01D0`): IDs 0 to 15 for outfield players, 16 to 28 for goalkeepers. Exercises with the same name run the same code in both lists. What each one adds to the balances (`0x4CA3A0`; every exercise also changes energy and EF):

| ID | Exercise | Adds to |
|---|---|---|
| 0 | *Descanso* | nothing |
| 1 | *Rondos* | quality *A*+1 |
| 2 | *Dribbling* | dribbling *A*, quality *A* |
| 3 | *Recuperación del balón* | tackling *A*+1, speed *A*, stamina *A* |
| 4 | *Pases al área* | pass *A*+1, quality *A* |
| 5 | *Carrera continua* | stamina *A*+1 |
| 6 | *Series de velocidad* | speed *A*+1 |
| 7 | *En zig-zag* | speed *A*, dribbling *A* |
| 8 | *Tiros a puerta* | shot *A*+1, quality *A* |
| 9 | *Remates* | headshot *A*+1 |
| 10 | *Ensayo de faltas* | free kick *A*+1, shot *A*, quality *A* |
| 11 | *Ensayo de penaltis* | penalty *A*+1, shot *A*, quality *A* |
| 12 | *Partidillo* | every balance, *A*/3 |
| 13 | *Gimnasio* | speed and stamina +*A*; quality, dribbling and pass −*A* |
| 14 | *Elasticidad* | speed *A* |
| 15 | *Masajes* | stamina *A* |
| 16 | *Salidas uno contra uno* (goalkeepers) | goalkeeper, quality, speed, stamina *A* |
| 17 | *Salidas por alto* | quality *A* |
| 18 | *Carrera continua* | as 5 |
| 19 | *Series de velocidad* | as 6 |
| 20–23 | *Tiros a puerta*, *Remates*, *Ensayo de faltas*, *Ensayo de penaltis* | goalkeeper *A*+1, quality *A* |
| 24 | *Partidillo* | goalkeeper, quality, speed, stamina, aggressiveness *A* |
| 25 | *Gimnasio* | speed and stamina +*A*; quality, goalkeeper and pass −*A* |
| 26 | *Elasticidad* | as 14 |
| 27 | *Masajes* | as 15 |
| 28 | *Descanso* | as 0 |

The numbers the *Entrenamiento* grid shows on each session (20, 22, 24, 26 after *Auto*) aren't identified; they may be the required level `0x4C9470` returns.

## After each match: balances become ratings

After each match a managed club plays, every player of its squad with a record goes through `0x4CA770` (from `0x4C9960`, called by the club's post-match processing `0x49BCE0`). For each balance, every 12 points away from 1000 become one rating point, and the balance keeps the remainder. The ten ratings stay within a **window around their base**:

| Rating | Lowest | Highest |
|---|---|---|
| speed, headshot, dribbling, pass, shot, tackling | base − 8 | base + 3 |
| quality | base − 10 | base + 3 |
| stamina | base − 15 | base + 5 |
| aggressiveness | base − 25 | base + 7 |
| goalkeeper | base − 6 | base + 3 |

A gain past the top sets the rating to the top, even if it was above it; a loss below the bottom sets it to the bottom, even if it was below it. Every rating is then kept between 5 and 99.

The penalty and the free kick have no window: they change by the whole amount, between 1 and 99. EF and energy have rules of their own.

Traced with Inter and *Auto* training: the balances stayed within a few points of 1000 all season, the ratings moved by 0 or 1 per match, and every rating stayed inside its window. With no training set (the start of the next season), the balances averaged almost 10 below 1000 within six weeks, and almost every player lost 1.5 to 3.5 points of average.

## The season end (managed clubs, before the ageing)

At the season end (`0x4C99A0`, from the club's season-end method `0x49F230`, before the ageing), every player in the training schedule has each of his ten ratings set:

- with *Envejecimiento de los jugadores* on, halfway between it and its base, rounded down;
- with it off, to its base.

He's then taken out of the schedule (`0x4C90B0`). Set pieces, EF and energy aren't touched.

## Ageing (once a season, every club)

With *Envejecimiento de los jugadores* on (option 11 of `0x4D0690` clear), at the season end, for every player of every real club, its youth players included (`0x4B4150`, `0x49F230`, `0x4AF4E0`). Free agents (9950) and the hidden youth pools (9955 to 9958) don't age this way.

| Age (from the birth date, on the season-end date) | Change |
|---|---|
| 30 or older | speed and stamina −5 if at least 35; aggressiveness and headshot −3 if at least 33 |
| 25 to 29 | none |
| under 25 | a gain to speed, stamina, aggressiveness and quality, each skipped if it would reach 99 or more |

The gain, by the average of those four (rounded down) and the matches of the season (`+0x68`, reset at each season start; whether it counts every competition isn't confirmed):

| Gain | Average | And |
|---|---|---|
| +3 | below 85 | 20 matches or more, or youth training on (`+0x101`) |
| +2 | below 90 | 15 matches or more |
| +1 | below 95 | 10 matches or more |
| none | otherwise | |

The rows are checked in order: below 85 with 15 to 19 matches gives +2.

In the original game the ageing changes only the ratings, so at managed clubs training and the season end, which measure from the base, take it back. The patch applies each change to the base too ([Ageing undone at managed clubs](GAME-INTERNALS.md#ageing-undone-at-managed-clubs)).

The other six ratings never change with age, and neither do set pieces. Over a career, then, a player's secondary skills stay within their training window around the database values, while his penalty and free kick can keep improving with *Ensayo de penaltis* and *Ensayo de faltas*, and his corners never change.

## Youth training

A youth player with *Entrenando* on (`+0x101`, [Youth training](GAME-INTERNALS.md#youth-training)) gets, in the periodic player update (`0x4AF0F8`, how often isn't identified), a 60% chance of +1 to speed, stamina, aggressiveness and quality, each capped at its base and at 99. It also counts as 20 matches for the ageing's +3.

## Retirement and regeneration

Players of clubs 9900 and 9950 who are 35 or older (33 with a flag at `+0x58`, probably goalkeepers) are regenerated at the ageing (`0x4C1FF0`): 10 to 12 years younger, a new name, and speed, stamina, aggressiveness and headshot reset to their base. A player who retires from a club (`0x4C18E0`, message `0x3ED`) is regenerated at once and moved; only from a managed club does the code send him to the free agents (9950).

## Not confirmed

- What the 5 × 21 schedule lists are beyond days and sessions.
- What `0x4C9470` returns, and the numbers on the *Entrenamiento* grid.
- That `+0xB0` is the injured flag, and `+0x137` the foot.
- Whether `+0x68` counts every competition.
- How often the youth training update runs.
