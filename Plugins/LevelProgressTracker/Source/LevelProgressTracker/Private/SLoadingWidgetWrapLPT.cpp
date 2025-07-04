// Pavel Gornostaev <https://github.com/Pavreally>

#include "SLoadingWidgetWrapLPT.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SBoxPanel.h"
#include "SlateOptMacros.h"


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SLoadingWidgetWrapLPT::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)
	];
}

void SLoadingWidgetWrapLPT::LoadEmbeddedUWidgetLPT(TSoftClassPtr<UUserWidget>& WidgetClass)
{
	if (WidgetClass.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("LoadSWidgetLPT: Widget not specified."));

		return;
	}
	
	// Load the asset via a soft link if it is not already loaded
	FStreamableManager& StreamableManager = UAssetManager::GetStreamableManager();
	UClass* LoadedClass = Cast<UClass>(StreamableManager.LoadSynchronous(
		WidgetClass.ToSoftObjectPath(),
		true
	));

	if (!LoadedClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("LoadSWidgetLPT: Failed to load UMG class."));

		return;
	}

	if (GEngine && GEngine->GameViewport)
	{
		// Creating an instance of a UMG widget
		UUserWidget* UserWidget = CreateWidget<UUserWidget>(
			GEngine->GameViewport->GetWorld(),
			WidgetClass.Get()
		);

		if (UserWidget)
		{
			EmbeddedWidget = UserWidget;

			// Insert UMG into Slate via TakeWidget
			ChildSlot[
				UserWidget->TakeWidget()
			];
		}
	}
}

void SLoadingWidgetWrapLPT::UnloadSWidgetLPT()
{
	// Clear UMG
	if (EmbeddedWidget.IsValid())
	{
		EmbeddedWidget.Reset();
	}

	// Clear Slate
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(AsShared());
	}
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION
