#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

// Abre el JSON de historia, actualiza SOLO los placements de los items de la escena activa
// haciendo match por actor label (UUID), y guarda el JSON en el mismo archivo.
// El resto del item (conditions, events, blocked_events, intercept_*) queda intacto.
class FENIXDEVTOOLS_API FFenixLevelExporter
{
public:

	// Entry point: pide el JSON al usuario, actualiza placements y lo sobreescribe.
	// Returns true si el archivo se guardó correctamente.
	static bool ExportCurrentLevel();

	// Merge completo de la escena activa en Root:
	//   - Item en JSON y en nivel  → actualiza placement (preserva conditions, events, etc.)
	//   - Item en JSON pero NO en nivel → se elimina (borrado manual en el editor)
	//   - Actor en nivel pero NO en JSON → se añade como item nuevo con defaults vacíos
	// Devuelve true si encontró la escena y realizó el merge.
	static bool UpdateScenePlacements(UWorld* World, const FString& MapName,
	                                  const TSharedPtr<FJsonObject>& Root);

	// Construye un JSON de escena con solo uuid + placement de cada actor.
	// Útil para debug o para crear una escena nueva desde cero.
	static TSharedPtr<FJsonObject> BuildSceneJson(UWorld* World);

private:

	// Abre el diálogo de selección de archivo JSON.
	static FString ShowOpenFileDialog();
};
