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

bool FFenixLevelExporter::ExportCurrentLevel()
{
	UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] ExportCurrentLevel called"));

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FenixDevTools] No editor world found"));
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] World: %s"), *World->GetMapName());

	const FString JsonStr = BuildJson(World);
	const FString SavePath = FPaths::ProjectDir() / TEXT("level_export.json");

	if (!FFileHelper::SaveStringToFile(JsonStr, *SavePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("[FenixDevTools] Failed to save: %s"), *SavePath);
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] Exported to: %s"), *SavePath);
	return true;
}

// ── Helpers ──────────────────────────────────────────────────

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
	O->SetObjectField(TEXT("location"), MakeVec3(Loc.X,     Loc.Y,     Loc.Z));
	O->SetObjectField(TEXT("rotation"), MakeRotation(Rot.Pitch, Rot.Yaw, Rot.Roll));
	O->SetObjectField(TEXT("scale"),    MakeVec3(Scale.X,   Scale.Y,   Scale.Z));
	return O;
}

static TSharedPtr<FJsonObject> MakeItem(const FString& BpClass, const TSharedPtr<FJsonObject>& Placement)
{
	TArray<TSharedPtr<FJsonValue>> EmptyArr;

	TSharedPtr<FJsonObject> Conditions = MakeShared<FJsonObject>();
	Conditions->SetStringField(TEXT("operator"), TEXT("AND"));
	Conditions->SetArrayField(TEXT("rules"), EmptyArr);

	TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
	Item->SetStringField(TEXT("uuid"),            TEXT(""));
	Item->SetStringField(TEXT("blueprint_class"), BpClass);
	Item->SetObjectField(TEXT("placement"),       Placement);
	Item->SetObjectField(TEXT("conditions"),      Conditions);
	Item->SetArrayField (TEXT("events"),          EmptyArr);
	Item->SetArrayField (TEXT("blocked_events"),  EmptyArr);
	Item->SetStringField(TEXT("intercept_npc"),   TEXT(""));
	return Item;
}

// ── Clases a excluir ─────────────────────────────────────────

static bool ShouldExcludeActor(AActor* Actor)
{
	static const TArray<FString> ExcludedClasses = {
		TEXT("WorldSettings"), TEXT("Brush"), TEXT("DefaultPhysicsVolume"),
		TEXT("AtmosphericFog"), TEXT("SkyAtmosphere"), TEXT("SkyLight"),
		TEXT("DirectionalLight"), TEXT("PointLight"), TEXT("SpotLight"),
		TEXT("RectLight"), TEXT("ExponentialHeightFog"),
		TEXT("UltraDynamicSky"), TEXT("Ultra_Dynamic_Sky"),
		TEXT("PostProcessVolume"), TEXT("LightmassImportanceVolume"),
		TEXT("PlayerStart"), TEXT("NavMeshBoundsVolume"),
		TEXT("SphereReflectionCapture"), TEXT("BoxReflectionCapture"),
		TEXT("LevelSequenceActor"), TEXT("AbstractNavData"), TEXT("RecastNavMesh"),
		TEXT("GameplayDebuggerCategoryReplicator"),
		TEXT("CameraActor"), TEXT("BP_Player"),
	};

	static const TArray<FString> ExcludedLabels = {
		TEXT("Ultra_Dynamic_Sky"), TEXT("BP_Player"), TEXT("CameraActor"),
	};

	if (Actor->IsEditorOnly() || Actor->HasAnyFlags(RF_Transient)) return true;

	const FString ClassName = Actor->GetClass()->GetName();
	for (const FString& Ex : ExcludedClasses)
		if (ClassName.StartsWith(Ex, ESearchCase::IgnoreCase)) return true;

	const FString Label = Actor->GetActorLabel();
	for (const FString& Ex : ExcludedLabels)
		if (Label.StartsWith(Ex, ESearchCase::IgnoreCase)) return true;

	return false;
}

// ── Build JSON ────────────────────────────────────────────────

FString FFenixLevelExporter::BuildJson(UWorld* World)
{
	TArray<TSharedPtr<FJsonValue>> ItemsArray;
	TSharedPtr<FJsonObject> CameraPlacement;
	TSharedPtr<FJsonObject> PlayerPlacement;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		const FString Label = Actor->GetActorLabel();
		const FString Class = Actor->GetClass()->GetName();

		// Extract camera and player separately
		if (Class.Contains(TEXT("CameraActor")) || Label.Contains(TEXT("CameraActor")))
		{
			CameraPlacement = MakePlacement(
				Actor->GetActorLocation(),
				Actor->GetActorRotation(),
				Actor->GetActorScale3D());
			continue;
		}

		if (Class.Contains(TEXT("BP_Player")) || Label.StartsWith(TEXT("BP_Player")))
		{
			PlayerPlacement = MakePlacement(
				Actor->GetActorLocation(),
				Actor->GetActorRotation(),
				Actor->GetActorScale3D());
			continue;
		}

		if (ShouldExcludeActor(Actor)) continue;

		// Clean _C suffix
		FString BpClass = Class;
		if (BpClass.EndsWith(TEXT("_C"))) BpClass = BpClass.LeftChop(2);

		TSharedPtr<FJsonObject> Placement = MakePlacement(
			Actor->GetActorLocation(),
			Actor->GetActorRotation(),
			Actor->GetActorScale3D());

		ItemsArray.Add(MakeShared<FJsonValueObject>(MakeItem(BpClass, Placement)));
	}

	// Scene template — paste-ready Fenix scene format
	TSharedPtr<FJsonObject> SceneTemplate = MakeShared<FJsonObject>();
	SceneTemplate->SetStringField(TEXT("uuid"), TEXT(""));
	SceneTemplate->SetStringField(TEXT("name"), World->GetMapName());

	if (CameraPlacement.IsValid())
		SceneTemplate->SetObjectField(TEXT("camera"), CameraPlacement);
	if (PlayerPlacement.IsValid())
		SceneTemplate->SetObjectField(TEXT("player"), PlayerPlacement);

	SceneTemplate->SetArrayField(TEXT("items"), ItemsArray);

	TArray<TSharedPtr<FJsonValue>> EmptyChars;
	SceneTemplate->SetArrayField(TEXT("characters"), EmptyChars);

	// Root
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("level"),       World->GetMapName());
	Root->SetStringField(TEXT("exported_at"), FDateTime::Now().ToString());
	Root->SetNumberField(TEXT("item_count"),  ItemsArray.Num());
	Root->SetObjectField(TEXT("scene_template"), SceneTemplate);

	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Output;
}
