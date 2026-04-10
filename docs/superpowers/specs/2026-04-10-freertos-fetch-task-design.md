# FreeRTOS Fetch Task Design

**Date:** 2026-04-10  
**Status:** Approved

---

## Problem

`updateADSBDataRenderSprites()` calls `fetchFlightData()` + `processFlightData()` synchronously on the Arduino loop task (core 1). The TCP connect, HTTP header skip, and JSON stream-parse block the loop for several seconds every 30 seconds, freezing the display, buttons, clock, and MQTT loop.

---

## Solution

Move the fetch + process work to a dedicated FreeRTOS task pinned to core 0. The main loop task (core 1) continues rendering, handling buttons, and running MQTT without interruption. A mutex protects the shared `_flightStats` struct during the brief swap from staging to live.

---

## Architecture

### New globals

| Symbol | Type | Location | Purpose |
|---|---|---|---|
| `_flightDetailsJSONDoc` | `SpiRamJsonDocument(65536)` | PSRAM global | Reused each fetch; never stack-allocated |
| `_flightStatsStaging` | `FlightStats` | global | Written only by fetch task |
| `_flightStatsMutex` | `SemaphoreHandle_t` | global | Guards `_flightStats` during swap and render |
| `_fetchTaskHandle` | `TaskHandle_t` | global | Target for `xTaskNotifyGive` |
| `_fetchInProgress` | `volatile bool` | global | Skip-if-busy guard; prevents re-trigger |

### Fetch task (core 0, stack 8192 bytes)

```
loop:
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY)   // sleep until notified
  _fetchInProgress = true
  fetchFlightData(host, path, port, _flightDetailsJSONDoc)
  processFlightData(_flightDetailsJSONDoc, _flightStatsStaging)  // writes staging, not _flightStats
  xSemaphoreTake(_flightStatsMutex, portMAX_DELAY)
  _flightStats = _flightStatsStaging         // C++ assignment; handles String members
  xSemaphoreGive(_flightStatsMutex)
  _fetchInProgress = false
  goto loop
```

### Main loop changes

**Timer trigger** (replaces direct `updateADSBDataRenderSprites()` call):
```cpp
if (_runCurrent - _runDataUpdate >= UPDATE_ADSBS_INTERVAL_MILLISECS || _forceUpdate) {
    if (!_fetchInProgress) {
        xTaskNotifyGive(_fetchTaskHandle);
        _runDataUpdate = millis();
    }
    _forceUpdate = false;
}
```

**Render guard** — `updateADSBDataRenderSprites()` wraps its `_flightStats` reads:
```cpp
xSemaphoreTake(_flightStatsMutex, portMAX_DELAY);
// ... all render calls that read _flightStats ...
xSemaphoreGive(_flightStatsMutex);
```

The mutex is held for the entire render pass (not per-field) to prevent partial reads.

---

## Data flow

```
core 0 fetch task:
  fetchFlightData() → _flightDetailsJSONDoc (PSRAM)
  processFlightData() → _flightStatsStaging
  [mutex] _flightStats = _flightStatsStaging [/mutex]

core 1 main loop (render):
  [mutex] read _flightStats → build sprites [/mutex]
  push sprites to AMOLED
```

---

## Key decisions

- **Skip if busy:** If the 30s timer fires while a fetch is in progress, the cycle is skipped. `_runDataUpdate` is not updated so the next loop iteration will immediately re-trigger once `_fetchInProgress` clears.
- **No immediate render on data ready:** New data takes effect on the next natural render cycle (up to 10s via `UPDATE_UI_FRAME_INTERVAL_MILLISECS`). No extra signalling needed.
- **PSRAM doc is global/persistent:** Avoids per-cycle heap churn; 64KB reservation is negligible against 8MB PSRAM.
- **Assignment not memcpy:** `FlightStats` contains `String` members — C++ assignment operator is used for the swap, not `memcpy`.
- **`processFlightData` gets a target parameter:** Signature changes from `void processFlightData(SpiRamJsonDocument&)` to `void processFlightData(SpiRamJsonDocument&, FlightStats&)` so the task can write to staging without touching the live struct.

---

## Files affected

| File | Change |
|---|---|
| `include/merlinFlightStats.h` | Add staging global, PSRAM doc global, mutex handle, task handle, `_fetchInProgress`; change `processFlightData` signature; move `_flightDetailsJSONDoc` from stack to global |
| `src/main.cpp` | Add `fetchTask()` function; add `xTaskCreatePinnedToCore` in `setup()`; update loop timer block; wrap render reads with mutex; create mutex in `setup()` |
