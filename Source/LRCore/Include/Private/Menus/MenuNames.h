#pragma once

// The numbers the screens name things by: MENUNAME.SRF's bindings and styles, MENUTEXT.SRF's strings, GENC0R0.SBK's
// sounds; and where each screen keeps its widgets in its items.

#include <cstddef>
#include <cstdint>

namespace lr {

// MENUNAME.SRF's numbers for the styles the screens use, and for the bindings by screen.
constexpr uint32_t StyleMenuFont = 55, StyleBanner = 58, StylePicker = 59, StyleItem = 66, StyleLeftArrow = 67, StyleCancel = 69,
				   StyleCheck = 70, StyleTextSelector = 76, StyleImageSelector = 77, StyleSoundSlider = 236;
constexpr uint32_t NameTextBox = 61, NameImageBox = 62, NameBack = 63, NameGoNext = 64, NameGoRace = 65, NameBrickBox = 71,
				   NameBackdrop = 73, NameContext = 150, NameDialog = 153, NamePlatform = 208;
constexpr uint32_t NameGarage = 3, NameOptions = 8, NameLogo = 82, NameCircuitRace = 85, NameSingleRace = 86, NameVersusRace = 87,
				   NameTimeRace = 88, NameQuit = 89;
constexpr uint32_t NameGameOptions = 237, NameVideoOptions = 238, NameAudioOptions = 239, NamePlayer1Controls = 240,
				   NamePlayer2Controls = 241, NameLanguage = 242, NameCredits = 39, NameOpponents = 243, NameLaps = 244,
				   NameOpponentsText = 256, NameLapsText = 257, NameMusicVolume = 249, NameSoundVolume = 250, NameMusicText = 260,
				   NameSoundText = 261;
// The controls' device ring, the nine bindings' rows and their labels; the languages.
constexpr uint32_t NameDevices = 273, NameControlRows = 264, NameControlLabels = 278, NameLanguages = 311;
constexpr uint32_t NameShowcase = 110, NameRacerBox = 112, NameNewRacer = 144, NameEditRacer = 145, NameCopyRacer = 146,
				   NameDeleteRacer = 147, NameTestDrive = 148, NameEditDriver = 15, NameEditLicense = 152, NameEditCar = 17;
constexpr uint32_t NameHosting = 91, NameChampion = 92, NameRaceName = 93, NameCircuitText = 95, NameCircuitNumber = 96, NameBestTime = 106,
				   NameBestType = 107, NameBestName = 108, NameTimeTrialWon = 109, NameControl = 9; // (racer select's CONFIG)

// MENUTEXT.SRF's strings. A "Title" is a screen's banner where an item says the same;
// player 2's racer select follows player 1's.
constexpr int32_t TextMainMenu = 2, TextBuildMenu = 3, TextEditRacerTitle = 4, TextCircuitRaceTitle = 13, TextSingleRaceTitle = 14,
				  TextTimeRaceTitle = 15, TextOptionsTitle = 16, TextRacerSelect = 20, TextPlayer1RacerSelect = 21,
				  TextVersusRaceTitle = 25, TextPickLanguage = 156;
constexpr int32_t TextFinish = 30, TextCancel = 31, TextContinue = 32, TextOk = 114, TextYes = 115, TextNo = 116;
constexpr int32_t TextCircuitRace = 33, TextSingleRace = 34, TextVersusRace = 35, TextTimeRace = 36, TextBuild = 37, TextOptions = 38,
				  TextQuit = 39;
constexpr int32_t TextGameOptions = 17, TextVideoOptions = 18, TextAudioOptions = 19, TextPlayer1Controls = 23, TextPlayer2Controls = 24,
				  TextViewCredits = 88, TextOpponentsLabel = 89, TextLapsLabel = 90, TextMusicVolumeLabel = 93, TextSoundVolumeLabel = 94;
// Digits 0 to 8, the nine bindings' labels, the languages.
constexpr int32_t TextDigits = 96, TextControlLabels = 105, TextLanguages = 157;
constexpr int32_t TextNewRacer = 40, TextEditRacer = 41, TextCopyRacer = 42, TextSaveRacer = 43, TextDeleteRacer = 44, TextTestDrive = 45,
				  TextBuildDriver = 9, TextMakeLicense = 10, TextBuildCar = 11;
// The builders': a name's characters, the buttons.
constexpr int32_t TextCharset = 1, TextMix = 56, TextPlayer = 58, TextSnapshot = 59, TextRemoveBricks = 60, TextQuickBuild = 61;
// The race setup's: the circuit race's labels and champions by circuit; the time race's record (NONE: no holder).
constexpr int32_t TextHosting = 74, TextCircuitNumber = 75, TextChampions = 76, TextBestLapTime = 70, TextBestTotalTime = 71,
				  TextNoLapHolder = 190, TextNoRaceHolder = 191, TextTimeTrialWon = 73, TextConfig = 185;
// The dialogs' messages.
constexpr int32_t TextQuitQuestion = 117, TextDeleteWarning = 118, TextCancelNewRacer = 119, TextLoseChanges = 123,
				  TextCircuitUnlocked = 124, TextBuildingLimit = 186, TextSaveFull = 188, TextOptionsWarning = 189;

// The sounds the screens play themselves (backup, camact1, cammove, cantdo2, connect, discnnct, hilight1,
// movbrick, moveact, noconnct, rotatepc, slider2, glasstap).
constexpr int32_t SoundBackup = 2, SoundCamAct = 5, SoundCamMove = 6, SoundCantDo = 8, SoundConnect = 10, SoundDisconnect = 13,
				  SoundHilight = 14, SoundMoveBrick = 16, SoundMoveAct = 17, SoundNoConnect = 18, SoundRotate = 22, SoundSlider = 24,
				  SoundGlassTap = 29;

// Each screen's widgets in the screen's items (the builders' are in BuilderParts.h).
inline constexpr size_t MainGarage = 0, MainCircuitRace = 1, MainSingleRace = 2, MainVersusRace = 3, MainTimeRace = 4, MainOptions = 5,
						MainQuit = 6;
// Options: the banner; page 0's buttons; page 1's selectors and labels; page 2's sliders and the music's label.
inline constexpr size_t OptionsBanner = 0, OptionsGame = 1, OptionsVideo = 2, OptionsAudio = 3, OptionsPlayer1 = 4, OptionsPlayer2 = 5,
						OptionsLanguage = 6, OptionsCredits = 7, OptionsBack = 8, OptionsOpponents = 9, OptionsLaps = 10,
						OptionsOpponentsLabel = 11, OptionsLapsLabel = 12, OptionsMusic = 13, OptionsSound = 14, OptionsMusicLabel = 15;
inline constexpr size_t ControlsDevice = 0, ControlsDone = 1;
inline constexpr size_t LanguageList = 0, LanguageBack = 1;
// The garage: the racers; page 0's buttons and the back button; page 1's builders; the banner.
inline constexpr size_t GarageRacers = 0, GarageNew = 1, GarageEdit = 2, GarageCopy = 3, GarageDelete = 4, GarageTestDrive = 5,
						GarageBack = 6, GarageDriver = 7, GarageLicense = 8, GarageCar = 9, GarageBanner = 10;
// The race setup screens: the locked circuit's dimmer, the circuits, the circuit race's texts, the races, the buttons,
// the time race's record.
inline constexpr size_t SetupDimmer = 0, SetupCircuits = 1, SetupChampion = 2, SetupRaceName = 3, SetupCircuitNumber = 4, SetupRaces = 5,
						SetupOk = 6, SetupBack = 7, SetupRecordTime = 8, SetupRecordHolder = 9, SetupRecordKind = 10,
						SetupTimeTrialWon = 11;
inline constexpr size_t SelectRacers = 0, SelectConfig = 1, SelectRace = 2, SelectNext = 3, SelectCancel = 4;

} // namespace lr
