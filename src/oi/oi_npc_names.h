/*
 * Nombres de NPC del motor, para que la app pueda pedir "spawn_rex" en vez de "spawn_npc 162".
 * La tabla la genera gen_npc_names.sh de src/npc_id.h: es el propio motor quien manda.
 */
#pragma once

#ifndef OI_NPC_NAMES_H
#define OI_NPC_NAMES_H

#include "../npc_id.h"

//! Devuelve el NPCID de un nombre en minusculas ("rex"), o NPCID_NULL si no existe.
NPCID OI_NpcByName(const char* name);

//! Cuantos nombres hay en la tabla.
int OI_NpcCount();

//! Nombre en la posicion index; si id_out no es nulo, deja ahi su NPCID. Para --dump-npcs.
const char* OI_NpcNameAt(int index, int* id_out);

#endif // OI_NPC_NAMES_H
