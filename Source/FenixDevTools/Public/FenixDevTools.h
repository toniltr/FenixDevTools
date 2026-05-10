#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FToolBarBuilder;
class FMenuBuilder;

// Editor-only plugin module.
// Registers a toolbar button that exports the current level's actors to JSON.
class FFenixDevToolsModule : public IModuleInterface
{
public:

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:

	void RegisterMenus();
	void ExportLevelToJson();

	TSharedPtr<class FUICommandList> PluginCommands;
};
