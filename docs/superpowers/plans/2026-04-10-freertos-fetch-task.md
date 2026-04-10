# FreeRTOS Fetch Task Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move ADS-B data fetching and processing off the Arduino loop task onto a dedicated FreeRTOS task pinned to core 0, so the display, buttons, clock, and MQTT loop on core 1 never freeze.

**Architecture:** A `fetchTask` function runs on core 0, sleeping via `ulTaskNotifyTake`. The main loop's 30-second timer triggers it with `xTaskNotifyGive` instead of calling the fetch directly. A mutex protects `_flightStats` during the swap from the staging struct (written by the fetch task) to the live struct (read by renderers).

**Tech Stack:** ESP32-S3, FreeRTOS (built into ESP-IDF/Arduino), ArduinoJson, WiFiClient, TFT_eSPI, espressif32@6.6.0 (Arduino < 3.0.0)

---

## Files

| File | Change |
|---|---|
| `include/merlinFlightStats.h` | Move `_flightDetailsJSONDoc` from stack-local to PSRAM global; add `_flightStatsStaging`; change `processFlightData` to accept a `FlightStats&` target; add `_flightStatsMutex`, `_fetchTaskHandle`, `_fetchInProgress` declarations |
| `src/main.cpp` | Add `fetchTask()` function; create mutex + task in `setup()`; replace timer-triggered `updateADSBDataRenderSprites()` call with `xTaskNotifyGive`; wrap `_flightStats` reads in render path with mutex; fix `_button2` double-click attachment |

---

## Task 1: Make `processFlightData` write to a target parameter

**Files:**
- Modify: `include/merlinFlightStats.h` — `processFlightData` signature and body

Currently `processFlightData(SpiRamJsonDocument &doc)` writes directly into the global `_flightStats`. Change it to accept an explicit `FlightStats&` target so the fetch task can write into `_flightStatsStaging` instead.

- [ ] **Step 1: Change the signature**

In `include/merlinFlightStats.h`, find:
```cpp
void processFlightData(SpiRamJsonDocument &doc)
{
```
Replace with:
```cpp
void processFlightData(SpiRamJsonDocument &doc, FlightStats &target)
{
```

- [ ] **Step 2: Replace all `_flightStats.` references inside the function body with `target.`**

The function body contains these writes — change every `_flightStats.` to `target.`:
- `_flightStats.totalAircraft`
- `_flightStats.highestAircraft`
- `_flightStats.lowestAircraft`
- `_flightStats.fastestAircraft`
- `_flightStats.slowestAircraft`
- `_flightStats.closestAircraft`
- `_flightStats.farthestAircraft`
- `_flightStats.emergencyCount`
- `_flightStats.emergencyAircraft[...]`
- `_flightStats.aircraft[...]`
- `_flightStats.avgAltitude`
- `_flightStats.avgSpeed`

Also the `isSquawkEmergency` call at line ~194 references `_flightStats.emergencyCount` — that is inside `isSquawkEmergency()` itself and refers to the wrong field (it should be the squawk code parameter). Do not change that; it is a pre-existing bug outside scope.

- [ ] **Step 3: Fix the existing call site in `updateFlightStats()` in `src/main.cpp`**

Find in `src/main.cpp`:
```cpp
    if (fetchFlightData(host, path, port, _flightDetailsJSONDoc))
    {
      processFlightData(_flightDetailsJSONDoc);
```
Replace with:
```cpp
    if (fetchFlightData(host, path, port, _flightDetailsJSONDoc))
    {
      processFlightData(_flightDetailsJSONDoc, _flightStats);
```
(This call site will be removed in Task 3, but it must compile cleanly first.)

- [ ] **Step 4: Verify it compiles**

```bash
cd "e:/GoogleDrive/Arduino/esp32_ADSB_AMOLED"
pio run -e T-Display-AMOLED 2>&1 | tail -20
```
Expected: `SUCCESS` with no errors. Fix any compile errors before continuing.

- [ ] **Step 5: Commit**

```bash
git add include/merlinFlightStats.h src/main.cpp
git commit -m "refactor: processFlightData accepts explicit FlightStats target"
```

---

## Task 2: Promote `_flightDetailsJSONDoc` and add staging/sync globals

**Files:**
- Modify: `include/merlinFlightStats.h`

Move the JSON document from a stack-local in `updateFlightStats()` to a PSRAM global. Add the staging struct and synchronisation primitives.

- [ ] **Step 1: Add globals to `merlinFlightStats.h`**

Find the block after the `SpiRamAllocator` / `SpiRamJsonDocument` typedef (around line 24), immediately before `const char* host = ...`. Insert:

```cpp
// --- FreeRTOS fetch-task globals ---
// PSRAM-backed JSON document, allocated once at boot. 64KB is negligible
// against the 8MB PSRAM and avoids per-cycle heap churn.
SpiRamJsonDocument _flightDetailsJSONDoc(65536);

// Staging struct — written exclusively by the fetch task on core 0.
// Swapped into _flightStats under mutex when a fetch completes.
FlightStats _flightStatsStaging;

// Mutex protecting _flightStats during swap (fetch task) and render reads (loop task).
SemaphoreHandle_t _flightStatsMutex = nullptr;

// Handle to the fetch task — used by the main loop to send task notifications.
TaskHandle_t _fetchTaskHandle = nullptr;

// Set true while the fetch task is running; prevents re-triggering.
volatile bool _fetchInProgress = false;
// --- end FreeRTOS fetch-task globals ---
```

- [ ] **Step 2: Remove the stack-local `SpiRamJsonDocument` from `updateFlightStats()` in `src/main.cpp`**

Find in `src/main.cpp` inside `updateFlightStats()`:
```cpp
    SpiRamJsonDocument _flightDetailsJSONDoc(65536);

    DisplayOut("Parsing flight data");
    if (fetchFlightData(host, path, port, _flightDetailsJSONDoc))
```
Replace with:
```cpp
    DisplayOut("Parsing flight data");
    if (fetchFlightData(host, path, port, _flightDetailsJSONDoc))
```
(The global `_flightDetailsJSONDoc` defined in `merlinFlightStats.h` is now used.)

- [ ] **Step 3: Verify it compiles**

```bash
cd "e:/GoogleDrive/Arduino/esp32_ADSB_AMOLED"
pio run -e T-Display-AMOLED 2>&1 | tail -20
```
Expected: `SUCCESS`. Fix any duplicate-definition errors — if `_flightDetailsJSONDoc` appears in multiple translation units, add `extern` declarations as needed.

- [ ] **Step 4: Commit**

```bash
git add include/merlinFlightStats.h src/main.cpp
git commit -m "refactor: promote JSON doc to PSRAM global, add staging and sync primitives"
```

---

## Task 3: Add `fetchTask` and wire it into `setup()`

**Files:**
- Modify: `src/main.cpp`

Add the FreeRTOS task function and create it in `setup()`.

- [ ] **Step 1: Add `fetchTask()` to `src/main.cpp`**

Add this function immediately before `setup()` (around line 1500):

```cpp
// FreeRTOS task — runs on core 0. Sleeps until notified by the main loop,
// then fetches + processes flight data into the staging struct, swaps it
// into _flightStats under mutex, then sleeps again.
void fetchTask(void *pvParameters)
{
  for (;;)
  {
    // Block indefinitely until xTaskNotifyGive() is called from the main loop.
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    if (WiFi.status() != WL_CONNECTED)
    {
      DEBUG_PRINTLN("fetchTask: WiFi not connected, skipping");
      _fetchInProgress = false;
      continue;
    }

    DEBUG_PRINTLN("fetchTask: fetching flight data");

    if (fetchFlightData(host, path, port, _flightDetailsJSONDoc))
    {
      processFlightData(_flightDetailsJSONDoc, _flightStatsStaging);

      // Swap staging into live struct under mutex so the render path
      // on core 1 never sees a partially-updated _flightStats.
      xSemaphoreTake(_flightStatsMutex, portMAX_DELAY);
      _flightStats = _flightStatsStaging;
      xSemaphoreGive(_flightStatsMutex);

      DEBUG_PRINTLN("fetchTask: data updated");
    }
    else
    {
      DEBUG_PRINTLN("fetchTask: fetchFlightData failed, keeping previous data");
    }

    _fetchInProgress = false;
  }
}
```

- [ ] **Step 2: Create the mutex and task in `setup()`**

Find in `setup()`:
```cpp
  DisplayOut("Free Heap Memory: " + String(ESP.getFreeHeap()));
  DisplayOut("Initialisation complete");
```
Insert before those two lines:

```cpp
  // Create the mutex that guards _flightStats between core 0 (fetch) and core 1 (render).
  _flightStatsMutex = xSemaphoreCreateMutex();

  // Create the fetch task on core 0. Stack 8192 bytes is sufficient —
  // JSON streaming writes into the global PSRAM doc, not the task stack.
  xTaskCreatePinnedToCore(
    fetchTask,        // task function
    "fetchTask",      // name (debug)
    8192,             // stack bytes
    nullptr,          // parameter
    1,                // priority (same as loop task)
    &_fetchTaskHandle,// handle — used by loop to notify
    0                 // core 0
  );
```

- [ ] **Step 3: Verify it compiles**

```bash
cd "e:/GoogleDrive/Arduino/esp32_ADSB_AMOLED"
pio run -e T-Display-AMOLED 2>&1 | tail -20
```
Expected: `SUCCESS`.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat: add FreeRTOS fetchTask on core 0"
```

---

## Task 4: Replace blocking fetch trigger in the main loop

**Files:**
- Modify: `src/main.cpp`

Replace the timer-triggered `updateADSBDataRenderSprites()` call (which blocked while fetching) with a task notification. Keep the render portion of `updateADSBDataRenderSprites()` but remove `updateFlightStats()` from it.

- [ ] **Step 1: Remove `updateFlightStats()` from `updateADSBDataRenderSprites()`**

Find in `src/main.cpp`:
```cpp
  DisplayOut("Updating ADSB departures");

  updateLocalTime();
  updateFlightStats();

  DisplayOut("Found " + String(_flightStats.totalAircraft) + " aircraft");

  _runDataUpdate = millis(); 
```
Replace with:
```cpp
  DisplayOut("Updating ADSB departures");

  updateLocalTime();

  DisplayOut("Found " + String(_flightStats.totalAircraft) + " aircraft");
```
(`_runDataUpdate` is now set in the loop trigger, not here.)

- [ ] **Step 2: Replace the loop trigger**

Find in `loop()`:
```cpp
    if (_runCurrent - _runDataUpdate >= UPDATE_ADSBS_INTERVAL_MILLISECS || _forceUpdate)
    {
      updateADSBDataRenderSprites();
      _forceUpdate = true;
    }
```
Replace with:
```cpp
    if (_runCurrent - _runDataUpdate >= UPDATE_ADSBS_INTERVAL_MILLISECS || _forceUpdate)
    {
      if (!_fetchInProgress)
      {
        _fetchInProgress = true;
        _runDataUpdate = millis();
        xTaskNotifyGive(_fetchTaskHandle);
      }
      _forceUpdate = false;
    }
```
Note: `_forceUpdate = false` is set regardless of whether we notified, so the force is consumed. `_runDataUpdate` is only updated when we actually notify (not when skipping due to busy).

- [ ] **Step 3: Fix the `_button2` double-click attachment**

In `setup()`, find:
```cpp
  _button2.attachDoubleClick(updateADSBDataRenderSprites);
```
`updateADSBDataRenderSprites` no longer fetches data. Replace with a small lambda or wrapper that triggers the task notification. Add this helper function near `advanceFrame()`:

```cpp
void triggerFetchFromButton()
{
  if (_fetchTaskHandle != nullptr && !_fetchInProgress)
  {
    _fetchInProgress = true;
    _runDataUpdate = millis();
    xTaskNotifyGive(_fetchTaskHandle);
  }
}
```

Then in `setup()` replace:
```cpp
  _button2.attachDoubleClick(updateADSBDataRenderSprites);
```
with:
```cpp
  _button2.attachDoubleClick(triggerFetchFromButton);
```

- [ ] **Step 4: Verify it compiles**

```bash
cd "e:/GoogleDrive/Arduino/esp32_ADSB_AMOLED"
pio run -e T-Display-AMOLED 2>&1 | tail -20
```
Expected: `SUCCESS`.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "feat: replace blocking fetch with task notification in loop and button handler"
```

---

## Task 5: Protect `_flightStats` reads in the render path with the mutex

**Files:**
- Modify: `src/main.cpp`

The render path reads `_flightStats` in two places: `updateADSBDataRenderSprites()` (builds sprites) and the frame switch in `loop()`. Wrap both with the mutex.

- [ ] **Step 1: Wrap `updateADSBDataRenderSprites()` body with mutex**

Find `updateADSBDataRenderSprites()`. It currently looks like:
```cpp
void updateADSBDataRenderSprites()
{
  clearSprite();
  _amoled.pushColors(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, (uint16_t *)_mainSprite.getPointer());

  DisplayOut("Updating ADSB departures");

  updateLocalTime();

  DisplayOut("Found " + String(_flightStats.totalAircraft) + " aircraft");

  int __maxrenderEmergencies = min(_flightStats.emergencyCount, MAXRENDER_EMERGENCIES);
  for (byte i = 0; i < __maxrenderEmergencies; i++)
  {
    DisplayOut("Rendering emergency #" + String(i));
    RenderEmergencySprite(i);
  }

  DisplayOut("Rendering overview screen");
  RenderAircraftToSprite(_overviewStatSprite, _flightStats.aircraft[_flightStats.closestAircraft]);

  DisplayOut("Rendering general statistics");
  RenderGeneralStatsSprite();

  DisplayOut("Rendering Map Sprite");
  renderMap(_mapSprite);
}
```

Replace with:
```cpp
void updateADSBDataRenderSprites()
{
  clearSprite();
  _amoled.pushColors(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, (uint16_t *)_mainSprite.getPointer());

  DisplayOut("Updating ADSB departures");

  updateLocalTime();

  xSemaphoreTake(_flightStatsMutex, portMAX_DELAY);

  DisplayOut("Found " + String(_flightStats.totalAircraft) + " aircraft");

  int __maxrenderEmergencies = min(_flightStats.emergencyCount, MAXRENDER_EMERGENCIES);
  for (byte i = 0; i < __maxrenderEmergencies; i++)
  {
    DisplayOut("Rendering emergency #" + String(i));
    RenderEmergencySprite(i);
  }

  DisplayOut("Rendering overview screen");
  RenderAircraftToSprite(_overviewStatSprite, _flightStats.aircraft[_flightStats.closestAircraft]);

  DisplayOut("Rendering general statistics");
  RenderGeneralStatsSprite();

  DisplayOut("Rendering Map Sprite");
  renderMap(_mapSprite);

  xSemaphoreGive(_flightStatsMutex);
}
```

- [ ] **Step 2: Wrap the `_flightStats.totalAircraft` check in `loop()` with mutex**

Find in `loop()`:
```cpp
      if (_flightStats.totalAircraft == 0)
      {
        renderEmpty();
        _amoled.pushColors(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, (uint16_t *)_mainSprite.getPointer());
      }
      else
      {

        switch (_currentFrame)
        {
        case 0:
          DEBUG_PRINTLN("Pushing System Info sprite to main sprite");
          renderSystemInfo();
          _skipDrawClock = true;
          break;
        case 1:
          DEBUG_PRINTLN("Pushing Overview sprite to main sprite");
          _overviewStatSprite.pushToSprite(&_mainSprite, 0, 0);
          _skipDrawClock = false;
          break;
        case 2:
          DEBUG_PRINTLN("Pushing TopStat sprite to main sprite");
          _topStatSprite.pushToSprite(&_mainSprite, 0, 0);
          _skipDrawClock = true;
          break;
        case 3:
          DEBUG_PRINTLN("Pushing map to main sprite");
          _mapSprite.pushToSprite(&_mainSprite, 0, 0);
          _skipDrawClock = true;
          break;
        default:
          if (_currentFrame > 3 && _currentFrame < 7 && _currentFrame < _flightStats.emergencyCount + 3)
          {
            DEBUG_PRINTLN("Pushing Emergency sprite [" + String(_currentSubFrame - 4) + "] to main sprite");
            _emergencySprite[_currentFrame - 4].pushToSprite(&_mainSprite, 0, 0);
            _skipDrawClock = false;
          }
          break;
        }
      }
```

Wrap the entire block with the mutex — take before the `if`, give after the closing `}`:
```cpp
      xSemaphoreTake(_flightStatsMutex, portMAX_DELAY);

      if (_flightStats.totalAircraft == 0)
      {
        renderEmpty();
        _amoled.pushColors(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, (uint16_t *)_mainSprite.getPointer());
      }
      else
      {

        switch (_currentFrame)
        {
        case 0:
          DEBUG_PRINTLN("Pushing System Info sprite to main sprite");
          renderSystemInfo();
          _skipDrawClock = true;
          break;
        case 1:
          DEBUG_PRINTLN("Pushing Overview sprite to main sprite");
          _overviewStatSprite.pushToSprite(&_mainSprite, 0, 0);
          _skipDrawClock = false;
          break;
        case 2:
          DEBUG_PRINTLN("Pushing TopStat sprite to main sprite");
          _topStatSprite.pushToSprite(&_mainSprite, 0, 0);
          _skipDrawClock = true;
          break;
        case 3:
          DEBUG_PRINTLN("Pushing map to main sprite");
          _mapSprite.pushToSprite(&_mainSprite, 0, 0);
          _skipDrawClock = true;
          break;
        default:
          if (_currentFrame > 3 && _currentFrame < 7 && _currentFrame < _flightStats.emergencyCount + 3)
          {
            DEBUG_PRINTLN("Pushing Emergency sprite [" + String(_currentSubFrame - 4) + "] to main sprite");
            _emergencySprite[_currentFrame - 4].pushToSprite(&_mainSprite, 0, 0);
            _skipDrawClock = false;
          }
          break;
        }
      }

      xSemaphoreGive(_flightStatsMutex);
```

- [ ] **Step 3: Verify it compiles**

```bash
cd "e:/GoogleDrive/Arduino/esp32_ADSB_AMOLED"
pio run -e T-Display-AMOLED 2>&1 | tail -20
```
Expected: `SUCCESS`.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat: guard _flightStats reads with mutex in render path"
```

---

## Task 6: Verify end-to-end on device

- [ ] **Step 1: Flash and monitor**

```bash
cd "e:/GoogleDrive/Arduino/esp32_ADSB_AMOLED"
pio run -e T-Display-AMOLED --target upload && pio device monitor --baud 115200
```

- [ ] **Step 2: Confirm fetch task wakes on schedule**

Watch serial output. Every ~30 seconds you should see:
```
fetchTask: fetching flight data
...
fetchTask: data updated
```
The display clock and frame transitions should continue uninterrupted during this period.

- [ ] **Step 3: Confirm double-click button still triggers a fetch**

Double-press button 2. Expect to see `fetchTask: fetching flight data` in serial output within 1–2 seconds.

- [ ] **Step 4: Confirm skip-if-busy works**

If you can trigger two fetches back-to-back (double-click rapidly), the second should be silently skipped (no second `fetchTask: fetching` line until the first completes).

- [ ] **Step 5: Final commit**

```bash
git add -A
git commit -m "feat: FreeRTOS fetch task complete — non-blocking ADS-B data updates"
```
