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
inline uint32 UsbOf(const FKey& Key)
{
	static const TMap<FKey, uint32> Codes = [] {
		TMap<FKey, uint32> M;
		const FKey Letters[26] = {EKeys::A, EKeys::B, EKeys::C, EKeys::D, EKeys::E, EKeys::F, EKeys::G, EKeys::H, EKeys::I, EKeys::J, EKeys::K, EKeys::L, EKeys::M, EKeys::N,
			EKeys::O, EKeys::P, EKeys::Q, EKeys::R, EKeys::S, EKeys::T, EKeys::U, EKeys::V, EKeys::W, EKeys::X, EKeys::Y, EKeys::Z};
		for (uint32 i = 0; i < 26; i++) {
			M.Add(Letters[i], 4 + i);
		}
		const FKey Digits[10] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine, EKeys::Zero};
		for (uint32 i = 0; i < 10; i++) {
			M.Add(Digits[i], 30 + i);
		}
		const FKey Functions[12] = {EKeys::F1, EKeys::F2, EKeys::F3, EKeys::F4, EKeys::F5, EKeys::F6, EKeys::F7, EKeys::F8, EKeys::F9, EKeys::F10, EKeys::F11, EKeys::F12};
		for (uint32 i = 0; i < 12; i++) {
			M.Add(Functions[i], 58 + i);
		}
		const FKey Pad[10] = {EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree, EKeys::NumPadFour, EKeys::NumPadFive, EKeys::NumPadSix, EKeys::NumPadSeven,
			EKeys::NumPadEight, EKeys::NumPadNine, EKeys::NumPadZero};
		for (uint32 i = 0; i < 10; i++) {
			M.Add(Pad[i], 89 + i);
		}
		const TPair<FKey, uint32> Others[] = {{EKeys::Enter, 40}, {EKeys::Escape, 41}, {EKeys::BackSpace, 42}, {EKeys::Tab, 43}, {EKeys::SpaceBar, 44}, {EKeys::Hyphen, 45},
			{EKeys::Equals, 46}, {EKeys::LeftBracket, 47}, {EKeys::RightBracket, 48}, {EKeys::Backslash, 49}, {EKeys::Semicolon, 51}, {EKeys::Apostrophe, 52}, {EKeys::Tilde, 53},
			{EKeys::Comma, 54}, {EKeys::Period, 55}, {EKeys::Slash, 56}, {EKeys::CapsLock, 57}, {EKeys::ScrollLock, 71}, {EKeys::Pause, 72}, {EKeys::Insert, 73}, {EKeys::Home, 74},
			{EKeys::PageUp, 75}, {EKeys::Delete, 76}, {EKeys::End, 77}, {EKeys::PageDown, 78}, {EKeys::Right, 79}, {EKeys::Left, 80}, {EKeys::Down, 81}, {EKeys::Up, 82},
			{EKeys::NumLock, 83}, {EKeys::Divide, 84}, {EKeys::Multiply, 85}, {EKeys::Subtract, 86}, {EKeys::Add, 87}, {EKeys::Decimal, 99}, {EKeys::LeftControl, 224},
			{EKeys::LeftShift, 225}, {EKeys::LeftAlt, 226}, {EKeys::LeftCommand, 227}, {EKeys::RightControl, 228}, {EKeys::RightShift, 229}, {EKeys::RightAlt, 230},
			{EKeys::RightCommand, 231}};
		for (const auto& [K, Code] : Others) {
			M.Add(K, Code);
		}
		return M;
	}();
	const uint32* Code = Codes.Find(Key);
	return Code ? *Code : 0;
}

// A pad's buttons as SDL numbers them (the bindings'); -1 not a button.
inline int32 PadButtonOf(const FKey& Key)
{
	static const FKey Buttons[15] = {EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_FaceButton_Left, EKeys::Gamepad_FaceButton_Top,
		EKeys::Gamepad_Special_Left, FKey(), EKeys::Gamepad_Special_Right, EKeys::Gamepad_LeftThumbstick, EKeys::Gamepad_RightThumbstick, EKeys::Gamepad_LeftShoulder,
		EKeys::Gamepad_RightShoulder, EKeys::Gamepad_DPad_Up, EKeys::Gamepad_DPad_Down, EKeys::Gamepad_DPad_Left, EKeys::Gamepad_DPad_Right};
	for (int32 i = 0; i < 15; i++) {
		if (Buttons[i].IsValid() && Buttons[i] == Key) {
			return i;
		}
	}
	return -1;
}

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
		const FTo Map[] = {{EKeys::Up, V::Up, false}, {EKeys::Down, V::Down, false}, {EKeys::Left, V::Back, false}, {EKeys::Right, V::Forward, false},
			{EKeys::Escape, V::Close, false}, {EKeys::Gamepad_DPad_Up, V::Up, true}, {EKeys::Gamepad_DPad_Down, V::Down, true}, {EKeys::Gamepad_DPad_Left, V::Back, true},
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
			static const lr::VideoSettings::Key Steps[4] = {lr::VideoSettings::Forward, lr::VideoSettings::Back, lr::VideoSettings::Down, lr::VideoSettings::Up};
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
