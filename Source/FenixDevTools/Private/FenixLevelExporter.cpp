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

// ── Helpers globales — deben estar antes de cualquier uso ─────

static FString GetShortMapName(const FString& MapName)
{
	FString Short = FPaths::GetBaseFilename(MapName);
	if (Short.EndsWith(TEXT("_C"))) Short = Short.LeftChop(2);
	return Short;
}

static bool ShouldSkipActor(const FString& Class, const FString& Label)
{
	static const TArray<FString> SkipSubstrings = {
		TEXT("UltraDynamicSky"), TEXT("Ultra_Dynamic_Sky"),
		TEXT("DirectionalLight"), TEXT("PointLight"), TEXT("SpotLight"), TEXT("SkyLight"),
		TEXT("SkyAtmosphere"), TEXT("ExponentialHeightFog"), TEXT("PostProcessVolume"),
		TEXT("NavMesh"), TEXT("WorldSettings"), TEXT("Brush"), TEXT("PlayerStart"),
		TEXT("LevelSequence"), TEXT("AbstractNavData"), TEXT("RecastNavMesh"),
		TEXT("SphereReflection"), TEXT("BoxReflection"), TEXT("LightmassImportance"),
	};
	for (const FString& S : SkipSubstrings)
	{
		if (Class.Contains(S, ESearchCase::IgnoreCase)) return true;
		if (Label.Contains(S, ESearchCase::IgnoreCase)) return true;
	}
	return false;
}

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

	// Leer el archivo original como bytes para preservar encoding
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

	// Verificar integridad: contar campos en el primer item de la primera escena
	const TArray<TSharedPtr<FJsonValue>>* ScenesCheck;
	if (Root->TryGetArrayField(TEXT("scenes"), ScenesCheck) && ScenesCheck->Num() > 0)
	{
		const TSharedPtr<FJsonObject>* FirstScene;
		const TArray<TSharedPtr<FJsonValue>>* FirstItems;
		if ((*ScenesCheck)[0]->TryGetObject(FirstScene) &&
			(*FirstScene)->TryGetArrayField(TEXT("items"), FirstItems) &&
			FirstItems->Num() > 0)
		{
			const TSharedPtr<FJsonObject>* FirstItem;
			if ((*FirstItems)[0]->TryGetObject(FirstItem))
			{
				const bool bHasBP = (*FirstItem)->HasField(TEXT("blueprint_class"));
				const bool bHasEvents = (*FirstItem)->HasField(TEXT("events"));
				UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] JSON integrity check — first item has blueprint_class: %s, events: %s"),
					bHasBP ? TEXT("YES") : TEXT("NO"),
					bHasEvents ? TEXT("YES") : TEXT("NO"));

				if (!bHasBP || !bHasEvents)
				{
					UE_LOG(LogTemp, Error,
						TEXT("[FenixDevTools] ABORT — JSON parsed incorrectly (missing fields). File may be corrupted or wrong encoding. NOT saving."));
					return false;
				}
			}
		}
	}

	// Actualizar solo los placements de la escena activa
	const FString MapName = World->GetMapName();
	const bool bUpdated = UpdateScenePlacements(World, MapName, Root);
	if (!bUpdated)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FenixDevTools] No updates made — file not saved"));
		return false;
	}

	// DEBUG: verificar campos de cada item de la escena activa ANTES de serializar
	const FString ShortMapDebug = GetShortMapName(MapName);
	const TArray<TSharedPtr<FJsonValue>>* ScenesDebug;
	if (Root->TryGetArrayField(TEXT("scenes"), ScenesDebug))
	{
		for (const auto& SV : *ScenesDebug)
		{
			const TSharedPtr<FJsonObject>* SO;
			if (!SV->TryGetObject(SO)) continue;
			FString SName;
			(*SO)->TryGetStringField(TEXT("name"), SName);
			if (!SName.Contains(ShortMapDebug, ESearchCase::IgnoreCase) &&
				!ShortMapDebug.Contains(SName, ESearchCase::IgnoreCase)) continue;

			const TArray<TSharedPtr<FJsonValue>>* IArr;
			if (!(*SO)->TryGetArrayField(TEXT("items"), IArr)) break;
			for (const auto& IV : *IArr)
			{
				const TSharedPtr<FJsonObject>* IO;
				if (!IV->TryGetObject(IO)) continue;
				FString IUuid;
				(*IO)->TryGetStringField(TEXT("uuid"), IUuid);
				UE_LOG(LogTemp, Log,
					TEXT("[FenixDevTools] PRE-SERIALIZE [%s] bp:%s cond:%s ev:%s blk:%s"),
					*IUuid,
					(*IO)->HasField(TEXT("blueprint_class")) ? TEXT("Y") : TEXT("N"),
					(*IO)->HasField(TEXT("conditions"))      ? TEXT("Y") : TEXT("N"),
					(*IO)->HasField(TEXT("events"))          ? TEXT("Y") : TEXT("N"),
					(*IO)->HasField(TEXT("blocked_events"))  ? TEXT("Y") : TEXT("N"));
			}
			break;
		}
	}

	// Serializar siempre como UTF-8 sin BOM
	FString Output;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	// DEBUG: volcar los primeros 500 chars del output para verificar que el JSON tiene todos los campos
	UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] Output preview (500 chars): %s"),
		*Output.Left(500));

	// Volcar también el item-bed-1 buscando en el output
	const int32 BedIdx = Output.Find(TEXT("item-bed-1"));
	if (BedIdx != INDEX_NONE)
	{
		UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] item-bed-1 context: %s"),
			*Output.Mid(BedIdx, 300));
	}

	// Guardar como UTF-8 sin BOM (EEncodingOptions::ForceUTF8WithoutBOM)
	if (!FFileHelper::SaveStringToFile(Output, *StoryPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogTemp, Warning, TEXT("[FenixDevTools] Failed to save: %s"), *StoryPath);
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] Scene '%s' updated in %s"),
		*GetShortMapName(MapName), *StoryPath);
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

bool FFenixLevelExporter::UpdateScenePlacements(UWorld* World, const FString& MapName,
                                                 const TSharedPtr<FJsonObject>& Root)
{
	// Nombre corto del mapa para hacer match con scene.name del JSON
	const FString ShortMap = GetShortMapName(MapName);
	UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] Map name: '%s' → short: '%s'"), *MapName, *ShortMap);

	// Construir mapa UUID → actor desde los actores del nivel
	TMap<FString, AActor*> ActorByUUID;
	AActor* CameraActor = nullptr;
	AActor* PlayerActor = nullptr;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor->HasAnyFlags(RF_Transient)) continue;

		const FString Class = Actor->GetClass()->GetName();
		const FString Label = Actor->GetActorLabel();

		// Excluir explícitamente actores que nunca son items Fenix
		if (ShouldSkipActor(Class, Label)) continue;

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

		// Solo actores cuyo label no sea igual al nombre de la clase (no son genéricos sin UUID)
		if (!Label.IsEmpty() && !Label.Equals(Class, ESearchCase::IgnoreCase))
			ActorByUUID.Add(Label, Actor);
	}

	UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] %d actors in UUID map"), ActorByUUID.Num());

	// Buscar la escena en el JSON por nombre corto
	const TArray<TSharedPtr<FJsonValue>>* ScenesArr;
	if (!Root->TryGetArrayField(TEXT("scenes"), ScenesArr))
	{
		UE_LOG(LogTemp, Warning, TEXT("[FenixDevTools] No 'scenes' array found in JSON"));
		return false;
	}

	int32 ItemsUpdated  = 0;
	int32 ItemsNotFound = 0;
	bool  bSceneFound   = false;

	for (const auto& SceneVal : *ScenesArr)
	{
		const TSharedPtr<FJsonObject>* SceneObjPtr;
		if (!SceneVal->TryGetObject(SceneObjPtr)) continue;
		TSharedPtr<FJsonObject> SceneObj = *SceneObjPtr;

		FString SceneName;
		SceneObj->TryGetStringField(TEXT("name"), SceneName);

		// Match flexible: nombre exacto O el short map contiene el nombre de la escena
		const bool bMatch = SceneName.Equals(ShortMap, ESearchCase::IgnoreCase)
		                 || ShortMap.Contains(SceneName, ESearchCase::IgnoreCase)
		                 || SceneName.Contains(ShortMap, ESearchCase::IgnoreCase);
		if (!bMatch) continue;

		bSceneFound = true;
		UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] Matched scene '%s' with map '%s'"), *SceneName, *ShortMap);

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

			// Log de integridad por item — detecta si el parse ha perdido campos
			UE_LOG(LogTemp, Verbose,
				TEXT("[FenixDevTools] Item '%s' — blueprint_class:%s conditions:%s events:%s"),
				*ItemUUID,
				ItemObj->HasField(TEXT("blueprint_class")) ? TEXT("✓") : TEXT("✗"),
				ItemObj->HasField(TEXT("conditions"))      ? TEXT("✓") : TEXT("✗"),
				ItemObj->HasField(TEXT("events"))          ? TEXT("✓") : TEXT("✗"));

			AActor** FoundActor = ActorByUUID.Find(ItemUUID);
			if (!FoundActor)
			{
				UE_LOG(LogTemp, Log,
					TEXT("[FenixDevTools] Item '%s' not found in level — placement unchanged"),
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
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[FenixDevTools] No scene matched map '%s' — nothing updated. Check scene 'name' in JSON matches the UE5 map name."),
			*ShortMap);
		return false;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[FenixDevTools] Done — %d placements updated, %d items not found in level"),
		ItemsUpdated, ItemsNotFound);
	return true;
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

		if (ShouldSkipActor(Class, Label)) continue;

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

		const FString UUID = Label;
		if (UUID.IsEmpty() || UUID.Equals(Class, ESearchCase::IgnoreCase)) continue;

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
