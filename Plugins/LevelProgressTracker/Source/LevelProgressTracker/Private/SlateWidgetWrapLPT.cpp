// Pavel Gornostaev <https://github.com/Pavreally>

#include "SlateWidgetWrapLPT.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "SlateOptMacros.h"
#include "Widgets/SOverlay.h"


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SWidgetWrapLPT::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SOverlay)
	];
}

void SWidgetWrapLPT::LoadEmbeddedUWidgetLPT(TSubclassOf<UUserWidget> UserWidget)
{
	if (!UserWidget)
	{
		UE_LOG(LogTemp, Warning, TEXT("LoadEmbeddedUWidgetLPT: Invalid widget class."));

		return;
	}

	UWorld* World = GEngine ? GEngine->GameViewport->GetWorld() : nullptr;
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("LoadEmbeddedUWidgetLPT: Invalid world context."));

		return;
	}

	// Create UMG widget
	UUserWidget* UWidgetInstance = CreateWidget<UUserWidget>(World, UserWidget);
	if (!UWidgetInstance)
	{
		UE_LOG(LogTemp, Warning, TEXT("LoadEmbeddedUWidgetLPT: Failed to create widget."));
		
		return;
	}

	EmbeddedWidget = UWidgetInstance;

	// Add to container
	SOverlay* OverlayPtr = static_cast<SOverlay*>(&ChildSlot.GetWidget().Get());
	if (OverlayPtr)
	{
		OverlayPtr->AddSlot()
		[
			UWidgetInstance->TakeWidget()
		];
	}
}

void SWidgetWrapLPT::UnloadSWidgetLPT()
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

