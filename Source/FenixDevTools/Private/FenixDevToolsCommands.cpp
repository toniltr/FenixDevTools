#include "FenixDevToolsCommands.h"

#define LOCTEXT_NAMESPACE "FFenixDevToolsModule"

void FFenixDevToolsCommands::RegisterCommands()
{
	UI_COMMAND(
		ExportLevelToJson,
		"Export Level",
		"Export all actors in the current level to a Fenix JSON file",
		EUserInterfaceActionType::Button,
		FInputChord()
	);

	UI_COMMAND(
		ImportSceneFromJson,
		"Import Scene",
		"Load a Fenix story JSON and spawn a scene into the current level",
		EUserInterfaceActionType::Button,
		FInputChord()
	);

	UI_COMMAND(
		ClearLevel,
		"Clear Level",
		"Destroy all actors except BP_Player, CameraActor and Ultra Dynamic Sky",
		EUserInterfaceActionType::Button,
		FInputChord()
	);
}

#undef LOCTEXT_NAMESPACE
