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

bool FFenixLevelExporter::ExportCurrentLevel()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FenixDevTools] No editor world found"));
		return false;
	}

	const FString JsonStr = BuildJson(World);
	const FString SavePath = ShowSaveDialog();

	if (SavePath.IsEmpty()) return false;

	if (!FFileHelper::SaveStringToFile(JsonStr, *SavePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("[FenixDevTools] Failed to save file: %s"), *SavePath);
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("[FenixDevTools] Level exported to: %s"), *SavePath);
	return true;
}

FString FFenixLevelExporter::BuildJson(UWorld* World)
{
	TArray<TSharedPtr<FJsonValue>> ActorsArray;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		// Skip editor-only actors (camera, sky, lights etc.)
		// Include only actors placed by the user (not transient)
		if (Actor->IsEditorOnly()) continue;
		if (Actor->HasAnyFlags(RF_Transient)) continue;

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

		// Actor entry
		TSharedPtr<FJsonObject> ActorObj = MakeShared<FJsonObject>();
		ActorObj->SetStringField(TEXT("name"),           Actor->GetName());
		ActorObj->SetStringField(TEXT("blueprint_class"), Actor->GetClass()->GetName());
		ActorObj->SetObjectField(TEXT("placement"),      PlacementObj);

		// Fenix item template — ready to paste into JSON
		TSharedPtr<FJsonObject> ItemTemplate = MakeShared<FJsonObject>();
		ItemTemplate->SetStringField(TEXT("uuid"),            TEXT(""));
		ItemTemplate->SetStringField(TEXT("blueprint_class"), Actor->GetClass()->GetName());
		ItemTemplate->SetObjectField(TEXT("placement"),       PlacementObj);

		TArray<TSharedPtr<FJsonValue>> EmptyArray;
		ItemTemplate->SetArrayField(TEXT("conditions_rules"), EmptyArray);
		ItemTemplate->SetArrayField(TEXT("events"),           EmptyArray);
		ItemTemplate->SetArrayField(TEXT("blocked_events"),   EmptyArray);
		ItemTemplate->SetStringField(TEXT("intercept_npc"),   TEXT(""));

		ActorObj->SetObjectField(TEXT("item_template"), ItemTemplate);

		ActorsArray.Add(MakeShared<FJsonValueObject>(ActorObj));
	}

	// Root object
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("level"),       World->GetMapName());
	Root->SetStringField(TEXT("exported_at"), FDateTime::Now().ToString());
	Root->SetNumberField(TEXT("actor_count"), ActorsArray.Num());
	Root->SetArrayField(TEXT("actors"),       ActorsArray);

	FString OutputStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputStr);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	return OutputStr;
}

FString FFenixLevelExporter::ShowSaveDialog()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform) return TEXT("");

	TArray<FString> SavePaths;
	const FString DefaultPath     = FPaths::ProjectContentDir();
	const FString DefaultFilename = TEXT("level_export.json");

	const bool bSaved = DesktopPlatform->SaveFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Export Level to Fenix JSON"),
		DefaultPath,
		DefaultFilename,
		TEXT("JSON Files (*.json)|*.json"),
		EFileDialogFlags::None,
		SavePaths
	);

	return (bSaved && SavePaths.Num() > 0) ? SavePaths[0] : TEXT("");
}
