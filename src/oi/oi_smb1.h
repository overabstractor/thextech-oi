/*
 * OverInteractive: comportamientos de Super Mario Bros que el motor no trae.
 *
 * El conversor de niveles (oi-smb1-levels/convert.py) y este modulo comparten unos IDs reservados:
 * el conversor coloca los objetos con esos IDs y aqui se reconocen.
 *
 * Plataformas: la plataforma SMB1 del motor (NPCID_PLATFORM_S1) solo sabe subir o bajar sin
 * parar. El conversor le pone en S1 el objeto original de SMB1 y aqui se simula con la misma
 * aritmetica que el juego (ImposeGravity, XMoveCntr...), en pixeles NES:
 *
 *   bits 0-3  movimiento
 *               0  de serie (sube/baja segun Direction)
 *               1  oscila en vertical 128 px              (SMB1 0x25)
 *               2  va y viene en horizontal, empieza a la izquierda (SMB1 0x28)
 *               3  cae mientras se la pisa                 (SMB1 0x29)
 *               4  ascensor que sube, vuelta cada 256 px   (SMB1 0x26, 0x2B)
 *               5  ascensor que baja                       (SMB1 0x27, 0x2C)
 *               6  balanza, por parejas                    (SMB1 0x24)
 *               7  al pisarla avanza a la derecha y ya no para (SMB1 0x2A)
 *   bits 4-5  ancho: 0 grande (96), 1 castillo o modo dificil (64), 2 pequena (48)
 *   bit  6    grafico de nube (fotograma 1; el 2 es vacio, por encima de la fila 0)
 *   bits 7-   numero de pareja de la balanza
 *
 * Se mueve por VELOCIDAD, no por posicion, para que el motor arrastre al jugador que va encima.
 * El ancho se reaplica cada frame: el motor lo devuelve al del tipo al reaparecer.
 *
 * Secuencias (con los IDs reservados de abajo):
 *   - Bandera: al tocar el mastil Mario se desliza con la bandera y camina solo hasta el castillo,
 *     donde el portal del conversor carga el siguiente nivel.
 *   - Puente de Bowser: al coger el hacha el puente se hunde de derecha a izquierda, Bowser cae y
 *     Mario camina solo hacia la sala del final (portal al siguiente mundo).
 *   - Oleadas: marcadores invisibles que activan cheeps voladores, balas o llamas de Bowser mientras
 *     Mario este pasado el marcador y antes del de fin.
 */
#pragma once

#ifndef OI_SMB1_H
#define OI_SMB1_H

namespace OiSmb1
{
// Decorados (BGO)
constexpr int BGO_POLE_TOP       = 147;
constexpr int BGO_POLE           = 154;
constexpr int BGO_FLAG           = 155;
constexpr int BGO_BRIDGE_CHAIN   = 142;
constexpr int BGO_FRENZY_CHEEP   = 146;
constexpr int BGO_FRENZY_BULLET  = 145;
constexpr int BGO_FRENZY_FLAME   = 144;
constexpr int BGO_FRENZY_STOP    = 143;
constexpr int BGO_VINE_ENTRY     = 148;   // llegada trepando (cielo de monedas / zona de warp del 4-2)
constexpr int BGO_VINE_EXIT      = 149;   // columna de una liana con salida por arriba
// Bloques
constexpr int BLOCK_BOWSER_BRIDGE = 450;
}

//! Movimiento de una plataforma SMB1 con S1 > 0. Se llama desde NPCSpecial.
void OI_Smb1Platform(int A);

//! Velocidad de la cabeza de la liana que crece (medio pixel NES por frame).
double OI_Smb1VineGrowSpeed();

//! Goomba, Koopas y sus caparazones de SMB1 con las cifras y reglas del juego (MoveNormalEnemy): velocidad,
//! caida, giro hacia Mario al aterrizar, patada y caparazon que revive. Desde NPCMovementLogic, antes de mover.
void OI_Smb1Enemy(int A);

//! Paratroopas de SMB1 de los niveles (Special 1 salta, 2 vuela en horizontal, 3 en vertical) con el movimiento
//! del juego. Desde NPCMovementLogic, justo despues de SpecialNPC (que es quien los mueve en el motor).
void OI_Smb1Troopa(int A);

//! El martillo de un Hermano Martillo sale de su mano (deja la pose de lanzar).
void OI_Smb1HammerReleased(int A);

//! Enemigos de SMB1 que en el juego no chocan con el escenario (Bowser, Hermano Martillo, Podoboo): se
//! mueven solo por su simulacion. Desde npc_update.cpp, en vez de NPCBlockLogic.
bool OI_Smb1NoBlockCollision(int A);

//! El Bowser de SMB1 (S1 = mundo | modo dificil<<3 | X NES de la pagina<<4). Se llama desde NPCSpecial.
void OI_Smb1Bowser(int A);

//! Bowser derrotado a fuego: en 1-4..7-4 deja caer su forma real (la saca OI_Smb1Frame). true = el
//! motor no pone su propio efecto de muerte.
bool OI_Smb1BowserDeath(int A);

//! Antes de la fisica: fuerza los controles durante las secuencias (bandera, puente).
void OI_Smb1Controls();

//! Despues de la fisica del jugador: secuencias y oleadas.
void OI_Smb1Frame();

#endif // OI_SMB1_H
