#pragma once

#include "CoreMinimal.h"

// Iterates all actors in the current level and exports their
// name, class, location, rotation and scale to a JSON file.
class FENIXDEVTOOLS_API FFenixLevelExporter
{
public:

	// Shows a save-file dialog and writes the JSON.
	// Returns true if the file was saved successfully.
	static bool ExportCurrentLevel();

private:

	// Build the JSON string from all level actors.
	static FString BuildJson(UWorld* World);

	// Show OS save-file dialog. Returns selected path or empty string.
	static FString ShowSaveDialog();
};
