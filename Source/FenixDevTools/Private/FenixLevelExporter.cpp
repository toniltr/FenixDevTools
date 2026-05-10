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
		UE_LOG(LogTemp, Warning, TEXT("[FenixDevTools] No editor world found — GEditor: %s"),
			GEditor ? TEXT("valid") : TEXT("null"));
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] World found: %s"), *World->GetMapName());

	const FString JsonStr = BuildJson(World);
	UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] JSON built — %d chars"), JsonStr.Len());

	// Save directly to project root — overwrite each time
	const FString SavePath = FPaths::ProjectDir() / TEXT("level_export.json");
	if (!FFileHelper::SaveStringToFile(JsonStr, *SavePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("[FenixDevTools] Failed to save: %s"), *SavePath);
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] Exported to: %s"), *SavePath);
	return true;
}

FString FFenixLevelExporter::BuildJson(UWorld* World)
{
	TArray<TSharedPtr<FJsonValue>> ActorsArray;

	// Classes to always exclude from export
	static const TArray<FString> ExcludedClasses = {
		TEXT("WorldSettings"),
		TEXT("Brush"),
		TEXT("DefaultPhysicsVolume"),
		TEXT("GameplayDebuggerCategoryReplicator"),
		TEXT("AtmosphericFog"),
		TEXT("SkyAtmosphere"),
		TEXT("SkyLight"),
		TEXT("DirectionalLight"),
		TEXT("PointLight"),
		TEXT("SpotLight"),
		TEXT("RectLight"),
		TEXT("ExponentialHeightFog"),
		TEXT("UltraDynamicSky"),
		TEXT("Ultra_Dynamic_Sky"),
		TEXT("PostProcessVolume"),
		TEXT("LightmassImportanceVolume"),
		TEXT("PlayerStart"),
		TEXT("NavMeshBoundsVolume"),
		TEXT("ReflectionCapture"),
		TEXT("SphereReflectionCapture"),
		TEXT("BoxReflectionCapture"),
		TEXT("LevelSequenceActor"),
		TEXT("AbstractNavData"),
		TEXT("RecastNavMesh"),
		TEXT("CameraActor"),
		TEXT("BP_Player"),
	};

	// Actor labels to always exclude regardless of class
	static const TArray<FString> ExcludedLabels = {
		TEXT("Ultra_Dynamic_Sky"),
		TEXT("BP_Player"),
		TEXT("CameraActor"),
	};

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		if (Actor->IsEditorOnly()) continue;
		if (Actor->HasAnyFlags(RF_Transient)) continue;

		// Skip internal/engine classes
		const FString ClassName = Actor->GetClass()->GetName();
		bool bExcluded = false;
		for (const FString& Excluded : ExcludedClasses)
		{
			if (ClassName.Equals(Excluded, ESearchCase::IgnoreCase) ||
				ClassName.StartsWith(Excluded, ESearchCase::IgnoreCase))
			{
				bExcluded = true;
				break;
			}
		}
		if (bExcluded) continue;

		// Also filter by actor label
		const FString ActorLabel = Actor->GetActorLabel();
		for (const FString& ExLabel : ExcludedLabels)
		{
			if (ActorLabel.StartsWith(ExLabel, ESearchCase::IgnoreCase))
			{
				bExcluded = true;
				break;
			}
		}
		if (bExcluded) continue;

		// Only include Blueprint actors from /Game/ — skip native engine actors
		const FString ClassPath = Actor->GetClass()->GetPathName();
		const bool bIsGameBlueprint = ClassPath.StartsWith(TEXT("/Game/"));
		const bool bIsNativeActor   = !ClassPath.Contains(TEXT("/"));
		if (bIsNativeActor && !bIsGameBlueprint) continue;

		const FVector  Loc   = Actor->GetActorLocation();
		const FRotator Rot   = Actor->GetActorRotation();
		const FVector  Scale = Actor->GetActorScale3D();

		// Location
		TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
		LocObj->SetNumberField(TEXT("x"), FMath::RoundToFloat(Loc.X * 100.f) / 100.f);
		LocObj->SetNumberField(TEXT("y"), FMath::RoundToFloat(Loc.Y * 100.f) / 100.f);
		LocObj->SetNumberField(TEXT("z"), FMath::RoundToFloat(Loc.Z * 100.f) / 100.f);

		// Rotation
		TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
		RotObj->SetNumberField(TEXT("pitch"), FMath::RoundToFloat(Rot.Pitch * 100.f) / 100.f);
		RotObj->SetNumberField(TEXT("yaw"),   FMath::RoundToFloat(Rot.Yaw   * 100.f) / 100.f);
		RotObj->SetNumberField(TEXT("roll"),  FMath::RoundToFloat(Rot.Roll  * 100.f) / 100.f);

		// Scale
		TSharedPtr<FJsonObject> ScaleObj = MakeShared<FJsonObject>();
		ScaleObj->SetNumberField(TEXT("x"), FMath::RoundToFloat(Scale.X * 100.f) / 100.f);
		ScaleObj->SetNumberField(TEXT("y"), FMath::RoundToFloat(Scale.Y * 100.f) / 100.f);
		ScaleObj->SetNumberField(TEXT("z"), FMath::RoundToFloat(Scale.Z * 100.f) / 100.f);

		// Placement
		TSharedPtr<FJsonObject> PlacementObj = MakeShared<FJsonObject>();
		PlacementObj->SetObjectField(TEXT("location"), LocObj);
		PlacementObj->SetObjectField(TEXT("rotation"), RotObj);
		PlacementObj->SetObjectField(TEXT("scale"),    ScaleObj);

		// Clean blueprint class name — remove _C suffix added by UE
		FString BpClass = Actor->GetClass()->GetName();
		if (BpClass.EndsWith(TEXT("_C")))
			BpClass = BpClass.LeftChop(2);

		// Actor entry
		TSharedPtr<FJsonObject> ActorObj = MakeShared<FJsonObject>();
		ActorObj->SetStringField(TEXT("name"),            Actor->GetActorLabel());
		ActorObj->SetStringField(TEXT("blueprint_class"), BpClass);
		ActorObj->SetObjectField(TEXT("placement"),       PlacementObj);

		// Fenix item template — ready to paste into JSON
		TSharedPtr<FJsonObject> ItemTemplate = MakeShared<FJsonObject>();
		ItemTemplate->SetStringField(TEXT("uuid"),            TEXT(""));
		ItemTemplate->SetStringField(TEXT("blueprint_class"), BpClass);
		ItemTemplate->SetObjectField(TEXT("placement"),       PlacementObj);

		TArray<TSharedPtr<FJsonValue>> EmptyArray;

		// Conditions group matching Fenix JSON format
		TSharedPtr<FJsonObject> ConditionsObj = MakeShared<FJsonObject>();
		ConditionsObj->SetStringField(TEXT("operator"), TEXT("AND"));
		ConditionsObj->SetArrayField(TEXT("rules"), EmptyArray);
		ItemTemplate->SetObjectField(TEXT("conditions"),    ConditionsObj);

		ItemTemplate->SetArrayField(TEXT("events"),         EmptyArray);
		ItemTemplate->SetArrayField(TEXT("blocked_events"), EmptyArray);
		ItemTemplate->SetStringField(TEXT("intercept_npc"), TEXT(""));

		ActorObj->SetObjectField(TEXT("item_template"), ItemTemplate);

		ActorsArray.Add(MakeShared<FJsonValueObject>(ActorObj));
	}

	// Find CameraActor and BP_Player placements
	TSharedPtr<FJsonObject> CameraPlacement;
	TSharedPtr<FJsonObject> PlayerPlacement;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		const FString Label = Actor->GetActorLabel();
		const FString Class = Actor->GetClass()->GetName();
		const bool bIsCamera = Class.Contains(TEXT("CameraActor")) || Label.Contains(TEXT("CameraActor"));
		const bool bIsPlayer = Class.Contains(TEXT("BP_Player"))   || Label.Contains(TEXT("BP_Player"));

		if (!bIsCamera && !bIsPlayer) continue;

		const FVector  Loc   = Actor->GetActorLocation();
		const FRotator Rot   = Actor->GetActorRotation();
		const FVector  Scale = Actor->GetActorScale3D();

		auto MakeVec = [](float X, float Y, float Z) {
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("x"), FMath::RoundToFloat(X));
			O->SetNumberField(TEXT("y"), FMath::RoundToFloat(Y));
			O->SetNumberField(TEXT("z"), FMath::RoundToFloat(Z));
			return O;
		};

		TSharedPtr<FJsonObject> Placement = MakeShared<FJsonObject>();
		Placement->SetObjectField(TEXT("location"), MakeVec(Loc.X, Loc.Y, Loc.Z));
		Placement->SetObjectField(TEXT("rotation"), MakeVec(Rot.Pitch, Rot.Yaw, Rot.Roll));
		Placement->SetObjectField(TEXT("scale"),    MakeVec(Scale.X, Scale.Y, Scale.Z));

		if (bIsCamera) CameraPlacement = Placement;
		if (bIsPlayer) PlayerPlacement = Placement;
	}

	// Root object
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("level"),       World->GetMapName());
	Root->SetStringField(TEXT("exported_at"), FDateTime::Now().ToString());
	Root->SetNumberField(TEXT("actor_count"), ActorsArray.Num());

	// Scene template — paste directly into the scene JSON
	TSharedPtr<FJsonObject> SceneTemplate = MakeShared<FJsonObject>();
	SceneTemplate->SetStringField(TEXT("uuid"), TEXT(""));
	SceneTemplate->SetStringField(TEXT("name"), World->GetMapName());
	if (CameraPlacement.IsValid()) SceneTemplate->SetObjectField(TEXT("camera"), CameraPlacement);
	if (PlayerPlacement.IsValid()) SceneTemplate->SetObjectField(TEXT("player"), PlayerPlacement);
	SceneTemplate->SetArrayField(TEXT("items"), ActorsArray);

	TArray<TSharedPtr<FJsonValue>> EmptyChars;
	SceneTemplate->SetArrayField(TEXT("characters"), EmptyChars);

	Root->SetObjectField(TEXT("scene_template"), SceneTemplate);
	Root->SetArrayField(TEXT("actors"),          ActorsArray);

	FString OutputStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputStr);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	return OutputStr;
}


