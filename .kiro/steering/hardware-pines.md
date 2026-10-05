# Regla: Cambios de hardware y pines

Cuando el usuario indique que cambió un componente o reasignó un pin, aplicar los cambios en el código **sin pedir confirmación**:

- Actualizar los `#define` o constantes de pines afectados.
- Actualizar los comentarios del encabezado del archivo `.ino`.
- Actualizar cualquier mensaje de diagnóstico en el Monitor Serial que mencione números de pin.
- Si el nuevo pin comparte hardware con un periférico del Mega (UART, SPI, I2C, timer), deshabilitar ese periférico en el código para evitar conflictos.
- No modificar la lógica de control ni la configuración de riego a menos que el usuario lo pida explícitamente.

## Estado actual del hardware

### Microcontrolador
- Arduino Mega 2560

### LCD 16x2 I2C (PCF8574, dirección 0x27)
| Señal | Pin Arduino |
|-------|-------------|
| SDA   | 20          |
| SCL   | 21          |
| VCC   | 5V          |
| GND   | GND         |

> Librería requerida: `LiquidCrystal_I2C`. Si la pantalla no enciende, probar dirección `0x3F`.

### Módulo RTC — MH-Real-Time Clock Module-2 (chip DS1302)
| Señal | Pin Arduino |
|-------|-------------|
| CLK   | A1          |
| DAT   | A2          |
| RST   | A3          |
| VCC   | 5V          |
| GND   | GND         |

> Pines A1/A2/A3 son GPIO puros sin periféricos compartidos.

### Bombas (16x 12V via transistor NPN)
| Bomba | Pin Arduino |
|-------|-------------|
| 1     | 2           |
| 2     | 3           |
| 3     | 4           |
| 4     | 5           |
| 5     | 6           |
| 6     | 7           |
| 7     | 8           |
| 8     | 9           |
| 9     | 10          |
| 10    | 11          |
| 11    | 12          |
| 12    | 13          |
| 13    | 14          |
| 14    | 15          |
| 15    | 16          |
| 16    | 17          |

### Botonera — Matriz 4x4
| Señal | Pin Arduino | Tipo         |
|-------|-------------|--------------|
| R1    | 28          | OUTPUT       |
| R2    | 30          | OUTPUT       |
| R3    | 34          | OUTPUT       |
| R4    | 32          | OUTPUT       |
| C1    | 36          | INPUT_PULLUP |
| C2    | 42          | INPUT_PULLUP |
| C3    | 40          | INPUT_PULLUP |
| C4    | 38          | INPUT_PULLUP |

Mapeo tecla → bomba: `bomba = fila*4 + columna` (0-indexed)
```
     C1   C2   C3   C4
R1 [  1] [  2] [  3] [  4]
R2 [  5] [  6] [  7] [  8]
R3 [  9] [ 10] [ 11] [ 12]
R4 [ 13] [ 14] [ 15] [ 16]
```
