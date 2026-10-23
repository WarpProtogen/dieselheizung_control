/*
  Lüftersteuerung für die Dieselheizung
  -------------------------------------
  Liest die Datenleitung zwischen Bedienpanel und Steuerplatine NUR MIT (es wird nie gesendet)
  und steuert damit einen Lüfter per PWM.

  Protokoll (siehe docs/protokoll.md):
    UART 9600 Baud, 8N1, invertiert (Ruhepegel LOW), eine Leitung für beide Richtungen.
    20-Byte-Frames:  FA ... FB  = Panel -> Heizung   (Byte 1 = Befehl)
                     EA ... EB  = Heizung -> Panel   (Status)
    Byte 18 = XOR von Byte 1..17.

  Läuft auf:
    - Arduino Uno / Nano / Pro Mini (SoftwareSerial mit invertierter Logik)
    - ESP32 (Hardware-UART mit Invertierung)

  Lüfterverhalten:
    - Nach dem Einschaltbefehl (A1) ist die Steuerung "aktiv".
    - Aktiv: Lüfter läuft, sobald die Heizungstemperatur T_EIN erreicht.
      Zwischen T_EIN und T_VOLL steigt die Drehzahl linear von PWM_MIN auf PWM_MAX.
      Fällt die Temperatur unter T_EIN - HYSTERESE, geht der Lüfter aus.
    - Beim Ausschalten geht der Lüfter aus (Details bei AUS_ERKENNUNG unten).
*/

// ============================ EINSTELLUNGEN ============================

// --- Pins ---
#if defined(ESP32)
const int PIN_BUS_RX = 16;  // Datenleitung (über Spannungsteiler/Pegelwandler!)
const int PIN_LUEFTER = 25;  // PWM-Ausgang zum MOSFET-Gate bzw. Lüfter-PWM-Eingang
#else
const int PIN_BUS_RX = 8;   // Datenleitung (über Spannungsteiler/Pegelwandler!)
const int PIN_LUEFTER = 9;  // PWM-fähiger Pin (Uno/Nano: 3, 5, 6, 9, 10, 11)
#endif

// Am Logic Analyzer war die Leitung invertiert (Ruhe = LOW).
// Hängt zwischen Bus und Arduino ein Transistor oder Optokoppler, dreht sich das
// noch einmal um -> dann hier false eintragen.
const bool BUS_INVERTIERT = true;

// --- Befehle des Panels (Byte 1 im FA-Frame) ---
const uint8_t CMD_EIN = 0xA1;  // in der Aufnahme gesehen
// Der Ausschaltbefehl ist noch NICHT aufgezeichnet. Sobald bekannt, hier eintragen.
// 0x00 = unbekannt, dann greift nur die Ersatzerkennung (siehe unten).
const uint8_t CMD_AUS = 0x00;

// Ersatzerkennung für "aus", solange CMD_AUS unbekannt ist:
//   Statusbyte 3 == 0xF0 (Heizung aus)  oder  Byte 11 == 0x0A (Nachlauf/Abkühlen).
const bool AUS_BEI_NACHLAUF = true;

// Läuft die Heizung schon, wenn der Arduino startet (Status F3, kein Nachlauf),
// gilt die Steuerung trotzdem als aktiv.
const bool AKTIV_WENN_HEIZUNG_LAEUFT = true;

// --- Temperaturbereich ---
// Rohwert aus Byte 7 der Heizungsantwort (in den Aufnahmen 64 kalt ... 110 im Betrieb).
// Die Einheit ist noch nicht kalibriert, darum hier Rohwerte.
const uint8_t T_EIN = 75;      // ab hier läuft der Lüfter an
const uint8_t T_VOLL = 100;    // ab hier volle Drehzahl
const uint8_t HYSTERESE = 3;   // Lüfter aus erst unter T_EIN - HYSTERESE

// --- Lüfter ---
const uint8_t PWM_MIN = 80;    // kleinste Drehzahl, bei der der Lüfter sicher anläuft (0..255)
const uint8_t PWM_MAX = 255;   // volle Drehzahl
// Für einen reinen Ein/Aus-Lüfter (Relais): PWM_MIN = 255 setzen.

// Kommt so lange kein gültiger Frame, wird der Lüfter abgeschaltet (Kabel ab, Panel aus ...).
const unsigned long TIMEOUT_MS = 5000;

const bool DEBUG = true;       // Ausgaben auf dem seriellen Monitor (115200 Baud)

// ========================== ENDE EINSTELLUNGEN ==========================

#if defined(ESP32)
HardwareSerial Bus(2);
#else
#include <SoftwareSerial.h>
// TX-Pin wird nicht benutzt, es wird nie gesendet.
SoftwareSerial Bus(PIN_BUS_RX, 255, BUS_INVERTIERT);
#endif

const uint8_t FRAME_LEN = 20;
uint8_t frame[FRAME_LEN];
uint8_t pos = 0;
unsigned long letzterFrameMs = 0;

bool aktiv = false;       // Heizung gilt als eingeschaltet
bool luefterAn = false;
uint8_t pwmIst = 0;
int temperatur = -1;      // Byte 7, -1 = noch kein Wert
uint8_t status = 0;       // Byte 3
uint8_t phase = 0;        // Byte 11

void luefterSetzen(uint8_t pwm) {
  if (pwm == pwmIst) return;
  pwmIst = pwm;
#if defined(ESP32) && ESP_ARDUINO_VERSION_MAJOR < 3
  ledcWrite(0, pwm);
#else
  analogWrite(PIN_LUEFTER, pwm);
#endif
}

void luefterBerechnen() {
  if (!aktiv || temperatur < 0) {
    luefterAn = false;
  } else if (luefterAn) {
    if (temperatur < (int)T_EIN - HYSTERESE) luefterAn = false;
  } else {
    if (temperatur >= T_EIN) luefterAn = true;
  }

  if (!luefterAn) {
    luefterSetzen(0);
    return;
  }
  long pwm;
  if (temperatur >= T_VOLL) {
    pwm = PWM_MAX;
  } else if (temperatur <= T_EIN) {
    pwm = PWM_MIN;
  } else {
    pwm = PWM_MIN + (long)(PWM_MAX - PWM_MIN) * (temperatur - T_EIN) / (T_VOLL - T_EIN);
  }
  luefterSetzen((uint8_t)pwm);
}

void setzeAktiv(bool neu, const char *grund) {
  if (neu == aktiv) return;
  aktiv = neu;
  if (DEBUG) {
    Serial.print(F("Steuerung "));
    Serial.print(aktiv ? F("AKTIV") : F("INAKTIV"));
    Serial.print(F(" ("));
    Serial.print(grund);
    Serial.println(')');
  }
}

void panelFrame(const uint8_t *f) {
  uint8_t cmd = f[1];
  if (cmd == CMD_EIN) {
    setzeAktiv(true, "Einschaltbefehl");
  } else if (CMD_AUS != 0x00 && cmd == CMD_AUS) {
    setzeAktiv(false, "Ausschaltbefehl");
  } else if (DEBUG && cmd != 0xC0 && cmd != 0xB3) {
    // Unbekannter Befehl -> ausgeben, so findet man z. B. den Ausschaltbefehl.
    Serial.print(F("Unbekannter Panel-Befehl: 0x"));
    Serial.println(cmd, HEX);
  }
}

void heizungFrame(const uint8_t *f) {
  status = f[3];
  temperatur = f[7];
  phase = f[11];

  bool heizungAus = (status == 0xF0);
  bool nachlauf = (phase == 0x0A);

  if (AUS_BEI_NACHLAUF && aktiv && (heizungAus || nachlauf)) {
    setzeAktiv(false, heizungAus ? "Status F0" : "Nachlauf");
  } else if (AKTIV_WENN_HEIZUNG_LAEUFT && !aktiv && status == 0xF3 && !nachlauf) {
    setzeAktiv(true, "Heizung laeuft bereits");
  }

  if (DEBUG) {
    float volt = f[6] + f[5] / 100.0;
    Serial.print(F("Status 0x"));
    Serial.print(status, HEX);
    Serial.print(F("  U="));
    Serial.print(volt, 2);
    Serial.print(F("V  T="));
    Serial.print(temperatur);
    Serial.print(F("  Phase=0x"));
    Serial.print(phase, HEX);
    Serial.print(F("  Luefter PWM="));
    Serial.println(pwmIst);
  }
}

void frameFertig() {
  uint8_t start = frame[0];
  if (frame[FRAME_LEN - 1] != start + 1) return;
  uint8_t x = 0;
  for (uint8_t i = 1; i <= 17; i++) x ^= frame[i];
  if (x != frame[18]) {
    if (DEBUG) Serial.println(F("Pruefsumme falsch"));
    return;
  }
  letzterFrameMs = millis();
  if (start == 0xFA) panelFrame(frame);
  else heizungFrame(frame);
  luefterBerechnen();
}

void byteEmpfangen(uint8_t b) {
  if (pos == 0 && b != 0xFA && b != 0xEA) return;  // auf Frameanfang warten
  frame[pos++] = b;
  if (pos == FRAME_LEN) {
    frameFertig();
    pos = 0;
  }
}

void setup() {
  pinMode(PIN_LUEFTER, OUTPUT);
#if defined(ESP32) && ESP_ARDUINO_VERSION_MAJOR < 3
  ledcSetup(0, 25000, 8);   // 25 kHz, wie bei 4-Pin-PC-Lüftern üblich
  ledcAttachPin(PIN_LUEFTER, 0);
#endif
  luefterSetzen(0);
  pwmIst = 0;

  Serial.begin(115200);
#if defined(ESP32)
  Bus.begin(9600, SERIAL_8N1, PIN_BUS_RX, -1, BUS_INVERTIERT);
#else
  Bus.begin(9600);
#endif
  if (DEBUG) Serial.println(F("Lueftersteuerung gestartet, lese mit ..."));
}

void loop() {
  static unsigned long letztesByteMs = 0;
  while (Bus.available()) {
    unsigned long jetzt = millis();
    // Zwischen zwei Frames liegen mindestens ~200 ms. Längere Pause = neuer Frame.
    if (pos != 0 && jetzt - letztesByteMs > 50) pos = 0;
    letztesByteMs = jetzt;
    byteEmpfangen(Bus.read());
  }

  if (letzterFrameMs != 0 && millis() - letzterFrameMs > TIMEOUT_MS) {
    if (pwmIst != 0 || luefterAn) {
      if (DEBUG) Serial.println(F("Keine Daten mehr -> Luefter aus"));
      luefterAn = false;
      luefterSetzen(0);
    }
  }
}
