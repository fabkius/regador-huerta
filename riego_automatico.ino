/*
 * ============================================================
 *  SISTEMA DE RIEGO AUTOMATICO - Arduino Mega 2560
 * ============================================================
 *  Hardware:
 *    - Arduino Mega 2560
 *    - Modulo RTC DS1302 (sin librerias externas)
 *        DAT → pin 48
 *        RST → pin 50
 *        CLK → pin 52
 *    - 6 bombas de agua 12V (via transistores NPN)
 *    - 6 botones de activacion manual
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

// ============================================================
//  PINES DS1302
// ============================================================
#define DS1302_DAT  48
#define DS1302_RST  50
#define DS1302_CLK  52

// ============================================================
//  PINES BOMBAS Y BOTONES
// ============================================================
const uint8_t PIN_BOMBA[6] = {3, 5, 8, 4, 7, 2};
const uint8_t PIN_BOTON[6] = {32, 22, 24, 26, 28, 30};

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

ConfigBomba configBomba[6] = {
  {17,  0,  3},   // Bomba 1: 06:00  durante 10 min
  {17,  30, 3},   // Bomba 2: 07:00  durante  8 min
  {18,  0,  5},   // Bomba 3: 08:00  durante  5 min
  {18,  30, 3},   // Bomba 4: 16:00  durante  5 min
  {19,  0,  3},   // Bomba 5: 17:00  durante  8 min
  {19,  30, 3},   // Bomba 6: 18:00  durante 10 min
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
    if (digitalRead(DS1302_DAT)) dato |= (1 << i);
    digitalWrite(DS1302_CLK, HIGH); delayMicroseconds(2);
    digitalWrite(DS1302_CLK, LOW);  delayMicroseconds(2);
  }
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
  delay(10);
  ds1302_writeReg(0x8E, 0x00);
  uint8_t seg = ds1302_readReg(0x80);
  ds1302_writeReg(0x80, seg & 0x7F);
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
bool    disparadoEsteMinuto[6] = {false,false,false,false,false,false};
uint8_t ultimoMinuto           = 255;

// Anti-rebote
unsigned long ultimoRebote[6] = {0,0,0,0,0,0};
const uint16_t DEBOUNCE_MS    = 200;

// ============================================================
//  CONTROL DE BOMBAS
// ============================================================

void apagarActual() {
  if (bombaEncendida >= 0) {
    digitalWrite(PIN_BOMBA[bombaEncendida], LOW);
    Serial.print(F(">> Bomba ")); Serial.print(bombaEncendida + 1);
    Serial.println(F(" APAGADA"));
    bombaEncendida = -1;
    esManual       = false;
  }
}

void encender(uint8_t idx, bool manual) {
  apagarActual();
  // Apagado de seguridad: garantiza que ninguna otra bomba este encendida
  for (uint8_t j = 0; j < 6; j++) digitalWrite(PIN_BOMBA[j], LOW);
  digitalWrite(PIN_BOMBA[idx], HIGH);
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
  for (uint8_t i = 0; i < 6; i++) {
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
          Serial.println(F("    Verifica: DAT=48  RST=50  CLK=52"));
        }
        Serial.println(F("============================================"));
        Serial.println();
        estadoConfig  = NORMAL;
        ultimoMinuto  = 255;  // reset para no bloquear primer disparo
        for (uint8_t i = 0; i < 6; i++) disparadoEsteMinuto[i] = false;
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

  for (uint8_t i = 0; i < 6; i++) {
    pinMode(PIN_BOMBA[i], OUTPUT);
    digitalWrite(PIN_BOMBA[i], LOW);
  }
  for (uint8_t i = 0; i < 6; i++) {
    pinMode(PIN_BOTON[i], INPUT_PULLUP);
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
    while (estadoConfig != NORMAL) leerSerial();
  } else {
    Serial.print(F("Reloj OK: ")); imprimirTiempo(horaActual);
    Serial.println(F("Comandos: SET | HORA | ESTADO"));
  }

  Serial.println(F("--------------------------------------------"));
  Serial.println(F("Programacion de riego:"));
  for (uint8_t i = 0; i < 6; i++) {
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
    for (uint8_t i = 0; i < 6; i++) disparadoEsteMinuto[i] = false;
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
  for (uint8_t i = 0; i < 6; i++) {
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
  //  D. BOTONES MANUALES
  //     Presionar boton de bomba apagada:
  //       → cancela cola, apaga la actual, enciende la pulsada
  //     Presionar boton de bomba encendida:
  //       → la apaga (toggle)
  // ----------------------------------------------------------
  for (uint8_t i = 0; i < 6; i++) {
    if (digitalRead(PIN_BOTON[i]) == LOW) {
      if (ahora_ms - ultimoRebote[i] > DEBOUNCE_MS) {
        ultimoRebote[i] = ahora_ms;

        if (bombaEncendida == i) {
          // Misma bomba activa → apagar y limpiar cola
          colaEspera = -1;
          apagarActual();
        } else {
          // Otra o ninguna → tomar control manual
          colaEspera = -1;
          encender(i, true);
        }
      }
    }
  }

  // ----------------------------------------------------------
  //  E. LOG CADA 10 SEGUNDOS
  // ----------------------------------------------------------
  static unsigned long ultimoPrint = 0;
  if (ahora_ms - ultimoPrint >= 10000UL) {
    ultimoPrint = ahora_ms;
    Serial.print(F("Hora: "));
    if (ahora.hora   < 10) Serial.print(F("0")); Serial.print(ahora.hora);
    Serial.print(F(":"));
    if (ahora.minuto < 10) Serial.print(F("0")); Serial.print(ahora.minuto);
    Serial.print(F(":"));
    if (ahora.segundo< 10) Serial.print(F("0")); Serial.print(ahora.segundo);

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

  delay(50);
}
