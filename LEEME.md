# Mycelium Sound System · VST3 para Windows (Cubase 12)

Sintetizador granular con tonos isocrónicos, delay ping-pong, reverb, MIDI Learn,
una sección musical (tonalidad, escala, armonía) con tempo de Cubase o del reloj MIDI de Traktor,
y salida/entrada OSC para visuales.

---

## 1. Compilar el plugin

### Opción A · Sin instalar nada (GitHub, recomendada)
1. Crea un repositorio nuevo en github.com y sube esta carpeta tal cual.
2. GitHub compila solo (pestaña **Actions** → *Build Windows VST3*, unos 10 min).
3. Descarga el artefacto **MyceliumSoundSystem-Windows** y descomprímelo.
4. Copia `Mycelium Sound System.vst3` a `C:\Program Files\Common Files\VST3\`.

### Opción B · En tu PC
1. Instala **Visual Studio 2022 Community** con la carga de trabajo *Desarrollo para el escritorio con C++*.
2. Instala **CMake** (cmake.org) y **Git**.
3. Abre *Developer PowerShell for VS 2022* en esta carpeta y ejecuta:
   ```
   cmake -B build -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release
   ```
   (La primera vez descarga JUCE 8.0.4 automáticamente.)
4. Copia `build\MyceliumSoundSystem_artefacts\Release\VST3\Mycelium Sound System.vst3` a `C:\Program Files\Common Files\VST3\`.
5. En Cubase: *Studio > VST Plug-in Manager > Update plug-in information*.

También se genera **Mycelium Sound System.exe** (Standalone) para probar sin Cubase.

---

## 2. Cubase 12 con el Biotron

1. Crea una **pista de instrumento** con *Mycelium Sound System*.
2. En la entrada MIDI de esa pista elige el **Biotron** (USB). Activa la monitorización (altavoz).
3. Las notas del Biotron se ajustan a la **Key** y **Scale** elegidas y se convierten en acordes según **Harmony**.
4. Para que los CC del Biotron muevan parámetros: pulsa **MIDI Learn** en el plugin, haz clic en un control
   (por ejemplo *Position* o *Iso rate*) y deja que el Biotron envíe ese CC. Aparece debajo, p. ej. `Cubase ch1 CC20`.

**Live input:** para granular audio en directo, activa *Side-Chain* en la cabecera del plugin
y envía una pista de audio a *Mycelium Sound System · Live In (Side-Chain)*. Pon **Source = Live input**.

---

## 3. Traktor: tempo, tonalidad y armonía

### Tempo (reloj MIDI)
Cubase no pasa el reloj MIDI a los plugins VST3, así que el plugin abre directamente el puerto de Traktor.

1. Instala **LoopBe1** (gratis, nerds.de). Reinicia.
2. Traktor → *Preferences > Controller Manager* → *Add > Generic MIDI*. **Out-Port: LoopBe Internal MIDI**, **In-Port: None**.
3. Traktor Pro 3/4 → *Preferences > External Sync* → **EXT** y marca **Enable MIDI Clock**.
   En el panel *Master Clock* pulsa play bajo **SEND**.
4. En Cubase: *Studio > Studio Setup > MIDI Port Setup* → **desmarca LoopBe** como entrada
   (con el MIDI clásico de Windows solo un programa puede abrir el puerto).
5. En el plugin: **Tempo > Source = Traktor MIDI clock** y **Traktor port = LoopBe Internal MIDI**.
   Verás *Traktor clock locked: 124.0 BPM*.

Con el tempo de Traktor puedes sincronizar:
- **Iso sync** (1/4 … 1/32): el pulso isocrónico sigue el beat. A 124 BPM, 1/16 = 8,27 Hz (alfa).
- **Delay sync** (1/4, 1/8 punteado, 1/8, … o *Iso pulse*).
Cada vez que pulsas play en SEND, el pulso se realinea al beat.

### Tonalidad y armonía desde Traktor
Mapea salidas en el mismo dispositivo *Generic MIDI* de Traktor (Out-Port LoopBe) y usa **MIDI Learn** en el plugin
(aparecerá como `Traktor ch1 CC…`). Los controles con opciones dividen 0–127 en partes iguales.

**Key** (valor CC para cada tonalidad):

| C | C# | D | D# | E | F | F# | G | G# | A | A# | B |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 5 | 16 | 26 | 37 | 48 | 58 | 69 | 80 | 90 | 101 | 112 | 122 |

**Scale** (12 opciones, mismos valores): Chromatic 5, Major 16, Natural minor 26, Dorian 37, Phrygian 48,
Lydian 58, Mixolydian 69, Harmonic minor 80, Major pentatonic 90, Minor pentatonic 101, Hirajoshi 112, Whole tone 122.

**Harmony** (9 opciones): Single 7, Octaves 21, Fifths 35, Triad 50, Seventh 64, Sus2 78, Sus4 92, Ninth 106, Open 120.

Alternativa más directa para la tonalidad: **Key follow = Notes from Traktor port**.
Cualquier nota que llegue por el puerto de Traktor fija la tonalidad (C, D, E… sin importar la octava),
así un botón de Traktor que envía la nota D4 pone el plugin en D.
Con **Key follow = Notes on channel 16**, lo mismo vale para notas del canal 16 que lleguen desde Cubase.

---

## 4. Todos los parámetros

| Sección | Parámetros |
|---|---|
| Source | Source (Sample / Oscillator / Live input), Waveform, cargar audio |
| Musical | Key, Scale, Harmony, Quantize, Transpose, Key follow, Drone |
| Grains | Position, Spray, Scan, Grain size, Density, Pitch, Detune, Stereo, Reverse |
| Isochronic | Gate, Sync, Rate, Depth, Duty, Edge + presets Delta/Theta/Alpha/Beta/Gamma |
| Tempo | Source (Internal / Cubase / Traktor), Internal BPM, puerto de Traktor |
| Master | Delay sync, time, feedback, tone, send · Reverb size, damping, pre-delay, send · Pulse FX tails · Volume |

Todos son automatizables en Cubase y asignables con MIDI Learn. Los mapeos, el sample cargado y el puerto
de Traktor se guardan con el proyecto de Cubase.

**Drone** toca la tónica de la tonalidad actual (con la armonía elegida), así un cambio de Key en Traktor
cambia el drone al instante.

---

## 5. OSC para visuales

En la sección **OSC** del plugin:
- **Send**: On. **Send to**: `127.0.0.1` y puerto `7000` (mismo PC). Para otro ordenador, pon su IP.
- **Receive**: On, puerto `9000`, para controlar el plugin desde TouchOSC, TouchDesigner, etc.

### Lo que envía (prefijo `/mss`)

| Dirección | Valor | Para qué |
|---|---|---|
| `/mss/pulse` | int 1 | Cada pulso isocrónico (trigger, flash, strobe) |
| `/mss/pulse/count` | int | Contador de pulsos |
| `/mss/beat` | int 1 | Cada negra del tempo (Cubase o Traktor) |
| `/mss/beat/bar` | int 1–4 | Posición en el compás |
| `/mss/gate` | float 0–1 | Envolvente del gate isocrónico (50 Hz) |
| `/mss/level` | float 0–1 | Nivel de salida del plugin |
| `/mss/position` | float 0–1 | Posición de los grains en el sample |
| `/mss/grains/norm` | float 0–1 | Densidad real de grains (`/mss/grains` = grains/s) |
| `/mss/isohz/norm` | float 0–1 | Frecuencia isocrónica (0–45 Hz) (`/mss/isohz` = Hz) |
| `/mss/band` | int 0–4 | Banda: delta, theta, alfa, beta, gamma |
| `/mss/bpm/norm` | float 0–1 | Tempo 40–200 BPM (`/mss/bpm` = BPM) |
| `/mss/ch1/note` | float 0–1 | Última nota de la planta en canal 1 (nota/127) |
| `/mss/ch1/velocity` | float 0–1 | Velocidad de esa nota (0 al soltarla) |
| `/mss/ch1/gate` | int 0/1 | Nota pulsada o no. `ch2` para la segunda planta, etc. |
| `/mss/note` | int ch, nota, vel | Todas las notas en un solo mensaje |
| `/mss/key`, `/mss/scale`, `/mss/harmony` | int | Al cambiar la tonalidad, escala o armonía |
| `/mss/param/<id>` | float 0–1 | Cualquier parámetro al cambiar (p. ej. `/mss/param/density`) |

Los canales `ch1`–`ch16` son las pistas MIDI de Cubase; `ch17`–`ch32` son mensajes del puerto de Traktor.

### Lo que recibe
- `/mss/param/<id>` float 0–1 → mueve el parámetro (p. ej. `/mss/param/isoRate 0.5`).
- `/mss/param/<id>/value` en unidades reales (p. ej. `/mss/param/isoRate/value 8.0` = 8 Hz).

IDs: source, wave, position, spray, scan, size, density, pitch, detune, stereo, reverse, isoOn, isoSync, isoRate,
isoDepth, isoDuty, isoEdge, key, scale, harmony, quantize, transpose, keyFollow, drone, tempoSource, bpm, dlySync,
dlyTime, dlyFb, dlyTone, dlySend, revSize, revDamp, revPre, revSend, fxPulse, gain.

### Resolume Arena / Avenue
1. *Preferences > OSC* → **OSC Input** activado, puerto **7000**.
2. *Shortcuts > Edit OSC*, haz clic en un parámetro de Resolume (opacidad de una capa, un efecto…).
3. Cuando el plugin envíe, Resolume aprende la dirección. Usa `/mss/pulse` para triggers y `/mss/gate` o `/mss/level` para opacidad o brillo.

### TouchDesigner
Añade un **OSC In CHOP** en el puerto **7000**. Cada dirección aparece como un canal (`mss/gate`, `mss/level`, `mss/ch1/note`…).
