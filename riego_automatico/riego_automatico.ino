/*
 * ============================================================
 *  SISTEMA DE RIEGO AUTOMATICO - Arduino Mega 2560
 * ============================================================
 *  Hardware:
 *    - Arduino Mega 2560
 *    - Modulo MH-Real-Time Clock Module-2 (chip DS1302)
 *        CLK → A1  (pin analogico, usado como GPIO)
 *        DAT → A2  (pin analogico, usado como GPIO)
 *        RST → A3  (pin analogico, usado como GPIO)
 *        VCC → 5V
 *        GND → GND
 *    - LCD 16x2 I2C (PCF8574, direccion 0x27)
 *        SDA → pin 20
 *        SCL → pin 21
 *        VCC → 5V
 *        GND → GND
 *    - 16 bombas de agua 12V (via transistores NPN), pines 2-17
 *    - Botonera matriz 4x4
 *        Filas   (OUTPUT):       R1=28, R2=30, R3=32, R4=34
 *        Columnas (INPUT_PULLUP): C1=40, C2=42, C3=26, C4=38
 *
 * ============================================================
 *  CONFIGURACION DE CADA BOMBA
 * ============================================================
 *  Edita la tabla "configBomba" mas abajo.
 *  Cada fila corresponde a una bomba:
 *    { hora_activacion, minuto_activacion, minutos_de_riego }
 *
 *  Ejemplo:
 *    Bomba 1 → se activa a las 06:00 y riega 10 minutos
 *    Bomba 2 → se activa a las 07:30 y riega 5 minutos
 *    ...
 *
 * ============================================================
 *  LOGICA
 * ============================================================
 *  - Cada bomba se activa a su propia hora configurada.
 *  - Solo una bomba puede estar encendida a la vez.
 *  - Si la hora de una bomba llega mientras otra esta activa,
 *    queda en cola y se activa cuando la actual termine.
 *  - MANUAL: presionar boton enciende esa bomba por su tiempo.
 *    Presionar de nuevo la apaga. Cancela cualquier cola.
 *
 * ============================================================
 *  COMANDOS Monitor Serial (9600 baudios):
 *    SET    → configurar fecha y hora del RTC
 *    HORA   → ver hora actual
 *    ESTADO → ver configuracion y estado de bombas
 * ============================================================
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// LCD 16x2 I2C — direccion 0x27 (la mas comun para PCF8574)
// Si no enciende, probar 0x3F
LiquidCrystal_I2C lcd(0x3F, 16, 2);

// ============================================================
//  PINES DS1302  —  MH-Real-Time Clock Module-2
//  Pines GPIO puros (sin perifericos compartidos)
//  CLK → A1 (pin 55) | DAT → A2 (pin 56) | RST → A3 (pin 57)
// ============================================================
#define DS1302_CLK  A1
#define DS1302_DAT  A2
#define DS1302_RST  A3

// ============================================================
//  PINES BOMBAS Y BOTONERA
//  Bombas: pines 2 al 17 (16 bombas)
//  Botonera: matriz 4x4 — orden conector fisico izq→der: C4,C3,C2,C1,R1,R2,R3,R4
//    Columnas (INPUT_PULLUP): C1=36, C2=42, C3=40, C4=38
//    Filas    (OUTPUT):       R1=28, R2=30, R3=34, R4=32
// ============================================================
const uint8_t PIN_BOMBA[16] = { 2,  3,  4,  5,  6,  7,  8,  9,
                                10, 11, 12, 13, 14, 15, 16, 17};

const uint8_t MATRIZ_FILAS[4]    = {28, 30, 34, 32};       // R1=28, R2=30, R3=34, R4=32
const uint8_t MATRIZ_COLUMNAS[4] = {36, 42, 40, 38};       // C1=36, C2=42, C3=40, C4=38

// Mapeo matriz → bomba (fila*4 + columna, 0-indexed)
// Tecla [R1,C1]=bomba 0 ... [R4,C4]=bomba 15
//   R1: bombas  1- 4  →  indices 0- 3
//   R2: bombas  5- 8  →  indices 4- 7
//   R3: bombas  9-12  →  indices 8-11
//   R4: bombas 13-16  →  indices 12-15

// ============================================================
//  CONFIGURACION POR BOMBA
//  Formato: { hora (0-23), minuto (0-59), duracion en minutos }
//
//  *** EDITA ESTOS VALORES SEGUN TU INSTALACION ***
// ============================================================
struct ConfigBomba {
  uint8_t  horaActivacion;
  uint8_t  minutoActivacion;
  uint16_t duracionMinutos;
};

ConfigBomba configBomba[16] = {
  {17,  0,  3},   // Bomba  1
  {17, 15,  3},   // Bomba  2
  {17, 30,  3},   // Bomba  3
  {17, 45,  3},   // Bomba  4
  {18,  0,  3},   // Bomba  5
  {18, 15,  3},   // Bomba  6
  {18, 30,  3},   // Bomba  7
  {18, 45,  3},   // Bomba  8
  {19,  0,  3},   // Bomba  9
  {19, 15,  3},   // Bomba 10
  {19, 30,  3},   // Bomba 11
  {19, 45,  3},   // Bomba 12
  {20,  0,  3},   // Bomba 13
  {20, 15,  3},   // Bomba 14
  {20, 30,  3},   // Bomba 15
  {20, 45,  3},   // Bomba 16
};

// ============================================================
//  ESTRUCTURA RTC
// ============================================================
struct TiempoRTC {
  uint16_t anio;
  uint8_t  mes, dia, hora, minuto, segundo;
};

// ============================================================
//  DRIVER DS1302 BIT-A-BIT (sin librerias externas)
// ============================================================
uint8_t decToBcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }
uint8_t bcdToDec(uint8_t v) { return ((v >> 4) * 10) + (v & 0x0F); }

void ds1302_writeByte(uint8_t dato) {
  pinMode(DS1302_DAT, OUTPUT);
  for (uint8_t i = 0; i < 8; i++) {
    digitalWrite(DS1302_DAT, dato & 0x01);
    dato >>= 1;
    digitalWrite(DS1302_CLK, HIGH); delayMicroseconds(2);
    digitalWrite(DS1302_CLK, LOW);  delayMicroseconds(2);
  }
}

uint8_t ds1302_readByte() {
  uint8_t dato = 0;
  pinMode(DS1302_DAT, INPUT);
  for (uint8_t i = 0; i < 8; i++) {
    // El DS1302 presenta el bit cuando CLK esta en LOW
    digitalWrite(DS1302_CLK, LOW);  delayMicroseconds(2);
    if (digitalRead(DS1302_DAT)) dato |= (1 << i);
    digitalWrite(DS1302_CLK, HIGH); delayMicroseconds(2);
  }
  digitalWrite(DS1302_CLK, LOW); delayMicroseconds(2);
  return dato;
}

void ds1302_writeReg(uint8_t reg, uint8_t valor) {
  digitalWrite(DS1302_CLK, LOW);
  digitalWrite(DS1302_RST, LOW);  delayMicroseconds(4);
  digitalWrite(DS1302_RST, HIGH); delayMicroseconds(4);
  ds1302_writeByte(reg);
  ds1302_writeByte(valor);
  digitalWrite(DS1302_CLK, HIGH); delayMicroseconds(2);
  digitalWrite(DS1302_RST, LOW);  delayMicroseconds(4);
}

uint8_t ds1302_readReg(uint8_t reg) {
  uint8_t valor;
  digitalWrite(DS1302_CLK, LOW);
  digitalWrite(DS1302_RST, LOW);  delayMicroseconds(4);
  digitalWrite(DS1302_RST, HIGH); delayMicroseconds(4);
  ds1302_writeByte(reg | 0x01);
  valor = ds1302_readByte();
  digitalWrite(DS1302_CLK, HIGH); delayMicroseconds(2);
  digitalWrite(DS1302_RST, LOW);  delayMicroseconds(4);
  return valor;
}

void ds1302_init() {
  pinMode(DS1302_CLK, OUTPUT);
  pinMode(DS1302_RST, OUTPUT);
  pinMode(DS1302_DAT, OUTPUT);
  digitalWrite(DS1302_CLK, LOW);
  digitalWrite(DS1302_RST, LOW);
  digitalWrite(DS1302_DAT, LOW);
  delay(10);
  ds1302_writeReg(0x8E, 0x00);          // desproteger escritura
  uint8_t seg = ds1302_readReg(0x80);
  ds1302_writeReg(0x80, seg & 0x7F);    // limpiar bit CH (clock halt)
}

void ds1302_setDateTime(TiempoRTC &t) {
  ds1302_writeReg(0x8E, 0x00);
  ds1302_writeReg(0x80, decToBcd(t.segundo) & 0x7F);
  ds1302_writeReg(0x82, decToBcd(t.minuto));
  ds1302_writeReg(0x84, decToBcd(t.hora) & 0x3F);
  ds1302_writeReg(0x86, decToBcd(t.dia));
  ds1302_writeReg(0x88, decToBcd(t.mes));
  ds1302_writeReg(0x8C, decToBcd(t.anio % 100));
  ds1302_writeReg(0x8E, 0x80);
}

TiempoRTC ds1302_getDateTime() {
  TiempoRTC t;
  t.segundo = bcdToDec(ds1302_readReg(0x80) & 0x7F);
  t.minuto  = bcdToDec(ds1302_readReg(0x82) & 0x7F);
  t.hora    = bcdToDec(ds1302_readReg(0x84) & 0x3F);
  t.dia     = bcdToDec(ds1302_readReg(0x86) & 0x3F);
  t.mes     = bcdToDec(ds1302_readReg(0x88) & 0x1F);
  t.anio    = 2000 + bcdToDec(ds1302_readReg(0x8C));
  return t;
}

bool ds1302_esValido(TiempoRTC &t) {
  return (t.hora <= 23 && t.minuto <= 59 && t.segundo <= 59 &&
          t.mes  >= 1  && t.mes   <= 12  &&
          t.dia  >= 1  && t.dia   <= 31  &&
          t.anio >= 2020);
}

// ============================================================
//  ESTADO GLOBAL
// ============================================================

// Bomba actualmente encendida (-1 = ninguna)
int8_t  bombaEncendida  = -1;
bool    esManual        = false;
unsigned long inicioEncendido = 0;

// Cola: bomba que esta esperando para encenderse cuando la
// actual termine. -1 = nadie en cola.
int8_t  colaEspera      = -1;

// Para no relanzar el automatico en el mismo minuto
bool    disparadoEsteMinuto[16] = {};
uint8_t ultimoMinuto            = 255;

// Anti-rebote
unsigned long ultimoRebote[16] = {};
const uint16_t DEBOUNCE_MS     = 200;

// ============================================================
//  CONTROL DE BOMBAS
// ============================================================

void apagarActual() {
  if (bombaEncendida >= 0) {
    digitalWrite(PIN_BOMBA[bombaEncendida], HIGH);  // HIGH = apagado (logica inversa)
    Serial.print(F(">> Bomba ")); Serial.print(bombaEncendida + 1);
    Serial.println(F(" APAGADA"));
    bombaEncendida = -1;
    esManual       = false;
  }
}

void encender(uint8_t idx, bool manual) {
  apagarActual();
  // Apagado de seguridad: garantiza que ninguna otra bomba este encendida
  for (uint8_t j = 0; j < 16; j++) digitalWrite(PIN_BOMBA[j], HIGH); // HIGH = apagado
  digitalWrite(PIN_BOMBA[idx], LOW);                                  // LOW  = encendido
  bombaEncendida  = idx;
  esManual        = manual;
  inicioEncendido = millis();
  Serial.print(F(">> Bomba ")); Serial.print(idx + 1);
  Serial.print(manual ? F(" [MANUAL]") : F(" [AUTO]  "));
  Serial.print(F(" ENCENDIDA — "));
  Serial.print(configBomba[idx].duracionMinutos);
  Serial.println(F(" min"));
}

// ============================================================
//  CONFIGURACION INTERACTIVA DEL RELOJ
// ============================================================
enum EstadoConfig {
  NORMAL,
  ESPERANDO_ANIO, ESPERANDO_MES,    ESPERANDO_DIA,
  ESPERANDO_HORA, ESPERANDO_MINUTO, ESPERANDO_SEGUNDO
};
EstadoConfig estadoConfig = NORMAL;
int cfg_anio, cfg_mes, cfg_dia, cfg_hora, cfg_minuto, cfg_segundo;
String bufferSerial = "";

void imprimirTiempo(TiempoRTC &t) {
  Serial.print(t.anio);                                    Serial.print(F("/"));
  if (t.mes    < 10) Serial.print(F("0")); Serial.print(t.mes);    Serial.print(F("/"));
  if (t.dia    < 10) Serial.print(F("0")); Serial.print(t.dia);    Serial.print(F("  "));
  if (t.hora   < 10) Serial.print(F("0")); Serial.print(t.hora);   Serial.print(F(":"));
  if (t.minuto < 10) Serial.print(F("0")); Serial.print(t.minuto); Serial.print(F(":"));
  if (t.segundo< 10) Serial.print(F("0")); Serial.println(t.segundo);
}

void imprimirHoraActual() {
  TiempoRTC t = ds1302_getDateTime();
  Serial.print(F("Hora RTC: "));
  imprimirTiempo(t);
}

void imprimirEstado() {
  imprimirHoraActual();
  Serial.println(F("------------------------------------------"));
  Serial.println(F("Bomba | Hora   | Duracion | Estado"));
  Serial.println(F("------------------------------------------"));
  for (uint8_t i = 0; i < 16; i++) {
    Serial.print(F("  "));
    Serial.print(i + 1); Serial.print(F("   | "));
    if (configBomba[i].horaActivacion   < 10) Serial.print(F("0"));
    Serial.print(configBomba[i].horaActivacion);
    Serial.print(F(":"));
    if (configBomba[i].minutoActivacion < 10) Serial.print(F("0"));
    Serial.print(configBomba[i].minutoActivacion);
    Serial.print(F("  | "));
    Serial.print(configBomba[i].duracionMinutos);
    Serial.print(F(" min    | "));
    if (bombaEncendida == i)
      Serial.print(esManual ? F("ACTIVA-MANUAL") : F("ACTIVA-AUTO "));
    else if (colaEspera == i)
      Serial.print(F("EN COLA      "));
    else
      Serial.print(F("apagada      "));
    Serial.println();
  }
  Serial.println(F("------------------------------------------"));
}

void repreguntarConfig() {
  switch (estadoConfig) {
    case ESPERANDO_ANIO:    Serial.print(F("Anio (ej: 2026): ")); break;
    case ESPERANDO_MES:     Serial.print(F("Mes (1-12): "));      break;
    case ESPERANDO_DIA:     Serial.print(F("Dia (1-31): "));      break;
    case ESPERANDO_HORA:    Serial.print(F("Hora (0-23): "));     break;
    case ESPERANDO_MINUTO:  Serial.print(F("Minuto (0-59): "));   break;
    case ESPERANDO_SEGUNDO: Serial.print(F("Segundo (0-59): "));  break;
    default: break;
  }
}

void iniciarConfiguracionRTC() {
  estadoConfig = ESPERANDO_ANIO;
  Serial.println();
  Serial.println(F("============================================"));
  Serial.println(F("   CONFIGURACION DEL RELOJ (RTC DS1302)    "));
  Serial.println(F("============================================"));
  Serial.println(F("Ingresa cada valor y presiona Enter."));
  Serial.println();
  Serial.print(F("Anio (ej: 2026): "));
}

void procesarLineaConfig(String linea) {
  linea.trim();
  if (linea.length() == 0) return;

  for (uint8_t k = 0; k < linea.length(); k++) {
    if (!isDigit(linea[k])) {
      Serial.println(F("[!] Solo numeros."));
      repreguntarConfig(); return;
    }
  }
  int v = linea.toInt();

  switch (estadoConfig) {
    case ESPERANDO_ANIO:
      if (v >= 2020 && v <= 2099) {
        cfg_anio = v; estadoConfig = ESPERANDO_MES;
        Serial.print(F("  -> Anio: ")); Serial.println(cfg_anio);
        Serial.print(F("Mes (1-12): "));
      } else { Serial.println(F("[!] Anio invalido (2020-2099).")); repreguntarConfig(); }
      break;
    case ESPERANDO_MES:
      if (v >= 1 && v <= 12) {
        cfg_mes = v; estadoConfig = ESPERANDO_DIA;
        Serial.print(F("  -> Mes: ")); Serial.println(cfg_mes);
        Serial.print(F("Dia (1-31): "));
      } else { Serial.println(F("[!] Mes invalido (1-12).")); repreguntarConfig(); }
      break;
    case ESPERANDO_DIA:
      if (v >= 1 && v <= 31) {
        cfg_dia = v; estadoConfig = ESPERANDO_HORA;
        Serial.print(F("  -> Dia: ")); Serial.println(cfg_dia);
        Serial.print(F("Hora (0-23): "));
      } else { Serial.println(F("[!] Dia invalido (1-31).")); repreguntarConfig(); }
      break;
    case ESPERANDO_HORA:
      if (v >= 0 && v <= 23) {
        cfg_hora = v; estadoConfig = ESPERANDO_MINUTO;
        Serial.print(F("  -> Hora: ")); Serial.println(cfg_hora);
        Serial.print(F("Minuto (0-59): "));
      } else { Serial.println(F("[!] Hora invalida (0-23).")); repreguntarConfig(); }
      break;
    case ESPERANDO_MINUTO:
      if (v >= 0 && v <= 59) {
        cfg_minuto = v; estadoConfig = ESPERANDO_SEGUNDO;
        Serial.print(F("  -> Minuto: ")); Serial.println(cfg_minuto);
        Serial.print(F("Segundo (0-59): "));
      } else { Serial.println(F("[!] Minuto invalido (0-59).")); repreguntarConfig(); }
      break;
    case ESPERANDO_SEGUNDO:
      if (v >= 0 && v <= 59) {
        cfg_segundo = v;
        Serial.print(F("  -> Segundo: ")); Serial.println(cfg_segundo);
        TiempoRTC nuevo = { (uint16_t)cfg_anio, (uint8_t)cfg_mes,    (uint8_t)cfg_dia,
                            (uint8_t)cfg_hora,  (uint8_t)cfg_minuto, (uint8_t)cfg_segundo };
        ds1302_writeReg(0x8E, 0x00);
        ds1302_setDateTime(nuevo);
        delay(50);
        TiempoRTC v2 = ds1302_getDateTime();
        Serial.println();
        if (ds1302_esValido(v2)) {
          Serial.println(F("OK - Reloj configurado:"));
          Serial.print(F("  ")); imprimirTiempo(v2);
        } else {
          Serial.println(F("[!] ERROR: DS1302 no responde."));
          Serial.println(F("    Verifica: CLK=A1  DAT=A2  RST=A3"));
        }
        Serial.println(F("============================================"));
        Serial.println();
        estadoConfig  = NORMAL;
        ultimoMinuto  = 255;  // reset para no bloquear primer disparo
        for (uint8_t i = 0; i < 16; i++) disparadoEsteMinuto[i] = false;
      } else { Serial.println(F("[!] Segundo invalido (0-59).")); repreguntarConfig(); }
      break;
    default: break;
  }
}

// ============================================================
//  LECTURA SERIAL NO BLOQUEANTE
// ============================================================
void leerSerial() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (bufferSerial.length() > 0) {
        if (estadoConfig != NORMAL) {
          procesarLineaConfig(bufferSerial);
        } else {
          bufferSerial.trim();
          bufferSerial.toUpperCase();
          if      (bufferSerial == "SET")    iniciarConfiguracionRTC();
          else if (bufferSerial == "HORA")   imprimirHoraActual();
          else if (bufferSerial == "ESTADO") imprimirEstado();
          else    Serial.println(F("Comandos: SET | HORA | ESTADO"));
        }
        bufferSerial = "";
      }
    } else {
      bufferSerial += c;
    }
  }
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(9600);

  // LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print(F("Riego Automatico"));
  lcd.setCursor(0, 1);
  lcd.print(F("Iniciando...    "));

  for (uint8_t i = 0; i < 16; i++) {
    pinMode(PIN_BOMBA[i], OUTPUT);
    digitalWrite(PIN_BOMBA[i], HIGH);  // HIGH = apagado en reposo (logica inversa)
  }
  // Botonera matriz 4x4: filas como OUTPUT alto, columnas como INPUT_PULLUP
  for (uint8_t r = 0; r < 4; r++) {
    pinMode(MATRIZ_FILAS[r], OUTPUT);
    digitalWrite(MATRIZ_FILAS[r], HIGH);  // reposo en HIGH
  }
  for (uint8_t c = 0; c < 4; c++) {
    pinMode(MATRIZ_COLUMNAS[c], INPUT_PULLUP);
  }

  ds1302_init();

  Serial.println();
  Serial.println(F("============================================"));
  Serial.println(F("    SISTEMA DE RIEGO AUTOMATICO v5.0       "));
  Serial.println(F("============================================"));

  TiempoRTC horaActual = ds1302_getDateTime();

  if (!ds1302_esValido(horaActual)) {
    Serial.println(F("[!] Reloj sin hora valida. Configure la hora:"));
    iniciarConfiguracionRTC();
    unsigned long t0 = millis();
    while (estadoConfig != NORMAL) {
      leerSerial();
      // Timeout de seguridad: si pasan 60s sin input, continuar
      // para que el log serial no quede completamente mudo.
      if (millis() - t0 > 60000UL) {
        estadoConfig = NORMAL;
        Serial.println(F("[!] Timeout config — use SET para configurar el reloj."));
        break;
      }
    }
  } else {
    Serial.print(F("Reloj OK: ")); imprimirTiempo(horaActual);
    Serial.println(F("Comandos: SET | HORA | ESTADO"));
  }

  Serial.println(F("--------------------------------------------"));
  Serial.println(F("Programacion de riego:"));
  for (uint8_t i = 0; i < 16; i++) {
    Serial.print(F("  Bomba ")); Serial.print(i + 1); Serial.print(F(": "));
    if (configBomba[i].horaActivacion   < 10) Serial.print(F("0"));
    Serial.print(configBomba[i].horaActivacion);   Serial.print(F(":"));
    if (configBomba[i].minutoActivacion < 10) Serial.print(F("0"));
    Serial.print(configBomba[i].minutoActivacion); Serial.print(F("  —  "));
    Serial.print(configBomba[i].duracionMinutos);  Serial.println(F(" min"));
  }
  Serial.println(F("============================================"));
}

// ============================================================
//  LOOP PRINCIPAL
// ============================================================
void loop() {
  unsigned long ahora_ms = millis();

  leerSerial();
  if (estadoConfig != NORMAL) return;

  TiempoRTC ahora = ds1302_getDateTime();

  // ----------------------------------------------------------
  //  A. RESET DEL MARCADOR AL CAMBIAR DE MINUTO
  // ----------------------------------------------------------
  if (ahora.minuto != ultimoMinuto) {
    ultimoMinuto = ahora.minuto;
    for (uint8_t i = 0; i < 16; i++) disparadoEsteMinuto[i] = false;
  }

  // ----------------------------------------------------------
  //  B. APAGADO POR TIEMPO
  //     Si la bomba activa cumplió su tiempo:
  //       - Apagar.
  //       - Si hay una bomba en cola (auto), encenderla.
  // ----------------------------------------------------------
  if (bombaEncendida >= 0) {
    unsigned long duracion = (unsigned long)configBomba[bombaEncendida].duracionMinutos
                             * 60UL * 1000UL;
    if (ahora_ms - inicioEncendido >= duracion) {
      apagarActual();

      // Encender la que estaba en cola (si existe)
      if (colaEspera >= 0) {
        uint8_t siguiente = colaEspera;
        colaEspera = -1;
        encender(siguiente, false);
      }
    }
  }

  // ----------------------------------------------------------
  //  C. DETECCION DE HORA DE DISPARO AUTOMATICO
  //     Revisa las 6 bombas cada minuto.
  //     Si coincide la hora y aun no se disparo hoy:
  //       - Si no hay bomba activa → encender.
  //       - Si hay bomba activa → poner en cola (ultima en llegar).
  //         (si ya habia una en cola, la nueva la reemplaza
  //          solo si tiene prioridad mayor — menor indice)
  // ----------------------------------------------------------
  for (uint8_t i = 0; i < 16; i++) {
    if (!disparadoEsteMinuto[i] &&
        ahora.hora   == configBomba[i].horaActivacion &&
        ahora.minuto == configBomba[i].minutoActivacion) {

      disparadoEsteMinuto[i] = true;

      if (bombaEncendida < 0) {
        // Ninguna activa → encender directamente
        encender(i, false);
      } else {
        // Hay una activa → encolar
        colaEspera = i;
        Serial.print(F(">> Bomba ")); Serial.print(i + 1);
        Serial.println(F(" en cola (espera que termine la actual)"));
      }
    }
  }

  // ----------------------------------------------------------
  //  D. BOTONERA MATRIZ 4x4
  //     Escaneo por filas: baja una fila a LOW, lee columnas.
  //     Tecla presionada → columna lee LOW.
  //     Mapeo: bomba = fila*4 + columna  (0-indexed)
  //     Toggle: misma bomba activa → apagar | otra → encender
  // ----------------------------------------------------------
  for (uint8_t r = 0; r < 4; r++) {
    digitalWrite(MATRIZ_FILAS[r], LOW);   // activar fila
    delayMicroseconds(50);                // estabilizar

    for (uint8_t c = 0; c < 4; c++) {
      uint8_t idx = r * 4 + c;           // indice de bomba 0-15

      if (digitalRead(MATRIZ_COLUMNAS[c]) == LOW) {
        if (ahora_ms - ultimoRebote[idx] > DEBOUNCE_MS) {
          ultimoRebote[idx] = ahora_ms;

          if (bombaEncendida == idx) {
            // Misma bomba activa → apagar y limpiar cola
            colaEspera = -1;
            apagarActual();
          } else {
            // Otra o ninguna → tomar control manual
            colaEspera = -1;
            encender(idx, true);
          }
        }
      }
    }

    digitalWrite(MATRIZ_FILAS[r], HIGH);  // desactivar fila
  }

  // ----------------------------------------------------------
  //  E. LOG CADA 10 SEGUNDOS  +  LCD CADA SEGUNDO
  // ----------------------------------------------------------
  static unsigned long ultimoPrint = 0;
  static unsigned long ultimoLCD   = 0;

  // -- Actualizar LCD cada 1 segundo --
  if (ahora_ms - ultimoLCD >= 1000UL) {
    ultimoLCD = ahora_ms;

    TiempoRTC t = ds1302_getDateTime();

    // Linea 0: "DD/MM  HH:MM:SS" (16 chars exactos)
    lcd.setCursor(0, 0);
    if (t.dia    < 10) lcd.print(F("0")); lcd.print(t.dia);
    lcd.print(F("/"));
    if (t.mes    < 10) lcd.print(F("0")); lcd.print(t.mes);
    lcd.print(F("  "));
    if (t.hora   < 10) lcd.print(F("0")); lcd.print(t.hora);
    lcd.print(F(":"));
    if (t.minuto < 10) lcd.print(F("0")); lcd.print(t.minuto);
    lcd.print(F(":"));
    if (t.segundo< 10) lcd.print(F("0")); lcd.print(t.segundo);
    lcd.print(F(" "));

    // Linea 1: "Bomba X AUT 2m12s" o "todas apagadas  "
    lcd.setCursor(0, 1);
    if (bombaEncendida >= 0) {
      unsigned long transcurrido = (ahora_ms - inicioEncendido) / 1000UL;
      unsigned long total    = (unsigned long)configBomba[bombaEncendida].duracionMinutos * 60UL;
      unsigned long restante = (total > transcurrido) ? (total - transcurrido) : 0;
      lcd.print(F("Bomba "));
      lcd.print(bombaEncendida + 1);
      lcd.print(esManual ? F(" MAN ") : F(" AUT "));
      lcd.print(restante / 60);
      lcd.print(F("m"));
      if ((restante % 60) < 10) lcd.print(F("0"));
      lcd.print(restante % 60);
      lcd.print(F("s "));
    } else {
      lcd.print(F("todas apagadas  "));
    }
  }

  // -- Log serial cada 10 segundos --
  if (ahora_ms - ultimoPrint >= 10000UL) {
    ultimoPrint = ahora_ms;

    TiempoRTC t = ds1302_getDateTime();

    if (!ds1302_esValido(t)) {
      Serial.println(F("[!] RTC sin respuesta — verifica cableado CLK=A1 DAT=A2 RST=A3"));
    } else {
      Serial.print(F("Hora: "));
      if (t.hora   < 10) Serial.print(F("0")); Serial.print(t.hora);
      Serial.print(F(":"));
      if (t.minuto < 10) Serial.print(F("0")); Serial.print(t.minuto);
      Serial.print(F(":"));
      if (t.segundo < 10) Serial.print(F("0")); Serial.print(t.segundo);

      if (bombaEncendida >= 0) {
        unsigned long transcurrido = (ahora_ms - inicioEncendido) / 1000UL;
        unsigned long total = (unsigned long)configBomba[bombaEncendida].duracionMinutos * 60UL;
        Serial.print(F("  | Bomba ")); Serial.print(bombaEncendida + 1);
        Serial.print(esManual ? F(" [MAN] ") : F(" [AUTO] "));
        Serial.print(transcurrido / 60); Serial.print(F("m"));
        Serial.print(transcurrido % 60); Serial.print(F("s / "));
        Serial.print(total / 60);        Serial.print(F("m"));
        Serial.print(total % 60);        Serial.print(F("s"));
        if (colaEspera >= 0) {
          Serial.print(F("  | En cola: bomba ")); Serial.print(colaEspera + 1);
        }
      } else {
        Serial.print(F("  | todas apagadas"));
      }
      Serial.println();
    }
  }

  delay(50);
}
