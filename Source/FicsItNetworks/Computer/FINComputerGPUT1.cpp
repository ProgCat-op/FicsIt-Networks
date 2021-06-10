#include "FINComputerGPUT1.h"

#include "FGPlayerController.h"
#include "FINComputerRCO.h"
#include "FicsItNetworks/Graphics/FINScreenInterface.h"
#include "Widgets/SInvalidationPanel.h"

void SScreenMonitor::Construct(const FArguments& InArgs) {
	Text = InArgs._Text;
	Foreground = InArgs._Foreground;
	Background = InArgs._Background;
	Font = InArgs._Font;
	ScreenSize = InArgs._ScreenSize;
	OnMouseDownEvent = InArgs._OnMouseDown;
	OnMouseUpEvent = InArgs._OnMouseUp;
	OnMouseMoveEvent = InArgs._OnMouseMove;
	OnKeyDownEvent = InArgs._OnKeyDown;
	OnKeyUpEvent = InArgs._OnKeyUp;
	SetCanTick(false);
}

FString SScreenMonitor::GetText() const {
	return Text.Get();
}

FVector2D SScreenMonitor::GetScreenSize() const {
	return ScreenSize.Get();
}

void SScreenMonitor::SetScreenSize(FVector2D NewScreenSize) {
	ScreenSize = NewScreenSize;
}

FVector2D SScreenMonitor::GetCharSize() const {
	FSlateApplication& app = FSlateApplication::Get();
	FSlateRenderer* renderer = app.GetRenderer();
	TSharedRef<FSlateFontMeasure> measure = renderer->GetFontMeasureService();
	return measure->Measure(L" ", Font.Get());
}

FVector2D SScreenMonitor::LocalToCharPos(FVector2D Pos) const {
	return Pos / GetCharSize();
}

int SScreenMonitor::MouseToInt(const FPointerEvent& MouseEvent) {
	int mouseEvent = 0;
	if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))	mouseEvent |= 0b0000000001;
	if (MouseEvent.IsMouseButtonDown(EKeys::RightMouseButton))	mouseEvent |= 0b0000000010;
	if (MouseEvent.IsControlDown())								mouseEvent |= 0b0000000100;
	if (MouseEvent.IsShiftDown())								mouseEvent |= 0b0000001000;
	if (MouseEvent.IsAltDown())									mouseEvent |= 0b0000010000;
	if (MouseEvent.IsCommandDown())								mouseEvent |= 0b0000100000;
	return mouseEvent;
}

int SScreenMonitor::InputToInt(const FInputEvent& KeyEvent) {
	int mouseEvent = 0;
	if (KeyEvent.IsControlDown())								mouseEvent |= 0b0000000100;
	if (KeyEvent.IsShiftDown())									mouseEvent |= 0b0000001000;
	if (KeyEvent.IsAltDown())									mouseEvent |= 0b0000010000;
	if (KeyEvent.IsCommandDown())								mouseEvent |= 0b0000100000;
	return mouseEvent;
}

FVector2D SScreenMonitor::ComputeDesiredSize(float f) const {
	return GetCharSize() * ScreenSize.Get();
}

int32 SScreenMonitor::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const {
	FVector2D CharSize = GetCharSize();
	FVector2D ScreenSizeV = ScreenSize.Get();
	FSlateBrush boxBrush = FSlateBrush();
	const TArray<FLinearColor>& ForegroundCache = this->Foreground.Get();
	const TArray<FLinearColor>& BackgroundCache = this->Background.Get();
	
	const FString& TextGrid = Text.Get();
	FSlateFontInfo FontToUse = Font.Get();
	
	for (int Y = 0; Y < ScreenSizeV.Y; ++Y) {
		for (int X = 0; X < ScreenSizeV.X; ++X) {
			int64 CharIndex = Y * ScreenSizeV.X + X;
			
			FLinearColor ForegroundV = FLinearColor(1,1,1,1);
			if (CharIndex < ForegroundCache.Num()) {
				ForegroundV = ForegroundCache[CharIndex];
			}
			FLinearColor BackgroundV = FLinearColor(0,0,0,0);
			if (CharIndex < BackgroundCache.Num()) {
				BackgroundV = BackgroundCache[CharIndex];
			}
			FString Char(1, &TextGrid[CharIndex]);
			if (Char.TrimStartAndEnd().Len() > 0) FSlateDrawElement::MakeText(
                OutDrawElements,
                LayerId+1,
                AllottedGeometry.ToOffsetPaintGeometry(FVector2D(X,Y) * CharSize),
                *Char,
				FontToUse,
                ESlateDrawEffect::None,
                ForegroundV
            );
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId,
				AllottedGeometry.ToPaintGeometry(FVector2D(X, Y) * CharSize, (CharSize*1), 1),
				&boxBrush,
				ESlateDrawEffect::None,
				BackgroundV);
		}
	}
	return LayerId+1;
}

FReply SScreenMonitor::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) {
	FVector2D CharPos = LocalToCharPos(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
	
	if (OnMouseDownEvent.IsBound()) return OnMouseDownEvent.Execute(CharPos.X, CharPos.Y, MouseToInt(MouseEvent));
	return FReply::Unhandled();
}

FReply SScreenMonitor::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) {
	FVector2D CharPos = LocalToCharPos(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
	if (OnMouseUpEvent.IsBound()) return OnMouseUpEvent.Execute(CharPos.X, CharPos.Y, MouseToInt(MouseEvent));
	return FReply::Unhandled(); 
}

FReply SScreenMonitor::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) {
	FVector2D CharPos = LocalToCharPos(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
	int x = CharPos.X;
	int y = CharPos.Y;
	if (lastMoveX == x && lastMoveY == y) return FReply::Unhandled();
	lastMoveX = x;
	lastMoveY = y;
	if (OnMouseMoveEvent.IsBound()) return OnMouseMoveEvent.Execute(x, y, MouseToInt(MouseEvent));
	return FReply::Unhandled();
}

FReply SScreenMonitor::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) {
	if (OnKeyDownEvent.IsBound()) return OnKeyDownEvent.Execute(InKeyEvent.GetCharacter(), InKeyEvent.GetKeyCode(), InputToInt(InKeyEvent));
	return FReply::Unhandled();
}

FReply SScreenMonitor::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) {
	if (OnKeyUpEvent.IsBound()) return OnKeyUpEvent.Execute(InKeyEvent.GetCharacter(), InKeyEvent.GetKeyCode(), InputToInt(InKeyEvent));
	return FReply::Unhandled();
}

bool SScreenMonitor::IsInteractable() const {
	return true;
}

bool SScreenMonitor::SupportsKeyboardFocus() const {
	return true;
}

SScreenMonitor::SScreenMonitor() {
	
}

AFINComputerGPUT1::AFINComputerGPUT1() {
	PrimaryActorTick.bCanEverTick = false;
	
	SetScreenSize(FVector2D(120, 30));

	PrimaryActorTick.bCanEverTick = true;
	SetActorTickEnabled(true);
}

void AFINComputerGPUT1::Tick(float DeltaSeconds) {
	Super::Tick(DeltaSeconds);
	if (HasAuthority() && bFlushed) {
		bFlushed = false;
		Flush();
		ForceNetUpdate();
	}
}

void AFINComputerGPUT1::BindScreen(const FFINNetworkTrace& screen) {
	Super::BindScreen(screen);
}

void AFINComputerGPUT1::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	DOREPLIFETIME(AFINComputerGPUT1, TextGrid);
	DOREPLIFETIME(AFINComputerGPUT1, Foreground);
	DOREPLIFETIME(AFINComputerGPUT1, Background);
	DOREPLIFETIME(AFINComputerGPUT1, ScreenSize);
}

TSharedPtr<SWidget> AFINComputerGPUT1::CreateWidget() {
	boxBrush = LoadObject<USlateBrushAsset>(NULL, TEXT("SlateBrushAsset'/FicsItNetworks/Computer/UI/ComputerCaseBorder.ComputerCaseBorder'"))->Brush;
	UFINComputerRCO* RCO = Cast<UFINComputerRCO>(Cast<AFGPlayerController>(GetWorld()->GetFirstPlayerController())->GetRemoteCallObjectOfClass(UFINComputerRCO::StaticClass()));
	return SAssignNew(CachedInvalidation, SInvalidationPanel)
	.Content()[
		SNew(SScreenMonitor)
		.ScreenSize_Lambda([this]() {
			return ScreenSize;
		})
		.Text_Lambda([this]() {
            return TextGrid;
		})
		.Foreground_Lambda([this]() {
			return Foreground;
		})
		.Background_Lambda([this]() {
			return Background;
		})
		.Font(FSlateFontInfo(LoadObject<UObject>(NULL, TEXT("Font'/FicsItNetworks/GuiHelpers/Inconsolata_Font.Inconsolata_Font'")), 12, "InConsolata"))
		.OnMouseDown_Lambda([this, RCO](int x, int y, int btn) {
			RCO->GPUMouseEvent(this, 0, x, y, btn);
			return FReply::Handled();
		})
		.OnMouseUp_Lambda([this, RCO](int x, int y, int btn) {
			RCO->GPUMouseEvent(this, 1, x, y, btn);
            return FReply::Handled();
        })
        .OnMouseMove_Lambda([this, RCO](int x, int y, int btn) {
			RCO->GPUMouseEvent(this, 2, x, y, btn);
            return FReply::Handled();
        })
		.OnKeyDown_Lambda([this, RCO](uint32 c, uint32 key, int btn) {
			RCO->GPUKeyEvent(this, 0,  c, key, btn);
			return FReply::Handled();
		})
		.OnKeyUp_Lambda([this, RCO](uint32 c, uint32 key, int btn) {
			RCO->GPUKeyEvent(this, 1,  c, key, btn);
			return FReply::Handled();
        })
    ];
}

void AFINComputerGPUT1::SetScreenSize(FVector2D size) {
	if (ScreenSize == size) return;
	FVector2D oldScreenSize = ScreenSize;
	ScreenSize = size;

	TextGrid.Empty();
	Foreground.Empty();
	Background.Empty();

	TextGrid = FString::ChrN(size.X * size.Y, ' ');
	for (int y = 0; y < size.Y; ++y) {
		for (int x = 0; x < size.X; ++x) {
			Foreground.Add(CurrentForeground);
			Background.Add(CurrentBackground);
		}
	}

	TextGridBuffer = TextGrid;
	ForegroundBuffer = Foreground;
	BackgroundBuffer = Background;

	if (PrimaryActorTick.bCanEverTick) netSig_ScreenSizeChanged(oldScreenSize.X, oldScreenSize.Y);

	ForceNetUpdate();
}

void AFINComputerGPUT1::Flush_Implementation() {
	if (CachedInvalidation) {
		CachedInvalidation->InvalidateRoot();
		CachedInvalidation->InvalidateChildOrder();
		CachedInvalidation->InvalidateScreenPosition();
		CachedInvalidation->InvalidatePrepass();
	}
}

void AFINComputerGPUT1::netSig_OnMouseDown_Implementation(int x, int y, int btn) {}
void AFINComputerGPUT1::netSig_OnMouseUp_Implementation(int x, int y, int btn) {}
void AFINComputerGPUT1::netSig_OnMouseMove_Implementation(int x, int y, int btn) {}
void AFINComputerGPUT1::netSig_ScreenSizeChanged_Implementation(int oldW, int oldH) {}
void AFINComputerGPUT1::netSig_OnKeyDown_Implementation(int64 c, int64 code, int btn) {}
void AFINComputerGPUT1::netSig_OnKeyUp_Implementation(int64 c, int64 code, int btn) {}

void AFINComputerGPUT1::netFunc_bindScreen(FFINNetworkTrace NewScreen) {
	if (Cast<IFINScreenInterface>(NewScreen.GetUnderlyingPtr().Get())) BindScreen(NewScreen);
}

UObject* AFINComputerGPUT1::netFunc_getScreen() {
	return Screen.Get();
}

void AFINComputerGPUT1::netFunc_setText(int x, int y, const FString& str) {
	FScopeLock Lock(&DrawingMutex);
	FString toSet = str;
	while (toSet.Len() > 0) {
		FString Line;
		bool newLine = toSet.Split("\n", &Line, &toSet);
		if (!newLine) {
			Line = toSet;
			toSet = "";
		}
		while (Line.Len() > 0) {
			FString inLine;
			bool returned = Line.Split("\r", &inLine, &Line);
			if (!returned) {
				inLine = Line;
				Line = "";
			}
			int oldX = x + inLine.Len();
			if (y >= 0 && x < ScreenSize.X && y < ScreenSize.Y) {
				if (x < 0) {
					if (inLine.Len() < FMath::Abs(x)) {
						x = -1;
					} else {
						inLine.RemoveAt(0, FMath::Abs(x));
						x = 0;
					}
				}
				if (x >= 0) {
					int replace = FMath::Clamp(inLine.Len(), 0, static_cast<int>(ScreenSize.X)-x-1);
					if (replace > 0) {
						int64 CharIndex = y * ScreenSize.X + x;
						TextGridBuffer.RemoveAt(CharIndex, replace);
						TextGridBuffer.InsertAt(CharIndex, inLine.Left(replace));
						for (int dx = 0; dx < replace; ++dx) {
							ForegroundBuffer[CharIndex + dx] = CurrentForeground;
							BackgroundBuffer[CharIndex + dx] = CurrentBackground;
						}
					}
				}
			}
			x = oldX;
			if (returned) x = 0;
		}
		if (newLine) ++y;
	}
}

void AFINComputerGPUT1::netFunc_fill(int x, int y, int dx, int dy, const FString& str) {
	FString c = str;
	if (FRegexMatcher(FRegexPattern("^[[:cntrl:]]?$"), c).FindNext()) c = " ";
	if (dx < 0) dx = 0;
	if (dy < 0) dy = 0;
	for (int ny = y; ny < y+dy; ++ny) {
		netFunc_setText(x, ny, FString::ChrN(dx, c[0]));
	}
}

void AFINComputerGPUT1::netFunc_getSize(int& w, int& h) {
	w = ScreenSize.X;
	h = ScreenSize.Y;
}

void AFINComputerGPUT1::netFunc_setSize(int w, int h) {
	FScopeLock Lock(&DrawingMutex);
	SetScreenSize(FVector2D(FMath::Clamp(w, 1, 300), FMath::Clamp(h, 1, 100)));
}

void AFINComputerGPUT1::netFunc_setForeground(float r, float g, float b, float a) {
	FScopeLock Lock(&DrawingMutex);
	CurrentForeground = FLinearColor(FMath::Clamp(r, 0.0f, 1.0f), FMath::Clamp(g, 0.0f, 1.0f), FMath::Clamp(b, 0.0f, 1.0f), FMath::Clamp(a, 0.0f, 1.0f));
}

void AFINComputerGPUT1::netFunc_setBackground(float r, float g, float b, float a) {
	FScopeLock Lock(&DrawingMutex);
	CurrentBackground = FLinearColor(FMath::Clamp(r, 0.0f, 1.0f), FMath::Clamp(g, 0.0f, 1.0f), FMath::Clamp(b, 0.0f, 1.0f), FMath::Clamp(a, 0.0f, 1.0f));
}

void AFINComputerGPUT1::netFunc_flush() {
	FScopeLock Lock(&DrawingMutex);
	TextGrid = TextGridBuffer;
	Foreground = ForegroundBuffer;
	Background = BackgroundBuffer;
	bFlushed = true;
}
