# SA-MP Forklift Auto-Job Mod (Anticheat Testing)

> **⚠️ SOLO PARA PRUEBAS DE ANTICHEAT** - Este mod está diseñado exclusivamente para probar sistemas anticheat en servidores propios.


Build Command: 
..\cmake-4.2.3-windows-x86_64\bin\cmake.exe --build . --config Release 2>&1

## Qué hace

Automatiza el trabajo de montacargas (forklift) en SA-MP 0.3DL:
1. Detecta el checkpoint de recogida (standard checkpoint)
2. Teletransporta el vehículo al checkpoint
3. Espera 5 segundos (simula recogida)
4. Detecta el checkpoint de entrega (race checkpoint)
5. Teletransporta el vehículo al checkpoint de entrega
6. Espera 5 segundos (simula entrega)
7. Repite el ciclo

**Tecla F5** para activar/desactivar el mod en el juego.

## Requisitos de desarrollo

### Software necesario

| Software | Versión | Descarga |
|----------|---------|----------|
| **Visual Studio 2022** | Community (gratis) | [Descargar](https://visualstudio.microsoft.com/vs/community/) |
| **CMake** | 3.15+ | [Descargar](https://cmake.org/download/) |
| **GTA San Andreas** | 1.0 US | — |
| **SA-MP Client** | 0.3.DL | [sa-mp.mp](https://sa-mp.mp/) |
| **ASI Loader** | Ultimate ASI Loader x86 | [GitHub](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases) |

### Instalación de Visual Studio

Al instalar Visual Studio, selecciona:
- ✅ **Desktop development with C++**
- ✅ **C++ CMake tools for Windows** (en "Individual components")

## Compilación

### Opción A: Línea de comandos (CMake)

```powershell
# Desde la raíz del proyecto
mkdir build
cd build

# Configurar (asegúrate de usar Win32)
cmake .. -A Win32

# Compilar
cmake --build . --config Release
```

El archivo `samp-forklift-mod.asi` se generará en `build/Release/`.

### Opción B: Visual Studio

1. Abre Visual Studio
2. File → Open → CMake → selecciona `CMakeLists.txt`
3. Configura como **x86-Release**
4. Build → Build All

## Instalación en el juego

1. Copia `dinput8.dll` (ASI Loader) a la carpeta de GTA SA
2. Copia `samp-forklift-mod.asi` a la carpeta de GTA SA
3. Inicia GTA SA con SA-MP 0.3DL
4. Conéctate a tu servidor
5. Súbete a un montacargas
6. Presiona **F5** para activar

## Estructura del proyecto

```
samp-forklift-mod/
├── CMakeLists.txt          # Build system
├── README.md               # Este archivo
├── .gitignore
└── src/
    ├── main.cpp            # Entry point, hook del game loop
    ├── samp.h              # Estructuras y offsets de SA-MP 0.3DL
    ├── game.h              # Funciones del juego (posición, teleport, etc.)
    ├── game.cpp            # Implementación de funciones del juego
    └── forklift.h          # Lógica de automatización del forklift job
```

## ⚠️ NOTAS IMPORTANTES

### Offsets de SA-MP
Los offsets en `samp.h` son **aproximados** para SA-MP 0.3DL R1. **DEBES verificarlos** con tu versión exacta:

1. Abre `samp.dll` en **IDA Free** o **Ghidra**
2. Busca las funciones `CChat::AddMessage`, `CInput::ProcessCommand`
3. Actualiza los valores en `SAMPOffsets` namespace

### Offsets de GTA SA
Los offsets en `game.h` son para **GTA SA 1.0 US (HOODLUM)**. Si usas otra versión:
- GTA SA 1.0 EU tiene offsets ligeramente diferentes
- downgrader a 1.0 US si es necesario

### Qué debería detectar tu anticheat

Este mod genera las siguientes anomalías detectables:

| Anomalía | Descripción | Cómo detectar |
|----------|-------------|---------------|
| **Teleport** | Cambio instantáneo de posición | Comparar posiciones entre ticks del servidor |
| **Speed hack** | Distancia/tiempo imposible | Calcular velocidad real vs máxima del vehículo |
| **Checkpoint timing** | Completar jobs demasiado rápido | Timer entre inicio y fin del job |
| **Position desync** | Posición client ≠ servidor | Verificación server-side |

### Modo gradual (anti-detección básica)

En `forklift.h` puedes activar `useGradualTP = true` para que el teleport sea en pasos. Esto es útil para probar si tu anticheat detecta teleports más sutiles.

## Licencia

Solo para uso educativo y testing de anticheat en servidores propios.
