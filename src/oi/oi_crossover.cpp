/*
 * OverInteractive: personajes invitados de otras franquicias. Ver oi_crossover.h.
 */
#include "oi_crossover.h"

#include <cmath>
#include <cstring>

#include "../eff_id.h"
#include "../effect.h"
#include "../floats.h"
#include "../globals.h"
#include "../layers.h"
#include "../npc.h"
#include "../npc/npc_queues.h"
#include "../npc_traits.h"
#include "../player.h"
#include "../rand.h"
#include "../sound.h"

using namespace OiCrossover;

namespace
{

// Vida (golpes que aguanta) de cada uno.
constexpr int kHpGoku = 5, kHpSonic = 3, kHpMadara = 4, kHpClone = 1;
constexpr int kInvul = 40;                  // frames sin recibir daño tras un golpe

bool isCharacter(NPCID t) { return t == NPC_GOKU || t == NPC_SONIC || t == NPC_MADARA; }
bool isProjectile(NPCID t) { return t == NPC_KI_BLAST || t == NPC_KAMEHAME || t == NPC_KUNAI || t == NPC_KATON; }

int nearestPlayer(const NPC_t& n)
{
    int best = 0;
    double bestD = 0.0;
    for(int B = 1; B <= numPlayers; B++)
    {
        const Player_t& p = Player[B];
        if(p.Dead || p.TimeToLive > 0 || p.Section != n.Section)
            continue;
        const double d = std::fabs((p.Location.X + p.Location.Width / 2) - (n.Location.X + n.Location.Width / 2));
        if(best == 0 || d < bestD)
        {
            best = B;
            bestD = d;
        }
    }
    return best;
}

double centerX(const Location_t& l) { return l.X + l.Width / 2.0; }
double centerY(const Location_t& l) { return l.Y + l.Height / 2.0; }

bool grounded(const NPC_t& n)
{
    // Tras el choque con el suelo el motor deja SpeedY a 0 (y en la IA ya lleva sumada la gravedad de un frame).
    return n.Slope > 0 || std::fabs(n.Location.SpeedY) < 0.01 || fEqual(n.Location.SpeedY, double(Physics.NPCGravity));
}

int spawn(NPCID type, double cx, double cy, int dir, double sx, double sy, int section)
{
    if(numNPCs + 1 >= maxNPCs)
        return 0;
    numNPCs++;
    NPC_t& n = NPC[numNPCs];
    n = NPC_t();
    n.Type = type;
    n.Location.Width = NPCWidth(type);
    n.Location.Height = NPCHeight(type);
    n.Location.X = cx - n.Location.Width / 2.0;
    n.Location.Y = cy - n.Location.Height / 2.0;
    n.Location.SpeedX = sx;
    n.Location.SpeedY = sy;
    n.DefaultLocationX = n.Location.X;
    n.DefaultLocationY = n.Location.Y;
    n.Direction = dir;
    n.DefaultDirection = dir;
    n.Section = (uint8_t)section;
    n.Layer = LAYER_SPAWNED_NPCS;
    n.Active = true;
    n.JustActivated = 1;
    n.TimeLeft = 100;
    n.SpecialX = sx;                        // los proyectiles guardan aquí su velocidad
    n.SpecialY = sy;
    syncLayers_NPC(numNPCs);
    return numNPCs;
}

void poof(const NPC_t& n)
{
    Location_t l = n.Location;
    l.X = centerX(n.Location) - 16.0;
    l.Y = centerY(n.Location) - 16.0;
    l.Width = l.Height = 32.0;
    NewEffect(EFFID_SMOKE_S3, l);
}

void kill(int A)
{
    NPC_t& n = NPC[A];
    if(n.Killed != 0)
        return;
    poof(n);
    n.Killed = 9;
    NPCQueues::Killed.push_back(A);
}

// ── Goku: se mantiene a media distancia de Mario, le lanza bolas de energía y de vez en cuando carga un
// Kamehameha (un rayo horizontal que cruza media pantalla). ─────────────────────────────────────────────
enum GokuState { G_WALK = 0, G_CHARGE = 1, G_FIRE = 2 };

void fireKamehameha(const NPC_t& g)
{
    // La bola grande de energía (la del especial del Goku de MUGEN) sale de sus manos y cruza la pantalla.
    const int dir = g.Direction < 0 ? -1 : 1;
    spawn(NPC_KAMEHAME, centerX(g.Location) + dir * 40.0, g.Location.Y + 20.0, dir, 6.0 * dir, 0.0, g.Section);
    PlaySound(SFX_BigFireball);
}

void goku(NPC_t& n)
{
    const int P = nearestPlayer(n);
    if(!P)
        return;
    const Player_t& p = Player[P];
    const double dx = centerX(p.Location) - centerX(n.Location);
    if(n.Special != G_FIRE)
        n.Direction = dx < 0 ? -1 : 1;

    if(n.SpecialY == 0.0)
    {
        n.SpecialY = 1.0;
        n.Special = G_WALK;
        n.Special2 = 240 + iRand(120);      // hasta el Kamehameha
        n.Special4 = 90;                    // hasta la próxima bola
    }

    switch(n.Special)
    {
    case G_WALK:
        // A unos 160 px de Mario: se acerca o se aleja; salta si se atasca contra algo.
        if(std::fabs(dx) > 200.0)
            n.Location.SpeedX = 1.6 * n.Direction;
        else if(std::fabs(dx) < 120.0)
            n.Location.SpeedX = -1.2 * n.Direction;
        else
            n.Location.SpeedX = 0.0;
        if(grounded(n) && iRand(160) == 0)
            n.Location.SpeedY = -7.0;
        if(--n.Special4 <= 0)
        {
            n.Special4 = 130 + iRand(60);
            const double vx = dx, vy = centerY(p.Location) - centerY(n.Location);
            const double len = std::max(1.0, std::sqrt(vx * vx + vy * vy));
            spawn(NPC_KI_BLAST, centerX(n.Location) + n.Direction * 20.0, n.Location.Y + 24.0, n.Direction,
                  4.0 * vx / len, 4.0 * vy / len, n.Section);
            PlaySound(SFX_Fireball);
        }
        if(--n.Special2 <= 0)
        {
            n.Special = G_CHARGE;
            n.Special2 = 70;
            PlaySound(SFX_Transform);
        }
        break;

    case G_CHARGE:
        n.Location.SpeedX = 0.0;
        if(--n.Special2 <= 0)
        {
            n.Special = G_FIRE;
            n.Special2 = 80;
            fireKamehameha(n);
        }
        break;

    case G_FIRE:
        n.Location.SpeedX = 0.0;
        if(--n.Special2 <= 0)
        {
            n.Special = G_WALK;
            n.Special2 = 300 + iRand(180);
        }
        break;
    }
}

// ── Sonic: corre hacia Mario, salta de vez en cuando y cada pocos segundos se hace bola, carga y sale
// disparado (en bola no se le puede pisar). ────────────────────────────────────────────────────────────────
enum SonicState { S_RUN = 0, S_CURL = 1, S_DASH = 2 };

void sonic(NPC_t& n)
{
    const int P = nearestPlayer(n);
    if(!P)
        return;
    const double dx = centerX(Player[P].Location) - centerX(n.Location);
    if(n.SpecialY == 0.0)
    {
        n.SpecialY = 1.0;
        n.Special = S_RUN;
        n.Special2 = 180 + iRand(90);
    }

    switch(n.Special)
    {
    case S_RUN:
        // Corre a 3 px/frame; al pasarse de Mario da la vuelta (con margen para no temblar).
        if(std::fabs(dx) > 48.0)
            n.Direction = dx < 0 ? -1 : 1;
        n.Location.SpeedX = 3.0 * n.Direction;
        if(grounded(n) && iRand(100) == 0)
            n.Location.SpeedY = -8.0;
        if(--n.Special2 <= 0 && grounded(n))
        {
            n.Special = S_CURL;
            n.Special2 = 40;
            n.Direction = dx < 0 ? -1 : 1;
            PlaySound(SFX_Spring);
        }
        break;

    case S_CURL:
        n.Location.SpeedX = 0.0;
        if(--n.Special2 <= 0)
        {
            n.Special = S_DASH;
            n.Special2 = 60;
            PlaySound(SFX_HeroDash);
        }
        break;

    case S_DASH:
        n.Location.SpeedX = 8.0 * n.Direction;
        if(--n.Special2 <= 0)
        {
            n.Special = S_RUN;
            n.Special2 = 200 + iRand(120);
        }
        break;
    }
}

// ── Madara: camina a media distancia de Mario y le lanza kunais; cada pocos segundos hace los sellos de mano y
// escupe la gran bola de fuego (Katon). Al primer golpe se desdobla en dos clones. ────────────────────────────
enum MadaraState { M_WALK = 0, M_SIGNS = 1, M_FIRE = 2, M_THROW = 3 };

void katon(const NPC_t& n)
{
    const int dir = n.Direction < 0 ? -1 : 1;
    spawn(NPC_KATON, centerX(n.Location) + dir * 44.0, n.Location.Y + 14.0, dir, 4.0 * dir, 0.0, n.Section);
    PlaySound(SFX_BigFireball);
}

void madara(NPC_t& n)
{
    const int P = nearestPlayer(n);
    if(!P)
        return;
    const double dx = centerX(Player[P].Location) - centerX(n.Location);
    if(n.Special != M_FIRE)
        n.Direction = dx < 0 ? -1 : 1;
    if(n.SpecialY == 0.0)
    {
        n.SpecialY = 1.0;
        n.Special = M_WALK;
        n.Damage = 70 + iRand(40);          // hasta el próximo kunai (Damage no lo usa el motor aquí)
        n.SpecialX = 260 + iRand(120);      // hasta el próximo Katon (los clones no lo hacen)
    }

    switch(n.Special)
    {
    case M_WALK:
    case M_THROW:
        // A unos 150 px de Mario: se acerca o se aleja; salta de vez en cuando.
        if(std::fabs(dx) > 190.0)
            n.Location.SpeedX = 1.4 * n.Direction;
        else if(std::fabs(dx) < 110.0)
            n.Location.SpeedX = -1.0 * n.Direction;
        else
            n.Location.SpeedX = 0.0;
        if(grounded(n) && iRand(200) == 0)
            n.Location.SpeedY = -7.0;
        if(n.Special == M_THROW && --n.Special2 <= 0)
            n.Special = M_WALK;
        n.Damage -= 1.0;
        if(n.Damage <= 0.0)
        {
            n.Damage = 90 + iRand(50);
            spawn(NPC_KUNAI, centerX(n.Location) + n.Direction * 20.0, n.Location.Y + 22.0, n.Direction,
                  7.0 * n.Direction, 0.0, n.Section);
            n.Special = M_THROW;
            n.Special2 = 15;
            PlaySound(SFX_Throw);
        }
        if(n.Special4 != 1)
        {
            n.SpecialX -= 1.0;
            if(n.SpecialX <= 0.0 && grounded(n))
            {
                n.Special = M_SIGNS;
                n.Special2 = 50;
                PlaySound(SFX_Transform);
            }
        }
        break;

    case M_SIGNS:
        n.Location.SpeedX = 0.0;
        if(--n.Special2 <= 0)
        {
            n.Special = M_FIRE;
            n.Special2 = 45;
            katon(n);
        }
        break;

    case M_FIRE:
        n.Location.SpeedX = 0.0;
        if(--n.Special2 <= 0)
        {
            n.Special = M_WALK;
            n.SpecialX = 300 + iRand(150);
        }
        break;
    }
}

void shadowClones(const NPC_t& n)
{
    for(int side = -1; side <= 1; side += 2)
    {
        const int c = spawn(NPC_MADARA, centerX(n.Location) + side * 64.0, centerY(n.Location), -side, 0.0, -4.0, n.Section);
        if(c)
        {
            NPC[c].Special4 = 1;            // clon: un golpe y se deshace
            NPC[c].SpecialX = 0.0;
            NPC[c].SpecialY = 0.0;
            poof(NPC[c]);
        }
    }
    PlaySound(SFX_Transform);
}

// ── Proyectiles: rectos, atraviesan el escenario y duran un rato. ────────────────────────────────────────
void projectile(int A, NPC_t& n)
{
    n.Location.SpeedX = n.SpecialX;
    n.Location.SpeedY = n.SpecialY;
    n.TimeLeft = std::max<int>(n.TimeLeft, 10);
    if(++n.Special2 > 240)
    {
        n.Killed = 9;
        NPCQueues::Killed.push_back(A);
    }
}

void setTrait(int t, int w, int h, int wg, int hg, int frames, bool noGravity, bool jumpHurt)
{
    NPCTraits[t].TWidth = (int16_t)w;
    NPCTraits[t].THeight = (int16_t)h;
    NPCTraits[t].WidthGFX = (int16_t)wg;
    NPCTraits[t].HeightGFX = (int16_t)hg;
    NPCTraits[t].TFrames = (int16_t)frames;
    NPCTraits[t].FrameStyle = 1;
    NPCTraits[t].NoGravity = noGravity;
    NPCTraits[t].NoClipping = noGravity;
    NPCTraits[t].JumpHurt = jumpHurt;
    NPCTraits[t].NoYoshi = true;
    NPCTraits[t].NoIceBall = true;
    NPCTraits[t].Score = 6;
}

} // namespace

void OI_CrossoverSetup()
{
    // Sprites de personajes de MUGEN (oi-crossover/mugen_chars.py): celdas con el eje de los pies centrado; el
    // motor dibuja el gráfico con su borde de abajo en el de la caja (+ FrameOffsetY = píxeles bajo los pies), así
    // quedan con los pies en el suelo; los ataques, centrados en su caja.
    setTrait(NPC_GOKU,     24, 40, 48, 56, 11, false, false);
    setTrait(NPC_SONIC,    24, 36, 34, 54, 10, false, false);
    setTrait(NPC_MADARA,   24, 46, 62, 56, 11, false, false);
    setTrait(NPC_KI_BLAST, 24, 24, 32, 32, 0, true, true);
    setTrait(NPC_KAMEHAME, 56, 40, 72, 64, 3, true, true);
    setTrait(NPC_KUNAI,    24, 10, 32, 16, 1, true, true);
    setTrait(NPC_KATON,    56, 40, 88, 64, 6, true, true);
    NPCTraits[NPC_GOKU].FrameOffsetY = 1;
    NPCTraits[NPC_SONIC].FrameOffsetY = 14;
    NPCTraits[NPC_MADARA].FrameOffsetY = 3;
    NPCTraits[NPC_KI_BLAST].FrameOffsetY = 4;
    NPCTraits[NPC_KAMEHAME].FrameOffsetY = 12;
    NPCTraits[NPC_KUNAI].FrameOffsetY = 3;
    NPCTraits[NPC_KATON].FrameOffsetY = 12;
}

bool OI_IsCrossover(int A)
{
    const NPCID t = NPC[A].Type;
    return isCharacter(t) || isProjectile(t);
}

void OI_CrossoverNpc(int A)
{
    NPC_t& n = NPC[A];
    if(n.Killed != 0 || n.Effect != NPCEFF_NORMAL || n.HoldingPlayer > 0)
        return;
    const NPCID t = n.Type;
    if(isProjectile(t))
    {
        projectile(A, n);
        return;
    }
    if(!isCharacter(t))
        return;
    if(n.Special5 > 0)
        n.Special5--;
    if(t == NPC_GOKU)
        goku(n);
    else if(t == NPC_SONIC)
        sonic(n);
    else
        madara(n);
}

bool OI_CrossoverHit(int A, int B, int C)
{
    NPC_t& n = NPC[A];
    const NPCID t = n.Type;
    if(!isCharacter(t) && !isProjectile(t))
        return false;
    if(n.Killed != 0)
        return true;

    // Lava, caída al vacío o aplastado: fuera sin más.
    if(B == 6 || B == 9)
    {
        kill(A);
        return true;
    }
    if(isProjectile(t))
        return true;                        // los ataques no se pueden romper
    // Sus propios ataques no le hacen daño.
    if(B == 3 && C >= 1 && C <= numNPCs && isProjectile(NPC[C].Type))
        return true;
    if(B != 1 && B != 2 && B != 3 && B != 4 && B != 5 && B != 7 && B != 8 && B != 10)
        return true;

    // Sonic hecho bola: pisarlo duele.
    if(t == NPC_SONIC && B == 1 && n.Special != S_RUN && C >= 1 && C <= numPlayers)
    {
        PlayerHurt(C);
        return true;
    }
    if(n.Special5 > 0)
        return true;

    n.Special3++;
    n.Special5 = kInvul;
    const int hp = (t == NPC_GOKU) ? kHpGoku : (t == NPC_SONIC) ? kHpSonic : (n.Special4 == 1 ? kHpClone : kHpMadara);
    if(n.Special3 >= hp)
    {
        PlaySound(t == NPC_MADARA && n.Special4 == 1 ? SFX_Transform : SFX_BossBeat);
        kill(A);
        return true;
    }
    PlaySound(B == 1 ? SFX_Stomp : SFX_SMBossHit);
    if(t == NPC_MADARA && n.Special4 != 1 && n.Special3 == 1)
        shadowClones(n);
    return true;
}

void OI_CrossoverFrames()
{
    static unsigned tick = 0;
    tick++;
    for(int A = 1; A <= numNPCs; A++)
    {
        NPC_t& n = NPC[A];
        const NPCID t = n.Type;
        if(n.Killed != 0 || !(isCharacter(t) || isProjectile(t)))
            continue;
        int f = 0;
        const bool moving = std::fabs(n.Location.SpeedX) > 0.2;
        switch((int)t)
        {
        case (int)NPC_GOKU:
            // 0-1 quieto, 2-5 andar, 6 en el aire, 7-8 cargar, 9-10 lanzar (orden de mugen_goku.py)
            if(n.Special == G_CHARGE)
                f = 7 + (tick / 6) % 2;
            else if(n.Special == G_FIRE)
                f = n.Special2 > 60 ? 9 : 10;
            else if(!grounded(n))
                f = 6;
            else if(moving)
                f = 2 + (tick / 5) % 4;
            else
                f = (tick / 16) % 2;
            break;
        case (int)NPC_SONIC:
            // 0 quieto, 1-4 correr, 5 en el aire, 6-7 bola cargando, 8-9 bola rodando (mugen_chars.py)
            if(n.Special == S_CURL)
                f = 6 + (tick / 3) % 2;
            else if(n.Special == S_DASH)
                f = 8 + (tick / 2) % 2;
            else if(!grounded(n))
                f = 5;
            else
                f = moving ? 1 + (tick / 4) % 4 : 0;
            break;
        case (int)NPC_MADARA:
            // 0-1 quieto, 2-5 andar, 6 en el aire, 7-8 sellos, 9 kunai, 10 fuego (mugen_chars.py)
            if(n.Special == M_SIGNS)
                f = 7 + (tick / 8) % 2;
            else if(n.Special == M_FIRE)
                f = 10;
            else if(n.Special == M_THROW)
                f = 9;
            else if(!grounded(n))
                f = 6;
            else if(moving)
                f = 2 + (tick / 6) % 4;
            else
                f = (tick / 16) % 2;
            break;
        case (int)NPC_KATON:
            f = (tick / 4) % 6;
            break;
        case (int)NPC_KI_BLAST:
            n.Frame = (tick / 4) % 2;
            continue;
        case (int)NPC_KAMEHAME:
            f = (tick / 4) % 3;
            break;
        default:
            break;
        }
        const int frames = NPCTraits[t].TFrames > 0 ? NPCTraits[t].TFrames : 1;
        n.Frame = f + (n.Direction > 0 ? frames : 0);
    }
}

NPCID OI_CrossoverByName(const char* name)
{
    if(!name)
        return NPCID_NULL;
    if(!std::strcmp(name, "goku"))
        return NPC_GOKU;
    if(!std::strcmp(name, "sonic"))
        return NPC_SONIC;
    if(!std::strcmp(name, "madara"))
        return NPC_MADARA;
    return NPCID_NULL;
}
