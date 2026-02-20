// ═══════════════════════════════════════════════════════════
// Coastguard Test Gamemode — servidor local para testear el mod
//
// - Login flow: email → password → character select → welcome
// - Bote modelo 472 en (719.12, -1698.42, 1.78)
// - Al entrar al bote, apretar "2" para empezar
// - 52 checkpoints EXACTOS del servidor real
// - Al terminar: $3,500 + te saca del vehiculo + spawn al lado del bote
// - /admins simula formato real, /fakeadmin toggle admin simulado
// ═══════════════════════════════════════════════════════════

#include <a_samp>

// ─── Configuración ───
#define BOAT_MODEL      472
#define BOAT_X          719.1288
#define BOAT_Y         -1698.4248
#define BOAT_Z          1.7874
#define BOAT_ANGLE      190.97
#define SPAWN_X         717.0
#define SPAWN_Y        -1695.0
#define SPAWN_Z         2.0
#define REWARD          3500
#define NUM_CPS         52

// ─── Dialog IDs (login flow) ───
#define DIALOG_EMAIL      100
#define DIALOG_PASSWORD   101
#define DIALOG_CHARACTER  102
#define DIALOG_WELCOME    103

// ─── Variables globales ───
new gBoatID = INVALID_VEHICLE_ID;
new bool:gJobActive[MAX_PLAYERS];
new gCurrentCP[MAX_PLAYERS];
new bool:gWaitingForKey2[MAX_PLAYERS];

// ─── Login state ───
new bool:gLoggedIn[MAX_PLAYERS];
new bool:gFakeAdminOnline = false;    // toggle con /fakeadmin

// ─── 52 Checkpoints EXACTOS del servidor real ───
new Float:gCheckpoints[NUM_CPS][3] = {
    { 719.4, -1635.7,  0.2},   // CP 1
    { 725.1, -1919.3,  0.1},   // CP 2
    { 797.5, -1918.7,  0.1},   // CP 3
    { 909.5, -1930.8,  0.2},   // CP 4
    { 949.8, -2054.1,  0.2},   // CP 5
    { 686.0, -1965.4,  0.3},   // CP 6
    { 558.9, -1946.2,  0.0},   // CP 7
    { 415.1, -1941.1,  0.0},   // CP 8
    { 333.7, -1933.3,  0.1},   // CP 9
    { 216.8, -1911.3,  0.1},   // CP 10
    { -43.7, -1839.7,  0.0},   // CP 11
    { -73.6, -1996.8,  0.2},   // CP 12
    {-158.3, -2079.6,  0.1},   // CP 13
    { 153.9, -1990.6,  0.1},   // CP 14
    { 264.9, -1992.2,  0.0},   // CP 15
    { 456.9, -1992.1, -0.1},   // CP 16
    { 642.3, -2003.2, -0.2},   // CP 17
    { 826.3, -2087.2,  0.1},   // CP 18
    { 891.1, -2193.2, -0.1},   // CP 19
    {1158.4, -2458.8, -0.1},   // CP 20
    {1197.3, -2522.6,  0.1},   // CP 21
    {1312.5, -2760.7,  0.2},   // CP 22
    {1499.6, -2793.0, -0.2},   // CP 23
    {1783.2, -2790.4, -0.1},   // CP 24
    {2035.7, -2795.5, -0.2},   // CP 25
    {2248.0, -2750.6, -0.3},   // CP 26
    {2289.5, -2570.4, -0.1},   // CP 27
    {2351.4, -2390.0,  0.1},   // CP 28
    {2492.5, -2284.2,  0.1},   // CP 29
    {2789.6, -2267.1,  0.3},   // CP 30
    {2905.4, -2181.7, -0.2},   // CP 31
    {2956.6, -2044.8,  0.0},   // CP 32
    {2985.7, -1862.8, -0.2},   // CP 33
    {3020.9, -1988.9, -0.6},   // CP 34
    {2895.9, -2244.3, -0.2},   // CP 35
    {2603.6, -2291.4, -0.2},   // CP 36
    {2345.3, -2422.6, -0.2},   // CP 37
    {2327.2, -2704.3,  0.1},   // CP 38
    {2218.6, -2796.0,  0.2},   // CP 39
    {1784.6, -2841.2,  0.1},   // CP 40
    {1575.6, -2839.7, -0.1},   // CP 41
    {1460.3, -2837.9, -0.2},   // CP 42
    {1253.9, -2779.2,  0.2},   // CP 43
    {1152.0, -2610.7, -0.2},   // CP 44
    {1048.6, -2481.1,  0.1},   // CP 45
    { 971.2, -2368.8,  0.0},   // CP 46
    { 917.3, -2272.3,  0.1},   // CP 47
    { 847.3, -2110.3, -0.1},   // CP 48
    { 765.8, -2018.2, -0.1},   // CP 49
    { 723.5, -1916.3,  0.0},   // CP 50
    { 719.6, -1777.9, -0.1},   // CP 51
    { 721.8, -1698.4,  0.0}    // CP 52 — vuelve al muelle
};

// ═══════════════════════════════════════════════════════════
// Callbacks principales
// ═══════════════════════════════════════════════════════════

public OnGameModeInit()
{
    SetGameModeText("Coastguard Test");
    ShowPlayerMarkers(PLAYER_MARKERS_MODE_GLOBAL);
    ShowNameTags(1);
    SetWorldTime(12);
    SetWeather(1);

    // Crear el bote
    gBoatID = CreateVehicle(BOAT_MODEL, BOAT_X, BOAT_Y, BOAT_Z, BOAT_ANGLE, -1, -1, -1);
    if(gBoatID == INVALID_VEHICLE_ID)
    {
        print("[COASTGUARD] ERROR: No se pudo crear el bote!");
    }
    else
    {
        printf("[COASTGUARD] Bote creado ID=%d modelo=%d", gBoatID, BOAT_MODEL);
    }

    // Clase del jugador (skin CJ)
    AddPlayerClass(0, SPAWN_X, SPAWN_Y, SPAWN_Z, 0.0, 0, 0, 0, 0, 0, 0);

    print("════════════════════════════════════════");
    print(" Coastguard Test Server - Listo!");
    print(" Bote en: 719.12, -1698.42, 1.78");
    print(" 52 checkpoints EXACTOS del servidor real");
    print(" Apretar 2 en el bote para empezar");
    print("════════════════════════════════════════");
    return 1;
}

public OnGameModeExit()
{
    return 1;
}

public OnPlayerConnect(playerid)
{
    gJobActive[playerid] = false;
    gCurrentCP[playerid] = 0;
    gWaitingForKey2[playerid] = false;
    gLoggedIn[playerid] = false;

    // ─── Start login flow: email dialog ───
    ShowPlayerDialog(playerid, DIALOG_EMAIL, DIALOG_STYLE_INPUT,
        "Ingrese su Email",
        "Por favor ingresa tu correo electrónico para continuar.\n\nEscribe tu email:",
        "Aceptar", "Cancelar");
    return 1;
}

public OnPlayerDisconnect(playerid, reason)
{
    ResetPlayerJob(playerid);
    return 1;
}

public OnPlayerSpawn(playerid)
{
    if(!gLoggedIn[playerid])
    {
        // Not logged in yet — re-show email dialog
        ShowPlayerDialog(playerid, DIALOG_EMAIL, DIALOG_STYLE_INPUT,
            "Ingrese su Email",
            "Por favor ingresa tu correo electrónico para continuar.\n\nEscribe tu email:",
            "Aceptar", "Cancelar");
        return 1;
    }
    // Dar plata inicial
    GivePlayerMoney(playerid, 10000);
    // Teletransportar al spawn cerca del bote
    SetPlayerPos(playerid, SPAWN_X, SPAWN_Y, SPAWN_Z);
    SetPlayerFacingAngle(playerid, 180.0);
    SetCameraBehindPlayer(playerid);
    SendClientMessage(playerid, 0xFFFF00FF, "Spawneaste al lado del bote. Subite y apreta 2!");
    return 1;
}

// ═══════════════════════════════════════════════════════════
// Dialog Response — Login flow chain
//
// Flow: Email(100) → Password(101) → CharSelect(102) → Welcome(103)
// ═══════════════════════════════════════════════════════════

public OnDialogResponse(playerid, dialogid, response, listitem, inputtext[])
{
    switch(dialogid)
    {
        case DIALOG_EMAIL:
        {
            if(!response)
            {
                // Pressed Cancel — re-show
                ShowPlayerDialog(playerid, DIALOG_EMAIL, DIALOG_STYLE_INPUT,
                    "Ingrese su Email",
                    "{FF0000}Debes ingresar tu email para jugar.\n\n{FFFFFF}Escribe tu email:",
                    "Aceptar", "Cancelar");
                return 1;
            }
            // Accept email — show password dialog
            new msg[128];
            format(msg, sizeof(msg), "[LOGIN] Email recibido: %s", inputtext);
            printf("%s", msg);
            SendClientMessage(playerid, 0x00FFFFFF, msg);

            ShowPlayerDialog(playerid, DIALOG_PASSWORD, DIALOG_STYLE_PASSWORD,
                "Contraseña",
                "Ingresa tu contraseña para acceder a tu cuenta:",
                "Iniciar sesion", "Cancelar");
            return 1;
        }

        case DIALOG_PASSWORD:
        {
            if(!response)
            {
                // Cancel — back to email
                ShowPlayerDialog(playerid, DIALOG_EMAIL, DIALOG_STYLE_INPUT,
                    "Ingrese su Email",
                    "Por favor ingresa tu correo electrónico para continuar.\n\nEscribe tu email:",
                    "Aceptar", "Cancelar");
                return 1;
            }
            // Accept password — show character select
            printf("[LOGIN] Password recibido (len=%d)", strlen(inputtext));
            SendClientMessage(playerid, 0x00FFFFFF, "[LOGIN] Contraseña aceptada.");

            ShowPlayerDialog(playerid, DIALOG_CHARACTER, DIALOG_STYLE_LIST,
                "Seleccionar Personaje",
                "Xylos Fernandez - Nivel 15\nPersonaje 2 - Vacio\nPersonaje 3 - Vacio",
                "Seleccionar", "Volver");
            return 1;
        }

        case DIALOG_CHARACTER:
        {
            if(!response)
            {
                // Cancel — back to password
                ShowPlayerDialog(playerid, DIALOG_PASSWORD, DIALOG_STYLE_PASSWORD,
                    "Contraseña",
                    "Ingresa tu contraseña para acceder a tu cuenta:",
                    "Iniciar sesion", "Cancelar");
                return 1;
            }
            // Selected character — show welcome message
            new msg[64];
            format(msg, sizeof(msg), "[LOGIN] Personaje seleccionado: slot %d", listitem);
            printf("%s", msg);
            SendClientMessage(playerid, 0x00FFFFFF, msg);

            ShowPlayerDialog(playerid, DIALOG_WELCOME, DIALOG_STYLE_MSGBOX,
                "Bienvenido a SARP",
                "{00FF00}¡Bienvenido de vuelta!\n\n{FFFFFF}Recuerda respetar las reglas del servidor.\nUsa /ayuda para ver los comandos disponibles.\n\n{FFFF00}¡Buena suerte!",
                "Aceptar", "");
            return 1;
        }

        case DIALOG_WELCOME:
        {
            // Login complete — mark as logged in and spawn
            gLoggedIn[playerid] = true;
            printf("[LOGIN] Jugador %d login completo — spawning", playerid);
            SendClientMessage(playerid, 0x00FF00FF, "═══ COASTGUARD TEST SERVER ═══");
            SendClientMessage(playerid, 0xFFFFFFFF, "Subite al bote y apreta 2 para empezar el trabajo.");
            SendClientMessage(playerid, 0xFFFFFFFF, "Usa /tp para ir al bote, /restart para reiniciar el job.");
            SendClientMessage(playerid, 0xFFFFFFFF, "Usa /fakeadmin para simular admin online.");

            SpawnPlayer(playerid);
            return 1;
        }
    }
    return 0;
}

// ═══════════════════════════════════════════════════════════
// Detección de tecla "2" — KEY_SUBMISSION
// ═══════════════════════════════════════════════════════════

public OnPlayerKeyStateChange(playerid, KEY:newkeys, KEY:oldkeys)
{
    // KEY_SUBMISSION = 512 = tecla "2"
    if((_:newkeys & 512) && !(_:oldkeys & 512))
    {
        if(gWaitingForKey2[playerid])
        {
            StartCoastguardJob(playerid);
        }
    }
    return 1;
}

// ═══════════════════════════════════════════════════════════
// Entrar/Salir del vehículo
// ═══════════════════════════════════════════════════════════

public OnPlayerEnterVehicle(playerid, vehicleid, ispassenger)
{
    return 1;
}

public OnPlayerStateChange(playerid, PLAYER_STATE:newstate, PLAYER_STATE:oldstate)
{
    // Entró al vehículo como conductor
    if(newstate == PLAYER_STATE_DRIVER)
    {
        new vid = GetPlayerVehicleID(playerid);
        if(vid == gBoatID && !gJobActive[playerid])
        {
            gWaitingForKey2[playerid] = true;
            SendClientMessage(playerid, 0x00FF00FF, "[COASTGUARD] Apreta 2 para empezar el trabajo.");
            // Mostrar gametext tipo SA-MP job
            GameTextForPlayer(playerid, "~w~Apreta ~r~2 ~w~para empezar", 5000, 3);
        }
    }
    // Salió del vehículo
    else if(oldstate == PLAYER_STATE_DRIVER && newstate == PLAYER_STATE_ONFOOT)
    {
        if(gJobActive[playerid])
        {
            // Se bajó durante el job → cancelar
            SendClientMessage(playerid, 0xFF0000FF, "[COASTGUARD] Te bajaste del bote. Job cancelado.");
            ResetPlayerJob(playerid);
        }
        gWaitingForKey2[playerid] = false;
    }
    return 1;
}

// ═══════════════════════════════════════════════════════════
// Checkpoint alcanzado
// ═══════════════════════════════════════════════════════════

public OnPlayerEnterRaceCheckpoint(playerid)
{
    if(!gJobActive[playerid]) return 1;

    new cp = gCurrentCP[playerid];
    DisablePlayerRaceCheckpoint(playerid);

    new msg[64];
    format(msg, sizeof(msg), "[COASTGUARD] CP %d/%d completado!", cp + 1, NUM_CPS);
    SendClientMessage(playerid, 0x00FFFFFF, msg);

    cp++;
    gCurrentCP[playerid] = cp;

    if(cp >= NUM_CPS)
    {
        // ¡Job completado!
        FinishCoastguardJob(playerid);
    }
    else
    {
        // Siguiente checkpoint
        SetNextCheckpoint(playerid, cp);
    }
    return 1;
}

// También soportar checkpoint normal (por si acaso)
public OnPlayerEnterCheckpoint(playerid)
{
    return 1;
}

// ═══════════════════════════════════════════════════════════
// Funciones del Job
// ═══════════════════════════════════════════════════════════

forward StartCoastguardJob(playerid);
public StartCoastguardJob(playerid)
{
    gWaitingForKey2[playerid] = false;
    gJobActive[playerid] = true;
    gCurrentCP[playerid] = 0;

    SendClientMessage(playerid, 0x00FF00FF, "[COASTGUARD] ¡Trabajo iniciado! Seguí los checkpoints.");
    GameTextForPlayer(playerid, "~g~Job iniciado!", 3000, 3);

    // Primer checkpoint
    SetNextCheckpoint(playerid, 0);
    return 1;
}

forward FinishCoastguardJob(playerid);
public FinishCoastguardJob(playerid)
{
    // Dar recompensa
    GivePlayerMoney(playerid, REWARD);

    new msg[128];
    format(msg, sizeof(msg), "[COASTGUARD] ¡Trabajo completado! Ganaste $%d.", REWARD);
    SendClientMessage(playerid, 0x00FF00FF, msg);
    GameTextForPlayer(playerid, "~g~+$3,500!", 5000, 3);

    // Sacar del vehículo (como el servidor real)
    new vid = GetPlayerVehicleID(playerid);
    if(vid != 0)
    {
        RemovePlayerFromVehicle(playerid);
        SetTimerEx("RespawnAfterJob", 1000, false, "i", playerid);
    }

    // Reset job state
    gJobActive[playerid] = false;
    gCurrentCP[playerid] = 0;
    DisablePlayerRaceCheckpoint(playerid);
    return 1;
}

forward RespawnAfterJob(playerid);
public RespawnAfterJob(playerid)
{
    SetPlayerPos(playerid, SPAWN_X, SPAWN_Y, SPAWN_Z);
    SetPlayerFacingAngle(playerid, 180.0);
    SetCameraBehindPlayer(playerid);

    // Recrear el bote (por si desapareció)
    if(!IsValidVehicle(gBoatID))
    {
        gBoatID = CreateVehicle(BOAT_MODEL, BOAT_X, BOAT_Y, BOAT_Z, BOAT_ANGLE, -1, -1, -1);
        printf("[COASTGUARD] Bote recreado ID=%d", gBoatID);
    }
    else
    {
        SetVehiclePos(gBoatID, BOAT_X, BOAT_Y, BOAT_Z);
        SetVehicleZAngle(gBoatID, BOAT_ANGLE);
    }

    SendClientMessage(playerid, 0xFFFF00FF, "[COASTGUARD] Listo para otra vuelta. Subite al bote!");
    return 1;
}

stock SetNextCheckpoint(playerid, cp)
{
    new nextCP = cp + 1;
    if(nextCP >= NUM_CPS)
    {
        // Último CP → sin dirección (tipo 1 = finish)
        SetPlayerRaceCheckpoint(playerid, 1,
            gCheckpoints[cp][0], gCheckpoints[cp][1], gCheckpoints[cp][2],
            0.0, 0.0, 0.0,
            12.0);
    }
    else
    {
        // CP normal con flecha al siguiente (tipo 0 = arrow)
        SetPlayerRaceCheckpoint(playerid, 0,
            gCheckpoints[cp][0], gCheckpoints[cp][1], gCheckpoints[cp][2],
            gCheckpoints[nextCP][0], gCheckpoints[nextCP][1], gCheckpoints[nextCP][2],
            12.0);
    }
}

stock ResetPlayerJob(playerid)
{
    gJobActive[playerid] = false;
    gCurrentCP[playerid] = 0;
    gWaitingForKey2[playerid] = false;
    DisablePlayerRaceCheckpoint(playerid);
}

// ═══════════════════════════════════════════════════════════
// Comandos útiles para testing
// ═══════════════════════════════════════════════════════════

public OnPlayerCommandText(playerid, cmdtext[])
{
    // /tp — ir al bote
    if(!strcmp(cmdtext, "/tp", true))
    {
        SetPlayerPos(playerid, SPAWN_X, SPAWN_Y, SPAWN_Z);
        SetCameraBehindPlayer(playerid);
        SendClientMessage(playerid, 0xFFFF00FF, "Teletransportado al bote.");
        return 1;
    }

    // /restart — reiniciar job
    if(!strcmp(cmdtext, "/restart", true))
    {
        ResetPlayerJob(playerid);
        SendClientMessage(playerid, 0xFFFF00FF, "Job reiniciado. Subite al bote y apreta 2.");
        return 1;
    }

    // /car — entrar al bote directamente (PutPlayerInVehicle)
    if(!strcmp(cmdtext, "/car", true))
    {
        if(gBoatID != INVALID_VEHICLE_ID)
        {
            PutPlayerInVehicle(playerid, gBoatID, 0);
            SendClientMessage(playerid, 0xFFFF00FF, "Warpeado al bote.");
        }
        return 1;
    }

    // /skip — saltar al CP actual (para testear rápido)
    if(!strcmp(cmdtext, "/skip", true))
    {
        if(gJobActive[playerid])
        {
            new cp = gCurrentCP[playerid];
            if(cp < NUM_CPS)
            {
                new vid = GetPlayerVehicleID(playerid);
                if(vid != 0)
                {
                    SetVehiclePos(vid, gCheckpoints[cp][0], gCheckpoints[cp][1], gCheckpoints[cp][2]);
                }
                new msg[64];
                format(msg, sizeof(msg), "Saltaste al CP %d/%d", cp + 1, NUM_CPS);
                SendClientMessage(playerid, 0xFFFF00FF, msg);
            }
        }
        return 1;
    }

    // /admins — simula formato REAL del servidor
    // Real: solo manda el header cuando no hay admins (SIN "No hay administradores")
    if(!strcmp(cmdtext, "/admins", true))
    {
        SendClientMessage(playerid, 0xFFFFFFFF, "[ _______________ ADMINISTRADORES _______________ ]");
        if(gFakeAdminOnline)
        {
            SendClientMessage(playerid, 0xFFFFFFFF, "(ID: 18) Lead Admin Zoom (reportes: 74) (dudas: 59)");
            SendClientMessage(playerid, 0xFFFFFFFF, "(ID: 22) Helper BLK (dudas: 1)");
        }
        // Cuando no hay admins: solo el header, nada mas (como el servidor real)
        return 1;
    }

    // /fakeadmin — toggle admin simulado
    if(!strcmp(cmdtext, "/fakeadmin", true))
    {
        gFakeAdminOnline = !gFakeAdminOnline;
        if(gFakeAdminOnline)
        {
            SendClientMessage(playerid, 0xFF0000FF, "[TEST] Admin simulado ONLINE — /admins mostrara admin");
        }
        else
        {
            SendClientMessage(playerid, 0x00FF00FF, "[TEST] Admin simulado OFFLINE — /admins mostrara vacio");
        }
        return 1;
    }

    // /info — estado actual
    if(!strcmp(cmdtext, "/info", true))
    {
        new msg[128];
        format(msg, sizeof(msg), "Job: %s | CP: %d/52 | Boat: %d | WaitKey2: %s",
            gJobActive[playerid] ? "ACTIVO" : "INACTIVO",
            gCurrentCP[playerid],
            gBoatID,
            gWaitingForKey2[playerid] ? "SI" : "NO");
        SendClientMessage(playerid, 0xFFFFFFFF, msg);
        return 1;
    }

    return 0;
}
