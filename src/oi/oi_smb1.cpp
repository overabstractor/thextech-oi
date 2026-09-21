/*
 * OverInteractive: comportamientos de Super Mario Bros. Ver oi_smb1.h.
 */
#include "oi_smb1.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "../floats.h"
#include "../globals.h"
#include "../layers.h"
#include "../npc.h"
#include "../main/trees.h"
#include "../npc/npc_queues.h"
#include "../npc_id.h"
#include "../npc_traits.h"
#include "../rand.h"
#include "../sound.h"

using namespace OiSmb1;

void OI_Smb1HammerReleased(int A);

namespace
{

// Los niveles convertidos ocupan las filas 0..12 (Y 0..416).
constexpr double kLevelBottom = 416.0;

bool anyPlayerStandingOn(int A)
{
    for(int i = 1; i <= numPlayers; i++)
    {
        if(Player[i].StandingOnNPC == A && !Player[i].Dead)
            return true;
    }
    return false;
}

// ── Plataformas: simulacion en unidades del NES ─────────────────────────────
//
// El conversor escribe Y = (y_NES - 32) * 2 (sin la barra de estado) y X = x_NES * 2. Las posiciones
// y velocidades se llevan como el juego: 1/256 de pixel NES (Y_Position.YMF_Dummy, Y_Speed.MoveForce).

double nesY(double smbxY) { return smbxY / 2.0 + 32.0; }
double smbxY(double nesY) { return (nesY - 32.0) * 2.0; }

// Anchos de la plataforma: grande (6 sprites), de castillo o modo dificil (4) y pequena (3).
constexpr double kPlatformWidth[3] = {96.0, 64.0, 48.0};

//! ImposeGravity del juego: y += v; v += bajada (tope q); si sube, v -= subida (tope -q).
void imposeGravity(double& y, int& v, int down, int up, int q, bool goUp)
{
    y += v;
    v = (int16_t)(v + down);
    if((int8_t)(uint8_t)(((v >> 8) & 0xff) - q) >= 0 && (v & 0xff) >= 0x80)
        v = q << 8;
    if(goUp)
    {
        v = (int16_t)(v - up);
        if((int8_t)(uint8_t)(((v >> 8) & 0xff) + q) < 0 && (v & 0xff) < 0x80)
            v = (int16_t)((-q * 256) | 0xff);
    }
}

// MovePlatformDown / MovePlatformUp: aceleracion 5, freno 10, tope 3 px/frame.
void platformDown(double& y, int& v) { imposeGravity(y, v, 5, 10, 3, false); }
void platformUp(double& y, int& v) { imposeGravity(y, v, 5, 10, 3, true); }

//! Pareja de una balanza (mismo numero de pareja en S1) o 0.
int balancePartner(int A)
{
    const int pair = NPC[A].Special >> 7;
    for(int B = 1; B <= numNPCs; B++)
    {
        if(B != A && NPC[B].Type == NPCID_PLATFORM_S1 && (NPC[B].Special & 15) == 6 &&
           (NPC[B].Special >> 7) == pair && NPC[B].Section == NPC[A].Section)
            return B;
    }
    return 0;
}

// ── Estado de las secuencias ─────────────────────────────────────────────────

enum class Seq
{
    None,
    FlagSlide,
    FlagWalk,
    BridgeCollapse,
    BridgeWalk,
    VineExit,       // Vine_AutoClimb: arriba del todo de la liana sube solo hasta salir por arriba
    VineGrow,       // llegada: agarrado abajo mientras la liana brota (VineHeight hasta $60)
    VineClimb,      // y sube por ella hasta y = $98
    VineStep,       // y se baja a la derecha hasta x = $48
};

Seq                 s_seq = Seq::None;
int                 s_timer = 0;
std::string         s_file;
int                 s_section = -1;

double              s_poleX = 0.0;
double              s_poleBottom = 0.0;
int                 s_flagBgo = 0;
double              s_flagBottom = 0.0;

bool                s_axeSeen = false;
std::vector<int>    s_bridge;       // bloques del puente, de derecha a izquierda
size_t              s_bridgeNext = 0;

int                 s_frenzyTimer = 0;
bool                s_frenzyHard = false;   // modo dificil del nivel (lo dice el Bowser del conversor)

bool                s_vineDone = false;
double              s_vineX = 0.0;
double              s_vineTop = 0.0;
double              s_climbX = 0.0;
double              s_climbY = 0.0;
double              s_vineGrown = 0.0;
unsigned            s_frameCounter = 0;

void resetLevelState()
{
    s_vineDone = false;
    s_seq = Seq::None;
    s_timer = 0;
    s_flagBgo = 0;
    s_axeSeen = false;
    s_bridge.clear();
    s_bridgeNext = 0;
    s_frenzyTimer = 0;
}

bool overlaps(double ax, double ay, double aw, double ah, const Location_t& b)
{
    return ax < b.X + b.Width && ax + aw > b.X && ay < b.Y + b.Height && ay + ah > b.Y;
}

// ── Liana ────────────────────────────────────────────────────────────────────
//
// Salida (Vine_AutoClimb): la liana crece medio pixel NES por frame; cuando Mario, trepando, pasa de
// y = $20 (fila 0), el juego le quita el mando y lo sube a 0,875 px/frame hasta sacarlo por arriba; ahi
// cambia de zona (en el conversor, un portal por encima de la pantalla, marcado con BGO_VINE_EXIT).
//
// Llegada (PlayerEntrance con JoypadOverride): Mario aparece abajo agarrado; la liana brota del suelo
// ($60 px NES, medio pixel por frame) y solo entonces sube a 0,875 px/frame hasta y = $98 y pasa a la
// derecha. BGO_VINE_ENTRY marca la x de la liana y su punta final ($90).

constexpr double kClimbSpeed = 1.75;        // 0,875 px NES (Y_Speed FF.20)
constexpr double kVineFullHeight = 192.0;   // VineHeight $60

//! Medio pixel NES por frame: 1 px NES en los frames con (FrameCounter & 2).
double vineGrowStep() { return (s_frameCounter & 2) ? 2.0 : 0.0; }

//! Mario sujeto por la secuencia: posicion fija, agarrado (sin gravedad) y con la animacion de trepar.
void holdClimbing(Player_t& p, double x, double y, bool moving)
{
    p.Location.X = x;
    p.Location.Y = y;
    p.Location.SpeedX = 0.0;
    p.Location.SpeedY = moving ? -kClimbSpeed : 0.0;
    p.Vine = 2;
}

void tryStartVineExit(Player_t& p)
{
    if(p.Vine <= 0 || p.Location.Y >= 0.0)
        return;
    for(int i = 1; i <= numBackground; i++)
    {
        const Background_t& b = Background[i];
        if(b.Hidden || b.Type != BGO_VINE_EXIT)
            continue;
        const double cx = p.Location.X + p.Location.Width / 2.0;
        if(std::fabs(cx - (b.Location.X + 16.0)) > 48.0)
            continue;
        s_seq = Seq::VineExit;
        s_timer = 0;
        s_climbX = p.Location.X;
        s_climbY = p.Location.Y;
        return;
    }
}

void vineExit(Player_t& p)
{
    // El portal de encima de la pantalla cambia de zona; esto solo es una red.
    s_climbY -= kClimbSpeed;
    holdClimbing(p, s_climbX, s_climbY, true);
    if(++s_timer > 60 * 5)
        s_seq = Seq::None;
}

//! Tallos de la liana de llegada: brotan del suelo. Los que aun estan bajo el borde van ocultos.
void placeEntryVine(double grown)
{
    for(int i = 1; i <= numNPCs; i++)
    {
        NPC_t& n = NPC[i];
        if(n.Type != NPCID_GRN_VINE_S3 || std::fabs(n.DefaultLocationX - (s_vineX + 8.0)) > 2.0)
            continue;
        const double y = n.DefaultLocationY + (kVineFullHeight - grown);
        n.Hidden = (y >= kLevelBottom);
        n.Location.Y = n.Hidden ? n.DefaultLocationY : y;
        n.Location.SpeedY = 0.0;
    }
}

void tryStartVineEntry(Player_t& p)
{
    if(s_vineDone)
        return;
    for(int i = 1; i <= numBackground; i++)
    {
        const Background_t& b = Background[i];
        if(b.Hidden || b.Type != BGO_VINE_ENTRY)
            continue;
        const double cx = p.Location.X + p.Location.Width / 2.0;
        if(std::fabs(cx - (b.Location.X + 16.0)) > 48.0 || p.Location.Y < b.Location.Y)
            continue;
        s_vineDone = true;
        s_vineX = b.Location.X;
        s_vineTop = b.Location.Y;
        s_climbX = p.Location.X;
        s_climbY = p.Location.Y;
        s_vineGrown = 0.0;
        s_seq = Seq::VineGrow;
        s_timer = 0;
        placeEntryVine(0.0);
        holdClimbing(p, s_climbX, s_climbY, false);
        return;
    }
}

void vineGrow(Player_t& p)
{
    s_vineGrown = std::fmin(kVineFullHeight, s_vineGrown + vineGrowStep());
    placeEntryVine(s_vineGrown);
    holdClimbing(p, s_climbX, s_climbY, false);
    if(s_vineGrown >= kVineFullHeight)
    {
        s_seq = Seq::VineClimb;
        s_timer = 0;
    }
}

void vineClimb(Player_t& p)
{
    // y = $98 (NES) = punta de la liana ($90) + 8 px NES.
    s_climbY = std::fmax(s_vineTop + 16.0, s_climbY - kClimbSpeed);
    holdClimbing(p, s_climbX, s_climbY, true);
    if(s_climbY <= s_vineTop + 16.0 || ++s_timer > 60 * 6)
    {
        s_seq = Seq::VineStep;
        s_timer = 0;
    }
}

void vineStep(Player_t& p)
{
    // AutoControlPlayer(derecha) hasta x = $48 (liana + 8 px NES). La caja del Mario del motor necesita
    // 8 px mas para quedar sobre la nube y no colarse por el hueco de la liana; ahi se suelta.
    s_climbX += 2.0;
    holdClimbing(p, s_climbX, s_climbY, false);
    if(s_climbX >= s_vineX + 24.0 || ++s_timer > 60 * 2)
    {
        p.Vine = 0;
        s_seq = Seq::None;
    }
}

// ── Bandera ──────────────────────────────────────────────────────────────────

void tryStartFlag(Player_t& p)
{
    for(int i = 1; i <= numBackground; i++)
    {
        const Background_t& b = Background[i];
        if(b.Hidden || (b.Type != BGO_POLE && b.Type != BGO_POLE_TOP))
            continue;

        // El mastil es la linea central del tile; se agarra en cuanto Mario la roza (16 px de margen,
        // como el borde derecho de Mario al saltar contra el en el juego original).
        const double px = b.Location.X + b.Location.Width / 2.0;
        if(!overlaps(px - 8.0, b.Location.Y, 16.0, b.Location.Height, p.Location))
            continue;

        // Pie del mastil: el tramo mas bajo de la misma columna.
        double bottom = b.Location.Y + b.Location.Height;
        for(int j = 1; j <= numBackground; j++)
        {
            const Background_t& o = Background[j];
            if(o.Type == BGO_POLE && std::fabs(o.Location.X - b.Location.X) < 1.0)
                bottom = std::fmax(bottom, o.Location.Y + o.Location.Height);
        }

        // La bandera cuelga a la izquierda del mastil.
        s_flagBgo = 0;
        for(int j = 1; j <= numBackground; j++)
        {
            const Background_t& o = Background[j];
            if(o.Type == BGO_FLAG && std::fabs((o.Location.X + o.Location.Width) - px) < 24.0)
            {
                s_flagBgo = j;
                break;
            }
        }

        s_poleX = px;
        s_poleBottom = bottom;
        s_flagBottom = bottom - 32.0;
        s_seq = Seq::FlagSlide;
        s_timer = 0;
        PlaySound(SFX_TapeExit);
        return;
    }
}

void flagSlide(Player_t& p)
{
    p.Location.SpeedX = 0.0;
    p.Location.SpeedY = 0.0;
    p.Direction = 1;
    p.Location.X = s_poleX - p.Location.Width + 2.0;

    // Baja hasta quedar apoyado en el bloque del pie del mastil. Se sujeta la altura en cada frame:
    // el bloque ocupa todo el tile y, si Mario se hunde en el, la fisica lo expulsa hacia un lado.
    bool marioDown = false;
    const double target = s_poleBottom - p.Location.Height;
    if(p.Location.Y < target - 0.5)
        p.Location.Y = std::fmin(p.Location.Y + 4.0, target);
    else
    {
        p.Location.Y = target;
        marioDown = true;
    }

    bool flagDown = true;
    if(s_flagBgo > 0 && s_flagBgo <= numBackground)
    {
        Background_t& f = Background[s_flagBgo];
        if(f.Location.Y + f.Location.Height < s_flagBottom)
        {
            f.Location.Y = std::fmin(f.Location.Y + 4.0, s_flagBottom - f.Location.Height);
            syncLayers_BGO(s_flagBgo);
            flagDown = false;
        }
    }

    if(marioDown && flagDown && ++s_timer > 20)
    {
        // Salta al otro lado del mastil, pasado el bloque del pie (16 px a cada lado del mastil), y
        // camina al castillo.
        p.Location.X = s_poleX + 18.0;
        p.Location.Y = target;
        s_seq = Seq::FlagWalk;
        s_timer = 0;
    }
}

// ── Puente de Bowser ────────────────────────────────────────────────────────

bool axeExists()
{
    for(int i = 1; i <= numNPCs; i++)
    {
        if(NPC[i].Type == NPCID_AXE && NPC[i].Killed == 0)
            return true;
    }
    return false;
}

void startBridgeCollapse(Player_t& p)
{
    s_bridge.clear();
    for(int b = 1; b <= numBlock; b++)
    {
        if(Block[b].Type == BLOCK_BOWSER_BRIDGE && !Block[b].Hidden)
            s_bridge.push_back(b);
    }
    std::sort(s_bridge.begin(), s_bridge.end(), [](int a, int b) { return Block[a].Location.X > Block[b].Location.X; });
    s_bridgeNext = 0;

    for(int i = 1; i <= numBackground; i++)
    {
        if(Background[i].Type == BGO_BRIDGE_CHAIN)
        {
            Background[i].Hidden = true;
            syncLayers_BGO(i);
        }
    }

    p.Location.SpeedX = 0.0;
    s_seq = Seq::BridgeCollapse;
    s_timer = 0;
}

void bridgeCollapse(Player_t& p)
{
    p.Location.SpeedX = 0.0;
    s_timer++;

    if(s_bridgeNext < s_bridge.size())
    {
        if(s_timer % 4 == 0)
        {
            const int b = s_bridge[s_bridgeNext++];
            Block[b].Hidden = true;
            syncLayersTrees_Block(b);
            if(s_bridgeNext == s_bridge.size())
            {
                PlaySound(SFX_VillainKilled);
            }
        }
        return;
    }

    if(s_timer > 4 * (int)s_bridge.size() + 90)
    {
        s_seq = Seq::BridgeWalk;
        s_timer = 0;
    }
}

// ── Oleadas ─────────────────────────────────────────────────────────────────

int activeFrenzy(const Player_t& p)
{
    // El marcador (inicio o fin) mas a la derecha que Mario ya dejo atras.
    int type = 0;
    double best = -1e18;
    for(int i = 1; i <= numBackground; i++)
    {
        const int t = Background[i].Type;
        if(t != BGO_FRENZY_CHEEP && t != BGO_FRENZY_BULLET && t != BGO_FRENZY_FLAME && t != BGO_FRENZY_STOP)
            continue;
        const double x = Background[i].Location.X;
        if(x <= p.Location.X && x > best)
        {
            best = x;
            type = t;
        }
    }
    return type == BGO_FRENZY_STOP ? 0 : type;
}

int countAlive(NPCID type, int section)
{
    int n = 0;
    for(int i = 1; i <= numNPCs; i++)
    {
        if(NPC[i].Type == type && NPC[i].Killed == 0 && NPC[i].Section == section)
            n++;
    }
    return n;
}

int spawnProjectile(NPCID type, double x, double y, int dir, double sx, double sy, int section)
{
    if(numNPCs + 1 >= maxNPCs)
        return 0;

    numNPCs++;
    NPC_t& n = NPC[numNPCs];
    n = NPC_t();
    n.Type = type;
    n.Location.Width = NPCWidth(type);
    n.Location.Height = NPCHeight(type);
    n.Location.X = x;
    n.Location.Y = y;
    n.Location.SpeedX = sx;
    n.Location.SpeedY = sy;
    n.Direction = dir;
    n.Section = (uint8_t)section;
    n.Layer = LAYER_SPAWNED_NPCS;
    n.Active = true;
    n.JustActivated = 1;
    n.TimeLeft = 100;
    syncLayers_NPC(numNPCs);
    return numNPCs;
}

// ── Bowser (RunBowser, InitBowserFlame, ProcBowserFlame, ProcHammerObj) ─────
//
// El conversor pone en S1 del NPC 200: mundo (0-7) | modo dificil<<3 | X NES de la pagina<<4. Todo va en
// pixeles NES; en el motor X = X_inicial + 2*(x - x0) e Y = (y - 32)*2 - 8 (el Bowser del motor mide 72,
// el del NES 64 a esta escala: los pies quedan sobre el puente).

constexpr int kFlameTag = 0x0F1A;
constexpr int kHammerTag = 0x0A33;
constexpr int kBroHammerTag = 0x0B33;     // del Hermano Martillo: Special4 = indice del dueno
// FlameYPosData y SetFlameTimer (tablas de la ROM).
constexpr int kFlameY[4] = {0x90, 0x80, 0x70, 0x90};
constexpr int kFlameTimer[8] = {0xbf, 0x40, 0xbf, 0xbf, 0xbf, 0x40, 0x40, 0xbf};
constexpr int kBowserRange[4] = {0x21, 0x41, 0x11, 0x31};
// BowserIdentities: Goomba, Koopa verde, Buzzy, Spiny, Lakitu, Blooper, Hammer Bro, Bowser.
const NPCID kIdentity[8] = {(NPCID)89, (NPCID)173, (NPCID)27, (NPCID)36, (NPCID)47, (NPCID)235, (NPCID)29, NPCID_VILLAIN_S1};

int s_flameCtrl = 0;                // BowserFlameTimerCtrl (compartido con las llamas sueltas)
int s_flameFrenzyTimer = 0;         // FrenzyEnemyTimer

int setFlameTimer()
{
    const int t = kFlameTimer[s_flameCtrl];
    s_flameCtrl = (s_flameCtrl + 1) & 7;
    return t;
}

struct BowserSim
{
    std::string file;
    int section = -1;
    bool init = false;
    int world = 0;
    bool hard = false;
    double x0 = 0.0;                // X del motor al empezar
    int origX = 0;                  // BowserOrigXPos (byte bajo)
    int x = 0;                      // Enemy_X_Position (sin envolver)
    double y = 0.0;                 // Enemy_Y_Position * 256
    int vy = 0;
    int feet = 0x20, body = 0, dir = 2, speed = 2, range = 0x21, frameTimer = 0x20, breath = 0xdf;
    bool falling = false;
    unsigned frame = 0;
    double lastX = 0.0, lastY = 0.0;
    bool pendingIdentity = false;
    NPCID identity = NPCID_NULL;
    int identityDir = -1;
    int lastSection = 0;
};

BowserSim s_bowser;

double bowserSmbxX(const BowserSim& b) { return b.x0 + (b.x - b.origX) * 2.0; }
double bowserSmbxY(const BowserSim& b) { return (b.y / 256.0 - 32.0) * 2.0 - 8.0; }

void bowserFlame(const NPC_t& npc)
{
    // InitBowserFlame con Bowser: sale de la boca (x - $0E, y + 8) y se va ajustando 1 px por frame
    // hacia una de las alturas de FlameYPosData.
    if(s_flameFrenzyTimer != 0)
        return;
    const BowserSim& b = s_bowser;
    const int target = kFlameY[iRand(4)];
    const double fx = bowserSmbxX(b) - 0x0e * 2.0;
    const double fy = (b.y / 256.0 + 8.0 - 32.0) * 2.0;
    const int n = spawnProjectile(NPCID_VILLAIN_FIRE, fx, fy, -1, b.hard ? -2.75 : -2.5, 0.0, npc.Section);
    if(n)
    {
        NPC[n].Special5 = kFlameTag;
        NPC[n].SpecialY = (target - 32.0) * 2.0;
        NPC[n].Location.Height = 16.0;
        PlaySound(SFX_BigFireball);
    }
}

void bowserHammer(const NPC_t& npc)
{
    // SpawnHammerObj: el martillo se sostiene 14 frames sobre Bowser (x + 2, y - 10) y sale con
    // velocidad X de 1 px/frame hacia donde mira y -2 en Y, gravedad 0x10 con tope 4 (ProcHammerObj).
    // (El juego elige una de 9 ranuras al azar y solo lanza si esta libre: aqui, 1 de cada 3 intentos
    // y como mucho 4 martillos a la vez.)
    int live = 0;
    for(int i = 1; i <= numNPCs; i++)
        if(NPC[i].Type == NPCID_HEAVY_THROWN && NPC[i].Special5 == kHammerTag && NPC[i].Killed == 0)
            live++;
    if(live >= 4 || iRand(3) != 0)
        return;
    const int n = spawnProjectile(NPCID_HEAVY_THROWN, bowserSmbxX(s_bowser) + 4.0, bowserSmbxY(s_bowser) - 12.0,
                                  s_bowser.dir == 1 ? 1 : -1, 0.0, 0.0, npc.Section);
    if(n)
    {
        NPC[n].Special5 = kHammerTag;
        NPC[n].Special3 = 14;
        NPC[n].Special2 = 0;
        NPC[n].SpecialX = NPC[n].Location.X;
        NPC[n].SpecialY = NPC[n].Location.Y;
    }
}

void updateBowserProjectiles()
{
    for(int i = 1; i <= numNPCs; i++)
    {
        NPC_t& n = NPC[i];
        if(n.Killed != 0)
            continue;
        if(n.Type == NPCID_VILLAIN_FIRE && n.Special5 == kFlameTag)
        {
            const double dy = n.SpecialY - n.Location.Y;
            n.Location.SpeedY = (std::fabs(dy) < 2.0) ? dy : (dy > 0 ? 2.0 : -2.0);
        }
        else if(n.Type == NPCID_HEAVY_THROWN && (n.Special5 == kHammerTag || n.Special5 == kBroHammerTag))
        {
            if(n.Special3 > 0)
            {
                n.Special3--;
                int dir = (s_bowser.dir == 1) ? 1 : -1;
                if(n.Special5 == kHammerTag)
                {
                    n.SpecialX = bowserSmbxX(s_bowser) + 4.0;
                    n.SpecialY = bowserSmbxY(s_bowser) - 12.0;
                }
                else
                {
                    // Sobre la cabeza del Hermano (x + 2, y - 10 NES). Si el dueno ya no esta, sale ya.
                    const int o = n.Special4;
                    if(o >= 1 && o <= numNPCs && NPC[o].Type == NPCID_HEAVY_THROWER && NPC[o].Killed == 0)
                    {
                        n.SpecialX = NPC[o].Location.X + 4.0;
                        n.SpecialY = NPC[o].Location.Y - 20.0;
                        dir = NPC[o].Direction;
                        if(n.Special3 == 0)
                            OI_Smb1HammerReleased(o);
                    }
                    else
                        n.Special3 = 0;
                }
                if(n.Special3 == 0)
                {
                    n.Direction = dir;
                    n.Special2 = -4 * 64;       // -2 px NES = -4 del motor, en 1/64
                }
            }
            else
            {
                n.SpecialX += 2.0 * n.Direction;
                n.Special2 = (vbint_t)std::min(n.Special2 + 8, 8 * 64);   // 0x10/256 NES = 1/8 del motor
                n.SpecialY += n.Special2 / 64.0;
            }
            n.Location.X = n.SpecialX;
            n.Location.Y = n.SpecialY;
            n.Location.SpeedX = 0.0;
            n.Location.SpeedY = 0.0;
        }
    }
}

bool bowserActive(int section)
{
    for(int i = 1; i <= numNPCs; i++)
        if(NPC[i].Type == NPCID_VILLAIN_S1 && NPC[i].DefaultSpecial > 0 && NPC[i].Active && NPC[i].Killed == 0 &&
           NPC[i].Section == section)
            return true;
    return false;
}

void frenzy(const Player_t& p)
{
    const int type = activeFrenzy(p);
    if(type == 0)
    {
        s_frenzyTimer = 0;
        return;
    }

    s_frenzyTimer++;
    const double screenLeft = -vScreen[1].X;
    const double screenRight = screenLeft + vScreen[1].Width;

    if(type == BGO_FRENZY_CHEEP && UnderWater[p.Section] && s_frenzyTimer % 70 == 0 && countAlive(NPCID_RED_FISH_S1, p.Section) < 4)
    {
        // Bajo el agua (2-2, 7-2): entran nadando por la derecha.
        const double y = 96.0 + 32.0 * iRand(8);
        spawnProjectile(NPCID_RED_FISH_S1, screenRight - 40.0, y, -1, -1.5, 0.0, p.Section);
    }
    else if(type == BGO_FRENZY_CHEEP && !UnderWater[p.Section] && s_frenzyTimer % 45 == 0 && countAlive(NPCID_RED_FISH_S1, p.Section) < 5)
    {
        // Saltan desde abajo en arco, como en los puentes de SMB1.
        const double x = screenLeft + 64.0 + iRand((int)std::fmax(64.0, vScreen[1].Width - 128.0));
        const int dir = (iRand(2) == 0) ? -1 : 1;
        spawnProjectile(NPCID_RED_FISH_S1, x, kLevelBottom + 16.0, dir, dir * (1.5 + dRand() * 1.5), -(9.0 + dRand() * 2.0), p.Section);
    }
    else if(type == BGO_FRENZY_BULLET && s_frenzyTimer % 100 == 0 && countAlive(NPCID_BULLET, p.Section) < 3)
    {
        const double y = 64.0 + 32.0 * iRand(9);
        // Dentro del borde: un NPC que nace fuera de camara se desactiva al instante.
        spawnProjectile(NPCID_BULLET, screenRight - 40.0, y, -1, -4.0, 0.0, p.Section);
        PlaySound(SFX_Bullet);
    }
    else if(type == BGO_FRENZY_FLAME && s_flameFrenzyTimer == 0 && !bowserActive(p.Section))
    {
        // InitBowserFlame sin Bowser: desde el borde derecho a una de las alturas de FlameYPosData;
        // FrenzyEnemyTimer = SetFlameTimer + $20 ($10 en modo dificil).
        const bool hard = s_frenzyHard;
        s_flameFrenzyTimer = setFlameTimer() + (hard ? 0x10 : 0x20);
        const double y = (kFlameY[iRand(4)] - 32.0) * 2.0;
        const int n = spawnProjectile(NPCID_VILLAIN_FIRE, screenRight - 16.0, y, -1, hard ? -2.75 : -2.5, 0.0, p.Section);
        if(n)
        {
            NPC[n].Special5 = kFlameTag;
            NPC[n].SpecialY = y;
            NPC[n].Location.Height = 16.0;
        }
        PlaySound(SFX_BigFireball);
    }
}

} // namespace

namespace
{

//! Lleva la plataforma a (x, y) por velocidad (el motor arrastra al jugador); los saltos grandes
//! (vuelta del ascensor) se hacen de golpe.
void platformMoveTo(NPC_t& npc, double x, double y)
{
    const double dx = x - npc.Location.X;
    const double dy = y - npc.Location.Y;
    if(std::fabs(dx) > 64.0 || std::fabs(dy) > 64.0)
    {
        npc.Location.X = x;
        npc.Location.Y = y;
        npc.Location.SpeedX = 0.0;
        npc.Location.SpeedY = 0.0;
    }
    else
    {
        npc.Location.SpeedX = dx;
        npc.Location.SpeedY = dy;
    }
}

//! BalancePlatform: la plataforma de la derecha de la pareja lleva a las dos. Pisar una la hunde y
//! sube la otra; al bajarse frenan; si una llega arriba del todo con el jugador en la otra, caen las dos.
void balanceStep(int A, int B)
{
    NPC_t& npc = NPC[A];
    double& y = npc.SpecialY;       // la propia
    double& yo = npc.SpecialX;      // la pareja
    int v = npc.Special2;

    if(npc.Special4 == 1)           // cayendo las dos (MoveFallingPlatform: tope 3, fuerza 0x20)
    {
        int vo = npc.Special3;
        imposeGravity(y, v, 0x20, 0, 3, false);
        imposeGravity(yo, vo, 0x20, 0, 3, false);
        npc.Special2 = v;
        npc.Special3 = vo;
        return;
    }

    const bool onSelf = anyPlayerStandingOn(A);
    const bool onOther = anyPlayerStandingOn(B);

    if(((int)y >> 8) < 0x2e || ((int)yo >> 8) < 0x2e)
    {
        const bool selfTop = ((int)y >> 8) < 0x2e;
        if(selfTop ? onOther : onSelf)
        {
            npc.Special4 = 1;       // InitPlatformFall
            npc.Special2 = 0;
            npc.Special3 = 0;
        }
        else
        {
            (selfTop ? y : yo) = 0x2f * 256.0;
            npc.Special2 = 0;       // StopPlatforms
        }
        return;
    }

    const double before = y;
    if(onSelf || onOther)
    {
        if(onSelf)
            platformDown(y, v);
        else
            platformUp(y, v);
    }
    else
    {
        const int yvel = (int16_t)(v + 5);
        if(yvel < 0)
            platformDown(y, v);
        else if(yvel <= 10)
            v = 0;
        else
            platformUp(y, v);
    }
    npc.Special2 = v;
    // La pareja se mueve lo mismo en sentido contrario (en pixeles enteros, como el juego).
    yo += std::floor(before / 256.0) * 256.0 - std::floor(y / 256.0) * 256.0;
}

// -- Enemigos normales (Goomba, Koopas y sus caparazones) ------------------------------------------
// MoveNormalEnemy / EnemyToBGCollisionDet / PlayerEnemyCollision del juego, sobre los NPC del motor.
// El motor pone las colisiones (bloques, pisotones, fuego); aqui van las cifras y las reglas del NES.

constexpr double kEnemyWalk     = 1.0;                  // Enemy_X_Speed 8: medio pixel NES por frame
constexpr double kEnemyGravity  = 0x3d / 256.0 * 2.0;   // MoveD_EnemyVertically
constexpr double kEnemyFallCap  = 7.0;                  // al pasar de 3,5 px NES/frame...
constexpr double kEnemyFallMax  = 6.0;                  // ...vuelve a 3 (ImposeGravity)
constexpr double kShellKick     = 6.0;                  // 0x30
constexpr double kShellKickAir  = 3.0;                  // 0x30 - 0x18 mientras cae
constexpr int    kShellRevive   = 16;                   // EnemyIntervalTimer al pisarlo
constexpr unsigned kIntervalFrames = 21;                // IntervalTimerControl 20..0

bool isShellS1(NPCID t) { return t == NPCID_GRN_SHELL_S1 || t == NPCID_RED_SHELL_S1; }
bool isKoopaS1(NPCID t) { return t == NPCID_GRN_TURTLE_S1 || t == NPCID_RED_TURTLE_S1 || t == NPCID_UNDER_FODDER; }
bool isWalkerS1(NPCID t) { return t == NPCID_FODDER_S1 || isKoopaS1(t); }

// Estado del NES que el NPC del motor no guarda. Va por indice; la firma (posicion de origen) detecta
// que el hueco lo ocupa otro NPC y lo reinicia.
struct EnemyState
{
    double  originX = -1e9;
    double  originY = -1e9;
    unsigned lastFrame = 0;     // ultimo frame en que se movio (si no fue el anterior, reaparecio)
    bool    air = false;        // cayendo el frame anterior
    bool    resting = false;    // caparazon quieto
    int     timer = 0;          // EnemyIntervalTimer
    // Paratroopas: simulacion en unidades del NES
    bool    sim = false;
    double  y = 0.0;            // Y NES * 256
    int     v = 0;              // Y_Speed.MoveForce (8.8)
    int     primary = 0;        // XMovePrimaryCounter
    int     secondary = 0;      // XMoveSecondaryCounter
    double  x = 0.0;            // desplazamiento X NES * 256 (Hermano Martillo: X del motor)
    // Hermano Martillo (ProcHammerBro)
    int     state = 0;          // Enemy_State: bit 0 en el aire, bit 3 lanzando
    int     frameTimer = 0;     // EnemyFrameTimer: sin aterrizar mientras dure
    int     jumpTimer = 0;      // HammerBroJumpTimer
    int     throwTimer = 0;     // HammerThrowingTimer
};
std::vector<EnemyState> s_enemies;

EnemyState& enemyState(int A)
{
    if(s_enemies.size() <= size_t(A))
        s_enemies.resize(size_t(A) + 64);
    EnemyState& st = s_enemies[size_t(A)];
    const NPC_t& n = NPC[A];
    if(st.originX != n.DefaultLocationX || st.originY != n.DefaultLocationY || st.lastFrame + 1 != s_frameCounter)
    {
        const bool sameNpc = st.originX == n.DefaultLocationX && st.originY == n.DefaultLocationY;
        const bool resting = sameNpc && st.resting;
        const int timer = st.timer;
        st = EnemyState();
        st.originX = n.DefaultLocationX;
        st.originY = n.DefaultLocationY;
        // Un caparazon quieto sigue contando aunque el motor lo haya parado un momento (tuberia...).
        st.resting = resting;
        st.timer = timer;
    }
    st.lastFrame = s_frameCounter;
    return st;
}

int facePlayer(const NPC_t& n)
{
    const double cx = n.Location.X + n.Location.Width / 2.0;
    double best = -1.0;
    int dir = n.Direction ? n.Direction : -1;
    for(int B = 1; B <= numPlayers; B++)
    {
        const Player_t& p = Player[B];
        if(p.Dead || p.Section != n.Section)
            continue;
        const double px = p.Location.X + p.Location.Width / 2.0;
        if(best < 0.0 || std::abs(px - cx) < best)
        {
            best = std::abs(px - cx);
            dir = (px < cx) ? -1 : 1;
        }
    }
    return dir;
}

// El caparazon vuelve a ser Koopa y echa a andar a un lado al azar (FrameCounter & 1).
void reviveShell(int A)
{
    NPC_t& n = NPC[A];
    const double bottom = n.Location.Y + n.Location.Height;
    const double cx = n.Location.X + n.Location.Width / 2.0;
    n.Type = (n.Type == NPCID_RED_SHELL_S1) ? NPCID_RED_TURTLE_S1 : NPCID_GRN_TURTLE_S1;
    n.Location.Height = n->THeight;
    n.Location.Width = n->TWidth;
    n.Location.Y = bottom - n.Location.Height;
    n.Location.X = cx - n.Location.Width / 2.0;
    n.Direction = (s_frameCounter & 1) ? -1 : 1;
    n.Location.SpeedX = kEnemyWalk * n.Direction;
    n.Location.SpeedY = 0;
    n.Projectile = false;
    n.Special = 0;
    n.Frame = 0;
    NPCQueues::Unchecked.push_back(A);
    treeNPCUpdate(A);
}

// Paratroopas. El motor los mueve en SpecialNPC (con su modo en Special, que el conversor pone como en el
// juego: 1 salta, 2 va y viene en horizontal, 3 sube y baja) y no pasan por el movimiento comun, asi que
// aqui se deshace lo que movio y se aplica el del NES.
constexpr int    kTroopaJumpGravity = 0x1c;     // MoveJ_EnemyVertically
constexpr int    kTroopaJumpSpeed   = -0x300;   // EnemyJump: Y_Speed 0xfd
constexpr int    kTroopaFlyRange    = 0x13;     // XMoveCntr_GreenPTroopa

void troopaHop(NPC_t& n, EnemyState& st)
{
    n.Location.X -= n.Location.SpeedX;
    n.Location.Y -= n.Location.SpeedY;

    // El motor lo acaba de posar en el suelo (su salto es SpeedY = -9): salto del juego.
    if(fEqual(n.Location.SpeedY, -9.0 + Physics.NPCGravity))
        st.v = kTroopaJumpSpeed;

    const double vy = st.v / 256.0 * 2.0;
    double dummy = 0.0;
    imposeGravity(dummy, st.v, kTroopaJumpGravity, 0, 3, false);

    n.Location.SpeedX = kEnemyWalk * (n.Direction < 0 ? -1 : 1);
    n.Location.SpeedY = vy;
    n.Location.X += n.Location.SpeedX;
    n.Location.Y += n.Location.SpeedY;
}

void troopaFlyVertical(NPC_t& n, EnemyState& st)
{
    // ProcMoveRedPTroopa: oscila alrededor de su centro (0x30 por debajo del origen, o 0x20 por encima
    // si esta en la mitad de abajo), acelerando 3/256 hacia el.
    const int orig = (int)std::floor(nesY(n.DefaultLocationY));
    const int center = orig < 0x80 ? orig + 0x30 : orig - 0x20;
    if(!st.sim)
    {
        st.sim = true;
        st.y = std::floor(nesY(n.Location.Y)) * 256.0;
        st.v = 0;
    }

    const double oldY = n.Location.Y - n.Location.SpeedY;
    const int y = (int)std::floor(st.y / 256.0);
    if(st.v == 0 && y < orig)
    {
        if((s_frameCounter & 7) == 0)
            st.y += 256.0;
    }
    else
        imposeGravity(st.y, st.v, 3, 6, 2, center <= y);

    n.Location.X -= n.Location.SpeedX;
    n.Location.SpeedX = 0.0;
    n.Location.Y = smbxY(std::floor(st.y / 256.0));
    n.Location.SpeedY = n.Location.Y - oldY;
}

void troopaFlyHorizontal(NPC_t& n, EnemyState& st)
{
    // MoveFlyGreenPTroopa: XMoveCntr (tope 0x13) cada 4 frames y un vaiven de 1 px cada 4 frames.
    if(!st.sim)
    {
        st.sim = true;
        st.y = std::floor(nesY(n.Location.Y)) * 256.0;
        st.x = std::floor((n.Location.X - n.DefaultLocationX) / 2.0) * 256.0;
        st.primary = 0;
        st.secondary = 0;
    }

    if((s_frameCounter & 3) == 0)
    {
        if(st.primary & 1)
        {
            if(st.secondary != 0)
                st.secondary--;
            else
                st.primary++;
        }
        else if(st.secondary != kTroopaFlyRange)
            st.secondary++;
        else
            st.primary++;
    }

    const bool right = (st.primary & 2) != 0;
    n.Direction = right ? 1 : -1;
    st.x += (right ? st.secondary : -st.secondary) * 16.0;

    if((s_frameCounter & 3) == 0)
        st.y += (s_frameCounter & 0x40) ? 256.0 : -256.0;

    const double oldX = n.Location.X - n.Location.SpeedX;
    const double oldY = n.Location.Y - n.Location.SpeedY;
    n.Location.X = n.DefaultLocationX + std::floor(st.x / 256.0) * 2.0;
    n.Location.Y = smbxY(std::floor(st.y / 256.0));
    n.Location.SpeedX = n.Location.X - oldX;
    n.Location.SpeedY = n.Location.Y - oldY;
}

// Podoboo (MovePodoboo): sale de debajo de la pantalla (Y NES 0x102) a -7 + azar, con la gravedad de
// los saltos (0x1c, tope 3), cada (azar & 15) | 6 ticks del temporizador de intervalos.
constexpr double kPodobooStartY = 0x102 * 256.0;

void podoboo(NPC_t& n, EnemyState& st)
{
    if(!st.sim)
    {
        st.sim = true;
        st.y = kPodobooStartY;
        st.v = 0;
        st.timer = 1;               // InitPodoboo
    }
    if(s_frameCounter % kIntervalFrames == 0 && st.timer > 0)
        st.timer--;
    if(st.timer == 0)
    {
        const int rnd = iRand(256);
        st.y = kPodobooStartY;
        st.v = (int16_t)(0xf900 | (rnd | 0x80));
        st.timer = (rnd & 0xf) | 6;
    }
    imposeGravity(st.y, st.v, kTroopaJumpGravity, 0, 3, false);
    if(st.y > kPodobooStartY)       // cayo por debajo de la pantalla: ahi espera
    {
        st.y = kPodobooStartY;
        st.v = 0;
    }

    const double oldY = n.Location.Y - n.Location.SpeedY;
    n.Location.X = n.DefaultLocationX;
    n.Location.Y = smbxY(std::floor(st.y / 256.0));
    n.Location.SpeedX = 0.0;
    n.Location.SpeedY = n.Location.Y - oldY;
    // El ciclo propio del motor (salpicaduras y sonido de lava) no pinta nada en el juego original.
    n.Special = 0;
    n.Special2 = 2;
    // Escondido bajo la pantalla sigue vivo mientras Mario este cerca (el motor lo desactivaria).
    if(std::abs(n.Location.X - Player[1].Location.X) < 1000.0)
        n.TimeLeft = std::max<int>(n.TimeLeft, 100);
}

// Hermano Martillo (ProcHammerBro, SetHJ, MoveHammerBroXDir, HammerBroBGColl). Se mueve por simulacion y
// sin chocar con el escenario: al saltar atraviesa los ladrillos y solo aterriza (mirando el metatile
// bajo sus pies, x + 8 e y + 24 NES) cuando se acaba EnemyFrameTimer.
constexpr int kBroThrow     = 0x30;     // HammerThrowingTimer (0x1c en modo dificil)
constexpr int kBroThrowHard = 0x1c;

bool solidAt(double x, double y, int section)
{
    Location_t probe;
    probe.X = x;
    probe.Y = y;
    probe.Width = 1.0;
    probe.Height = 1.0;
    for(int B : treeBlockQuery(probe, SORTMODE_NONE))
    {
        const Block_t& b = Block[B];
        if(b.Hidden || b.Invis || BlockNoClipping[b.Type])
            continue;
        if(x >= b.Location.X && x < b.Location.X + b.Location.Width &&
           y >= b.Location.Y && y < b.Location.Y + b.Location.Height)
            return true;
    }
    (void)section;
    return false;
}

bool spawnBroHammer(int A)
{
    // SpawnHammerObj elige una de 9 ranuras al azar y solo lanza si esta libre.
    int live = 0;
    for(int i = 1; i <= numNPCs; i++)
        if(NPC[i].Type == NPCID_HEAVY_THROWN && NPC[i].Killed == 0 &&
           (NPC[i].Special5 == kHammerTag || NPC[i].Special5 == kBroHammerTag))
            live++;
    if(live >= 9 || iRand(9) < live)
        return false;
    const NPC_t& bro = NPC[A];
    const int h = spawnProjectile(NPCID_HEAVY_THROWN, bro.Location.X + 4.0, bro.Location.Y - 20.0,
                                  bro.Direction, 0.0, 0.0, bro.Section);
    if(!h)
        return false;
    NPC[h].Special5 = kBroHammerTag;
    NPC[h].Special4 = A;
    NPC[h].Special3 = 14;           // Misc_State 0x90: 14 frames en la mano
    NPC[h].Special2 = 0;
    NPC[h].SpecialX = NPC[h].Location.X;
    NPC[h].SpecialY = NPC[h].Location.Y;
    return true;
}

void hammerBro(int A, EnemyState& st)
{
    NPC_t& n = NPC[A];
    const bool hard = (n.DefaultSpecial >> 1) & 1;
    const double oldX = n.Location.X - n.Location.SpeedX;
    const double oldY = n.Location.Y - n.Location.SpeedY;

    if(!st.sim)
    {
        st.sim = true;
        st.x = oldX;
        st.y = std::floor(nesY(oldY)) * 256.0;
        st.v = 0;
        st.state = 0;
        st.frameTimer = 0;
        st.jumpTimer = 0;
        st.throwTimer = 0;
        st.timer = hard ? 0x50 : 0x80;      // EnemyIntervalTimer: luego va a por Mario
    }

    // DecTimers
    if(st.frameTimer > 0)
        st.frameTimer--;
    if(s_frameCounter % kIntervalFrames == 0 && st.timer > 0)
        st.timer--;

    // HammerBroBGColl (antes de moverse)
    const double ny = st.y / 256.0;
    if(solidAt(st.x + 16.0, smbxY(std::floor(ny) + 24.0), n.Section) && st.frameTimer == 0)
    {
        if(st.state & 1)
        {
            st.state &= 0x88;
            st.v = 0;                                   // EnemyLanding
            st.y = double(((int)ny & 0xf0) | 8) * 256.0;
        }
    }
    else
        st.state |= 1;

    // MoveHammerBroXDir: vaiven de 4/16 px cada 64 frames, de cara a Mario; pasado el temporizador
    // de intervalos, si Mario esta a su izquierda, va a por el (0xf8).
    const Player_t& p = Player[1];
    const bool playerRight = oldX < p.Location.X;   // PlayerEnemyDiff negativo
    auto moveXDir = [&]()
    {
        int xs = ((s_frameCounter & 0x40) == 0) ? 4 : -4;
        if(!playerRight && st.timer == 0)
            xs = -8;
        n.Direction = playerRight ? 1 : -1;
        if((st.state & 7) == 1)
            imposeGravity(st.y, st.v, 0x3d, 0, 3, false);   // MoveD_EnemyVertically
        st.x += xs / 16.0 * 2.0;
    };

    if(st.jumpTimer != 0)
    {
        st.jumpTimer--;
        if(st.throwTimer == 0)
        {
            st.throwTimer = hard ? kBroThrowHard : kBroThrow;
            if(spawnBroHammer(A))
                st.state |= 8;
            else
                st.throwTimer--;
        }
        else
            st.throwTimer--;
        moveXDir();
    }
    else if((st.state & 7) == 1)
        moveXDir();
    else
    {
        // SetHJ: salto grande (0xfa) desde abajo, pequeno (0xfd, para bajar) desde arriba, o al azar.
        const int y = (int)std::floor(st.y / 256.0) & 0xff;
        int vy;
        bool low = false;
        if(y >= 0x80)
            vy = -6;
        else if(y < 0x70)
        {
            vy = -3;
            low = true;
        }
        else
            vy = (iRand(2) != 0) ? -3 : -6;
        st.v = vy * 256;
        st.state |= 1;
        st.frameTimer = 0x20;
        if(low && hard && iRand(2) != 0)
            st.frameTimer = 0x37;
        st.jumpTimer = iRand(256) | 0xc0;
        moveXDir();
    }

    n.Location.X = st.x;
    n.Location.Y = smbxY(std::floor(st.y / 256.0));
    n.Location.SpeedX = n.Location.X - oldX;
    n.Location.SpeedY = n.Location.Y - oldY;
    // La IA propia del motor (sus saltos y martillos) queda parada; SpecialX < 0 es su pose de lanzar.
    n.Special2 = 0;
    n.SpecialX = (st.state & 8) ? -1.0 : 0.0;
}

} // namespace

void OI_Smb1HammerReleased(int A)
{
    if(A >= 1 && size_t(A) < s_enemies.size())
        s_enemies[size_t(A)].state &= ~8;      // SetHSpd: Enemy_State & 0xf7
}

bool OI_Smb1NoBlockCollision(int A)
{
    const NPC_t& n = NPC[A];
    if(n.DefaultSpecial <= 0)
        return false;
    return n.Type == NPCID_VILLAIN_S1 || n.Type == NPCID_HEAVY_THROWER || n.Type == NPCID_LAVABUBBLE;
}

void OI_Smb1Platform(int A)
{
    NPC_t& npc = NPC[A];
    const int mode = npc.Special & 15;
    const int size = (npc.Special >> 4) & 3;
    const bool cloud = (npc.Special >> 6) & 1;

    npc.Location.Width = kPlatformWidth[size < 3 ? size : 0];

    if(npc.Special5 == 0)           // estado inicial (tambien tras reaparecer)
    {
        npc.Special5 = 1;
        npc.SpecialY = std::round(nesY(npc.DefaultLocationY)) * 256.0;
        npc.SpecialX = 0.0;
        npc.Special2 = 0;
        npc.Special3 = 0;
        npc.Special4 = 0;
    }
    npc.Special5 = (npc.Special5 % 30000) + 1;
    const int frame = npc.Special5;

    double x = npc.DefaultLocationX;
    double y;

    switch(mode)
    {
    case 1: // 0x25 YMovingPlatform: oscila 128 px NES alrededor de su centro (64 por debajo o encima)
    {
        const int y0 = (int)std::lround(nesY(npc.DefaultLocationY));
        const int center = (y0 < 0x80) ? y0 + 0x40 : y0 - 0x40;
        int v = npc.Special2;
        if(((int)npc.SpecialY >> 8) >= center)
            platformUp(npc.SpecialY, v);
        else
            platformDown(npc.SpecialY, v);
        npc.Special2 = v;
        break;
    }

    case 2: // 0x28 XMovingPlatform: contadores de XMoveCntr (tope 14), empieza hacia la izquierda
    {
        int primary = npc.Special4, secondary = npc.Special3;
        if((frame & 3) == 0)
        {
            if(primary & 1)
            {
                if(secondary != 0)
                    secondary--;
                else
                    primary = (primary + 1) & 0xff;
            }
            else if(secondary != 0x0e)
                secondary++;
            else
                primary = (primary + 1) & 0xff;
        }
        npc.Special4 = primary;
        npc.Special3 = secondary;
        // Velocidad X del juego: 4.4 con signo -> 16/256 de pixel por unidad.
        npc.SpecialX += ((primary & 2) ? secondary : -secondary) * 16.0;
        break;
    }

    case 3: // 0x29 DropPlatform: solo se mueve mientras se la pisa (fuerza 0x7F, tope 2)
        if(anyPlayerStandingOn(A))
        {
            int v = npc.Special2;
            imposeGravity(npc.SpecialY, v, 0x7f, 0, 2, false);
            npc.Special2 = v;
        }
        break;

    case 4: // 0x26 / 0x2B: ascensor que sube (velocidad FF.10) y da la vuelta a los 256 px
    case 5: // 0x27 / 0x2C: ascensor que baja (00.F0)
        npc.SpecialY += (mode == 4) ? -0xf0 : 0xf0;
        npc.SpecialY = std::fmod(npc.SpecialY + 65536.0, 65536.0);
        break;

    case 6: // 0x24 balanza
    {
        const int B = balancePartner(A);
        if(B == 0)
            break;
        if(npc.DefaultLocationX > NPC[B].DefaultLocationX)
        {
            if(npc.Special4 == 0 && npc.SpecialX == 0.0)
                npc.SpecialX = std::round(nesY(NPC[B].DefaultLocationY)) * 256.0;
            balanceStep(A, B);
        }
        else
        {
            // La de la izquierda sigue lo que calcula su pareja.
            const NPC_t& ctl = NPC[B];
            if(ctl.Special5 != 0 && ctl.SpecialX != 0.0)
                npc.SpecialY = ctl.SpecialX;
        }
        break;
    }

    case 7: // 0x2A RightPlatform: al pisarla coge velocidad 0x10 (1 px/frame) y ya no para
        if(anyPlayerStandingOn(A))
            npc.Special4 = 1;
        if(npc.Special4 == 1)
            npc.SpecialX += 256.0;
        break;

    default:
        npc.Location.SpeedY = npc.Direction * 2;
        return;
    }

    if(mode == 2 || mode == 7)      // en la balanza SpecialX es la Y de la pareja
        x += npc.SpecialX / 256.0 * 2.0;
    y = smbxY(npc.SpecialY / 256.0);

    // Por encima de la fila 0 (la barra de estado del NES) no se dibuja: fotograma 2 vacio.
    npc.Frame = (y < -8.0) ? 2 : (cloud ? 1 : 0);

    platformMoveTo(npc, x, y);
}

void OI_Smb1Bowser(int A)
{
    NPC_t& npc = NPC[A];
    BowserSim& b = s_bowser;
    const int cfg = npc.DefaultSpecial;

    // Special2 = 1 marca que este NPC ya tiene simulacion: al recargar el nivel (o reaparecer tras salir de
    // camara) el motor lo devuelve a 0 y se empieza de cero, como el juego al volver a cargar a Bowser.
    if(!b.init || npc.Special2 != 1 || b.file != FileName || b.section != npc.Section)
    {
        npc.Special2 = 1;
        b = BowserSim();
        b.init = true;
        b.file = FileName;
        b.section = npc.Section;
        b.world = cfg & 7;
        b.hard = (cfg >> 3) & 1;
        b.origX = b.x = (cfg >> 4) & 0xff;
        b.x0 = npc.DefaultLocationX;
        b.y = std::round((npc.DefaultLocationY + 8.0) / 2.0 + 32.0) * 256.0;
    }

    b.frame++;
    // DecTimers: cada frame.
    if(b.frameTimer > 0) b.frameTimer--;
    if(b.breath > 0) b.breath--;

    const bool frozen = (s_seq == Seq::BridgeCollapse);   // TimerControl mientras cae el puente
    if(!b.falling && s_seq == Seq::BridgeWalk)
        b.falling = true;                                   // puente hundido: Bowser cae
    if(b.falling)
    {
        imposeGravity(b.y, b.vy, 0x0f, 0, 2, false);      // MoveD_Bowser
        if(((int)b.y >> 8) >= 0xe0 && npc.Killed == 0)
        {
            npc.Killed = 9;                                 // KillAllEnemies: desaparece sin mas
            NPCQueues::Killed.push_back(A);
        }
    }
    else if(!frozen)
    {
        bool skipMove = false;
        if(b.body < 0x80)
        {
            if(--b.feet == 0)
            {
                b.feet = 0x20;
                b.body ^= 1;
            }
            if((b.frame & 0xf) == 0)
                b.dir = 2;
            // PlayerEnemyDiff negativo: Mario ha pasado a Bowser.
            if(b.frameTimer != 0 && Player[1].Location.X > npc.Location.X)
            {
                b.dir = 1;
                b.speed = 2;
                b.frameTimer = 0x20;
                b.breath = 0x20;
                if((b.x & 0xff) > 199)
                    skipMove = true;
            }
            if(!skipMove && (b.frame & 3) == 0)
            {
                if(b.x == b.origX)
                    b.range = kBowserRange[iRand(4)];
                b.x += (b.speed == 0xff) ? -1 : b.speed;
                if(b.dir != 1)
                {
                    int d = b.x - b.origX;
                    int ns = 0xff;
                    if(d < 0)
                    {
                        d = -d;
                        ns = 1;
                    }
                    if(b.range <= d)
                        b.speed = ns;
                }
            }
        }

        // HammerChk: salto (EnemyFrameTimer) y martillos en el aire desde el 6-4.
        if(b.frameTimer == 0)
        {
            imposeGravity(b.y, b.vy, 0x0f, 0, 2, false);  // MoveEnemySlowVert
            if(b.world >= 5 && (b.frame & 3) == 0)
                bowserHammer(npc);
            if(((int)b.y >> 8) >= 0x80)
                b.frameTimer = kBowserRange[iRand(4)];
        }
        else if(b.frameTimer == 1)
        {
            b.y -= 256.0;
            b.vy = -2 * 256;
        }

        // ChkFireB: fuego en todos menos el 6-4 y el 7-4.
        if(!(b.world != 7 && b.world >= 5) && b.breath == 0)
        {
            b.breath = 0x20;
            b.body ^= 0x80;
            if(b.body & 0x80)
            {
                b.breath = setFlameTimer() - (b.hard ? 0x10 : 0);
                bowserFlame(npc);
            }
        }
    }

    // Motor: la posicion de la simulacion, aplicada como velocidad (el motor la suma en el movimiento
    // comun). No choca con el escenario (npc_update.cpp), asi que llega siempre donde dice RunBowser.
    const double tx = bowserSmbxX(b), ty = bowserSmbxY(b);
    npc.Location.SpeedX = tx - npc.Location.X;
    npc.Location.SpeedY = ty - npc.Location.Y;
    npc.Direction = (b.dir == 1) ? 1 : -1;
    npc.Special = (b.body & 0x80) ? 1 : 0;                 // boca abierta mientras dura el fuego
    b.lastX = npc.Location.X;
    b.lastY = npc.Location.Y;
    b.lastSection = npc.Section;
    s_frenzyHard = b.hard;
}

bool OI_Smb1BowserDeath(int A)
{
    const NPC_t& npc = NPC[A];
    const int world = npc.DefaultSpecial & 7;
    if(world >= 7)
        return false;
    s_bowser.pendingIdentity = true;
    s_bowser.identity = kIdentity[world];
    s_bowser.identityDir = npc.Direction;
    s_bowser.lastX = npc.Location.X;
    s_bowser.lastY = npc.Location.Y;
    s_bowser.lastSection = npc.Section;
    return true;
}

void OI_Smb1Troopa(int A)
{
    NPC_t& n = NPC[A];
    const NPCID t = n.Type;

    // Podoboo y Hermano Martillo de los niveles (S1 del conversor); los del chat, con la IA del motor.
    if((t == NPCID_LAVABUBBLE || t == NPCID_HEAVY_THROWER) && n.DefaultSpecial > 0)
    {
        EnemyState& st = enemyState(A);
        if(n.HoldingPlayer > 0 || n.Effect != NPCEFF_NORMAL || n.Killed != 0 || (t == NPCID_HEAVY_THROWER && n.Projectile))
        {
            st.sim = false;
            return;
        }
        if(t == NPCID_LAVABUBBLE)
            podoboo(n, st);
        else
            hammerBro(A, st);
        return;
    }

    // Paratroopas de los niveles (los que invoca el chat, Special 0, persiguen como en el motor)
    if((t != NPCID_GRN_FLY_TURTLE_S1 && t != NPCID_RED_FLY_TURTLE_S1) || n.Special < 1 || n.Special > 3)
        return;

    EnemyState& st = enemyState(A);
    if(n.HoldingPlayer > 0 || n.Effect != NPCEFF_NORMAL || n.Killed != 0 || n.Projectile)
    {
        st.sim = false;
        return;
    }
    if(n.Special == 1)
        troopaHop(n, st);
    else if(n.Special == 2)
        troopaFlyHorizontal(n, st);
    else
        troopaFlyVertical(n, st);
}

void OI_Smb1EnemyFacing(int A)
{
    NPC_t& n = NPC[A];
    if(!isWalkerS1(n.Type) || n.Projectile || n.HoldingPlayer > 0 || n.Effect != NPCEFF_NORMAL)
        return;
    if(n.Location.SpeedX > 0.0)
        n.Direction = 1;
    else if(n.Location.SpeedX < 0.0)
        n.Direction = -1;
}

void OI_Smb1Enemy(int A)
{
    NPC_t& n = NPC[A];
    const NPCID t = n.Type;
    const bool shell = isShellS1(t);
    if(!shell && !isWalkerS1(t))
        return;

    EnemyState& st = enemyState(A);

    if(n.HoldingPlayer > 0 || n.Effect != NPCEFF_NORMAL || n.Killed != 0 || n.Wet > 0)
    {
        st.air = false;
        st.resting = false;
        return;
    }

    // En el suelo el motor deja SpeedY a 0 y le suma su gravedad: si no es eso, esta en el aire y
    // cae como en el juego (0x3d/256 por frame; al pasar de 3,5 vuelve a 3).
    const bool air = n.Slope == 0 && !fEqual(n.Location.SpeedY, double(Physics.NPCGravity));
    if(air)
    {
        double v = n.Location.SpeedY - Physics.NPCGravity + kEnemyGravity;
        if(v >= kEnemyFallCap)
            v = kEnemyFallMax;
        n.Location.SpeedY = v;
    }

    if(shell)
    {
        if(n.Projectile && std::abs(n.Location.SpeedX) > Physics.NPCWalkingSpeed)
        {
            // Pateado: 0x30, y la mitad mientras cae.
            st.resting = false;
            n.Location.SpeedX = (n.Location.SpeedX < 0 ? -1.0 : 1.0) * (air ? kShellKickAir : kShellKick);
        }
        else if(!n.Projectile && n.Location.SpeedX == 0.0)
        {
            // Quieto: a los 16 ticks del temporizador de intervalos vuelve a salir el Koopa.
            if(!st.resting)
            {
                st.resting = true;
                st.timer = kShellRevive;
            }
            else if(s_frameCounter % kIntervalFrames == 0 && --st.timer <= 0)
            {
                st.resting = false;
                reviveShell(A);
            }
        }
        st.air = air;
        return;
    }

    st.resting = false;
    if(n.Projectile)
    {
        st.air = air;
        return;
    }

    // Al aterrizar, los Koopas (no los Goombas) se dan la vuelta hacia Mario (EnemyToBGCollisionDet).
    if(st.air && !air && t != NPCID_FODDER_S1)
        n.Direction = facePlayer(n);
    st.air = air;

    if(std::abs(n.Location.SpeedX) <= Physics.NPCWalkingSpeed + 0.001)
        n.Location.SpeedX = kEnemyWalk * (n.Direction < 0 ? -1 : 1);
}

double OI_Smb1VineGrowSpeed()
{
    return -vineGrowStep();
}

void OI_Smb1Controls()
{
    if(s_seq == Seq::None)
        return;

    Controls_t& c = Player[1].Controls;
    const bool walk = (s_seq == Seq::FlagWalk || s_seq == Seq::BridgeWalk);
    c = Controls_t();
    c.Right = walk;
}

void OI_Smb1Frame()
{
    if(GameMenu || LevelSelect || GameOutro || LevelEditor || numPlayers < 1)
        return;

    Player_t& p = Player[1];
    s_frameCounter++;

    if(FileName != s_file || p.Section != s_section)
    {
        s_file = FileName;
        s_section = p.Section;
        resetLevelState();
        s_axeSeen = axeExists();
    }

    if(p.Dead || p.TimeToLive > 0)
    {
        s_seq = Seq::None;
        return;
    }

    switch(s_seq)
    {
    case Seq::None:
        if(p.Effect == PLREFF_NORMAL)
        {
            tryStartVineEntry(p);
            tryStartVineExit(p);
            tryStartFlag(p);

            const bool axe = axeExists();
            if(s_axeSeen && !axe)
                startBridgeCollapse(p);
            s_axeSeen = axe;
        }
        break;

    case Seq::FlagSlide:
        flagSlide(p);
        break;

    case Seq::BridgeCollapse:
        bridgeCollapse(p);
        break;

    case Seq::VineExit:
        vineExit(p);
        break;

    case Seq::VineGrow:
        vineGrow(p);
        break;

    case Seq::VineClimb:
        vineClimb(p);
        break;

    case Seq::VineStep:
        vineStep(p);
        break;

    case Seq::FlagWalk:
    case Seq::BridgeWalk:
        // El portal del final carga el siguiente nivel; esto solo es una red por si no llega.
        if(++s_timer > 60 * 20)
            s_seq = Seq::None;
        break;
    }

    if(s_flameFrenzyTimer > 0)
        s_flameFrenzyTimer--;
    updateBowserProjectiles();

    // Impostor derrotado a fuego (1-4..7-4): su forma real cae dada la vuelta.
    if(s_bowser.pendingIdentity)
    {
        s_bowser.pendingIdentity = false;
        const int n = spawnProjectile(s_bowser.identity, s_bowser.lastX + 16.0, s_bowser.lastY + 24.0,
                                      s_bowser.identityDir, 0.0, 0.0, s_bowser.lastSection);
        if(n)
            KillNPC(n, 3);
    }

    if(s_seq == Seq::None)
        frenzy(p);
}
