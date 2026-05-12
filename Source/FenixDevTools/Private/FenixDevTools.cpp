#include "FenixDevTools.h"
#include "FenixDevToolsCommands.h"
#include "FenixLevelExporter.h"
#include "FenixSceneImporter.h"
#include "LevelEditor.h"
#include "ToolMenus.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "FFenixDevToolsModule"

void FFenixDevToolsModule::StartupModule()
{
	UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] Module started"));

	FFenixDevToolsCommands::Register();

	PluginCommands = MakeShareable(new FUICommandList);
	PluginCommands->MapAction(
		FFenixDevToolsCommands::Get().ExportLevelToJson,
		FExecuteAction::CreateRaw(this, &FFenixDevToolsModule::ExportLevelToJson),
		FCanExecuteAction::CreateLambda([]() { return true; })
	);

	PluginCommands->MapAction(
		FFenixDevToolsCommands::Get().ImportSceneFromJson,
		FExecuteAction::CreateRaw(this, &FFenixDevToolsModule::ImportSceneFromJson),
		FCanExecuteAction::CreateLambda([]() { return true; })
	);

	// Extend the Level Editor toolbar
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
	ToolbarExtender->AddToolBarExtension(
		"Play",
		EExtensionHook::After,
		PluginCommands,
		FToolBarExtensionDelegate::CreateRaw(this, &FFenixDevToolsModule::AddToolbarButton)
	);
	LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);

	UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] Toolbar button registered"));
}

void FFenixDevToolsModule::ShutdownModule()
{
	FFenixDevToolsCommands::Unregister();
}

void FFenixDevToolsModule::AddToolbarButton(FToolBarBuilder& Builder)
{
	UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] AddToolbarButton called"));

	Builder.AddToolBarButton(
		FFenixDevToolsCommands::Get().ExportLevelToJson,
		NAME_None,
		LOCTEXT("ExportLevelBtn", "Export Level"),
		LOCTEXT("ExportLevelTooltip", "Export all level actors to a Fenix JSON file"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Export")
	);

	Builder.AddToolBarButton(
		FFenixDevToolsCommands::Get().ImportSceneFromJson,
		NAME_None,
		LOCTEXT("ImportSceneBtn", "Import Scene"),
		LOCTEXT("ImportSceneTooltip", "Load a Fenix story JSON and spawn a scene into the level"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Import")
	);
}

void FFenixDevToolsModule::ExportLevelToJson()
{
	UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] Export button clicked"));
	FFenixLevelExporter::ExportCurrentLevel();
}

void FFenixDevToolsModule::ImportSceneFromJson()
{
	UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] Import button clicked"));
	FFenixSceneImporter::ShowImportDialog();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FFenixDevToolsModule, FenixDevTools)
