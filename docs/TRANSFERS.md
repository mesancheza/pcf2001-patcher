# Transfers and contract offers

How PC FÚTBOL 2001 v3.6 decides transfers: whether a player and his club accept an offer, and which players the computer's clubs go for. Addresses are VAs in `manager.exe` (file offset + `0x400000`). Learned from the code only: none of it has been traced in play yet. What isn't confirmed says so. Amounts are in pesetas, as floats.

## Offers and contracts

A player's offers are a list at `+0xE0` (count at `+0xE4`), one 0x28-byte entry per club. His current contract, at `+0xB4`, has the same layout (so the clause is the player's `+0xB8` and the salary `+0xBC`):

| Entry | Contents |
|---|---|
| `+0x00` | the club's ID (word) |
| `+0x04` | release clause (*cláusula de rescisión*) |
| `+0x08` | yearly salary (*ficha anual*) |
| `+0x0C` | *Primas por gol*, the bonus per goal (the offer screen's label comes from `[0xBCFC24]`, a function per language: `0x9C6670` gives *Primas por gol*, `0x9C6C10` *Scoring bonus*) |
| `+0x10` | *Prima por Copa Nacional* |
| `+0x14` | *Prima ascenso/ganar liga* |
| `+0x18` | *Prima por Copa Internacional* |
| `+0x1C` | *Prima conseguir objetivo* |
| `+0x20` | years |
| `+0x21` | not identified (from the contract's `+0x22`; 20 after accepting messages `0x5B6`/`0x5BE`) |
| `+0x22` | options, below |
| `+0x23` | state: bit 0 open, bit 1 already weighed by the player, bit 2 accepted by him, bit 3 needs his club's agreement |
| `+0x24` | how many times the offer has been processed |

The options are the tick boxes of the offer screen (`0x504DA1` to `0x504F59`), in bit order:

| Bit | Option |
|---|---|
| `0x01` | *Libertad por descenso* |
| `0x02` | *Casa y coche* |
| `0x04` | *Libre si no asciende o gana la liga* |
| `0x08` | *Libre si no se clasifica para Europa* |
| `0x10` | *Libre para ofertas de mejores clubs* |
| `0x20` | the offer has a release clause (from how the code uses it) |

The negotiation with the player's club (`0x4B1640` finds it, by player and club) is a separate record: the fee at `+0x04`, up to two players offered in exchange at `+0x08` and `+0x0C`, state at `+0x10` (bit 0 open, bit 1 decided, bit 2 accepted) and a count of times processed at `+0x11`.

## Club levels

Every club has a **level** at `+0xEC`, from 0 to 13, which the salaries and the player's judgement of an offer depend on. 0 is the richest: the salary a level expects scales with (1 − 0.1 × level)^4.5 (`0x491680`, levels above 9 count as 9), so a level-1 club pays 62% of a level-0 one, and a level-3 club 20%. **How the level is set** (`0x49A649`–`0x49A6C3`): the average of speed, stamina, aggressiveness and quality over the squad (`0x49AB30`: the best 80% of the players for clubs whose `+0x112` is 30, English and Welsh clubs ([DATABASE.md](DATABASE.md#countries)); every player otherwise), turned into a level by `0x4A1B90`:

| Average | 80+ | 76–79 | 72–75 | 67–71 | 64–66 | 60–63 | 56–59 | 54–55 | 52–53 | below 52 |
|---|---|---|---|---|---|---|---|---|---|---|
| Level | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |

It's worked out where the club's competition (`+0xD8`) is set; when that runs isn't checked. The same function places a player at a level by his own average (the playing-time rule below). Nothing about the stadium enters it, nor any other part of the decisions in this document: the stadium only helps keep a club's own players, through their playing time (*Calefacción césped* +2, *Vestuarios* +4 and an *Enfermería* level +4 for the whole squad when the work ends).

## When offers are processed

`0x4BF950` processes one player's offers. The transfer processing of his club (`0x4C1030`) calls it for each player with offers, with the club's squad size (free agents: 255). For each open offer it adds 1 to the entry's `+0x24`, then goes through the checks below.

## Refused at once

| Condition | Message |
|---|---|
| He already has a move agreed (`+0xDC`) | `0x44B` |
| His club is a real club (ID below 9900) with 16 players or fewer, and it isn't a renewal | `0x491` |
| With *Envejecimiento de los jugadores* on: his age, plus the years he has left (`+0xD5`) for a renewal, is above 35 (33 if `+0x58` is set, probably goalkeepers; `0x4C1FE0`) | `0x490` |
| The buying club would go over its foreign-player limit (`0x4B1C90`, [GAME-INTERNALS.md](GAME-INTERNALS.md#foreign-players)) | `0x4AD` |

## The selling club

Unless the offer is a renewal, the player's club must agree to the fee. It accepts at once if:

- the offer doesn't need its agreement (state bit 3 clear);
- the player has a release clause (contract option `0x20`) and the fee is at least that clause;
- the club is a hidden team (ID 9900 or above), such as the free agents.

A club a person manages decides by hand. Any other club decides once, on a processing where a random number below 100 is below (count × 3 + 3) × 10. That gives 60% the first time, 90% the second, and certain after that.

**What the offer is worth** (`0x4BE500`): the fee, plus a value for each player offered in exchange. If a player offered in exchange wouldn't accept the move, or can't move, the club negotiation is closed (messages `0x49A`, `0x46C`, `0x49B`).

**What the club thinks the player is worth** (`0x4A0C20`), *W*. It starts from his market value *M* (`0x491430`, from the club's level, his average and his age) and his clause *C*:

| Case | *W* |
|---|---|
| *C* at most 1.5 × *M* | the larger of *C* and *M*, × *p* / 100 |
| *C* above 1.5 × *M* | *C* × *p* / 150 |
| Transfer-listed (`+0x13D` bit 0) | the above × 0.75 |

*p* is a percentage between 50 and 150: 100 if the club's `+0x14C` is below 4, otherwise worked out from his salary (`+0x6C`) against `+0x14C`, from `+0x7C`, from the club's level and from other values not worked out yet. With a large release clause, the club wants close to the clause.

**The decision:**

| Offer's worth | Result | Message |
|---|---|---|
| at least 0.95 × *W* (0.5 × *W* if he's transfer-listed) | accepted | `0x44A` |
| below 0.85 × *W* (0.4 × *W* if listed) | refused | `0x460` |
| in between | refused, with a reply that depends on *W* against his clause | `0x45F`, `0x461` |

## The player

He weighs each offer once (state bit 1), with the same 60%, 90%, then certain chance per processing.

### The score of an offer

`0x491860` gives a contract a score, for a player at a club of level *a* and an offer from a club of level *b*:

**score = *S* ÷ current × *S* ÷ *E* × (min(*a* + 1, 10) ÷ min(*b* + 1, 10)) × (1 + 0.1 × *y*) × options**

**The money.** Current is his current yearly salary (`+0xBC`). *E* is the salary a player of his average and age expects (`0x491680`, at the level `0x4ADBA0` gives him). *S* is the offer's yearly salary plus these extras:

| Extra | Counts as, if at least 1 million |
|---|---|
| *Primas por gol* | 15 ×, whatever his position: as if he scored 15 goals a season |
| *Prima por Copa Nacional* | 0.4 × at level *b* 0, 0.2 × at level 3 or above, nothing at levels 1 and 2 |
| *Prima ascenso/ganar liga* | 0.2 ×, only at levels 1, 4, 7 and 10 |
| *Prima por Copa Internacional* | 0.2 × at level 1 or above, nothing at level 0 |
| *Prima conseguir objetivo* | 0.4 × |
| *Casa y coche* | (80 − 5 × min(*b* + 1, 10)) million, so 75 million at level 0, whatever it costs the club (not checked) |

*S* counts twice, once against each salary, so the score grows with its square: doubling the money multiplies it by 4.

**The levels.** An offer from a richer club (a lower level) scores more: from a level-0 club to a player at a level-3 club, × 4.

**The years.** *y* depends on the offer's years:

| Years | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| × | 1.1 | 1.2 | 1.3 | 1.4 | 1.4 | 1.3 | 1.2 |

**The options**, by the levels:

| Option | Effect |
|---|---|
| *Libertad por descenso* | without it, × 0.8 if *b* is 3, 6, 9 or 12 |
| *Libre si no se clasifica para Europa* | without it, × 0.8 if *b* is 2 or 3 and *a* is at least 1 |
| *Libre si no asciende o gana la liga* | × 1.2 if *b* is 4, 7, 10 or 13 and *a* is above *b* |
| *Libre para ofertas de mejores clubs* | × 2 if *b* is above 3 |

### What he compares it with

His current contract, scored the same way at his own club's level for both *a* and *b* (`0x4BEFB0`), times (*A* ÷ 70)². *A* (`0x4B0360`) is (`+0x110` × 50 + `+0x10F` × 30) ÷ 100, plus 15 if he isn't transfer-listed (`+0x13D` bit 0) and 5 if bit 1 of `+0x13D` is clear (probably not loan-listed). *A* reaches 99, so it can double his current contract's score: traced with Zidane at Juventus, *A* 99, current contract 3.43 against an offer of his own salary for 4 years, 2.00.

**What *A* is made of** (from the code; not traced over time):

| Byte | What changes it |
|---|---|
| `+0x110`, playing time (half of *A*) | After a match (`0x4AEB60`): set to 99 if he played 90 minutes or more, or is injured (`+0xB0`), or `+0x112` is set. Otherwise it goes down: by 33 if his average is 85 or more, or if his average puts him at a better level than his club's (`0x4A1B90` gives a lower level number than the club's `+0xEC`); by 11 at 82 to 84; by 5 or 7 otherwise. Halved if `+0x58` is clear, × 1.5 if his *EF* is above 80, halved again if he played some minutes. The stadium's weekly update adds 2 or 4 to the whole squad when some works end (`0x4CD549`, `0x4CD598`, `0x4CD7C5`). Kept from 0 to 99 (`0x4B03B0`) |
| `+0x10F`, rivals (30%) | When a player arrives at his club (`0x4BE940`, called with 1 from `0x4BE3D3`), every other squad player is compared with him by current average (*ME*, `0x406970`), specific position (`+0x5F`), line (`+0x58`: 0 for goalkeepers) and salary. A squad player who shares the position or the line gets `+0x10F` = 99 − *X*, at most `+0x10E` (99 unless set elsewhere), with *X* in the table below. Each arrival sets it again rather than adding up. When a player leaves (called with 0 from `0x4BEF93`), the same comparison sets it to `+0x10E` + *X*, so back to 99 |
| Both | Set to 99, with `+0x10D` and `+0x10E`, by `0x4BD9D0`, which moves a player into a club from a list: called by the *Jóvenes promesas* screen (`0x451C2D`, `0x451E41`) and four places not identified. A transfer completed by the club's processing (`0x4BE090`) and a renewal (`0x4B0E80`, from `0x4C138B`) don't touch them. 99 too for players loaded from the database (`0x4AE9B3`) |

*X* for a squad player compared with the newcomer:

| His average against the newcomer's | Same position, he earns less | Same position, he earns as much or more | Same line only, he earns less | Same line only, as much or more |
|---|---|---|---|---|
| within 2 | 60 | 40 | 50 | 30 |
| 3 or more below | 60 | 30 | 35 | 20 |
| 3 or more above | 50 | 50 | 35 | 35 |

A squad player whose own club isn't that club (`+0xB4`) gets 50 or 40 (within 2) or 35 or 20 (3 or more below), with no salary test.

So a player who plays every match, with no rival signed for his position, stays at 99, renewed or not; a star left out of a few matches loses most of the playing-time half. The buying club can't change *A*.

For a renewal the offer is multiplied by (*A* ÷ 70)² too, so *A* cancels out. A free agent compares with a standard contract (`0x4912F0`) times 0.51.

### The decision

If the offer scores at least as much as his current contract, he accepts (`0x44E`), unless the offer has a release clause above what he allows. That maximum is `0x491570` for the buying club's level, his average and his age (*B*). If the salary alone is above the salary `0x491680` gives at that level (*L*), the maximum rises to *B* + salary × *B* ÷ (4 × *L*), up to 10 × *B*. Above it he asks to lower the clause (`0x5AB` with the amount, or `0x5AA`), or for more money when the salary is below *E* (`0x5AC`).

If the offer scores less, and a person manages the buying club, he tries changes to it, in order, and asks for the first that would make it good enough:

| Change | Message (renewal) |
|---|---|
| More years, up to 4, or fewer, down to 1 | `0x5AE` more, `0x5B1` fewer (`0x5BB`, `0x5BC`) |
| 1.1 × the salary | `0x5AD` with the amount (`0x5BA`) |
| 1.375 × the salary (1.25 × the previous step) | `0x5AC`, without the amount (`0x5B9`); confirmed in play: Zidane, with an attachment of 99, asked for it after an offer of his own salary for 4 years |
| *Libertad por descenso* | `0x5B4` (`0x5BD`) |

Accepting one of these demands makes the new offer with it (`0x489C9C` sets *Libertad por descenso*). *Libertad por descenso* only changes the score when the buying club's level is 3, 6, 9 or 12, so for other clubs the last try never works. If nothing works, he refuses:

| Case | Message |
|---|---|
| A renewal | `0x3FE` |
| *A* above 85, and he isn't a free agent | `0x1781` |
| The buying club's level is at least 4 above his club's | `0x177E` (same country), `0x1771` (another country, named) |
| Otherwise | `0x177F` |

The buying club is then told the outcome (`0x492` if he accepted, `0x3FC` if not). The texts of the message numbers aren't identified.

### More than one offer

Of the offers that both he and his club have accepted, he takes the one with the highest score, the computer's clubs' offers included. The move goes ahead at the earliest on the offer's second processing; how it's carried out hasn't been read.

## In practice

For a club signing a player:

- A selling club with 16 players or fewer never sells.
- The fee must reach 95% of the club's value of the player, and that value follows his release clause when the clause is large. Paying the clause itself is always accepted.
- Raising the salary helps with its square. *Casa y coche* is worth 75 million a year to a player joining a level-0 club.
- *Primas por gol* counts 15 times in the player's judgement whatever his position, while the club pays it only per goal (not checked): a goal bonus to a defender or a goalkeeper costs little and weighs a lot.
- 4 or 5 years score best; 7 years scores less than 3.
- A release clause in the offer can make a player who likes the money refuse it.
- A richer rival offer wins: he takes the best-scoring one.

## The computer's clubs' signings

How a club the computer runs chooses players to sign, which the mods `fichajes-de-mas-nivel` and `fichajes-del-doble-de-nivel` change ([MODS.md](MODS.md#fichajes-de-mas-nivel-and-fichajes-del-doble-de-nivel)).

`0x494AA0` runs every day for each club, from the club's daily processing (`0x49EFFB` in `0x49E1E0`), when `0x4A7F00` returned 0 (presumably no one manages the club; not identified). It gives up unless:

- option 10 of `0x4D0690` is clear;
- the club's ID is below 9900, so not a hidden team;
- the squad has fewer than 25 players, and a random number below the squad size is below 16;
- the club's `+0x108` is set, or a 1-in-35 chance passes.

It works out the club's average *T*: the sum of speed, stamina, aggressiveness and quality over the **whole squad**, reserves included, divided by 4 × the squad size. So *T* is well below the average of the club's best players.

It then picks one position it needs (`0x49A980`, `0x494400`): the first one it has no player for, or, if it has them all, a random one from 1 to 19 (19 skips to the last place below). It looks in these places, in order, and stops at the first offer it makes. Each place takes a player only if his average (of the same four ratings) is in a window around *T*:

| Where | Which player | Window | Checks (VA) |
|---|---|---|---|
| The free agents (club 9950) | the best for the position (`0x494400`), with no pending move (`+0xDC`) and fewer than 3 offers | at least *T* − 4 | `0x494CF5` |
| The transfer list (the context's `+0x40`: the players with `+0x13D` bit 0, `0x4B3B12`) | see below | *T* − 2 to *T* + 4 | `0x494E51`, `0x494E5C` |
| The loan list (the context's `+0x44`: `+0x13D` bit 1, `0x4B3B3E`); the offer is a loan (`0x4B16B0`) | as the transfer list, with a different chance (below) | *T* − 2 to *T* + 4 | `0x4951DE`, `0x4951E9` |
| Other clubs (the context's `+0x4C`), each with a 1-in-6000 chance | his club's best for the position, by contract years and salary | *T* − 2 to *T* + 2 | `0x4954C1`, `0x4954CC` |
| A random club's squad, on 3% of days after the 7th not divisible by 4 | any of its players | above *T*, up to *T* + 5; or up to *T* + 10 if *A* (`0x4B0360`) is below 25 | `0x495860` to `0x495883` |

**The transfer list.** For each listed player, the club makes an offer only if all of these hold (the mod `transferibles-sin-limite-de-nivel` drops the window's top, [MODS.md](MODS.md#transferibles-sin-limite-de-nivel)):

- he has no move agreed and plays the position (`0x4933E0`);
- his average is in the window;
- his club has more than 16 players and isn't the buyer (`0x494A30`);
- a random number below *N* is 0, so a 1-in-*N* chance. *N* is 65, or 55 if a person manages his club; 16 less if both clubs are in the same competition (`+0xD8`), 26 less if in the same country (`+0xA4`), 10 less if his club's `+0x108` is set, 2 less if the buyer has fewer than 20 players. If the buyer has fewer than 16 players, *N* is 0;
- he has fewer than 3 offers, and he fits the buyer's foreign-player limit;
- with 2 years or more left on his contract (`+0xD5`), his release clause is at most twice the buyer's `+0x124` (a double, probably its money).

The offer (`0x4A0500`): his salary times 90% to 100%, his contract's years plus 0 to 2 (up to 7), and a fee of 1.2 × his market value (`0x491430`, at the buyer's level) or 70% to 100% of his clause, whichever is less. With fewer than 2 years left, there's no fee: the offer doesn't need his club's agreement.

On the loan list there's no release-clause check and no limit of 3 offers. *N* starts the same, takes 36 off (not 10) if his club's `+0x108` is set, and 2 off if the buyer has fewer than 20 players. When both clubs are in the same competition it takes 4 off if a flag of the competition is set, and 12 more if further checks pass (`0x4C3DA0`, not identified).

Every margin is the 8-bit immediate of one `lea` or `add`, and each check is an unsigned `ja`/`jbe`, so the window includes its ends. The foreign-player limit is checked too, and the offers are made by `0x4A0500` (`0x49511F`, `0x495717`, `0x495A17`) and `0x4B16B0`; what those check isn't identified. Once made, the computer's offers go through the same decisions as anyone's, above.

**Renewals:** the club's transfer processing (`0x4C1030`) renews a player in the last year of his contract (`+0xD5` is 1, checked every 8 days) if his average is at least *T*, or his salary (`+0x6C`) is at least half of the club's `+0x14C`.

## Not confirmed

- When a club's level is worked out, and what the levels above 9 (up to 12) are for.
- Which *Equipamiento* row is which work type (assumed to follow the panel's order).
- When `0x4AEB60` runs, what `+0x10E`, `+0x112` and `+0x58` are, and that bit 1 of `+0x13D` is loan-listing.
- The meaning of the offer's `+0x21`.
- How the percentage *p* is worked out in full, and what `0x491430` and `0x491570` compute.
- What *Casa y coche* costs the club.
- The texts of the message numbers.
- How often the transfer processing runs, and how the move is carried out.
- All of it in play.
