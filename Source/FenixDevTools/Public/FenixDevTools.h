#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FToolBarBuilder;

class FFenixDevToolsModule : public IModuleInterface
{
public:

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:

	void AddToolbarButton(FToolBarBuilder& Builder);
	void ExportLevelToJson();

	TSharedPtr<class FUICommandList> PluginCommands;
};
