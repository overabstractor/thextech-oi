/*
 * OverInteractive bridge for TheXTech. Ver oi_bridge.h.
 */
#include "oi_bridge.h"

#include <cstdlib>

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#   define WIN32_LEAN_AND_MEAN
#   include <winsock2.h>
#   include <ws2tcpip.h>
#   pragma comment(lib, "ws2_32.lib")
#   define OI_SOCK SOCKET
#   define OI_BAD  INVALID_SOCKET
#   define OI_CLOSE(s) closesocket(s)
#else
#   include <arpa/inet.h>
#   include <netinet/in.h>
#   include <netinet/tcp.h>
#   include <sys/socket.h>
#   include <unistd.h>
#   define OI_SOCK int
#   define OI_BAD  (-1)
#   define OI_CLOSE(s) close(s)
#endif

#include <json/json.hpp>
#include <Logger/logger.h>

#include "../globals.h"
#include "../global_constants.h"
#include "../layers.h"
#include "../player.h"
#include "../npc_id.h"
#include "../npc_traits.h"
#include "oi_npc_names.h"
#include "oi_smb1.h"

//! Puerto del canal. 58480 OoT, 58481 SM64, 58482 Smash 64, 58483 Majora, 58484 MK64.
static constexpr unsigned short OI_DEFAULT_PORT = 58485;

//! Puerto de la app. OI_BRIDGE_PORT lo cambia (banco de pruebas junto a una partida abierta).
static unsigned short oiPort()
{
    static unsigned short port = 0;
    if(port == 0)
    {
        const char* env = std::getenv("OI_BRIDGE_PORT");
        const int v = env ? std::atoi(env) : 0;
        port = (v > 0 && v < 65536) ? (unsigned short)v : OI_DEFAULT_PORT;
    }
    return port;
}

bool g_oiCliEpisode = false;
bool g_oiSmb1 = false;

namespace
{

struct OiEffect
{
    long long id = 0;
    bool remove = false;
    std::string name;
    std::vector<int> params;
};

std::thread          s_thread;
std::atomic<bool>    s_running{false};
std::atomic<OI_SOCK> s_sock{OI_BAD};
std::mutex           s_queueMutex;
std::deque<OiEffect>   s_queue;
std::mutex           s_writeMutex;

// debug_hold: botones forzados y cuantos frames quedan (solo se tocan desde el hilo del juego).
int                  s_holdMask = 0;
int                  s_holdFrames = 0;

// ── Red (hilo propio) ───────────────────────────────────────────────────────

void sendRaw(const std::string& payload)
{
    OI_SOCK s = s_sock.load();
    if(s == OI_BAD)
        return;

    std::string framed = payload;
    framed.push_back('\0');

    std::lock_guard<std::mutex> lock(s_writeMutex);
    size_t sent = 0;
    while(sent < framed.size())
    {
        int n = (int)send(s, framed.data() + sent, (int)(framed.size() - sent), 0);
        if(n <= 0)
            return;
        sent += (size_t)n;
    }
}

void sendResult(long long id, const char* status)
{
    nlohmann::json j;
    j["id"] = id;
    j["type"] = "result";
    j["status"] = status;
    sendRaw(j.dump());
}

void closeSocket()
{
    OI_SOCK s = s_sock.exchange(OI_BAD);
    if(s != OI_BAD)
        OI_CLOSE(s);
}

void handleMessage(const std::string& raw)
{
    nlohmann::json j = nlohmann::json::parse(raw, nullptr, false);
    if(j.is_discarded() || !j.is_object())
        return;

    if(j.value("type", std::string()) != "effect")
        return;

    const auto& e = j["effect"];
    if(!e.is_object())
        return;

    OiEffect eff;
    eff.id = j.value("id", 0LL);
    eff.remove = (e.value("type", std::string("apply")) == "remove");
    eff.name = e.value("name", std::string());

    if(e.contains("parameters") && e["parameters"].is_array())
    {
        for(const auto& p : e["parameters"])
        {
            if(p.is_number())
                eff.params.push_back(p.get<int>());
        }
    }

    if(eff.name.empty())
        return;

    std::lock_guard<std::mutex> lock(s_queueMutex);
    s_queue.push_back(std::move(eff));
}

void netThread()
{
#ifdef _WIN32
    WSADATA wsa;
    if(WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        pLogWarning("[OI] WSAStartup fallo: el canal queda apagado");
        return;
    }
#endif

    std::string pending;
    char buf[4096];

    while(s_running.load())
    {
        OI_SOCK s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if(s == OI_BAD)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(oiPort());
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        if(connect(s, (sockaddr*)&addr, sizeof(addr)) != 0)
        {
            OI_CLOSE(s);
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        int one = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
        s_sock.store(s);
        pending.clear();
        pLogDebug("[OI] conectado a la app en 127.0.0.1:%d", (int)oiPort());

        while(s_running.load())
        {
            int n = (int)recv(s, buf, sizeof(buf), 0);
            if(n <= 0)
                break;

            for(int i = 0; i < n; ++i)
            {
                if(buf[i] == '\0')
                {
                    if(!pending.empty())
                        handleMessage(pending);
                    pending.clear();
                }
                else
                    pending.push_back(buf[i]);
            }
        }

        closeSocket();
        pLogDebug("[OI] conexion perdida, reintentando");

        {
            std::lock_guard<std::mutex> lock(s_queueMutex);
            s_queue.clear();
        }

        if(s_running.load())
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }

#ifdef _WIN32
    WSACleanup();
#endif
}

// ── Ejecucion dentro del frame ──────────────────────────────────────────────

//! Hay partida en marcha y se puede tocar el nivel.
bool gameReady()
{
    if(GameMenu || LevelSelect || GameOutro || LevelEditor)
        return false;

    if(LevelMacro != LEVELMACRO_OFF)
        return false;

    if(numPlayers < 1)
        return false;

    const Player_t& p = Player[1];
    if(p.Dead || p.TimeToLive > 0 || p.Effect != PLREFF_NORMAL)
        return false;

    return true;
}

//! Coloca NPCs alrededor del jugador, alternando lados para que una tanda no se apile en un punto.
int spawnNPC(NPCID type, int count)
{
    if(type <= NPCID_NULL || (int)type > maxNPCType)
        return 0;

    const Player_t& p = Player[1];
    int done = 0;

    for(int i = 0; i < count; ++i)
    {
        if(numNPCs + 1 >= maxNPCs)
            break;

        double side = ((i % 2) == 0) ? 1.0 : -1.0;
        double dist = 96.0 + 48.0 * (i / 2);

        numNPCs++;
        NPC_t& n = NPC[numNPCs];
        n = NPC_t();
        n.Type = type;
        n.Location.Width  = NPCWidth(type);
        n.Location.Height = NPCHeight(type);
        n.Location.X = p.Location.X + p.Location.Width / 2.0 - n.Location.Width / 2.0 + side * dist;
        n.Location.Y = p.Location.Y + p.Location.Height - n.Location.Height - 64.0;
        n.Location.SpeedX = 0.0;
        n.Location.SpeedY = 0.0;
        n.Direction = (side > 0) ? -1 : 1;
        // Su origen es donde aparece: varias IA del motor lo usan (las plantas se destruyen si no están en su
        // X de origen, los peces y fantasmas se mueven alrededor de él).
        n.DefaultLocationX = n.Location.X;
        n.DefaultLocationY = n.Location.Y;
        n.DefaultDirection = n.Direction;
        // Un pez fuera del agua atravesaría el suelo: en tierra salta desde abajo hasta la altura de Mario
        // (el modo de pez saltarín del motor, como los Cheep Cheep de los puentes).
        if(n->IsFish && p.Wet == 0)
            n.Special = 2;
        n.Section = (uint8_t)p.Section;
        n.Layer = LAYER_SPAWNED_NPCS;
        n.Active = true;
        n.JustActivated = 1;
        n.TimeLeft = 100;

        syncLayers_NPC(numNPCs);
        done++;
    }

    return done;
}

//! Cuantos enemigos pide el efecto (primer parametro), con tope de cordura.
int countOf(const OiEffect& eff, size_t index)
{
    int count = (eff.params.size() > index) ? eff.params[index] : 1;

    if(count < 1)
        count = 1;
    if(count > 50)
        count = 50;

    return count;
}

//! Ejecuta un efecto ya sacado de la cola. Devuelve el status que se le responde a la app.
const char* run(const OiEffect& eff)
{
    // Las consultas de depuracion no tocan el nivel: valen tambien durante un warp o la bandera.
    const bool readOnly = eff.name == "debug_player" || eff.name == "debug_npcs" || eff.name == "debug_screenshot";
    if(readOnly ? (GameMenu || LevelSelect || numPlayers < 1) : !gameReady())
        return "try_again";

    // spawn_<nombre>: los nombres salen de la tabla generada del propio motor.
    if(eff.name.rfind("spawn_", 0) == 0 && eff.name != "spawn_npc")
    {
        NPCID type = OI_NpcByName(eff.name.c_str() + 6);
        if(type == NPCID_NULL)
            return "failure";

        return spawnNPC(type, countOf(eff, 0)) > 0 ? "success" : "try_again";
    }

    // spawn_npc <id> [cantidad]: escotilla para probar los 292 tipos sin pasar por la tabla.
    if(eff.name == "spawn_npc")
    {
        if(eff.params.empty())
            return "failure";

        NPCID type = (NPCID)eff.params[0];
        return spawnNPC(type, countOf(eff, 1)) > 0 ? "success" : "failure";
    }

    // debug_state: cuantos NPC siguen vivos, por tipo. Es la unica forma honesta de saber si un
    // spawn sobrevivio: que la app lo acepte no significa que el motor no lo haya descartado.
    if(eff.name == "debug_state")
    {
        nlohmann::json alive = nlohmann::json::object();
        int total = 0;

        for(int i = 1; i <= numNPCs; ++i)
        {
            if(!NPC[i].Active || NPC[i].Killed != 0)
                continue;

            const char* name = nullptr;
            for(int k = 0; k < OI_NpcCount(); ++k)
            {
                int id = 0;
                const char* n = OI_NpcNameAt(k, &id);
                if(id == (int)NPC[i].Type)
                {
                    name = n;
                    break;
                }
            }

            if(!name)
                continue;

            alive[name] = alive.value(name, 0) + 1;
            total++;
        }

        nlohmann::json j;
        j["type"] = "hook";
        j["hook"] = { {"type", "OiDebugState"}, {"numNPCs", numNPCs}, {"alive", total}, {"byType", alive} };
        sendRaw(j.dump());
        return "success";
    }

    // debug_player: donde esta Mario (archivo, seccion, posicion). Para probar tuberias y finales.
    if(eff.name == "debug_player")
    {
        const Player_t& p = Player[1];
        nlohmann::json j;
        j["type"] = "hook";
        j["hook"] = { {"type", "OiDebugPlayer"}, {"file", FileName}, {"section", p.Section},
                      {"x", (int)p.Location.X}, {"y", (int)p.Location.Y}, {"dead", p.Dead},
                      {"levelMacro", (int)LevelMacro}, {"speedX", p.Location.SpeedX},
                      {"ground", p.Pinched.Bottom1 == 2 || p.StandingOnNPC != 0 || p.Slope != 0},
                      {"ctl", (p.Controls.Up ? 1 : 0) | (p.Controls.Down ? 2 : 0) | (p.Controls.Left ? 4 : 0) |
                              (p.Controls.Right ? 8 : 0) | (p.Controls.Jump ? 16 : 0) | (p.Controls.Run ? 32 : 0)},
                      {"hold", s_holdFrames}, {"effect", (int)p.Effect} };
        sendRaw(j.dump());
        return "success";
    }

    // debug_goto_<nivel>: carga otro nivel del episodio igual que un portal (p. ej. debug_goto_4-2).
    if(eff.name.rfind("debug_goto_", 0) == 0)
    {
        // Directo a EndLevel (lo mismo que hace PlayerEffectWarpWait al terminar): el efecto de
        // espera lee Warp[Player.Warp] y sin un warp de verdad salta una asercion del motor.
        GoToLevel = eff.name.substr(11) + ".lvlx";
        GoToLevelNoGameThing = true;
        StartWarp = 0;
        EndLevel = true;
        return "success";
    }

    // debug_god <0|1>: invulnerable (el --god-mode de la linea de comandos solo vale en --leveltest de
    // un nivel suelto, no al arrancar un episodio).
    if(eff.name == "debug_god")
    {
        GodMode = !eff.params.empty() && eff.params[0] != 0;
        return "success";
    }

    // debug_speed <vx> <vy>: velocidad de Mario en decimas (p. ej. 0 -80 = salto hacia arriba).
    if(eff.name == "debug_speed")
    {
        if(eff.params.size() < 2)
            return "failure";
        Player[1].Location.SpeedX = eff.params[0] / 10.0;
        Player[1].Location.SpeedY = eff.params[1] / 10.0;
        return "success";
    }

    // debug_npcs <tipo>: posicion, velocidad y efecto de los NPC de ese tipo (por hook).
    if(eff.name == "debug_npcs")
    {
        if(eff.params.empty())
            return "failure";
        nlohmann::json list = nlohmann::json::array();
        for(int i = 1; i <= numNPCs; ++i)
        {
            const NPC_t& n = NPC[i];
            if((int)n.Type != eff.params[0])
                continue;
            list.push_back({ {"x", (int)n.Location.X}, {"y", (int)n.Location.Y}, {"sx", n.Location.SpeedX},
                             {"sy", n.Location.SpeedY}, {"active", n.Active}, {"effect", (int)n.Effect},
                             {"killed", (int)n.Killed}, {"hidden", n.Hidden}, {"w", (int)n.Location.Width},
                             {"dir", (int)n.Direction}, {"frame", (int)n.Frame}, {"s1", (int)n.Special} });
        }
        nlohmann::json j;
        j["type"] = "hook";
        j["hook"] = { {"type", "OiDebugNpcs"}, {"npcs", list} };
        sendRaw(j.dump());
        return "success";
    }

    // debug_power <estado>: 1 pequeno, 2 grande, 3 fuego (pruebas de enemigos que solo mueren a fuego).
    if(eff.name == "debug_power")
    {
        if(eff.params.empty() || eff.params[0] < 1 || eff.params[0] > 3)
            return "failure";
        Player_t& pl = Player[1];
        if(pl.State == 1 && eff.params[0] > 1)
        {
            pl.Location.Y += pl.Location.Height;
            pl.Location.Height = Physics.PlayerHeight[pl.Character][2];
            pl.Location.Y -= pl.Location.Height;
        }
        pl.State = eff.params[0];
        return "success";
    }

    // debug_lives <n>: fija las vidas (para que las pruebas largas no acaben en game over).
    if(eff.name == "debug_lives")
    {
        if(eff.params.empty())
            return "failure";
        Lives = eff.params[0];
        return "success";
    }

    // debug_teleport <x> <y>: mueve a Mario dentro de su seccion.
    if(eff.name == "debug_teleport")
    {
        if(eff.params.size() < 2)
            return "failure";
        Player[1].Location.X = eff.params[0];
        Player[1].Location.Y = eff.params[1];
        Player[1].Location.SpeedX = 0;
        Player[1].Location.SpeedY = 0;
        CheckSection(1); // como tras un warp: seccion y camara nuevas si el punto cae en otra
        return "success";
    }

    // debug_hold <botones> <frames>: 1 arriba, 2 abajo, 4 izq., 8 der., 16 salto, 32 correr.
    if(eff.name == "debug_hold")
    {
        if(eff.params.size() < 2)
            return "failure";
        s_holdMask = eff.params[0];
        s_holdFrames = eff.params[1];
        return "success";
    }

    // debug_screenshot: dispara la captura propia del motor (queda en screenshots/).
    if(eff.name == "debug_screenshot")
    {
        TakeScreen = true;
        return "success";
    }

    return "failure";
}

} // namespace

// ── API ─────────────────────────────────────────────────────────────────────

void OI_Init()
{
    if(s_running.exchange(true))
        return;

    s_thread = std::thread(netThread);
    pLogDebug("[OI] canal arrancado (puerto %d)", (int)oiPort());
}

void OI_Shutdown()
{
    if(!s_running.exchange(false))
        return;

    closeSocket();

    if(s_thread.joinable())
        s_thread.join();
}

void OI_AfterControls()
{
    if(g_oiSmb1)
        OI_Smb1Controls();

    if(s_holdFrames <= 0)
        return;

    // Sustituye (no suma): un mando conectado con el stick torcido no debe colarse en la prueba.
    Controls_t& c = Player[1].Controls;
    c = Controls_t();
    c.Up    = (s_holdMask & 1) != 0;
    c.Down  = (s_holdMask & 2) != 0;
    c.Left  = (s_holdMask & 4) != 0;
    c.Right = (s_holdMask & 8) != 0;
    c.Jump  = (s_holdMask & 16) != 0;
    c.Run   = (s_holdMask & 32) != 0;
    s_holdFrames--;
}

void OI_Poll()
{
    for(;;)
    {
        OiEffect eff;
        {
            std::lock_guard<std::mutex> lock(s_queueMutex);
            if(s_queue.empty())
                return;
            eff = std::move(s_queue.front());
            s_queue.pop_front();
        }

        const char* status = run(eff);

        if(eff.id != 0)
            sendResult(eff.id, status);

        // Si el juego no esta listo, el resto de la cola tampoco va a entrar en este frame:
        // la app reintenta, que es quien lleva la cola de verdad.
        if(status[0] == 't')
            return;
    }
}
