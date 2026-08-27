# BMS Common Parameter Protocol V2.1

This protocol separates application-visible BMS parameters from vendor AFE registers. PC tools, mobile apps and BLE/RS485 clients operate on stable logical parameter IDs and physical units; the firmware's `bms_afe` adapter translates supported parameters to the selected AFE.

## Compatibility

- Modbus slave: `0x01`.
- Functions: `0x03`, `0x06`, `0x10`.
- BLE SPP transports the same Modbus RTU frame as RS485.
- Existing clients can continue using `0x2100..0x2140` with the historical 16-bit encodings.
- New clients should discover `0x2000`, read descriptors at `0x4000`, then use signed 32-bit common values at `0x4400` and effective values at `0x4500`.
- Protection runtime state is AFE-independent and available at `0xD130..0xD13A`.

## Discovery block: 0x2000

| Offset | Meaning |
|---|---|
| +0 | Magic `0x424D` (`BM`) |
| +1 | Protocol version, currently `0x0201` |
| +2 | Parameter schema version |
| +3 | AFE type (`0x3510` = SH3673510) |
| +4 | Cell count |
| +5 | Temperature-channel bit mask |
| +6 | Logical parameter count |
| +7 | Legacy parameter base (`0x2100`) |
| +8 | Capability descriptor base (`0x4000`) |
| +9 | Capability descriptor stride (`14`) |
| +10 | AFE feature bits low 16 |
| +11 | AFE feature bits high 16 |
| +12 | Common requested-value base (`0x4400`) |
| +13 | Common effective-value base (`0x4500`) |
| +14 | Common value stride (`2` registers) |
| +15 | Common value encoding version |

## Stable logical IDs

Logical ID format is `0x10GF`: `G` is the parameter group and `F` is the field.

### Groups

| G | Group | Primary unit |
|---|---|---|
| 0 | Cell over-voltage | mV |
| 1 | Cell under-voltage | mV |
| 2 | Bus/pack over-voltage compatibility group | mV |
| 3 | Bus/pack under-voltage compatibility group | mV |
| 4 | Charge over-current | mA |
| 5 | Discharge over-current | mA |
| 6 | Charge high-temperature | 0.1 degC signed |
| 7 | Charge low-temperature | 0.1 degC signed |
| 8 | Discharge high-temperature | 0.1 degC signed |
| 9 | Discharge low-temperature | 0.1 degC signed |
| A | MOS high-temperature | 0.1 degC signed |
| B | Cell voltage delta | mV |
| C | Low SOC | % |

The same group index is also the bit position in the common runtime protection bitmaps. For example bit0 is Cell OV, bit4 is Charge OC and bit10 is MOS OT.

### Fields

| F | Meaning |
|---|---|
| 0 | Level 1 |
| 1 | Level 2 |
| 2 | Level 3 |
| 3 | Recovery |
| 4 | Filter/delay, unit ms |

Example: `0x1002` is Cell OV Level 3. Its logical identity does not change when the AFE changes.

## Capability descriptor: 0x4000

There are 14 Modbus registers per logical parameter. Descriptor `N` begins at `0x4000 + N*14`.

| Word | Meaning |
|---|---|
| 0 | Logical parameter ID |
| 1 | Legacy register address |
| 2 | Flags/enforcement/quantization |
| 3 | Unit |
| 4..5 | Minimum, signed32, high word first |
| 6..7 | Maximum, signed32 |
| 8..9 | Step, signed32 |
| 10..11 | Requested value, signed32 |
| 12..13 | Effective value, signed32 |

Word 2 encoding:

- bit0: supported by the common parameter model;
- bit1: readable;
- bit2: writable;
- bit3: active/enforced in the current firmware;
- bits5:4: enforcement (`0 none`, `1 software`, `2 AFE`, `3 hybrid`);
- bits7:6: quantization (`0 nearest`, `1 floor`, `2 ceil`).

Unit encoding:

- `0`: none;
- `1`: mV;
- `2`: ms;
- `3`: mA;
- `4`: signed 0.1 degC;
- `5`: percent.

`SUPPORTED=1, ACTIVE=0` deliberately means the parameter is recognized/storable but the current firmware does not claim that it is enforcing that protection. A generic app should display it accordingly instead of assuming every logical parameter is active on every board.

## Common requested values: 0x4400

Each parameter occupies two registers as a signed32 physical-unit value, high word first:

`address = 0x4400 + parameter_index * 2`

- Read with FC03.
- Write with FC10 only.
- FC06 is rejected because it would update only half of a signed32 value.
- FC10 must start at a high-word boundary and contain complete high/low pairs.
- Multiple consecutive parameters may be written in one FC10 transaction.

Before committing, firmware validates all values against their descriptors, quantizes AFE-backed values conservatively, applies the hardware projections, and only then commits the RAM parameter DB. If an AFE write fails partway through, earlier AFE projections are rolled back from the previous effective values.

## Effective values: 0x4500

Each parameter occupies two read-only signed32 registers:

`address = 0x4500 + parameter_index * 2`

This is the value the device actually uses after AFE-specific range/step quantization. A generic app should normally show the requested value while also reading the effective value after a write. If they differ, the UI can show the actual hardware value without knowing any AFE register formula.

## Legacy 0x2100 compatibility

The historical 65-register map remains available. Internally it is translated into the same common parameter DB:

- current groups: legacy A*10 <-> common mA;
- temperature groups: legacy `(degC + 40) * 10` <-> common signed 0.1 degC;
- voltage, SOC and delay groups retain their existing scalar encoding.

Legacy and V2.1 clients therefore modify the same parameter state; there are not two independent configurations.

## Runtime protection status: 0xD130

Read 11 registers with FC03. The block is intentionally AFE-independent so the same PC/app diagnostics can be used across SH36735xx, SH367309, BQ769x2 or later adapters.

| Address | Meaning |
|---|---|
| `0xD130` | Magic `0x5052` (`PR`) |
| `0xD131` | Protection status version, currently `1` |
| `0xD132` | Level-1 active group bitmap |
| `0xD133` | Level-2 active group bitmap |
| `0xD134` | Level-3 active group bitmap |
| `0xD135` | Any-active group bitmap (`L1 | L2 | L3`) |
| `0xD136` | MOS request/veto/effective state flags |
| `0xD137` | Last AFE MOS command result, signed16 |
| `0xD138` | Normalized AFE fault bitmap low 16 |
| `0xD139` | Normalized AFE fault bitmap high 16 |
| `0xD13A` | Parameter persistence status |

`0xD136` bit assignment:

- bit0: user charge-MOS request ON;
- bit1: user discharge-MOS request ON;
- bit2: software charge veto active;
- bit3: software discharge veto active;
- bit4: effective charge-MOS request sent/desired after software policy;
- bit5: effective discharge-MOS request sent/desired after software policy.

The effective bits describe the common software decision. The AFE remains authoritative for hardware protection and may refuse/override a MOS-on request while a hardware protection condition is active.

## Software protection policy

Except for unresolved Bus/Pack OV/UV compatibility groups, common L1/L2/L3 parameters are active in the software protection engine.

- L1 is status/alarm only and does not veto a MOS.
- L2 and L3 veto the corresponding current direction.
- Cell OV, Charge OC, Charge OT and Charge UT veto charging.
- Cell UV, Discharge OC, Discharge OT, Discharge UT and Low SOC veto discharging.
- MOS OT vetoes both directions.
- Cell delta is evaluated and reported in the protection bitmaps but currently does not directly veto a MOS.
- The common-port opposite-current reopen policy is enabled: a software one-direction veto can re-open the opposite-current path when current reverses, while an AFE hardware veto still has final authority.

## Current SH3673510 mapping

The SH3673510 adapter currently has direct hardware projection for the mappings that are unambiguous in the common schema:

- Cell OV Level 3 -> SH3673510 OV, 3000..4500 mV, 5 mV step, conservative floor quantization.
- Cell UV Level 3 -> SH3673510 UV, 1000..3500 mV, 5 mV step, conservative ceil quantization.

These parameters therefore report `HYBRID` enforcement because both the software protection engine and AFE hardware protection participate. Other active groups currently report `SOFTWARE` enforcement unless/until an explicit AFE mapping is added.

OCD1/OCD2/OCC/SC are not guessed from the common L1/L2/L3 current fields. Their mapping remains an AFE/product policy decision because the native AFE stages have different semantics from the common three-level model.

## Persistence

Common requested parameter values are persisted by the firmware after a debounce delay rather than erasing flash on every Modbus write.

- CRC32 protects each record.
- Two independent flash slots are alternated with a monotonically increasing sequence number.
- Startup selects the newest valid slot.
- A restored record is accepted only if every stored value is still valid under the current schema and AFE capability; otherwise defaults are kept atomically.
- The AFE is initialized from the common parameter DB after restore, so AFE registers are a runtime projection rather than the source of truth.

`0xD13A` reports persistence state. The low bits include storage supported, valid record present, last save success, storage error and active slot; additional common-layer bits report dirty/pending-save and restore accepted/rejected state. Apps should treat this as diagnostics, not as a parameter value.

## Adding another AFE

A new AFE implementation changes only the adapter layer:

1. report AFE type/features/cell/temp capability;
2. normalize samples and faults into `bms_afe_sample_t`;
3. return AFE-supported range/step for logical parameter IDs with direct hardware projection;
4. translate effective physical-unit values into the AFE register/data-memory representation;
5. keep raw vendor registers confined to engineering/debug paths.

The Modbus addresses, logical IDs, physical units, software protection bitmaps and app workflow remain unchanged.
