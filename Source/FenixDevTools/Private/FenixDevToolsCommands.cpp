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
}

#undef LOCTEXT_NAMESPACE
