/*
 * OverInteractive: personajes invitados de otras franquicias (Goku, Sonic, Naruto) con sus ataques.
 *
 * Usan los huecos libres de NPC 293-298 (en el pack de assets solo tienen un "?" de relleno): gráficos propios en
 * graphics/npc/npc-29X.png (oi-crossover/make_sprites.py), rasgos puestos al arrancar (OI_CrossoverSetup) y su IA
 * aquí. Funcionan en cualquier mundo. Estado en los campos Special* del NPC (el motor no les da uso a estos tipos):
 *   Special  estado          Special2 temporizador       Special3 golpes recibidos
 *   Special4 temporizador 2  Special5 invulnerable (frames)  SpecialY 1 = ya inicializado
 */
#pragma once

#ifndef OI_CROSSOVER_H
#define OI_CROSSOVER_H

#include "../npc_id.h"

namespace OiCrossover
{
constexpr NPCID NPC_GOKU      = NPCID(293);
constexpr NPCID NPC_KI_BLAST  = NPCID(294);
constexpr NPCID NPC_KAMEHAME  = NPCID(295);     // tramo del rayo
constexpr NPCID NPC_SONIC     = NPCID(296);
constexpr NPCID NPC_NARUTO    = NPCID(297);     // Special4 = 1: clon de sombra
constexpr NPCID NPC_KUNAI     = NPCID(298);
}

//! Rasgos de los NPC invitados. Desde SetupVars, antes de guardar los valores por defecto.
void OI_CrossoverSetup();

//! true si A es uno de los invitados.
bool OI_IsCrossover(int A);

//! IA: desde NPCMovementLogic, antes de aplicar la velocidad.
void OI_CrossoverNpc(int A);

//! Golpes a un invitado (pisotón, fuego, bloque...). true = resuelto aquí (NPCHit no sigue).
bool OI_CrossoverHit(int A, int B, int C);

//! Fotogramas de los invitados. Después de actualizar los NPC.
void OI_CrossoverFrames();

//! Nombre -> NPC para el puente ("goku", "sonic", "naruto"); NPCID_NULL si no es uno de ellos.
NPCID OI_CrossoverByName(const char* name);

#endif // OI_CROSSOVER_H
