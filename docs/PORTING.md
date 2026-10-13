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

Ziel: Jeden Wert in `bm1362.cpp` mit eigenem Mitschnitt bestätigen, danach
`traits.verified = true` setzen. Viele Werte sind bereits aus Mujinas
BM13xx-Referenz übernommen (teils aus einem S19j-Pro-Mitschnitt); die Tags im
Code zeigen die Herkunft: `[CAPTURE]`, `[REF]`, `[INFERRED]`, `[DERIVED]`,
`[UNKNOWN]`.

Aufbau: Logic-Analyzer an TX/RX einer Kette zwischen Steuerplatine und
Hashboard (UART, anfangs 115200 Baud, 8N1), zusätzlich I²C (SDA/SCL) zum PIC
bzw. DC-DC und zum APW12. Stock-Firmware kalt starten und bis zum stabilen
Hashen mitschneiden.

| # | Was | Stand (Quelle) | Im eigenen Mitschnitt prüfen |
|---|---|---|---|
| 1 | Chip-ID, Antwortlänge | `13 62 03 00`, 11 Byte (Capture) | ob Chips direkt nach Reset die 9-Byte-Kurzform senden |
| 2 | Antwort-CRC5 und Typbits | CRC5 über Payload + 3 Typbits, gegen veröffentlichte Frames getestet | – |
| 3 | Adressabstand | Schritt 2, Chip 110 = 0xDC (Capture) | Adressen 0x00 … 0xFA |
| 4 | Version-Mask-Register | `0x9000FFFF`, 1× vor, 2× nach Erkennung (Capture) | – |
| 5 | Init-Register | SOFT_RESET, MISC `0xB000C100`, Core-Mailbox (Capture/Ref), Rest abgeleitet | **vollständige Reihenfolge** (Referenz bricht ab) |
| 6 | PLL | FB_DIV 0x10–0xFA, VCO 2000–3200 MHz, Feldlayout (Ref) | Rampenschritte und Endwert der Stock-Firmware |
| 7 | Fast-UART | Register 0x28 = `0x11300000` → 3,125 Mbaud (Capture) | Zeitpunkt in der Sequenz |
| 8 | Jobframe | 82 Byte, Hashes mit umgekehrter Wortreihenfolge (Ref) | an einem mitgeschnittenen Job nachrechnen |
| 9 | Nonce-Antwort | 5-Bit-Job-ID, 3-Bit-Subcore, Chipadresse in Nonce-Bits 24–18 (Capture) | – |
| 10 | HASH_COUNTING_NUMBER | aus Formel abgeleitet (Derived) | **tatsächlicher Wert** |
| 11 | Temperatur | BM1362-ADC über 0xD0/0xDC/0xE4, nicht entschlüsselt | **Sequenz und Umrechnung** |
| 12 | APW12 | unbekannt | **I²C-Protokoll** |
| 13 | PIC | unbekannt | **I²C-Protokoll** |
| 14 | noPIC-Boards | unbekannt | DC-DC-Controller-Typ |
| 15 | Spannungsdomänen | 42 × 3 Chips, kein UART-Relay (Capture) | – |

## Quellen

- Mujina, BM13xx Chip Reference (GPL-3.0-or-later):
  https://github.com/256foundation/mujina/blob/main/mujina-miner/src/asic/bm13xx/REFERENCE.md
- ESP-Miner (Bitaxe, GPL-3.0): https://github.com/bitaxeorg/ESP-Miner
- skot/BM1397 (BM1397/BM1366-Doku): https://github.com/skot/BM1397

Übernommen werden nur Protokollfakten (Registeradressen, Werte, Formate),
kein Code.

## Inbetriebnahme-Reihenfolge auf echter Hardware

1. `--dry-run` mit dem Profil der Platine.
2. Ein einzelnes Hashboard, Labornetzteil mit Strombegrenzung, `[psu] type = none`.
3. Nur Enumeration: `frequency_mhz` = Minimum, Pool `local-test`.
4. Frequenz in kleinen Schritten anheben; Temperaturen im Blick behalten.
5. Alle drei Boards, echte PSU, Pool.
6. Erst danach `[tuning] enabled = true`.
