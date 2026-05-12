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

#include "Widgets/Input/SEditableTextBox.h"
#include "Misc/Guid.h"

const FString FFenixDevToolsModule::CreateSceneOption = TEXT("+ Create New Scene");

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
				SelectedScene = Item;

				// "Create" es índice 0 del combo — no importa escena real
				if (*Item == CreateSceneOption)
				{
					SelectedSceneIndex = INDEX_NONE;
					return;
				}

				// El índice real en LoadedScenes = índice en SceneOptions - 1 (por el slot "Create")
				int32 ComboIndex = SceneOptions.IndexOfByPredicate([&](const TSharedPtr<FString>& S)
				{
					return S.IsValid() && *S == *Item;
				});

				int32 SceneIndex = ComboIndex - 1; // descuenta el slot "Create"
				if (!LoadedScenes.IsValidIndex(SceneIndex)) return;

				SelectedSceneIndex = SceneIndex;
				FFenixSceneImporter::ImportScene(LoadedScenes[SceneIndex]);
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
	 // ── Caso: "Create New Scene" seleccionado ──────────────────────
    if (SelectedScene.IsValid() && *SelectedScene == CreateSceneOption)
    {
        if (!LoadedJsonRoot.IsValid() || LoadedJsonPath.IsEmpty())
        {
            UE_LOG(LogTemp, Warning, TEXT("[FenixDevTools] No hay historia cargada. Usa 'Load Story' primero."));
            return;
        }
        ShowCreateSceneDialog();
        return;
    }

    // ── Caso 1: hay escena seleccionada → parchear el JSON de la historia ──
    if (LoadedJsonRoot.IsValid()
        && LoadedScenes.IsValidIndex(SelectedSceneIndex)
        && !LoadedJsonPath.IsEmpty())
    {
        // Construir scene_template desde el nivel actual
        TSharedPtr<FJsonObject> SceneData = FFenixLevelExporter::BuildSceneJson(
            GEditor ? GEditor->GetEditorWorldContext().World() : nullptr
        );
        if (!SceneData.IsValid()) return;

        // Recuperar el array "scenes" del root
        const TArray<TSharedPtr<FJsonValue>>* ScenesArr;
        if (!LoadedJsonRoot->TryGetArrayField(TEXT("scenes"), ScenesArr)) return;

        // Preservar el uuid y name originales de la escena seleccionada
        TSharedPtr<FJsonObject> OriginalScene = LoadedScenes[SelectedSceneIndex];
        FString OrigUUID, OrigName;
        OriginalScene->TryGetStringField(TEXT("uuid"), OrigUUID);
        OriginalScene->TryGetStringField(TEXT("name"), OrigName);

        SceneData->SetStringField(TEXT("uuid"), OrigUUID);
        SceneData->SetStringField(TEXT("name"), OrigName);

        // Actualizar el objeto en el array (in-place)
        // Reconstruimos el array con el elemento parcheado
        TArray<TSharedPtr<FJsonValue>> NewScenesArr;
        for (int32 i = 0; i < ScenesArr->Num(); ++i)
        {
            if (i == SelectedSceneIndex)
                NewScenesArr.Add(MakeShared<FJsonValueObject>(SceneData));
            else
                NewScenesArr.Add((*ScenesArr)[i]);
        }
        LoadedJsonRoot->SetArrayField(TEXT("scenes"), NewScenesArr);
        LoadedScenes[SelectedSceneIndex] = SceneData;   // sync en memoria

        // Serializar y guardar sobreescribiendo el fichero original
        FString Output;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
        FJsonSerializer::Serialize(LoadedJsonRoot.ToSharedRef(), Writer);

        if (FFileHelper::SaveStringToFile(Output, *LoadedJsonPath))
        {
            UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] Scene '%s' updated in %s"),
                *OrigName, *LoadedJsonPath);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[FenixDevTools] Failed to save patched JSON to %s"),
                *LoadedJsonPath);
        }
        return;
    }

    // ── Caso 2: no hay escena seleccionada → export genérico de siempre ──
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

	LoadedJsonPath = Files[0];
	LoadedJsonRoot = Root; 

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
	SelectedSceneIndex = INDEX_NONE;

	// ── Opción fija "Create" — siempre la primera ──
	SceneOptions.Add(MakeShared<FString>(CreateSceneOption));

	for (const auto &Val : *ScenesArr)
	{
		const TSharedPtr<FJsonObject> *SceneObj;
		if (!Val->TryGetObject(SceneObj))
			continue;

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

void FFenixDevToolsModule::ShowCreateSceneDialog()
{
    // Buffers compartidos con el diálogo vía TSharedPtr
    TSharedPtr<FString> OutName = MakeShared<FString>();
    TSharedPtr<FString> OutUUID = MakeShared<FString>(
        FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens).ToLower()
    );

    TSharedRef<SWindow> Window = SNew(SWindow)
        .Title(FText::FromString(TEXT("Create New Scene")))
        .ClientSize(FVector2D(400, 160))
        .SupportsMaximize(false)
        .SupportsMinimize(false)
        .SizingRule(ESizingRule::FixedSize);

    TWeakPtr<SWindow> WeakWindow = Window;

    // Widgets de edición
    TSharedPtr<SEditableTextBox> NameBox;
    TSharedPtr<SEditableTextBox> UUIDBox;

    TSharedRef<SWidget> Content =
        SNew(SVerticalBox)

        // ── Nombre ──────────────────────────────────────────────
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(12.f, 12.f, 12.f, 4.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .VAlign(VAlign_Center)
            .FillWidth(0.3f)
            [
                SNew(STextBlock).Text(LOCTEXT("SceneNameLabel", "Name"))
            ]
            + SHorizontalBox::Slot()
            .FillWidth(0.7f)
            [
                SAssignNew(NameBox, SEditableTextBox)
                .HintText(LOCTEXT("SceneNameHint", "My Scene"))
            ]
        ]

        // ── UUID ─────────────────────────────────────────────────
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(12.f, 4.f, 12.f, 4.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .VAlign(VAlign_Center)
            .FillWidth(0.3f)
            [
                SNew(STextBlock).Text(LOCTEXT("SceneUUIDLabel", "UUID"))
            ]
            + SHorizontalBox::Slot()
            .FillWidth(0.7f)
            [
                SAssignNew(UUIDBox, SEditableTextBox)
                .Text(FText::FromString(*OutUUID))
            ]
        ]

        // ── Botones ───────────────────────────────────────────────
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(12.f, 12.f)
        .HAlign(HAlign_Right)
        [
            SNew(SHorizontalBox)

            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(0.f, 0.f, 8.f, 0.f)
            [
                SNew(SButton)
                .Text(LOCTEXT("CreateBtn", "Create"))
                .OnClicked_Lambda([this, NameBox, UUIDBox, WeakWindow]() -> FReply
                {
                    const FString SceneName = NameBox->GetText().ToString().TrimStartAndEnd();
                    const FString SceneUUID = UUIDBox->GetText().ToString().TrimStartAndEnd();

                    if (SceneName.IsEmpty())
                    {
                        UE_LOG(LogTemp, Warning, TEXT("[FenixDevTools] Scene name cannot be empty"));
                        return FReply::Handled();
                    }
                    if (SceneUUID.IsEmpty())
                    {
                        UE_LOG(LogTemp, Warning, TEXT("[FenixDevTools] UUID cannot be empty"));
                        return FReply::Handled();
                    }

                    // Construir la nueva escena desde el nivel actual
                    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
                    TSharedPtr<FJsonObject> NewScene = FFenixLevelExporter::BuildSceneJson(World);
                    if (!NewScene.IsValid()) return FReply::Handled();

                    NewScene->SetStringField(TEXT("uuid"), SceneUUID);
                    NewScene->SetStringField(TEXT("name"), SceneName);

                    // Añadir al array "scenes" del JSON root
                    const TArray<TSharedPtr<FJsonValue>>* ScenesArr;
                    TArray<TSharedPtr<FJsonValue>> NewScenesArr;
                    if (LoadedJsonRoot->TryGetArrayField(TEXT("scenes"), ScenesArr))
                        NewScenesArr = *ScenesArr;

                    NewScenesArr.Add(MakeShared<FJsonValueObject>(NewScene));
                    LoadedJsonRoot->SetArrayField(TEXT("scenes"), NewScenesArr);

                    // Guardar al disco
                    FString Output;
                    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
                    FJsonSerializer::Serialize(LoadedJsonRoot.ToSharedRef(), Writer);
                    FFileHelper::SaveStringToFile(Output, *LoadedJsonPath);

                    // Actualizar el combo con la nueva escena
                    LoadedScenes.Add(NewScene);
                    SceneOptions.Add(MakeShared<FString>(SceneName));
                    if (SceneComboBox.IsValid())
                        SceneComboBox->RefreshOptions();

                    UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] Scene '%s' (%s) created and saved"),
                        *SceneName, *SceneUUID);

                    if (TSharedPtr<SWindow> Win = WeakWindow.Pin())
                        Win->RequestDestroyWindow();

                    return FReply::Handled();
                })
            ]

            + SHorizontalBox::Slot()
            .AutoWidth()
            [
                SNew(SButton)
                .Text(LOCTEXT("CancelBtn", "Cancel"))
                .OnClicked_Lambda([WeakWindow]() -> FReply
                {
                    if (TSharedPtr<SWindow> Win = WeakWindow.Pin())
                        Win->RequestDestroyWindow();
                    return FReply::Handled();
                })
            ]
        ];

    Window->SetContent(Content);
    FSlateApplication::Get().AddWindow(Window);
}



#undef LOCTEXT_NAMESPACE
IMPLEMENT_MODULE(FFenixDevToolsModule, FenixDevTools)