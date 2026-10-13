# Portierung auf neue Hardware

## A. Neues Modell mit bekanntem Chip → nur ein Profil

Beispiel: `profiles/example_bm1366_s19kpro.ini` nutzt denselben Daemon mit dem
BM1366-Treiber. Anzupassen sind:

* `[board] platform` (`beaglebone`, `zynq`, `amlogic`, `linux-generic`, `sim`)
* `[chain.N] chip`, `chips`, `uart`, `reset_gpio`, `regulator`, `voltage_v`,
  `frequency_mhz`
* `[psu] type` und Adressen, `[fan.N]`, `[temp.N]`, `[safety]`

Test ohne Strom: `minerd --profile X.ini --dry-run` öffnet alle Geräte und baut
die Treiberobjekte, schaltet aber nichts ein.

## B. Neuer Bitmain-Chip (BM13xx-Familie)

1. Neue Datei `src/asic/bm13xx/bmXXXX.cpp` nach dem Muster von `bm1362.cpp`:
   von der nächsten bekannten Variante kopieren und Abweichungen setzen.
2. In `variants.hpp` eine Funktion `const Bm13xxVariant& bmXXXX();` deklarieren
   und in `builtin_variants()` (bm1366.cpp) eintragen.
3. Datei in `CMakeLists.txt` aufnehmen.
4. Test im Stil von `bm13xx_chain_enumerates_and_maps_results` mit einem
   `ScriptedSerial`, der echte, mitgeschnittene Antworten abspielt.

Felder, die pro Chip typischerweise abweichen:

| Feld | Bedeutung |
|---|---|
| `traits.chip_id`, `hashes_per_clock`, Frequenzfenster | Identität und Leistung |
| `init_registers` | Registersequenz nach der Adressierung |
| `response_len`, `decode_response` | Antwortformat, Typ-Flag, Chip-Adresse in der Nonce |
| `encode_job`, `job_id_count/step` | Jobformat und Job-ID-Raum |
| `version_mask_value`, `hash_counting_value`, `fast_uart` | Spezialregister |
| `pll` (`PllLimits`) | Teiler- und VCO-Grenzen |

## C. Chip eines anderen Herstellers

`IAsicChain` direkt implementieren (siehe `sim_chain.cpp` als minimales Beispiel)
und in `register_builtin_chips()` eintragen:

```cpp
r.add("XYZ123", [](ChainContext c) { return xyz::make_chain(std::move(c)); }, xyz::traits());
```

Der Rest des Daemons bleibt unverändert. `ChainContext::options` liefert alle
Schlüssel aus `[chain.N]`, so dass herstellerspezifische Einstellungen ohne
Änderung am Profil-Parser möglich sind.

## D. Neue Steuerplatine

Meist reicht ein Profil mit den richtigen Gerätepfaden. Code ist nur nötig,
wenn eine Ressource nicht über Standard-Linux erreichbar ist (z. B. Lüfter-PWM
in FPGA-Registern): dann eine neue `hal::IFan`-/`ISerialPort`-Implementierung
und eine Zeile in `board/platform.cpp`.

## Mitschnitt-Checkliste S19j Pro (BM1362)

Ziel: Jede `VERIFY`-Markierung in `bm1362.cpp` und in den S19j-Pro-Profilen
durch gemessene Werte ersetzen, danach `traits.verified = true` setzen.

Aufbau: Logic-Analyzer an TX/RX einer Kette zwischen Steuerplatine und
Hashboard (UART, anfangs 115200 Baud, 8N1). Zusätzlich I²C (SDA/SCL) zum
PIC bzw. DC-DC und zum APW12. Stock-Firmware einmal kalt starten und bis zum
stabilen Hashen mitschneiden.

| # | Was | Wo im Code |
|---|---|---|
| 1 | Antwort auf `55 AA 52 05 00 00 0A` → Chip-ID, Antwortlänge | `traits.chip_id`, `response_len` |
| 2 | Registersequenz nach der Adressierung | `init_registers` |
| 3 | Adressabstand (126 Chips → 2?) | wird berechnet; prüfen |
| 4 | Ticket-Mask- und Version-Mask-Werte | `reg_ticket_mask`, `version_mask_value` |
| 5 | PLL-Schritte der Frequenzrampe | `pll`, `pll_register_value` |
| 6 | Wechsel auf schnelle UART (Register + Wert) | `fast_uart` |
| 7 | Jobframe: Länge, Feldreihenfolge, Byte-Order | `encode_job` |
| 8 | Nonce-Antwort: Job-ID-Bits, Version-Bits, Chip-Adresse, Typ-Flag | `decode_response` |
| 9 | Temperatursensoren über ASIC-I²C-Passthrough | `IAsicChain::read_temperatures` (BM13xx: TODO) |
| 10 | APW12: Einschalten, Spannung setzen, Rücklesen, Fehlerbits | `power.cpp` `Apw12Psu` |
| 11 | PIC: Enable, Spannung, Heartbeat (falls vorhanden) | `power.cpp` `PicRegulator` |
| 12 | noPIC-Boards: DC-DC-Controller-Typ | neuer Regulator `dcdc-direct` |

## Inbetriebnahme-Reihenfolge auf echter Hardware

1. `--dry-run` mit dem Profil der Platine.
2. Ein einzelnes Hashboard, Labornetzteil mit Strombegrenzung, `[psu] type = none`.
3. Nur Enumeration: `frequency_mhz` = Minimum, Pool `local-test`.
4. Frequenz in kleinen Schritten anheben; Temperaturen im Blick behalten.
5. Alle drei Boards, echte PSU, Pool.
6. Erst danach `[tuning] enabled = true`.
