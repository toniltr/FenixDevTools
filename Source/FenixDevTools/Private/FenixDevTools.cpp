#include "FenixDevTools.h"
#include "FenixDevToolsCommands.h"
#include "FenixLevelExporter.h"
#include "LevelEditor.h"
#include "ToolMenus.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "FFenixDevToolsModule"

void FFenixDevToolsModule::StartupModule()
{
	FFenixDevToolsCommands::Register();

	PluginCommands = MakeShareable(new FUICommandList);
	PluginCommands->MapAction(
		FFenixDevToolsCommands::Get().ExportLevelToJson,
		FExecuteAction::CreateRaw(this, &FFenixDevToolsModule::ExportLevelToJson),
		FCanExecuteAction()
	);

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FFenixDevToolsModule::RegisterMenus)
	);
}

void FFenixDevToolsModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	FFenixDevToolsCommands::Unregister();
}

void FFenixDevToolsModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	// Add button to the Level Editor toolbar
	UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.AssetsToolBar");
	FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("FenixDevTools");
	Section.Label = LOCTEXT("FenixSection", "Fenix");

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		FFenixDevToolsCommands::Get().ExportLevelToJson,
		LOCTEXT("ExportLevelBtn", "Export Level"),
		LOCTEXT("ExportLevelTooltip", "Export all level actors to a Fenix JSON file"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Export")
	));
}

void FFenixDevToolsModule::ExportLevelToJson()
{
	UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] Button clicked!"));
	FFenixLevelExporter::ExportCurrentLevel();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FFenixDevToolsModule, FenixDevTools)
