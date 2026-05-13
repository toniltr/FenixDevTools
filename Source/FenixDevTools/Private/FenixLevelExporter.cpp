#include "FenixLevelExporter.h"
#include "Interaction/FenixActor.h"   // AFenixActor — leer conditions/events en export
#include "FenixStoryData.h"           // FFenixConditionGroup, FFenixEvent
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

// ── Serialización de condiciones ─────────────────────────────

static TSharedPtr<FJsonObject> SerializeConditionRule(const FFenixConditionRule& Rule)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"),   Rule.Type);
	O->SetStringField(TEXT("target"), Rule.Target);
	if (!Rule.Status.IsEmpty())
		O->SetStringField(TEXT("status"), Rule.Status);
	if (Rule.Amount != 0)
		O->SetNumberField(TEXT("amount"), Rule.Amount);
	return O;
}

static TSharedPtr<FJsonObject> SerializeConditionGroup(const FFenixConditionGroup& Group)
{
	TArray<TSharedPtr<FJsonValue>> RulesArr;
	for (const FFenixConditionRule& Rule : Group.Rules)
		RulesArr.Add(MakeShared<FJsonValueObject>(SerializeConditionRule(Rule)));

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("operator"), Group.Operator.IsEmpty() ? TEXT("AND") : Group.Operator);
	O->SetArrayField(TEXT("rules"), RulesArr);
	return O;
}

// ── Serialización de eventos ─────────────────────────────────

static TSharedPtr<FJsonObject> SerializeEvent(const FFenixEvent& Event)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"),   Event.Type);
	O->SetStringField(TEXT("target"), Event.Target);
	O->SetNumberField(TEXT("amount"), Event.Amount);
	return O;
}

static TArray<TSharedPtr<FJsonValue>> SerializeEvents(const TArray<FFenixEvent>& Events)
{
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FFenixEvent& Ev : Events)
		Arr.Add(MakeShared<FJsonValueObject>(SerializeEvent(Ev)));
	return Arr;
}

// ── MakeItem — lee datos del AFenixActor si el actor hereda de él ──

static TSharedPtr<FJsonObject> MakeItem(const FString& BpClass, const TSharedPtr<FJsonObject>& Placement, AActor* Actor)
{
	// Valores por defecto (actor sin datos Fenix)
	FString ExportedUUID      = TEXT("");
	FString InterceptNpc      = TEXT("");
	TSharedPtr<FJsonObject> ConditionsObj;
	TArray<TSharedPtr<FJsonValue>> EventsArr;
	TArray<TSharedPtr<FJsonValue>> BlockedEventsArr;

	if (AFenixActor* FenixActor = Cast<AFenixActor>(Actor))
	{
		// Leer directamente las UPROPERTYs editables del actor
		ExportedUUID     = FenixActor->ItemUUID;
		ConditionsObj    = SerializeConditionGroup(FenixActor->Conditions);
		EventsArr        = SerializeEvents(FenixActor->Events);
		BlockedEventsArr = SerializeEvents(FenixActor->BlockedEvents);
		InterceptNpc     = FenixActor->InterceptNpcUUID;
	}
	else
	{
		// Actor genérico — conditions vacías
		FFenixConditionGroup EmptyGroup;
		EmptyGroup.Operator = TEXT("AND");
		ConditionsObj = SerializeConditionGroup(EmptyGroup);
	}

	TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
	Item->SetStringField(TEXT("uuid"),            ExportedUUID);
	Item->SetStringField(TEXT("blueprint_class"), BpClass);
	Item->SetObjectField(TEXT("placement"),       Placement);
	Item->SetObjectField(TEXT("conditions"),      ConditionsObj);
	Item->SetArrayField (TEXT("events"),          EventsArr);
	Item->SetArrayField (TEXT("blocked_events"),  BlockedEventsArr);
	Item->SetStringField(TEXT("intercept_npc"),   InterceptNpc);
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

// ── BuildSceneJson ────────────────────────────────────────────

TSharedPtr<FJsonObject> FFenixLevelExporter::BuildSceneJson(UWorld* World)
{
	if (!World) return nullptr;

	TArray<TSharedPtr<FJsonValue>> ItemsArray;
	TSharedPtr<FJsonObject> CameraPlacement;
	TSharedPtr<FJsonObject> PlayerPlacement;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

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
		if (ShouldExcludeActor(Actor)) continue;

		FString BpClass = Actor->GetClass()->GetName();
		if (BpClass.EndsWith(TEXT("_C"))) BpClass = BpClass.LeftChop(2);

		TSharedPtr<FJsonObject> Placement = MakePlacement(
			Actor->GetActorLocation(),
			Actor->GetActorRotation(),
			Actor->GetActorScale3D());

		// Pasar el Actor para que MakeItem pueda leer sus UPROPERTYs Fenix
		ItemsArray.Add(MakeShared<FJsonValueObject>(MakeItem(BpClass, Placement, Actor)));
	}

	TSharedPtr<FJsonObject> Scene = MakeShared<FJsonObject>();
	Scene->SetStringField(TEXT("uuid"), TEXT(""));
	Scene->SetStringField(TEXT("name"), World->GetMapName());

	if (CameraPlacement.IsValid())
		Scene->SetObjectField(TEXT("camera"), CameraPlacement);
	if (PlayerPlacement.IsValid())
		Scene->SetObjectField(TEXT("player"), PlayerPlacement);

	Scene->SetArrayField(TEXT("items"), ItemsArray);

	TArray<TSharedPtr<FJsonValue>> EmptyChars;
	Scene->SetArrayField(TEXT("characters"), EmptyChars);

	return Scene;
}

// ── BuildJson ─────────────────────────────────────────────────

FString FFenixLevelExporter::BuildJson(UWorld* World)
{
	TSharedPtr<FJsonObject> SceneTemplate = BuildSceneJson(World);
	if (!SceneTemplate.IsValid()) return TEXT("{}");

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("level"),         World->GetMapName());
	Root->SetStringField(TEXT("exported_at"),   FDateTime::Now().ToString());
	Root->SetObjectField(TEXT("scene_template"), SceneTemplate);

	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Output;
}
