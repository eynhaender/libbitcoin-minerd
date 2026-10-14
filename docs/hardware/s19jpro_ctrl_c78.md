# S19j Pro – Steuerplatine Ctrl_C78 (BeagleBone-Variante)

Beobachtet an einem eigenen Gerät (Fotos, 2026-10-13). Nur Sichtprüfung, noch
keine Messungen.

## Identifikation

| Teil | Aufschrift | Anmerkung |
|---|---|---|
| Rechenmodul (schwarz, aufgesteckt) | Aufkleber `Model: Ctrl_C78`, `Vers: V2.6010` | Bitmain-Bezeichnung |
| | `W/O: HQDZ2022060…` | Fertigungsauftrag, vermutlich Juni 2022 |
| | `SN: HQDZC78BB…` | „BB" passt zur BeagleBone-Ausführung |
| | Bedruckung `BeagleBone…V2.6` | Bitmain-Nachbau im BeagleBone-Format (TI-AM335x-Klasse), kein Original-BeagleBone |
| Trägerplatine (grün) | `S19J_IO_BOARD_V2_0`, Bitmain | Anschlussplatine |

Zum Vergleich: Die bei Altair angebotene Ersatzplatine ist „S19j Pro Vers 2.3"
(SKU BBLDTCS19JPRO2012), also eine ältere Revision. Pinbelegungen können
zwischen den Revisionen abweichen.

## Aufbau

- Das Modul steckt mit zwei 2×23-Stiftleisten (BeagleBone-Format) auf der IO-Platine.
- microSD-Slot auf dem Modul (Boot von SD möglich).
- IO-Platine:
  - Hashboard-Anschlüsse: weiße 2-reihige Stecker mit Flachbandkabeln, unten rechts.
    Neben dem Stecker für Kette 3 stehen Signalnamen wie `CH3_RX…` und `CH3_TX…`
    (genaue Beschriftung auf dem Foto nicht vollständig lesbar).
  - Lüfter: vier 4-polige Stecker unten links, beschriftet `FAN…` (z. B. `FAN2`).
  - Stromversorgung und vermutlich Netzteil-Steuerleitung: Kabelbündel links.

## Offene Punkte

| # | Was | Vermutung | Wie prüfen |
|---|---|---|---|
| 1 | Debug-UART | senkrechte Reihe von 5–6 Löchern auf dem Modul, rechts der Mitte; auf der Original-BeagleBone-Black liegt dort J1 (Pin 1 GND, Pin 4 RX, Pin 5 TX) | Durchgang zu GND (Strom aus), ~3,3 V und Aktivität beim Booten auf TX (Strom an) |
| 2 | weiterer Kandidat | drei goldene Lötpunkte links auf der IO-Platine | wie oben |
| 3 | UART-Geräte der Ketten | `/dev/ttyO*` bzw. `/dev/ttyS*` | über die Debug-Konsole: `dmesg`, `/proc/tty/driver/` |
| 4 | Pinbelegung der Hashboard-Stecker | CH-Labels auf der IO-Platine | Nahaufnahme der Bedruckung, Durchgangsmessung |
| 5 | Hashboard-Typ (PIC/noPIC), Netzteil | – | Aufkleber Hashboard (BHB…) und APW12 |
| 6 | Firmware-Version | – | Web-Oberfläche, Firmware-Upgrade-Seite |
