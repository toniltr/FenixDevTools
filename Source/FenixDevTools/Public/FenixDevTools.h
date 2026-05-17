#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Framework/Commands/UICommandList.h"
#include "Dom/JsonObject.h"
#include "Widgets/Input/SComboBox.h"

class FFenixDevToolsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void AddToolbarButton(FToolBarBuilder& Builder);
	void ExportLevelToJson();
	void ImportSceneFromJson();
	void ClearLevel();
	void ShowCreateSceneDialog();

	TSharedPtr<FUICommandList> PluginCommands;

	// Estado del combo
	TArray<TSharedPtr<FJsonObject>>  LoadedScenes;       // Escenas parseadas del JSON
	TArray<TSharedPtr<FString>>      SceneOptions;        // Nombres para el combo
	TSharedPtr<FString>              SelectedScene;       // Opción actualmente seleccionada
	TSharedPtr<SComboBox<TSharedPtr<FString>>> SceneComboBox; // Referencia al widget
	FString LoadedJsonPath;
	TSharedPtr<FJsonObject> LoadedJsonRoot;
	int32 SelectedSceneIndex = INDEX_NONE;

	static const FString CreateSceneOption;
};