#include "FenixDevTools.h"
#include "FenixDevToolsCommands.h"
#include "FenixLevelExporter.h"
#include "FenixSceneImporter.h"
#include "LevelEditor.h"
#include "ToolMenus.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"
#include "IDesktopPlatform.h"
#include "DesktopPlatformModule.h"

#define LOCTEXT_NAMESPACE "FFenixDevToolsModule"

void FFenixDevToolsModule::StartupModule()
{
	FFenixDevToolsCommands::Register();

	PluginCommands = MakeShareable(new FUICommandList);
	PluginCommands->MapAction(
		FFenixDevToolsCommands::Get().ExportLevelToJson,
		FExecuteAction::CreateRaw(this, &FFenixDevToolsModule::ExportLevelToJson)
	);
	PluginCommands->MapAction(
		FFenixDevToolsCommands::Get().ImportSceneFromJson,
		FExecuteAction::CreateRaw(this, &FFenixDevToolsModule::ImportSceneFromJson)
	);

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
	ToolbarExtender->AddToolBarExtension(
		"Play",
		EExtensionHook::After,
		PluginCommands,
		FToolBarExtensionDelegate::CreateRaw(this, &FFenixDevToolsModule::AddToolbarButton)
	);
	LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);
}

void FFenixDevToolsModule::ShutdownModule()
{
	FFenixDevToolsCommands::Unregister();
}

void FFenixDevToolsModule::AddToolbarButton(FToolBarBuilder& Builder)
{
	// Botón Export
	Builder.AddToolBarButton(
		FFenixDevToolsCommands::Get().ExportLevelToJson,
		NAME_None,
		LOCTEXT("ExportLevelBtn", "Export"),
		LOCTEXT("ExportLevelTooltip", "Export level actors to Fenix JSON"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Save")
	);

	// Botón Import — carga el JSON y puebla el combo
	Builder.AddToolBarButton(
		FFenixDevToolsCommands::Get().ImportSceneFromJson,
		NAME_None,
		LOCTEXT("ImportSceneBtn", "Load Story"),
		LOCTEXT("ImportSceneTooltip", "Load a Fenix story JSON to populate the scene selector"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Import")
	);

	// Separador
	Builder.AddSeparator();

	// ComboBox de escenas
	Builder.AddWidget(
		SNew(SBox)
		.WidthOverride(200.f)
		.VAlign(VAlign_Center)
		[
			SAssignNew(SceneComboBox, SComboBox<TSharedPtr<FString>>)
			.OptionsSource(&SceneOptions)
			.IsEnabled_Lambda([this]() { return SceneOptions.Num() > 0; })
			.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type SelectType)
			{
				if (!Item.IsValid()) return;

				int32 Index = SceneOptions.IndexOfByPredicate([&](const TSharedPtr<FString>& S)
				{
					return S.IsValid() && *S == *Item;
				});

				if (!LoadedScenes.IsValidIndex(Index)) return;

				SelectedScene = Item;
				FFenixSceneImporter::ImportScene(LoadedScenes[Index]);
			})
			.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
			{
				return SNew(STextBlock).Text(FText::FromString(*Item));
			})
			.Content()
			[
				SNew(STextBlock)
				.Text_Lambda([this]() -> FText
				{
					return SelectedScene.IsValid()
						? FText::FromString(*SelectedScene)
						: LOCTEXT("ComboPlaceholder", "— Select scene —");
				})
			]
		],
		NAME_None,
		/*bSimpleMode=*/true
	);
}

void FFenixDevToolsModule::ExportLevelToJson()
{
	FFenixLevelExporter::ExportCurrentLevel();
}

void FFenixDevToolsModule::ImportSceneFromJson()
{
	// 1. File dialog
	IDesktopPlatform* DP = FDesktopPlatformModule::Get();
	if (!DP) return;

	TArray<FString> Files;
	DP->OpenFileDialog(
		nullptr,
		TEXT("Select Fenix Story JSON"),
		FPaths::ProjectContentDir(),
		TEXT(""),
		TEXT("JSON files (*.json)|*.json"),
		EFileDialogFlags::None,
		Files
	);
	if (Files.Num() == 0) return;

	// 2. Leer y parsear JSON
	FString JsonStr;
	if (!FFileHelper::LoadFileToString(JsonStr, *Files[0])) return;

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return;

	const TArray<TSharedPtr<FJsonValue>>* ScenesArr;
	if (!Root->TryGetArrayField(TEXT("scenes"), ScenesArr) || ScenesArr->Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FenixDevTools] No 'scenes' array found in JSON"));
		return;
	}

	// 3. Poblar combo
	LoadedScenes.Empty();
	SceneOptions.Empty();
	SelectedScene.Reset();

	for (const auto& Val : *ScenesArr)
	{
		const TSharedPtr<FJsonObject>* SceneObj;
		if (!Val->TryGetObject(SceneObj)) continue;

		FString Name;
		(*SceneObj)->TryGetStringField(TEXT("name"), Name);

		LoadedScenes.Add(*SceneObj);
		SceneOptions.Add(MakeShared<FString>(Name));
	}

	// 4. Refrescar el combo widget
	if (SceneComboBox.IsValid())
		SceneComboBox->RefreshOptions();

	UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] Story loaded — %d scenes available"), SceneOptions.Num());
}


#undef LOCTEXT_NAMESPACE
IMPLEMENT_MODULE(FFenixDevToolsModule, FenixDevTools)