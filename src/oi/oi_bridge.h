/*
 * OverInteractive bridge for TheXTech.
 *
 * Canal con la app: cliente TCP a 127.0.0.1:58485, protocolo "Sail" (JSON terminado en NUL),
 * el mismo que hablan Ocarina of Time y Super Mario 64. La app hospeda el listener y el juego
 * reintenta la conexion cada segundo, asi que da igual quien arranque primero.
 *
 * Todo lo que llega se encola en el hilo de red y se ejecuta en OI_Poll(), que corre dentro del
 * frame del juego: nada toca los globales del motor desde otro hilo.
 */
#pragma once

#ifndef OI_BRIDGE_H
#define OI_BRIDGE_H

//! true si el episodio se arranco por linea de comandos (asi lo lanza la app): sin pausa de
//! "anadir jugador"; el primer mando o tecla que se use se asigna solo.
extern bool g_oiCliEpisode;

//! Arranca el hilo de red. Idempotente.
void OI_Init();

//! Cierra la conexion y para el hilo.
void OI_Shutdown();

//! Drena la cola y ejecuta los efectos. Se llama una vez por frame desde GameLoop().
void OI_Poll();

//! Justo despues de que el motor lea los mandos: aplica los botones forzados (debug_hold).
void OI_AfterControls();

#endif // OI_BRIDGE_H
