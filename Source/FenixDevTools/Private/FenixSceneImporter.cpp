#include "FenixSceneImporter.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/Actor.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Views/SListView.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"

// ── File Dialog ───────────────────────────────────────────────

FString FFenixSceneImporter::ShowOpenFileDialog()
{
	IDesktopPlatform* DP = FDesktopPlatformModule::Get();
	if (!DP) return TEXT("");

	TArray<FString> Files;
	DP->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Select Fenix Story JSON"),
		FPaths::ProjectContentDir(),
		TEXT(""),
		TEXT("JSON Files (*.json)|*.json"),
		EFileDialogFlags::None,
		Files
	);
	return Files.Num() > 0 ? Files[0] : TEXT("");
}

// ── Entry Point ───────────────────────────────────────────────

void FFenixSceneImporter::ShowImportDialog()
{
	const FString FilePath = ShowOpenFileDialog();
	if (FilePath.IsEmpty()) return;

	FString JsonStr;
	if (!FFileHelper::LoadFileToString(JsonStr, *FilePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("[FenixDevTools] Could not read file: %s"), *FilePath);
		return;
	}

	TArray<TSharedPtr<FJsonObject>> Scenes;
	if (!ParseScenes(JsonStr, Scenes))
	{
		UE_LOG(LogTemp, Warning, TEXT("[FenixDevTools] Could not parse scenes from JSON"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] Found %d scenes"), Scenes.Num());
	ShowScenePicker(Scenes);
}

// ── Parse ─────────────────────────────────────────────────────

bool FFenixSceneImporter::ParseScenes(const FString& JsonStr,
	TArray<TSharedPtr<FJsonObject>>& OutScenes)
{
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return false;

	const TArray<TSharedPtr<FJsonValue>>* ScenesArr;
	if (!Root->TryGetArrayField(TEXT("scenes"), ScenesArr)) return false;

	for (const auto& Val : *ScenesArr)
	{
		const TSharedPtr<FJsonObject>* Obj;
		if (Val->TryGetObject(Obj))
			OutScenes.Add(*Obj);
	}
	return OutScenes.Num() > 0;
}

// ── Scene Picker Window ───────────────────────────────────────

void FFenixSceneImporter::ShowScenePicker(const TArray<TSharedPtr<FJsonObject>>& Scenes)
{
	// Build list of scene names
	TSharedPtr<TArray<TSharedPtr<FJsonObject>>> SceneList =
		MakeShared<TArray<TSharedPtr<FJsonObject>>>(Scenes);

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(FText::FromString(TEXT("Select Scene to Import")))
		.ClientSize(FVector2D(350, 400))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	TSharedRef<SScrollBox> ScrollBox = SNew(SScrollBox);

	for (const TSharedPtr<FJsonObject>& Scene : Scenes)
	{
		FString SceneUUID, SceneName;
		Scene->TryGetStringField(TEXT("uuid"), SceneUUID);
		Scene->TryGetStringField(TEXT("name"), SceneName);

		const FString Label = FString::Printf(TEXT("%s  (%s)"), *SceneName, *SceneUUID);
		TSharedPtr<FJsonObject> SceneCopy = Scene;

		ScrollBox->AddSlot()
		.Padding(4.f)
		[
			SNew(SButton)
			.HAlign(HAlign_Left)
			.OnClicked_Lambda([SceneCopy, &Window]()
			{
				FFenixSceneImporter::ImportScene(SceneCopy);
				// Close window after import
				if (Window->GetNativeWindow().IsValid())
					Window->RequestDestroyWindow();
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(FText::FromString(Label))
			]
		];
	}

	Window->SetContent(
		SNew(SBox)
		.Padding(8.f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				ScrollBox
			]
		]
	);

	FSlateApplication::Get().AddWindow(Window);
}

// ── Clear Level ───────────────────────────────────────────────

bool FFenixSceneImporter::ShouldPreserve(AActor* Actor)
{
	if (!Actor) return true;

	static const TArray<FString> PreservedClasses = {
		TEXT("CameraActor"),
		TEXT("BP_Player"),
		TEXT("Ultra_Dynamic_Sky"),
		TEXT("UltraDynamicSky"),
	};

	const FString ClassName = Actor->GetClass()->GetName();
	const FString Label     = Actor->GetActorLabel();

	for (const FString& P : PreservedClasses)
	{
		if (ClassName.StartsWith(P, ESearchCase::IgnoreCase)) return true;
		if (Label.StartsWith(P, ESearchCase::IgnoreCase))     return true;
	}

	// Always preserve engine internals
	if (Actor->IsA<AWorldSettings>()) return true;

	return false;
}

void FFenixSceneImporter::ClearLevel(UWorld* World)
{
	TArray<AActor*> ToDestroy;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!ShouldPreserve(Actor) && !Actor->HasAnyFlags(RF_Transient))
			ToDestroy.Add(Actor);
	}

	for (AActor* Actor : ToDestroy)
		Actor->Destroy();

	UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] Cleared %d actors from level"), ToDestroy.Num());
}

// ── Spawn ─────────────────────────────────────────────────────

void FFenixSceneImporter::SpawnItem(UWorld* World, const TSharedPtr<FJsonObject>& Item)
{
	FString BpClass;
	Item->TryGetStringField(TEXT("blueprint_class"), BpClass);
	if (BpClass.IsEmpty()) return;

	// Resolve Blueprint class from asset registry
	const FString AssetPath = FString::Printf(TEXT("/Game/Blueprints/%s.%s_C"), *BpClass, *BpClass);
	UClass* ActorClass = LoadClass<AActor>(nullptr, *AssetPath);

	if (!ActorClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FenixDevTools] Class not found: %s"), *AssetPath);
		return;
	}

	// Read placement
	const TSharedPtr<FJsonObject>* PlacementObj;
	if (!Item->TryGetObjectField(TEXT("placement"), PlacementObj)) return;

	auto GetVec = [](const TSharedPtr<FJsonObject>& O, const FString& Key) -> FVector
	{
		const TSharedPtr<FJsonObject>* Sub;
		if (!O->TryGetObjectField(*Key, Sub)) return FVector::ZeroVector;
		double X=0, Y=0, Z=0;
		(*Sub)->TryGetNumberField(TEXT("x"), X);
		(*Sub)->TryGetNumberField(TEXT("y"), Y);
		(*Sub)->TryGetNumberField(TEXT("z"), Z);
		return FVector(X, Y, Z);
	};

	auto GetRot = [](const TSharedPtr<FJsonObject>& O) -> FRotator
	{
		const TSharedPtr<FJsonObject>* Sub;
		if (!O->TryGetObjectField(TEXT("rotation"), Sub)) return FRotator::ZeroRotator;
		double P=0, Y=0, R=0;
		(*Sub)->TryGetNumberField(TEXT("pitch"), P);
		(*Sub)->TryGetNumberField(TEXT("yaw"),   Y);
		(*Sub)->TryGetNumberField(TEXT("roll"),  R);
		return FRotator(P, Y, R);
	};

	const FVector  Loc   = GetVec(*PlacementObj, TEXT("location"));
	const FRotator Rot   = GetRot(*PlacementObj);
	const FVector  Scale = GetVec(*PlacementObj, TEXT("scale"));

	FTransform Transform(Rot, Loc, Scale.IsZero() ? FVector::OneVector : Scale);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* Spawned = World->SpawnActor<AActor>(ActorClass, Transform, Params);
	if (Spawned)
	{
		// Set actor label to uuid for identification
		FString UUID;
		Item->TryGetStringField(TEXT("uuid"), UUID);
		if (!UUID.IsEmpty()) Spawned->SetActorLabel(UUID);
	}
}

// ── Import Scene ──────────────────────────────────────────────

void FFenixSceneImporter::ImportScene(const TSharedPtr<FJsonObject>& Scene)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FenixDevTools] No editor world"));
		return;
	}

	FString SceneName;
	Scene->TryGetStringField(TEXT("name"), SceneName);
	UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] Importing scene: %s"), *SceneName);

	// 1. Clear level
	ClearLevel(World);

	// 2. Spawn items
	const TArray<TSharedPtr<FJsonValue>>* ItemsArr;
	if (Scene->TryGetArrayField(TEXT("items"), ItemsArr))
	{
		int32 Spawned = 0;
		for (const auto& Val : *ItemsArr)
		{
			const TSharedPtr<FJsonObject>* ItemObj;
			if (Val->TryGetObject(ItemObj))
			{
				SpawnItem(World, *ItemObj);
				++Spawned;
			}
		}
		UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] Spawned %d items"), Spawned);
	}

	// 3. Reposition camera if present
	const TSharedPtr<FJsonObject>* CameraObj;
	if (Scene->TryGetObjectField(TEXT("camera"), CameraObj))
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if ((*It)->GetClass()->GetName().Contains(TEXT("CameraActor")))
			{
				const TSharedPtr<FJsonObject>* LocObj;
				if ((*CameraObj)->TryGetObjectField(TEXT("location"), LocObj))
				{
					double X=0, Y=0, Z=0;
					(*LocObj)->TryGetNumberField(TEXT("x"), X);
					(*LocObj)->TryGetNumberField(TEXT("y"), Y);
					(*LocObj)->TryGetNumberField(TEXT("z"), Z);
					(*It)->SetActorLocation(FVector(X, Y, Z));
				}
				const TSharedPtr<FJsonObject>* RotObj;
				if ((*CameraObj)->TryGetObjectField(TEXT("rotation"), RotObj))
				{
					double P=0, Yaw=0, R=0;
					(*RotObj)->TryGetNumberField(TEXT("pitch"), P);
					(*RotObj)->TryGetNumberField(TEXT("yaw"),   Yaw);
					(*RotObj)->TryGetNumberField(TEXT("roll"),  R);
					(*It)->SetActorRotation(FRotator(P, Yaw, R));
				}
				break;
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] Scene '%s' imported successfully"), *SceneName);
}
