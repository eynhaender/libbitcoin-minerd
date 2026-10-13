# Architektur von minerd

## Schichten

```
                ┌──────────────────────────────────────────────────────────┐
  Upstream      │ pool::IPoolClient                                        │
                │   StratumV1Client (Pools, libbitcoin-server `bs`)        │
                │   LocalTestPool   (offline)      [geplant: SV2, GBT/solo]│
                └───────────────▲───────────────────────┬──────────────────┘
                     Share      │                       │ Job
                ┌───────────────┴───────────────────────▼──────────────────┐
  Orchestrator  │ control::Miner                                           │
                │   Dispatcher je Kette ── WorkGenerator (en2, Merkle,     │
                │                                          Midstates)      │
                │   Collector je Kette ── check_share (Host-Verifikation)  │
                │   Control-Loop 1 Hz ── FanController, Throttle, Tuner    │
                │   SafetySupervisor (eigener Thread, fail-closed)         │
                └───────────────┬───────────────────────▲──────────────────┘
                     WorkItem   │                       │ NonceResult
                ┌───────────────▼───────────────────────┴──────────────────┐
  Board         │ board::HashBoard = IAsicChain + IVoltageRegulator        │
                │ board::build_hardware(profile) ← BoardProfile (.ini)     │
                └───────────────┬──────────────────────────────────────────┘
                ┌───────────────▼──────────────────────────────────────────┐
  CHIP-GRENZE   │ asic::IAsicChain  +  asic::ChipTraits  +  ChipRegistry   │
                │   bm13xx::Bm13xxChain ← Bm13xxVariant (BM1362, BM1366…)  │
                │   sim::SimChain                                          │
                │   <anderer Hersteller>::Chain  (direkt IAsicChain)       │
                └───────────────┬──────────────────────────────────────────┘
                ┌───────────────▼──────────────────────────────────────────┐
  HAL / Power   │ hal::ISerialPort/II2cBus/IGpioLine/IFan/ITempSensor      │
                │ power::IPowerSupply (APW12…) / IVoltageRegulator (PIC…)  │
                │ Linux-Backends (termios2, i2c-dev, sysfs, hwmon) + Sim   │
                └──────────────────────────────────────────────────────────┘
```

## Die Chip-Grenze

Alles oberhalb von `IAsicChain` ist chip-agnostisch. Dispatcher, Verifikation,
Pool-Code, Tuner und Safety sehen nur:

| Schnittstelle | Inhalt |
|---|---|
| `ChipTraits` | Name, Chip-ID, Hashes/Takt, Arbeitsformat (Header vs. Midstates), Frequenz- und Spannungsfenster, `verified`-Flag |
| `IAsicChain` | `init`, `ramp_frequency`, `set_chip_frequency`, `set_chip_difficulty`, `set_version_mask`, `shutdown`, `send_work`, `poll_result`, `work_interval`, `effective_chip_difficulty`, optionale Temperaturen |
| `WorkItem` / `NonceResult` | chipneutrale Arbeit rein, dekodierte Nonce mit Host-`work_id` und voller Version raus |

Die Treiber übersetzen in beide Richtungen: Ein BM13xx-Treiber vergibt eigene
4-Bit-Job-IDs und mappt sie zurück auf `work_id`. Ein Chip mit Midstates bekommt
sie über `ChipTraits::work_format` automatisch vom `WorkGenerator`.

Drei Wege, einen neuen Miner zu unterstützen (Details in PORTING.md):

1. **Gleicher Chip, andere Platine/anderes Modell** → nur ein neues Profil (.ini).
2. **Neuer Bitmain-Chip (BM13xx)** → eine `Bm13xxVariant` (Registertabelle +
   Job-/Response-Codec) in einer .cpp-Datei; Transport, Enumeration,
   Adressierung, PLL-Rampe, Job-ID-Verwaltung kommen aus `Bm13xxChain`.
3. **Chip eines anderen Herstellers** → eigene `IAsicChain`-Implementierung,
   registriert in `register_builtin_chips()`.

## Threads und Nebenläufigkeit

| Thread | Aufgabe | Zugriff auf Chain |
|---|---|---|
| Dispatcher (je Kette) | alle `work_interval()` neue Arbeit; sofort bei neuem Job | `send_work`, `set_version_mask` |
| Collector (je Kette) | `poll_result`, Host-Verifikation, Submit | `poll_result`, `effective_chip_difficulty` |
| Control (1 Hz) | Telemetrie, Lüfter, Throttle, Autotuning | `set_chip_frequency`, `read_temperatures` |
| Safety | prüft Snapshots, Heartbeat; schaltet ab | `shutdown` (über `HashBoard::power_down`) |

Vertrag für Treiber: `send_work` und `poll_result` laufen parallel; Setter
können parallel zur Arbeits-I/O kommen; `shutdown()` ist idempotent und von
jedem Thread aus aufrufbar. Der ThreadSanitizer-Build
(`-DCMAKE_CXX_FLAGS=-fsanitize=thread`) läuft ohne Befund.

## Sicherheitskonzept

* **Fail-closed-Treiber:** Unbekannte Protokolle (APW12, PIC) liefern
  `Status::not_implemented` statt still „Erfolg" zu melden.
* **Unverifizierte Chips** (`ChipTraits::verified == false`) werden nur mit
  `allow_unverified_chips = true` gestartet.
* **Reihenfolge beim Start:** Lüfter 100 % → Safety-Supervisor → Control-Loop
  → PSU → Boards einzeln (Spannung → Init → Frequenzrampe) → Pool.
* **Supervisor löst aus bei:** Temperatur ≥ `shutdown_temp_c`, fehlenden
  Temperaturen bei laufenden Boards, Lüfter unter `min_fan_rpm`, PSU-Fehler,
  veralteter Telemetrie (Control-Loop hängt), fehlendem Heartbeat (Main-Loop
  hängt). Danach: Chips aus, Regler aus, PSU aus, Lüfter 100 %.
* **Weich:** Über `max_chip_temp_c` taktet der Control-Loop alle Chips herunter.
* **Noch offen:** Hardware-Watchdog (`/dev/watchdog`) als Rückfallebene, falls
  der ganze Prozess hängt.

## Rolle von libbitcoin

* **Gegenstelle:** libbitcoin-server (`bs`) bietet eine Stratum-Schnittstelle;
  `[pool] url` zeigt darauf. Damit läuft Solo-/Heim-Mining komplett in C++.
* **Im Daemon:** optional (`-DMINERD_WITH_LIBBITCOIN=ON`) als zweite,
  unabhängige SHA256d-Implementierung zum Gegenprüfen. Später: Coinbase-Bau,
  Skripte und Adressen für einen GetBlockTemplate-/Solo-Client.
* Die Midstate-SHA256 bleibt im Daemon, weil Chips rohe Kompressionszustände
  brauchen, die eine Bibliotheks-API meist nicht herausgibt.
* Lizenz: libbitcoin steht unter AGPLv3. Wer es einlinkt, sollte minerd
  ebenfalls unter einer AGPL-kompatiblen Lizenz veröffentlichen.
