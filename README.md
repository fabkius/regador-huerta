# regador-huerta
regador de huerta
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
