# TheXTech — fork de OverInteractive

Fork de [TheXTech](https://github.com/TheXTech/TheXTech) 1.3.7.3 (GPLv3) que usa OverInteractive para que el
público de un directo (TikTok, Twitch, Kick) invoque enemigos y power-ups en el juego. Se distribuye bajo la
misma licencia GPLv3 que el original (ver `LICENSE`).

Cambios respecto al original (todo en `src/oi/` salvo unos pocos ganchos marcados con `OverInteractive:`):

- `oi_bridge`: cliente TCP a `127.0.0.1:58485` (JSON terminado en NUL) que recibe efectos de la app y los
  aplica en el hilo del juego: invocar NPCs por nombre o id, y consultas de depuración.
- `oi_npc_names`: tabla nombre → NPCID generada desde `src/npc_id.h`.
- `oi_smb1`: reglas del Super Mario Bros original (plataformas, bandera, puente de Bowser, lianas, oleadas e IA
  de Goombas, Koopas, Paratroopas, Podoboo, Hermano Martillo y Bowser) para el episodio convertido. Solo se
  aplican a los mundos cuya carpeta trae `oi-smb1.txt`; el resto juega con las reglas del motor.

Compilación: igual que el original (CMake + Ninja; probado con MSVC 2022 y
`-DTHEXTECH_ENABLE_LUA=OFF -DTHEXTECH_ENABLE_LUAU=OFF`).
