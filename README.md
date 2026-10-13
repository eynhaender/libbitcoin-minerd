# minerd

Offener Mining-Daemon in C++20 für ASIC-Steuerplatinen. Erstes Ziel ist der
Antminer S19j Pro (BM1362). Die Chip-Schicht ist so abstrahiert, dass andere
Miner per Profil oder per zusätzlicher Treiberdatei unterstützt werden.
Gegenstelle für Solo-/Heim-Mining ist libbitcoin-server (`bs`) über Stratum.

## Stand

| Bereich | Status |
|---|---|
| Arbeitspipeline (Stratum V1 + BIP310, Merkle, extranonce2, Midstates, Host-Verifikation) | fertig, getestet gegen Referenzwerte |
| BM13xx-Protokoll (CRC5/CRC16, Framing, PLL, Ticket-Mask) | fertig, CRC5 gegen bekannte Frames geprüft |
| Generischer BM13xx-Kettentreiber | fertig, getestet gegen geskriptetes Chipmodell |
| BM1362-Variante (S19j Pro) | **Gerüst, unverifiziert** – braucht UART-Mitschnitt |
| BM1366-Variante | Werte aus öffentlichem Single-Chip-Code, Antminer-Ketten unverifiziert |
| APW12-Netzteil, PIC-Regler | **fail-closed Platzhalter** – braucht I²C-Mitschnitt |
| Simulator (Ketten, Lüfter, PSU, lokaler Pool) | fertig, Daemon läuft komplett ohne Hardware |
| Lüfterregelung, Throttling, Safety-Supervisor, Autotuner | fertig, getestet |
| cgminer-kompatible API (Port 4028, nur lesend) | fertig |
| Stratum V2, GetBlockTemplate/Solo, Hardware-Watchdog, Web-UI | offen |

## Bauen und Testen

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
(cd build && ctest --output-on-failure)

# Kompletter Daemon im Simulator (3 Ketten, lokaler Job-Server, API auf :4028)
./build/minerd --profile profiles/sim.ini
echo '{"command":"summary"}' | nc 127.0.0.1 4028

./build/minerd --list-chips
```

Cross-Compile für die Steuerplatinen:

```sh
cmake -S . -B build-arm -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/armhf-linux.cmake -DMINERD_BUILD_TESTS=OFF
ninja -C build-arm
```

Optional mit libbitcoin-system (zweite SHA256d-Implementierung zum Gegenprüfen):
`-DMINERD_WITH_LIBBITCOIN=ON` (pkg-config muss `libbitcoin-system` finden).

## Gegen libbitcoin-server minen

```ini
[pool]
url  = stratum+tcp://<host-mit-bs>:<stratum-port>
user = <auszahlungsadresse>.<worker>
```

Den Stratum-Port und die Version (V1/V2) des `bs` in dessen
`docs/configuration.md` nachsehen. minerd spricht derzeit Stratum V1.

## Verzeichnisse

```
include/minerd/   öffentliche Schnittstellen (asic/, hal/, power/, board/, work/, pool/, control/, api/)
src/              Implementierungen; chip-spezifisch nur unter src/asic/<familie>/
profiles/         Board-Profile (.ini) – ein Profil pro Modell × Steuerplatine
docs/             ARCHITECTURE.md, PORTING.md (inkl. Mitschnitt-Checkliste)
tests/            Unit- und End-to-End-Tests (ohne externe Abhängigkeiten)
```

## Sicherheit

Ein S19j Pro zieht rund 3 kW. Unverifizierte Chiptreiber starten nur mit
`allow_unverified_chips = true`, unbekannte Netzteil- und Reglerprotokolle
schlagen absichtlich fehl. Erste Tests nur mit einem Hashboard an einem
strombegrenzten Labornetzteil. Siehe docs/PORTING.md, Abschnitt
„Inbetriebnahme-Reihenfolge".

## Lizenz

GNU Affero General Public License v3.0 – siehe [LICENSE](LICENSE).

minerd ist ein unabhängiges Projekt und kein offizieller Bestandteil von
libbitcoin. libbitcoin-server dient als Gegenstelle (Stratum), libbitcoin-system
ist optional einbindbar.
