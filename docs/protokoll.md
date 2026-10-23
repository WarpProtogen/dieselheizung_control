# Protokoll Bedienpanel ⇄ Steuerplatine (aus den Logic-Analyzer-Aufnahmen)

Ausgewertet: `einschalten.sal` (665 s, Kaltstart) und `betrieb.sal` (848 s, laufender Betrieb bis Abschalten).
Decoder: `python3 tools/decode_sal.py <datei.sal> [--alle]`

## Physikalische Schicht

| Eigenschaft | Wert |
|---|---|
| Leitung | eine Datenleitung (Kanal 0), beide Seiten senden abwechselnd (Halbduplex) |
| Kodierung | **UART 8N1, 9600 Baud** (Bitzeit ≈ 104 µs, gemessen 2500 Samples bei 24 MHz) |
| Polarität | **invertiert**: Ruhepegel LOW, Startbit HIGH (am Messpunkt) |
| Byteabstand | ≈ 0,2 Bit Pause zwischen den Bytes |

**Kein LIN.** Der in der Aufnahme eingestellte LIN-Analyzer (20 kBit/s) passt nicht; in Logic 2 stattdessen
„Async Serial“, 9600 Baud, 8 Bit, keine Parität, 1 Stoppbit, **„Inverted“** einstellen.

## Rahmen

Jeder Frame hat 20 Bytes:

```
Byte 0   Startkennung   FA = Panel → Heizung,  EA = Heizung → Panel
Byte 1   Befehl (beim Panel) bzw. Echo des Befehls (bei der Heizung)
Byte 2–17  Nutzdaten (16 Bytes)
Byte 18  Prüfsumme = XOR von Byte 1 bis 17
Byte 19  Endkennung    FB bzw. EB  (= Startkennung + 1)
```

Ablauf: Das Panel sendet ca. **jede Sekunde** einen FA-Frame, die Heizung antwortet **≈ 230 ms** später mit einem EA-Frame.
Alle 3145 Frames beider Aufnahmen haben eine korrekte Prüfsumme.

## Befehle des Panels (Byte 1 im FA-Frame, Nutzdaten immer 00)

| Code | Vorkommen | Vermutung |
|---|---|---|
| `C0` | fast jeder Frame | Statusabfrage / Polling |
| `B3` | ca. alle 62 s | periodische Zusatzabfrage oder Keep-Alive (Antwort sieht aus wie bei C0) |
| `A1` | einmal, bei 17,3 s in `einschalten` | **Einschalten** – direkt danach wechselt der Status F0 → F2 → F3 |

Ein Ausschaltbefehl taucht in den Aufnahmen nicht auf (siehe unten).

## Antwort der Heizung (EA-Frame)

| Byte | Beispiel | Bedeutung | Sicherheit |
|---|---|---|---|
| 1 | C0 / B3 / A1 | Echo des Panel-Befehls | sicher |
| 2 | 05 | konstant | ? |
| 3 | F0 / F2 / F3 | Gerätestatus: F0 = aus/Standby, F2 = Start angenommen, F3 = in Betrieb | wahrscheinlich |
| 4 | 00 | konstant | ? |
| 5–6 | 03 0C | **Bordspannung**: Byte 6 = Volt, Byte 5 = Hundertstel → 12,03 V; sinkt beim Glühen auf ~11,7 V | sehr wahrscheinlich |
| 7 | 40 … 6E | Temperatur 1, steigt im Betrieb langsam, fällt beim Abkühlen | wahrscheinlich Temperatur (Einheit/Offset offen) |
| 8–10 | 1E 50 3C (30, 80, 60) | konstant – vermutlich Einstell-/Konfigwerte | ? |
| 11 | 00, 0B/0C, 09, 0E→15, 0D/0A | Betriebsphase oder Leistungsstufe (00 = aus, 15 = Volllast, 0A = Nachlauf) | Vermutung |
| 12 | 40 … 6D | Temperatur 2, läuft Byte 7 hinterher | wahrscheinlich Temperatur |
| 13 | 00, 0F, Rampe bis 3B, später 5A | Gebläse- oder Pumpenstellwert (Rampe beim Start, 5A im Betrieb, fällt beim Abschalten auf 0) | Vermutung |
| 14 | 06 … 1F | steigt erst nach erfolgreicher Zündung deutlich, fällt im Nachlauf → vermutlich Brennkammer-/Abgastemperatur | Vermutung |
| 15–16 | 05 3C | konstant | ? |
| 17 | 00 | konstant | ? |

## Was in den Aufnahmen passiert

**einschalten.sal**
- 0–17 s: Heizung aus (Status F0), 12,0 V.
- 17,3 s: Panel sendet `A1` → Status F2, kurz darauf F3.
- ab ~34 s: Byte 13 = 0F, ab ~119 s Rampe bis 3B → erster Startversuch; Spannung sinkt (Glühkerze).
- ~283–311 s: Byte 13 fällt auf 0, Byte 11 = 09, Byte 14 steigt kaum → erster Zündversuch offenbar **fehlgeschlagen**.
- ab ~407 s: zweiter Versuch, Byte 14 steigt auf 1D, Byte 13 = 5A, Temperaturen steigen → Heizung läuft.

**betrieb.sal**
- 0–615 s: Normalbetrieb (Byte 11 = 15, Byte 13 = 5A), Temperaturen steigen von 0x50 auf 0x6D.
- 615 s: Byte 11 springt auf 0D/0A, Byte 13 fährt herunter auf 0, Byte 14 und Temperaturen sinken → **Abschalten/Nachlauf**.
  Dabei schickt das Panel nur `C0`, also keinen eigenen Ausschaltbefehl. Entweder hat die Heizung selbst abgeregelt
  (z. B. Temperatur erreicht) oder ausgeschaltet wurde an anderer Stelle.

## Offene Punkte

- Wie Temperatur-Sollwert und Ausschalten übertragen werden: dazu eine Aufnahme machen, bei der am Panel
  nacheinander Ausschalten, Sollwert hoch/runter und ggf. Modus gedrückt wird (mit Zeitnotizen).
- Wer welche Seite treibt: ein zweiter Kanal vor/hinter einem Serienwiderstand oder eine Analogmessung würde das bestätigen.
