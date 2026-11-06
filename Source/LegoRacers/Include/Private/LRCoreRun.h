#pragma once

// What the game mode runs that isn't a UObject: the program, its files, the settings, the input as it arrives and what
// the frames hand the presentation.

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "LRCore/Common/Program.h"
#include "LRCore/Menus/VideoSettings.h"
#include "LRCore/Show/ShowState.h"
#include "LRCoreGameData.h"
#include "LRCoreSettings.h"
#include "LRCoreHarness.h"

#include <vector>

// UE's keys as USB usages.
uint32 UsbOf(const FKey& Key);
// A pad's buttons as SDL numbers them (the bindings'); -1 not a button.
int32 PadButtonOf(const FKey& Key);

// Everything the program needs that isn't a UObject.
struct FLRCoreRun
{
	FLRCoreData Files; // the install and its data cache
	lr::MenuData Data;
	lr::Program Program;
	lr::Show Show; // the race's show: advanced after each race frame
	FString SaveDir;
	// The presentation's settings (LRCoreSettings.h) and its VIDEO SETTINGS screen: open, the keys
	// it gets (the core gets none while it's open), the mouse buttons it saw, its draw list.
	FLRSettings Settings;
	lr::VideoSettings Video;
	bool bVideoOpen = false;
	TArray<TPair<lr::VideoSettings::Key, bool>> VideoKeys;
	uint32 VideoButtons = 0;
	std::vector<lr::MenuElement> VideoElements;
	std::vector<lr::MenuGlyph> VideoGlyphs;

	// Input: the key events since the last frame (menu keys), what is held (keyboard codes; each pad's buttons and stick
	// directions as bindings), the mouse in 640x480 space.
	struct FInput : public IInputProcessor
	{
		FLRCoreRun* Run = nullptr;
		virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}
		virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Event) override
		{
			if (Run->OwnsInput()) {
				return false;
			}
			if (!Event.IsRepeat()) {
				if (Run->bVideoOpen) {
					Run->VideoKey(Event.GetKey());
				}
				else {
					Run->Key(Event.GetKey(), true, Event.GetUserIndex());
				}
			}
			return false;
		}
		virtual bool HandleKeyUpEvent(FSlateApplication&, const FKeyEvent& Event) override
		{
			if (Run->OwnsInput()) {
				return false;
			}
			if (!Run->bVideoOpen) {
				Run->Key(Event.GetKey(), false, Event.GetUserIndex());
			}
			return false;
		}
		virtual bool HandleAnalogInputEvent(FSlateApplication&, const FAnalogInputEvent& Event) override
		{
			if (Run->OwnsInput()) {
				return false;
			}
			Run->Stick(Event.GetKey(), Event.GetAnalogValue(), Event.GetUserIndex());
			return false;
		}
		virtual bool HandleMouseButtonDownEvent(FSlateApplication&, const FPointerEvent& Event) override
		{
			Run->MouseButton(Event.GetEffectingButton(), true);
			return false;
		}
		virtual bool HandleMouseButtonUpEvent(FSlateApplication&, const FPointerEvent& Event) override
		{
			Run->MouseButton(Event.GetEffectingButton(), false);
			return false;
		}
		virtual const TCHAR* GetDebugName() const override { return TEXT("LRCore"); }
	};
	TSharedPtr<FInput> Input;
	TArray<lr::MenuKey> Keys;
	TArray<uint32> Held;
	TArray<uint32> PadHeld[2];
	float StickX[2] = {}, StickY[2] = {};
	int32 MouseX = 0, MouseY = 0;
	uint32 Buttons = 0;
	// Where the 640x480 screen is in the window.
	FVector2D Origin = FVector2D::ZeroVector;
	float Scale = 1, WindowAspect = 4.0f / 3;

	// The frames run and the clock of real time not yet run.
	int32 Frames = 0;
	double ClockMs = 0;

	// What the frames of a tick started (sounds, saves) and the tunes playing.
	TArray<int32> Sounds;
	bool TuneChanged = false, TuneLoops = false;
	FString Tune; // the tune to play when it changed (a file: "theme.tun"; empty: none)
	TArray<FString> RaceMusic; // the track's LEGOMSC
	// The 2D layers' pixels and the views between them: element ranges and the views' rects and scenes.
	TArray<TArray<uint32>> LayerPixels;
	TArray<uint32> LayerCrcs;
	struct FView
	{
		FIntRect Rect;
		int32 Scene;
	};
	TArray<FView> Views;
	bool RaceView = false; // the race drawn by UE's camera under the layers
	bool WideRace = false; // a one-player race in a wide window: its 3D fills it, its HUD keeps to its edges
	TArray<bool> ViewShown; // the view has something to show (else the 2D under it stays)
	bool bBooted = false;

	// A tune to start (empty: none) as the frames ask (presentation draws the racing tune from the candidates).
	void SetTune(const FString& File, bool bLoops)
	{
		TuneChanged = true;
		Tune = File;
		TuneLoops = bLoops;
	}
	// A run's harness (LRCoreHarness.h); none in normal play.
	TUniquePtr<FLRHarness> Harness;
	bool OwnsInput() const { return Harness && Harness->OwnsInput(); }
	void Log(const FString& Line)
	{
		if (Harness) {
			Harness->Log(Line);
		}
	}

	void Key(const FKey& K, bool Down, int32 User)
	{
		const int32 Button = PadButtonOf(K);
		if (Button >= 0) {
			// The menus see every pad button as one; the race its own number.
			PadHeld[User & 1].Remove(lr::SourcePad | uint32(Button));
			if (Down) {
				PadHeld[User & 1].Add(lr::SourcePad | uint32(Button));
			}
			Keys.Add({lr::MenuPadButton, Down, false});
			return;
		}
		if (K.IsMouseButton()) {
			return;
		}
		const uint32 Usb = UsbOf(K);
		const uint32 Code = lr::menuKey(Usb);
		if (!Code) {
			return;
		}
		Held.Remove(Code);
		if (Down) {
			Held.Add(Code);
			// A text field's character before its key.
			if (Program.menus.top() == lr::ScreenLicense && lr::menuCharacter(Usb)) {
				Keys.Add({lr::SourceCharacter | lr::menuCharacter(Usb), true, false});
			}
		}
		Keys.Add({Code, Down, false});
	}
	// The VIDEO SETTINGS screen's keys: the arrows and Esc, a pad's d-pad and B (its stick in Stick).
	void VideoKey(const FKey& K)
	{
		using V = lr::VideoSettings;
		struct FTo
		{
			FKey From;
			V::Key Key;
			bool bPad;
		};
		const FTo Map[] = {{EKeys::Up, V::Up, false}, {EKeys::Down, V::Down, false}, {EKeys::Left, V::Back, false},
			{EKeys::Right, V::Forward, false}, {EKeys::Escape, V::Close, false}, {EKeys::Gamepad_DPad_Up, V::Up, true},
			{EKeys::Gamepad_DPad_Down, V::Down, true}, {EKeys::Gamepad_DPad_Left, V::Back, true},
			{EKeys::Gamepad_DPad_Right, V::Forward, true}, {EKeys::Gamepad_FaceButton_Right, V::Close, true}};
		for (const FTo& To : Map) {
			if (To.From == K) {
				VideoKeys.Add({To.Key, To.bPad});
			}
		}
	}
	// A stick past 60% is an axis-button (0 right, 1 left, 2 down, 3 up).
	void Stick(const FKey& K, float Value, int32 User)
	{
		const bool X = K == EKeys::Gamepad_LeftX, Y = K == EKeys::Gamepad_LeftY;
		if (!X && !Y) {
			return;
		}
		float& Axis = X ? StickX[User & 1] : StickY[User & 1];
		const auto Direction = [&](float V) { return V > 0.6f ? (X ? 0 : 3) : V < -0.6f ? (X ? 1 : 2) : -1; };
		const int32 Before = Direction(Axis), After = Direction(Value);
		Axis = Value;
		if (Before == After) {
			return;
		}
		if (bVideoOpen && After >= 0) {
			static const lr::VideoSettings::Key Steps[4] = {lr::VideoSettings::Forward, lr::VideoSettings::Back, lr::VideoSettings::Down,
				lr::VideoSettings::Up};
			VideoKeys.Add({Steps[After], true}); // (the core's keys are dropped while the screen is open)
		}
		if (Before >= 0) {
			PadHeld[User & 1].Remove(lr::SourceAxis | uint32(Before));
			Keys.Add({lr::SourceAxis | uint32(Before), false, false});
		}
		if (After >= 0) {
			PadHeld[User & 1].Add(lr::SourceAxis | uint32(After));
			Keys.Add({lr::SourceAxis | uint32(After), true, false});
		}
	}
	void MouseButton(const FKey& K, bool Down)
	{
		const uint32 Bit = K == EKeys::LeftMouseButton ? 1 : K == EKeys::RightMouseButton ? 2 : K == EKeys::MiddleMouseButton ? 4 : 0;
		if (OwnsInput()) {
			return;
		}
		Buttons = Down ? Buttons | Bit : Buttons & ~Bit;
	}
};
