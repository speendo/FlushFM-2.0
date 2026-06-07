# DESIGN: Dual-EMA Edge-Detection Light Sensor (TEMT6000)

## 1. Problem

The current hysteresis-based implementation uses absolute ADC thresholds (BTD=1200, DTB=2800) to distinguish dark from bright. This cannot differentiate between a room light toggle (fast step change) and natural ambient light shifts (clouds, day/night — slow ramps). The sensor must detect artificial light sources turning on/off against a shifting natural-light baseline.

## 2. Solution: Differential EMA Flank Detection

Two exponential moving averages run in parallel on every ADC sample:

| Filter | Weight | Purpose |
|--------|--------|---------|
| Fast EMA | 40% (400/1000) | Reacts instantly to state changes |
| Trend EMA | 2% (20/1000) | Follows slowly, represents ambient baseline |

The **flank** (pure edge) is the difference between them:
```
flank = FastEMA - TrendEMA
```

A single static threshold on the flank signal triggers state changes — the absolute ADC level is irrelevant.

### 2.1 Algorithm

Each EMA has its own divisor (compile-time constants, not exposed to CLI):

```
FastEMA = ((raw * fastWeight) + (FastEMA * (FAST_DIVISOR - fastWeight))) / FAST_DIVISOR
TrendEMA = ((raw * trendWeight) + (TrendEMA * (TREND_DIVISOR - trendWeight))) / TREND_DIVISOR
flank = FastEMA - TrendEMA

if (!isLightOn && flank >  BASE_THRESHOLD) → isLightOn = true
if ( isLightOn && flank < -BASE_THRESHOLD) → isLightOn = false
```

All arithmetic is `int32_t`. No arrays, no shift registers, no floating point, zero heap.

### 2.2 Why This Works

- A room light toggle produces a sharp step in raw ADC → Fast EMA reacts in ~2-3 samples, Trend EMA barely moves → large positive/negative flank spike.
- Clouds or sunset produce gradual changes → both EMAs track together → flank stays near zero, below threshold.

## 3. Interface (`ILightSensor.h`)

### 3.1 Retained (unchanged contract)

```cpp
virtual bool begin() = 0;
virtual uint16_t readRaw() const = 0;
virtual bool setAttenuation(float volts) = 0;
virtual float getAttenuation() const = 0;
virtual void setRawReadingIntervalMs(uint16_t ms) = 0;
virtual uint16_t getRawReadingIntervalMs() const = 0;
```

### 3.2 Complete Removal Checklist

#### Enums

```cpp
enum class LightZone : uint8_t { DARK, HYSTERESIS_GAP, BRIGHT };  // DELETE
enum class LightState : uint8_t { DARK, BRIGHT };                 // DELETE
```

#### Interface methods (`ILightSensor.h`)

```cpp
virtual bool setThresholds(uint16_t brightToDark, uint16_t darkToBright) = 0;  // DELETE
virtual uint16_t getBrightToDarkThreshold() const = 0;                          // DELETE
virtual uint16_t getDarkToBrightThreshold() const = 0;                          // DELETE
virtual uint16_t readFiltered() = 0;                                            // DELETE
virtual LightZone readZone() = 0;                                               // DELETE
virtual LightState readState() = 0;                                             // DELETE
virtual bool setFilterShift(uint8_t shift) = 0;                                 // DELETE
virtual uint8_t getFilterShift() const = 0;                                     // DELETE
```

#### Compile-time defines (`LightSensor.h`)

```cpp
#define LIGHT_SENSOR_DEFAULT_BTD <value>           // DELETE
#define LIGHT_SENSOR_DEFAULT_DTB <value>           // DELETE
#define LIGHT_SENSOR_FILTER_SHIFT <value>          // DELETE
```

#### Private member variables (`LightSensor.h`)

```cpp
uint16_t brightToDarkThreshold_;    // DELETE
uint16_t darkToBrightThreshold_;    // DELETE
LightState latchedState_;           // DELETE (replaced by bool isLightOn_)
uint8_t filterShift_;               // DELETE
uint32_t filteredAccum_;            // DELETE (shift-based accumulator — gone)
bool seeded_;                       // DELETE (seeding now happens in begin())
```

#### Private helper (`LightSensor.cpp`)

```cpp
static constexpr float kValidAttenuations[] = {1.1f, 1.5f, 2.2f, 3.3f};  // KEEP
void applyAttenuation();  // KEEP
```

#### New compile-time defines (`LightSensor.h`)

```cpp
#define LIGHT_SENSOR_DEFAULT_BASE_THRESHOLD  150          // NEW
#define LIGHT_SENSOR_DEFAULT_FAST_EMA_WEIGHT 400          // NEW
#define LIGHT_SENSOR_DEFAULT_TREND_EMA_WEIGHT 20          // NEW
#define LIGHT_SENSOR_FAST_EMA_DIVISOR  1000               // NEW
#define LIGHT_SENSOR_TREND_EMA_DIVISOR 1000               // NEW
// These stay:
#define LIGHT_SENSOR_DEFAULT_ATTENUATION      3.3f        // KEEP
#define LIGHT_SENSOR_DEFAULT_RAW_READING_INTERVAL_MS 20   // KEEP
```

### 3.3 Added

```cpp
virtual void poll() = 0;                                   // sample ADC, update EMAs, evaluate flank
virtual bool isLightOn() const = 0;                        // current latched state (idempotent)
virtual bool setBaseThreshold(int32_t threshold) = 0;      // single edge-detection threshold
virtual int32_t getBaseThreshold() const = 0;              // for NVS readback
virtual bool setEmaWeights(int32_t fastWeight, int32_t trendWeight) = 0;
virtual int32_t getEmaFastWeight() const = 0;              // for NVS readback
virtual int32_t getEmaTrendWeight() const = 0;             // for NVS readback
// Diagnostic getters for CLI status display:
virtual int32_t getFastEma() const = 0;
virtual int32_t getTrendEma() const = 0;
virtual int32_t getFlank() const = 0;
```

`poll()` samples ADC on interval, updates both EMAs, computes flank, and updates the latched `isLightOn_` state. `isLightOn()` is a const accessor — it does not trigger sampling. The diagnostic getters return the last computed internal values for the CLI status readout.

## 4. Implementation (`LightSensor.h` / `LightSensor.cpp`)

### 4.1 Compile-time defaults

```cpp
#define LIGHT_SENSOR_DEFAULT_BASE_THRESHOLD  150
#define LIGHT_SENSOR_DEFAULT_FAST_EMA_WEIGHT 400
#define LIGHT_SENSOR_DEFAULT_TREND_EMA_WEIGHT 20
#define LIGHT_SENSOR_DEFAULT_ATTENUATION      3.3f
#define LIGHT_SENSOR_DEFAULT_RAW_READING_INTERVAL_MS 20
#define LIGHT_SENSOR_FAST_EMA_DIVISOR  1000
#define LIGHT_SENSOR_TREND_EMA_DIVISOR 1000
```

### 4.2 Constructor (replaces old parameters)

Old constructor — **DELETE all these parameters**:
```cpp
LightSensor(int pin,
            uint16_t brightToDark = LIGHT_SENSOR_DEFAULT_BTD,       // DELETE
            uint16_t darkToBright = LIGHT_SENSOR_DEFAULT_DTB,       // DELETE
            float attenuation = LIGHT_SENSOR_DEFAULT_ATTENUATION,   // KEEP
            uint8_t filterShift = LIGHT_SENSOR_FILTER_SHIFT,        // DELETE
            uint16_t rawReadingIntervalMs = ...);                   // KEEP
```

New constructor:
```cpp
LightSensor(int pin,
            float attenuation = LIGHT_SENSOR_DEFAULT_ATTENUATION,
            uint16_t rawReadingIntervalMs = LIGHT_SENSOR_DEFAULT_RAW_READING_INTERVAL_MS);
```
All other settings (baseThreshold, fastWeight, trendWeight) use their compile-time defaults and are tuned at runtime via CLI / NVS.

Divisors are compile-time only (not runtime-configurable, not exposed to CLI). Weights are runtime-configurable via `setEmaWeights()` and must satisfy `0 < trend < trendDivisor` and `0 < fast < fastDivisor` with `trend/trendDivisor < fast/fastDivisor` (trend must track slower than fast).

### 4.3 Member variables (old → new)

| Old (`LightSensor.h`) | New | Notes |
|-----------------------|-----|-------|
| `pin_` | `pin_` | KEEP |
| `brightToDarkThreshold_` | — | DELETE |
| `darkToBrightThreshold_` | — | DELETE |
| `latchedState_` (LightState) | `isLightOn_` (bool) | REPLACE |
| `attenuationVolts_` | `attenuationVolts_` | KEEP |
| `filterShift_` | — | DELETE |
| `rawReadingIntervalMs_` | `rawReadingIntervalMs_` | KEEP |
| `filteredAccum_` | — | DELETE |
| `lastSampleMs_` | `lastSampleMs_` | KEEP |
| `seeded_` | — | DELETE |
| — | `baseThreshold_` (int32_t) | NEW |
| — | `fastWeight_` (int32_t) | NEW |
| — | `trendWeight_` (int32_t) | NEW |
| — | `fastEma_` (int32_t) | NEW |
| — | `trendEma_` (int32_t) | NEW |
| `kValidAttenuations[]` | `kValidAttenuations[]` | KEEP |

### 4.4 `poll()` behavior

`poll()` is the workhorse: samples ADC on interval, updates EMAs, computes flank, evaluates state:

```cpp
void poll() {
    if (millis() - lastSampleMs_ < rawReadingIntervalMs_) return;
    int32_t raw = analogRead(pin_);
    fastEma_ = ((raw * fastWeight_) + (fastEma_ * (FAST_DIVISOR - fastWeight_))) / FAST_DIVISOR;
    trendEma_ = ((raw * trendWeight_) + (trendEma_ * (TREND_DIVISOR - trendWeight_))) / TREND_DIVISOR;
    int32_t flank = fastEma_ - trendEma_;
    if (!isLightOn_ && flank >  baseThreshold_) isLightOn_ = true;
    if ( isLightOn_ && flank < -baseThreshold_) isLightOn_ = false;
    lastSampleMs_ = millis();
}
```

`isLightOn()` is a const accessor — idempotent, returns `isLightOn_`. This keeps the component's polling pattern compatible with US-0046.

### 4.4 `setEmaWeights(fast, trend)` validation

- Rejects if `fast <= 0` or `fast >= FAST_DIVISOR`
- Rejects if `trend <= 0` or `trend >= TREND_DIVISOR`
- Rejects if `trend/TREND_DIVISOR >= fast/FAST_DIVISOR` (trend must be slower than fast)
- Stores weights directly

## 5. CLI Changes (`debug_cli.cpp`)

| Old command | New command | Notes |
|-------------|-------------|-------|
| `light thresh <BTD> <DTB>` | `light thresh <n>` | Single int32_t threshold |
| `light shift <1-6>` | `light ema <fast> <trend>` | EMA weights (0 < trend < trendDivisor, 0 < fast < fastDivisor, trend must be slower ratio than fast) |
| `light atten <V>` | unchanged | |
| `light interval <ms>` | unchanged | |
| `light status` | updated | New columns: `raw | fastEMA | trendEMA | flank | state | thresh | atten | intv | fastW | trendW` |

The status loop calls `poll()` to update EMAs, then reads diagnostic getters (`getFastEma()`, `getTrendEma()`, `getFlank()`) and `isLightOn()` for display.

## 6. Tests (`test/test_light_sensor/test_main.cpp`)

### 6.1 Ported from current tests

| Test | Adapted to |
|------|-----------|
| `test_readRaw_returns_injected_value` | Unchanged |
| `test_setAttenuation_accepts_valid_values` | Unchanged |
| `test_setAttenuation_rejects_invalid_values` | Unchanged |
| `test_raw_reading_interval_default_and_set` | Unchanged |
| `test_filter_*` series (7 tests) | Removed — different filter formula |

### 6.2 New tests

| Test | What it covers |
|------|---------------|
| `test_begin_seeds_emas_with_first_reading` | Both EMAs = first raw value |
| `test_poll_updates_emas_correctly` | One iteration of EMA math for known inputs |
| `test_flank_computed_as_difference` | flank = fastEma - trendEma |
| `test_light_turns_on_when_flank_exceeds_threshold` | Step up → isLightOn = true |
| `test_light_turns_off_when_flank_below_negative_threshold` | Step down → isLightOn = false |
| `test_light_stays_on_when_flank_between_thresholds` | Sub-threshold holds previous state |
| `test_setBaseThreshold_rejects_negative` | threshold < 0 returns false |
| `test_setBaseThreshold_accepts_valid` | threshold >= 0 returns true |
| `test_setEmaWeights_rejects_zero_or_out_of_range` | 0 or >= 1000 rejected |
| `test_setEmaWeights_rejects_trend_not_slower_than_fast` | trend >= fast rejected |
| `test_setEmaWeights_accepts_valid` | 0 < trend < fast < 1000 accepted |
| `test_default_latch_is_dark` | New sensor starts with isLightOn = false |

### 6.3 Mock strategy

`MockLightSensor` overrides `poll()` to accept an injected raw value, runs the real EMA math, and updates `isLightOn_`. This tests the actual algorithm without Arduino ADC hardware.

## 7. US-0046 Handoff: Baseline on Boot

`isLightOn` always starts **false** (regardless of the ambient reading at boot). The sensor itself has no concept of a "boot baseline" — it only detects changes.

US-0046's `LightSensorComponent::poll()` MUST handle the first reading as a **baseline establishment**, not a transition trigger:

```cpp
void LightSensorComponent::poll() {
    sensor_.poll();
    if (baselineEstablished_) {
        if (sensor_.isLightOn()) {
            // request transition to LIVE
        } else {
            // request transition to READY
        }
    } else {
        // First poll: absorb current state as baseline, do NOT transition.
        baselineEstablished_ = sensor_.isLightOn();
    }
}
```

This means the room can be lit or dark at boot — the first actual toggle (someone leaving and turning the light OFF, or entering and turning it ON) correctly triggers the transition.

## 7. Files Changed

```
lib/LightSensor/ILightSensor.h        — interface rewrite
lib/LightSensor/LightSensor.h         — header rewrite
lib/LightSensor/LightSensor.cpp       — dual-EMA implementation
src/components/cli/debug_cli.cpp      — CLI command adaptation
test/test_light_sensor/test_main.cpp  — test rewrite
```

**No changes** to: `main.cpp`, `system_components.cpp`, `system_components.h`, `component_types.h`, `config.h`, `pinout.md`.

## 8. NVS Readiness

All settings are accessible via typed getters returning `int32_t`, `uint16_t`, or `float`, ready for a future NVS persistence layer:

| Getter | Type | NVS key candidate |
|--------|------|-------------------|
| `getBaseThreshold()` | `int32_t` | `ls_thresh` |
| `getEmaFastWeight()` | `int32_t` | `ls_fastw` |
| `getEmaTrendWeight()` | `int32_t` | `ls_trendw` |
| `getAttenuation()` | `float` | `ls_atten` |
| `getRawReadingIntervalMs()` | `uint16_t` | `ls_intv` |
