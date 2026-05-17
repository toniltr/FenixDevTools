#include "FenixLevelExporter.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Editor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"

// ── Entry point ───────────────────────────────────────────────

bool FFenixLevelExporter::ExportCurrentLevel()
{
	UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] ExportCurrentLevel called"));

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FenixDevTools] No editor world found"));
		return false;
	}

	// Pedir al usuario el JSON de historia a actualizar
	const FString StoryPath = ShowOpenFileDialog();
	if (StoryPath.IsEmpty())
	{
		UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] Export cancelled by user"));
		return false;
	}

	FString JsonStr;
	if (!FFileHelper::LoadFileToString(JsonStr, *StoryPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("[FenixDevTools] Could not read: %s"), *StoryPath);
		return false;
	}

	// Parsear el JSON original
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[FenixDevTools] Invalid JSON in: %s"), *StoryPath);
		return false;
	}

	// Actualizar solo los placements de la escena activa
	const FString MapName = World->GetMapName();
	UpdateScenePlacements(World, MapName, Root);

	// Serializar de vuelta al mismo archivo
	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	if (!FFileHelper::SaveStringToFile(Output, *StoryPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("[FenixDevTools] Failed to save: %s"), *StoryPath);
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] Placements updated in: %s"), *StoryPath);
	return true;
}

// ── File Dialog ───────────────────────────────────────────────

FString FFenixLevelExporter::ShowOpenFileDialog()
{
	IDesktopPlatform* DP = FDesktopPlatformModule::Get();
	if (!DP) return TEXT("");

	TArray<FString> Files;
	DP->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Select Fenix Story JSON to update"),
		FPaths::ProjectContentDir(),
		TEXT(""),
		TEXT("JSON Files (*.json)|*.json"),
		EFileDialogFlags::None,
		Files);
	return Files.Num() > 0 ? Files[0] : TEXT("");
}

// ── Helpers de placement ──────────────────────────────────────

static TSharedPtr<FJsonObject> MakeVec3(float X, float Y, float Z)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetNumberField(TEXT("x"), FMath::RoundToFloat(X * 100.f) / 100.f);
	O->SetNumberField(TEXT("y"), FMath::RoundToFloat(Y * 100.f) / 100.f);
	O->SetNumberField(TEXT("z"), FMath::RoundToFloat(Z * 100.f) / 100.f);
	return O;
}

static TSharedPtr<FJsonObject> MakeRotation(float Pitch, float Yaw, float Roll)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetNumberField(TEXT("pitch"), FMath::RoundToFloat(Pitch * 100.f) / 100.f);
	O->SetNumberField(TEXT("yaw"),   FMath::RoundToFloat(Yaw   * 100.f) / 100.f);
	O->SetNumberField(TEXT("roll"),  FMath::RoundToFloat(Roll  * 100.f) / 100.f);
	return O;
}

static TSharedPtr<FJsonObject> MakePlacement(const FVector& Loc, const FRotator& Rot, const FVector& Scale)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetObjectField(TEXT("location"), MakeVec3(Loc.X, Loc.Y, Loc.Z));
	O->SetObjectField(TEXT("rotation"), MakeRotation(Rot.Pitch, Rot.Yaw, Rot.Roll));
	O->SetObjectField(TEXT("scale"),    MakeVec3(Scale.X, Scale.Y, Scale.Z));
	return O;
}

// ── Core: actualiza solo los placements de la escena activa ───

void FFenixLevelExporter::UpdateScenePlacements(UWorld* World, const FString& MapName,
                                                 const TSharedPtr<FJsonObject>& Root)
{
	// Construir mapa UUID → transform desde los actores del nivel
	// Key: actor label (que es el UUID del item)
	TMap<FString, AActor*> ActorByUUID;
	AActor* CameraActor = nullptr;
	AActor* PlayerActor = nullptr;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor->HasAnyFlags(RF_Transient)) continue;

		const FString Class = Actor->GetClass()->GetName();
		const FString Label = Actor->GetActorLabel();

		if (Class.Contains(TEXT("CameraActor")) || Label.Contains(TEXT("CameraActor")))
		{
			CameraActor = Actor;
			continue;
		}
		if (Class.Contains(TEXT("BP_Player")) || Label.StartsWith(TEXT("BP_Player")))
		{
			PlayerActor = Actor;
			continue;
		}

		// Solo actores con label que parezca un UUID (no vacío, no nombre genérico de clase)
		if (!Label.IsEmpty() && Label != Class)
			ActorByUUID.Add(Label, Actor);
	}

	// Buscar la escena en el JSON por nombre (MapName)
	const TArray<TSharedPtr<FJsonValue>>* ScenesArr;
	if (!Root->TryGetArrayField(TEXT("scenes"), ScenesArr))
	{
		UE_LOG(LogTemp, Warning, TEXT("[FenixDevTools] No 'scenes' array found in JSON"));
		return;
	}

	int32 ItemsUpdated      = 0;
	int32 ItemsNotFound     = 0;
	bool  bSceneFound       = false;

	for (const auto& SceneVal : *ScenesArr)
	{
		const TSharedPtr<FJsonObject>* SceneObjPtr;
		if (!SceneVal->TryGetObject(SceneObjPtr)) continue;
		TSharedPtr<FJsonObject> SceneObj = *SceneObjPtr;

		FString SceneName;
		SceneObj->TryGetStringField(TEXT("name"), SceneName);

		// Match por nombre de mapa (ignorar sufijo _C si lo tiene)
		FString CleanMap = MapName;
		if (CleanMap.EndsWith(TEXT("_C"))) CleanMap = CleanMap.LeftChop(2);

		if (!SceneName.Equals(CleanMap, ESearchCase::IgnoreCase))
			continue;

		bSceneFound = true;
		UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] Found scene '%s' in JSON"), *SceneName);

		// Actualizar camera y player
		if (CameraActor)
		{
			SceneObj->SetObjectField(TEXT("camera"),
				MakePlacement(CameraActor->GetActorLocation(),
				              CameraActor->GetActorRotation(),
				              CameraActor->GetActorScale3D()));
		}
		if (PlayerActor)
		{
			SceneObj->SetObjectField(TEXT("player"),
				MakePlacement(PlayerActor->GetActorLocation(),
				              PlayerActor->GetActorRotation(),
				              PlayerActor->GetActorScale3D()));
		}

		// Actualizar solo el placement de cada item — preservar todo lo demás
		const TArray<TSharedPtr<FJsonValue>>* ItemsArr;
		if (!SceneObj->TryGetArrayField(TEXT("items"), ItemsArr))
			break;

		for (const auto& ItemVal : *ItemsArr)
		{
			const TSharedPtr<FJsonObject>* ItemObjPtr;
			if (!ItemVal->TryGetObject(ItemObjPtr)) continue;
			TSharedPtr<FJsonObject> ItemObj = *ItemObjPtr;

			FString ItemUUID;
			ItemObj->TryGetStringField(TEXT("uuid"), ItemUUID);
			if (ItemUUID.IsEmpty()) continue;

			AActor** FoundActor = ActorByUUID.Find(ItemUUID);
			if (!FoundActor)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[FenixDevTools] Item UUID '%s' not found in level — placement unchanged"),
					*ItemUUID);
				++ItemsNotFound;
				continue;
			}

			// Solo actualizamos placement — el resto del item queda intacto
			ItemObj->SetObjectField(TEXT("placement"),
				MakePlacement((*FoundActor)->GetActorLocation(),
				              (*FoundActor)->GetActorRotation(),
				              (*FoundActor)->GetActorScale3D()));
			++ItemsUpdated;
		}

		break; // Solo una escena por mapa
	}

	if (!bSceneFound)
		UE_LOG(LogTemp, Warning,
			TEXT("[FenixDevTools] Scene '%s' not found in JSON — nothing updated"), *MapName);
	else
		UE_LOG(LogTemp, Log,
			TEXT("[FenixDevTools] Updated %d item placements (%d not found in level)"),
			ItemsUpdated, ItemsNotFound);
}

// ── BuildSceneJson — mantenido para uso externo si se necesita ──

TSharedPtr<FJsonObject> FFenixLevelExporter::BuildSceneJson(UWorld* World)
{
	if (!World) return nullptr;

	TArray<TSharedPtr<FJsonValue>> ItemsArray;
	TSharedPtr<FJsonObject> CameraPlacement;
	TSharedPtr<FJsonObject> PlayerPlacement;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor->HasAnyFlags(RF_Transient)) continue;

		const FString Label = Actor->GetActorLabel();
		const FString Class = Actor->GetClass()->GetName();

		if (Class.Contains(TEXT("CameraActor")) || Label.Contains(TEXT("CameraActor")))
		{
			CameraPlacement = MakePlacement(Actor->GetActorLocation(),
			                               Actor->GetActorRotation(),
			                               Actor->GetActorScale3D());
			continue;
		}
		if (Class.Contains(TEXT("BP_Player")) || Label.StartsWith(TEXT("BP_Player")))
		{
			PlayerPlacement = MakePlacement(Actor->GetActorLocation(),
			                               Actor->GetActorRotation(),
			                               Actor->GetActorScale3D());
			continue;
		}
		if (Actor->IsEditorOnly()) continue;

		const FString UUID = Label;
		if (UUID.IsEmpty() || UUID == Class) continue;

		TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("uuid"), UUID);
		Item->SetObjectField(TEXT("placement"),
			MakePlacement(Actor->GetActorLocation(),
			              Actor->GetActorRotation(),
			              Actor->GetActorScale3D()));
		ItemsArray.Add(MakeShared<FJsonValueObject>(Item));
	}

	TSharedPtr<FJsonObject> Scene = MakeShared<FJsonObject>();
	Scene->SetStringField(TEXT("name"), World->GetMapName());
	if (CameraPlacement.IsValid()) Scene->SetObjectField(TEXT("camera"), CameraPlacement);
	if (PlayerPlacement.IsValid()) Scene->SetObjectField(TEXT("player"), PlayerPlacement);
	Scene->SetArrayField(TEXT("items"), ItemsArray);
	return Scene;
}
