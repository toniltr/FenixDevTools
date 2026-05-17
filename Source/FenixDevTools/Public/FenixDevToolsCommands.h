#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"

class FFenixDevToolsCommands : public TCommands<FFenixDevToolsCommands>
{
public:

	FFenixDevToolsCommands()
		: TCommands<FFenixDevToolsCommands>(
			TEXT("FenixDevTools"),
			NSLOCTEXT("Contexts", "FenixDevTools", "Fenix Dev Tools"),
			NAME_None,
			FAppStyle::GetAppStyleSetName())
	{}

	virtual void RegisterCommands() override;

	TSharedPtr<FUICommandInfo> ExportLevelToJson;
	TSharedPtr<FUICommandInfo> ImportSceneFromJson;
	TSharedPtr<FUICommandInfo> ClearLevel;
};
