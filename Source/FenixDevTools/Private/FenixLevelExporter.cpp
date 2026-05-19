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

	// ── 1. Indexar actores del nivel ─────────────────────────────────────────
	// UUID → actor  (label = UUID para items Fenix)
	// También capturamos clase BP para poder crear items nuevos
	TMap<FString, AActor*> ActorByUUID;      // items Fenix existentes
	TMap<FString, FString> ClassByUUID;      // blueprint_class de cada actor del nivel
	AActor* CameraActor = nullptr;
	AActor* PlayerActor = nullptr;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor->HasAnyFlags(RF_Transient)) continue;

		const FString Class = Actor->GetClass()->GetName();
		const FString Label = Actor->GetActorLabel();

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

		// Solo actores con label distinto al nombre de clase (tienen UUID asignado)
		if (!Label.IsEmpty() && !Label.Equals(Class, ESearchCase::IgnoreCase))
		{
			ActorByUUID.Add(Label, Actor);

			// Obtener blueprint_class limpio: quitar sufijo _C
			FString BpClass = Class;
			if (BpClass.EndsWith(TEXT("_C")))
				BpClass = BpClass.LeftChop(2);
			ClassByUUID.Add(Label, BpClass);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] %d actors in level"), ActorByUUID.Num());

	// ── 2. Localizar la escena en el JSON ─────────────────────────────────────
	const TArray<TSharedPtr<FJsonValue>>* ScenesArr;
	if (!Root->TryGetArrayField(TEXT("scenes"), ScenesArr))
	{
		UE_LOG(LogTemp, Warning, TEXT("[FenixDevTools] No 'scenes' array found in JSON"));
		return false;
	}

	bool bSceneFound = false;

	for (const auto& SceneVal : *ScenesArr)
	{
		const TSharedPtr<FJsonObject>* SceneObjPtr;
		if (!SceneVal->TryGetObject(SceneObjPtr)) continue;
		TSharedPtr<FJsonObject> SceneObj = *SceneObjPtr;

		FString SceneName;
		SceneObj->TryGetStringField(TEXT("name"), SceneName);

		const bool bMatch = SceneName.Equals(ShortMap, ESearchCase::IgnoreCase)
		                 || ShortMap.Contains(SceneName, ESearchCase::IgnoreCase)
		                 || SceneName.Contains(ShortMap, ESearchCase::IgnoreCase);

		UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] Checking scene '%s' vs '%s' → %s"),
			*SceneName, *ShortMap, bMatch ? TEXT("MATCH") : TEXT("skip"));

		if (!bMatch) continue;

		bSceneFound = true;
		UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] Matched scene '%s'"), *SceneName);

		// ── 3. Camera y Player ────────────────────────────────────────────────
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

		// ── 4. Merge de items ─────────────────────────────────────────────────
		//
		//  A) Item en JSON y en nivel  → actualiza solo placement (preserva todo lo demás)
		//  B) Item en JSON pero NO en nivel → se elimina (borrado manual en editor)
		//  C) Actor en nivel pero NO en JSON → se añade como item nuevo con defaults vacíos

		const TArray<TSharedPtr<FJsonValue>>* ItemsArr;
		if (!SceneObj->TryGetArrayField(TEXT("items"), ItemsArr))
		{
			// Si no hay array de items todavía, creamos uno vacío para el paso C
			SceneObj->SetArrayField(TEXT("items"), TArray<TSharedPtr<FJsonValue>>());
			SceneObj->TryGetArrayField(TEXT("items"), ItemsArr);
		}

		// Rastrear qué UUIDs del JSON hemos procesado
		TSet<FString> ProcessedUUIDs;

		// Nuevo array resultante
		TArray<TSharedPtr<FJsonValue>> NewItems;
		int32 Updated = 0, Removed = 0;

		if (ItemsArr)
		{
			for (const auto& ItemVal : *ItemsArr)
			{
				const TSharedPtr<FJsonObject>* ItemObjPtr;
				if (!ItemVal->TryGetObject(ItemObjPtr)) continue;
				TSharedPtr<FJsonObject> ItemObj = *ItemObjPtr;

				FString ItemUUID;
				ItemObj->TryGetStringField(TEXT("uuid"), ItemUUID);
				if (ItemUUID.IsEmpty()) continue;

				ProcessedUUIDs.Add(ItemUUID);

				AActor** FoundActor = ActorByUUID.Find(ItemUUID);
				if (!FoundActor)
				{
					// CASO B: no está en el nivel → eliminado por el usuario → no lo añadimos
					UE_LOG(LogTemp, Log,
						TEXT("[FenixDevTools] Item '%s' removed from level → deleted from JSON"),
						*ItemUUID);
					++Removed;
					continue;
				}

				// CASO A: existe en nivel → actualizar solo placement
				ItemObj->SetObjectField(TEXT("placement"),
					MakePlacement((*FoundActor)->GetActorLocation(),
					              (*FoundActor)->GetActorRotation(),
					              (*FoundActor)->GetActorScale3D()));
				++Updated;

				NewItems.Add(MakeShared<FJsonValueObject>(ItemObj));
			}
		}

		// CASO C: actores del nivel que NO estaban en el JSON → items nuevos
		int32 Added = 0;
		for (const auto& Pair : ActorByUUID)
		{
			const FString& UUID = Pair.Key;
			if (ProcessedUUIDs.Contains(UUID)) continue; // ya procesado en el paso A

			AActor* Actor = Pair.Value;
			const FString* BpClass = ClassByUUID.Find(UUID);

			// Construir item nuevo con estructura completa y defaults vacíos
			TSharedPtr<FJsonObject> NewItem = MakeShared<FJsonObject>();
			NewItem->SetStringField(TEXT("uuid"),            UUID);
			NewItem->SetStringField(TEXT("blueprint_class"), BpClass ? *BpClass : TEXT(""));
			NewItem->SetObjectField(TEXT("placement"),
				MakePlacement(Actor->GetActorLocation(),
				              Actor->GetActorRotation(),
				              Actor->GetActorScale3D()));

			// conditions vacías
			TSharedPtr<FJsonObject> Conditions = MakeShared<FJsonObject>();
			Conditions->SetStringField(TEXT("operator"), TEXT("AND"));
			Conditions->SetArrayField(TEXT("rules"), TArray<TSharedPtr<FJsonValue>>());
			NewItem->SetObjectField(TEXT("conditions"), Conditions);

			// events, blocked_events e intercept vacíos
			NewItem->SetArrayField(TEXT("events"),         TArray<TSharedPtr<FJsonValue>>());
			NewItem->SetArrayField(TEXT("blocked_events"), TArray<TSharedPtr<FJsonValue>>());
			NewItem->SetStringField(TEXT("intercept_character"), TEXT(""));

			NewItems.Add(MakeShared<FJsonValueObject>(NewItem));
			++Added;

			UE_LOG(LogTemp, Log,
				TEXT("[FenixDevTools] New actor '%s' (%s) → added to JSON"),
				*UUID, BpClass ? **BpClass : TEXT("unknown"));
		}

		SceneObj->SetArrayField(TEXT("items"), NewItems);

		UE_LOG(LogTemp, Log,
			TEXT("[FenixDevTools] Merge done — %d updated, %d removed, %d added"),
			Updated, Removed, Added);

		break; // Solo una escena por mapa
	}

	if (!bSceneFound)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[FenixDevTools] No scene matched map '%s'. Check scene 'name' in JSON."),
			*ShortMap);
		return false;
	}

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
