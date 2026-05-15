#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

// Imports a Fenix story JSON and spawns a selected scene into the current level.
// Clears all existing Fenix actors first (keeps BP_Player, CameraActor, UltraDynamicSky).
class FENIXDEVTOOLS_API FFenixSceneImporter
{
public:

	// Show file dialog to pick a JSON, then show scene picker, then import.
	static void ShowImportDialog();
	
	static void ImportScene(const TSharedPtr<FJsonObject>& Scene);

private:

	// Parse scene list from story JSON. Returns scene names and their index.
	static bool ParseScenes(const FString& JsonStr,
		TArray<TSharedPtr<FJsonObject>>& OutScenes);

	// Show a window to pick a scene from the list.
	static void ShowScenePicker(const TArray<TSharedPtr<FJsonObject>>& Scenes);

	// Destroy all non-preserved actors in the level.
	static void ClearLevel(UWorld* World);

	// True if actor should be preserved (BP_Player, CameraActor, UDS).
	static bool ShouldPreserve(AActor* Actor);

	// Spawn a single item actor from JSON.
	static void SpawnItem(UWorld* World, const TSharedPtr<FJsonObject>& Item);

	// Spawn a single character character actor from JSON.
	static void SpawnCharacter(UWorld* World, const TSharedPtr<FJsonObject>& Character);

	// Open OS file picker for JSON files. Returns path or empty.
	static FString ShowOpenFileDialog();
};
