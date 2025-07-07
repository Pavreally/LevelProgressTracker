// Pavel Gornostaev <https://github.com/Pavreally>

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"


class UUserWidget;

/**
 * Slate wrapper widget that hosts a UMG widget inside a Slate container.
 */
class SWidgetWrapLPT : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SWidgetWrapLPT) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * Loading UMG into adding it to the Slate widget's child container.
	 * @param WidgetClass Reference to UMG widget.
	 */
	void LoadEmbeddedUWidgetLPT(TSubclassOf<UUserWidget> UserWidget);

	/**
	 * Removes the widget from the viewport and resets the link.
	 */
	void UnloadSWidgetLPT();

private:
	// Weak reference to the instance of the created UMG widget.
	TWeakObjectPtr<UUserWidget> EmbeddedWidget;
};
