#pragma once

// Archipelago multiworld support for RE4 UHD.
// The module talks to the "Resident Evil 4 UHD Client" (Archipelago launcher) over 127.0.0.1:46400.

namespace re4t
{
	namespace init
	{
		void Archipelago();
	}
}

// Called once per game frame from the main game thread (cSceSys::scheduler hook in Game.cpp)
void Archipelago_Tick();

// Called from the ImGui frame in EndSceneHook.cpp
void Archipelago_Render();
