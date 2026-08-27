# BMS Common Parameter Protocol V2.1

This protocol separates application-visible BMS parameters from vendor AFE registers. PC tools, mobile apps and BLE/RS485 clients operate on stable logical parameter IDs and physical units; the firmware's `bms_afe` adapter translates supported parameters to the selected AFE.

## Compatibility

- Modbus slave: `0x01`.
- Functions: `0x03`, `0x06`, `0x10`.
- BLE SPP transports the same Modbus RTU frame as RS485.
- Existing clients can continue using `0x2100..0x2140` with the historical 16-bit encodings.
- New clients should discover `0x2000`, read descriptors at `0x4000`, then use signed 32-bit common values at `0x4400` and effective values at `0x4500`.

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

`SUPPORTED=1, ACTIVE=0` deliberately means the parameter is recognized/storable but the current firmware does not claim that it is enforcing that protection yet. A generic app should display it accordingly instead of assuming every AFE implements every logical level.

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

This is the value the device actually uses after AFE-specific range/step quantization. For example, a requested OV value not exactly representable by the AFE may differ from the effective value.

A generic app should normally show the requested value while also reading the effective value after a write. If they differ, the UI can show the actual hardware value without knowing any AFE register formula.

## Legacy 0x2100 compatibility

The historical 65-register map remains available. Internally it is translated into the same common parameter DB:

- current groups: legacy A*10 <-> common mA;
- temperature groups: legacy `(degC + 40) * 10` <-> common signed 0.1 degC;
- voltage, SOC and delay groups retain their existing scalar encoding.

Legacy and V2.1 clients therefore modify the same parameter state; there are not two independent configurations.

## Current SH3673510 mapping

The abstraction is live on this branch. The SH3673510 adapter currently declares direct AFE enforcement only where the mapping is unambiguous:

- Cell OV Level 3 -> SH3673510 OV, 3000..4500 mV, 5 mV step, conservative floor quantization.
- Cell UV Level 3 -> SH3673510 UV, 1000..3500 mV, 5 mV step, conservative ceil quantization.

Other logical parameters remain `ACTIVE=0` until their software-protection or AFE-protection mapping is explicitly implemented. In particular, OCD1/OCD2/OCC/SC are not guessed from the old L1/L2/L3 current fields because their physical thresholds and semantics depend on the selected AFE and the board shunt.

## Adding another AFE

A new AFE implementation changes only the adapter layer:

1. report AFE type/features/cell/temp capability;
2. normalize samples and faults into `bms_afe_sample_t`;
3. return the AFE-supported range/step for each logical parameter ID;
4. translate effective physical-unit values into the AFE's registers/data-memory representation;
5. keep raw vendor registers confined to engineering/debug paths.

The Modbus addresses, logical IDs, units and app workflow remain unchanged.
