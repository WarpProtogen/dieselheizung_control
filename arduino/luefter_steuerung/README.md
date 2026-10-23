# Lüftersteuerung (Arduino-Sketch)

Der Sketch liest die Leitung zwischen Bedienpanel und Steuerplatine nur mit und steuert einen Lüfter per PWM.
Auf den Bus wird nie gesendet.

## Verhalten
- Nach dem Einschaltbefehl `A1` wird die Steuerung aktiv. Das passiert auch, wenn die Heizung beim Start des Arduino schon läuft.
- Ab dem Temperatur-Rohwert `T_EIN` läuft der Lüfter mit `PWM_MIN`. Bis `T_VOLL` steigt die Drehzahl linear auf `PWM_MAX`.
  Unter `T_EIN - HYSTERESE` geht der Lüfter wieder aus.
- Beim Ausschalten geht der Lüfter aus. Der Ausschaltbefehl ist noch nicht aufgezeichnet (`CMD_AUS = 0x00`).
  Bis dahin dient als Ersatz Status `F0` oder der Beginn des Nachlaufs (Byte 11 = `0A`).
- Kommt 5 s lang kein gültiger Frame, geht der Lüfter aus.

## Getestet
Die Logik wurde am PC mit beiden Aufnahmen durchgespielt, auf echter Hardware aber noch nicht:
- `einschalten.sal`: Bei 17 s wird die Steuerung aktiv, ab ca. 642 s (T = 75) läuft der Lüfter an.
- `betrieb.sal`: Der Lüfter läuft mit der Temperatur hoch bis auf volle Drehzahl und geht bei 617 s (Nachlauf) aus.

## Anschluss (wichtig)
- **Pegel der Datenleitung zuerst messen.** Liegen mehr als 5 V (beim ESP32 3,3 V) an, braucht es einen Spannungsteiler
  oder einen Optokoppler. Den Bus nie direkt an den Pin legen.
- Ein Transistor oder Optokoppler im Eingang dreht die Polarität um. Dann `BUS_INVERTIERT = false` setzen.
- GND des Arduino mit GND der Heizung verbinden.
- 12-V-Lüfter über einen Logic-Level-MOSFET mit Freilaufdiode anschließen, einen 4-Pin-PC-Lüfter direkt über seinen PWM-Eingang.
- Uno/Nano: Bus an D8, Lüfter an D9. ESP32: Bus an GPIO16, Lüfter an GPIO25.

## Kalibrieren
`T_EIN`/`T_VOLL` sind Rohwerte aus Byte 7. In den Aufnahmen liegt der Wert kalt bei 64 und im Betrieb bei bis zu 110.
Die Werte im seriellen Monitor (115200 Baud) zeigen, was bei der gewünschten Temperatur ankommt.
