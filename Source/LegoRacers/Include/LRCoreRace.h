#pragma once

// The race in the Classic look: drawn from
// the core's world and the show only, never posed by UE itself. One scene capture per player viewport (split screen:
// two), each with its camera from the core; the track's worlds, the racers' members on their skeletons, the power-ups,
// the sky re-centred on each eye, the effects, decals and the cars' shadows, the time race's ghosts and the warp tube;
// materials and textures from the data cache (unlit: texture times the vertex colour decoded per pixel).

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "LRCore/Common/Math.h"
#include "LRCore/Menus/Menus.h"
#include "LRCore/Race/Track.h"
#include "LRCore/Show/View.h"
#include "LRCoreModelMesh.h"
#include "LRData/Look.h"
#include "LRData/Model.h"

#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "LRCoreRace.generated.h"

class AActor;
class UMaterialInstanceDynamic;
class USceneComponent;
class UMaterialInterface;
class UProceduralMeshComponent;
class USceneCaptureComponent2D;
class UTexture2D;
class UTextureRenderTarget2D;
class UWorld;

namespace lr
{
class Cache;
class Show;
struct World;
struct Game;
struct MenuData;
struct RaceFile;
struct PieceLibrary;
struct CarPiece;
struct Decal;
struct Brick;
struct Action;
} // namespace lr

struct FLRModelMesh; // a model's sections in UE's space (LRCoreRace.cpp)

UCLASS()
class ULRRaceView : public UObject
{
	GENERATED_BODY()

public:
	// The look's settings: set before Build.
	struct FLook
	{
		float Scale = 1; // the views' pixels per window pixel
	};
	FLook Settings;
	// Frame smoothing: while set, Pose leaves the captures to Smooth, which shows the views between the
	// last two poses at a fraction of the next frame.
	bool bSmoothing = false;
	void Smooth(float Fraction);
	// A race set up: its track, racers, power-ups and sky built (the loading screen's work).
	void Build(UWorld* World, const lr::Cache& Cache, const lr::World& W, const lr::Game& Game, const lr::MenuData& Data);
	void Clear();
	// After the frames of a tick: every view posed from the core and the show, and captured at the window's scale.
	void Pose(const lr::World& W, const lr::Show& Show, float WindowScale, float WindowAspect = 4.0f / 3);
	// The views to composite under the 2D (640x480 rectangles, their targets).
	int32 ViewCount() const { return Views.Num(); }
	UTextureRenderTarget2D* ViewTarget(int32 i) const { return Views.IsValidIndex(i) ? Views[i].Target.Get() : nullptr; }
	FIntRect ViewRect(int32 i) const { return Views.IsValidIndex(i) ? Views[i].Rect : FIntRect(); }
	bool bBuilt = false;

	virtual void BeginDestroy() override;

	// The menus' 3D (LRCoreMenus.cpp): a view's world and things posed
	// from the core's scene only, at the view's own place, with the cache's materials; the camera it is seen from (the
	// world's own when it has one). Kept across races.
	struct FMenuCamera
	{
		FVector Eye = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		float Fov = 45; // vertical, degrees
		bool bValid = false;
	};
	FMenuCamera PoseMenuView(UWorld* World, const lr::Cache& Cache, const lr::MenuData& Data, int32 View, const FVector& At, const lr::MenuScene& Scene, const lr::Menus& Menus,
		const lr::Game& Game, AActor*& OutActor);

private:
	struct FView
	{
		TWeakObjectPtr<USceneCaptureComponent2D> Capture;
		TWeakObjectPtr<UTextureRenderTarget2D> Target;
		FIntRect Rect;
		TWeakObjectPtr<UProceduralMeshComponent> Effects; // this view's particles, decals, beams (facing its camera)
		TWeakObjectPtr<AActor> Sky; // its sky, re-centred on its eye
		TArray<TWeakObjectPtr<UProceduralMeshComponent>> SkyDome;
	};
	TArray<FView> Views;
	// A sprite's material by name, found once: kept alive by Materials.
	struct FSpriteLook
	{
		UMaterialInstanceDynamic* Material = nullptr;
		bool bFlip = false;
	};
	TMap<FString, FSpriteLook> SpriteLooks;
	static void AddSprite(const std::array<lr::Vec3, 4>& Corners, bool bFlip, TArray<FVector>& P, TArray<FVector2D>& Uv, TArray<FColor>& Colour, TArray<int32>& T);

	// A model drawn somewhere: its components (one per part) and how they're posed.
	struct FPlaced
	{
		TWeakObjectPtr<AActor> Actor;
		TArray<TWeakObjectPtr<UProceduralMeshComponent>> Parts; // by part (node); null where the model has none
		const FLRModelMesh* Mesh = nullptr;
		bool bLit = false;
		TArray<FColor> Diffuse, Ambient; // each section's material's (lighting)
	};
	// A model posed per vertex (each vertex on the node current at its load), skinned on the CPU: one
	// component, a section per material, its vertices placed in the world every frame.
	struct FSkin
	{
		TWeakObjectPtr<UProceduralMeshComponent> Component;
		lr::Model Model;
		struct FSection
		{
			TArray<int32> Vertices; // the model's, per section vertex
			FColor Diffuse, Ambient;
			std::string Material; // the model's material it draws (what a material track's slot swaps)
		};
		TArray<FSection> Sections;
		// A cinematic colour effect's transform of its prelit colours.
		bool bColoured = false;
		int32 Shift[3] = {}, Offset[3] = {};
		// A race's built driver: each section's colours, lit once at build; none: lit as posed.
		TArray<TArray<FColor>> Prelit;
	};
	FPlaced Place(AActor* Actor, const FString& Path, const TArray<std::pair<std::string, std::string>>& Libraries, bool bLit, bool bMirrored = false, float Alpha = 1);
	void PoseNodes(FPlaced& P, const TArray<FTransform>& Nodes, const FTransform& Whole);
	void Relight(FPlaced& P, const TArray<FQuat>& Rotations);
	// The cache's model as UE sections (made once per path and mirroring); a model in hand, made once per key.
	const FLRModelMesh* Model(const FString& Path, bool bMirrored);
	const FLRModelMesh* ModelOf(const FString& Key, const lr::Model* Source, bool bMirrored);
	FPlaced PlaceMesh(AActor* Actor, const FLRModelMesh* Mesh, const TArray<std::pair<std::string, std::string>>& Libraries, bool bLit, float Alpha,
		const TMap<FString, UMaterialInterface*>* Overrides);
	// The light lit models are lit with: the race's, or a menu scene's.
	int32 LitAmbient[3] = {60, 60, 60}, LitColour[3] = {255, 255, 255};
	lr::Vec3 LitDirection = lr::normalize({-0.825175881f, -0.278099984f, -0.491675943f});
	// More lights (a menu world's or a cinematic's, up to 7 in all; 0 for the race), and a vertex lit by them all.
	int32 LitMore = 0;
	lr::Vec3 LitMoreDirections[6];
	int32 LitMoreColours[6][3] = {};
	FColor LitVertex(const lr::Vec3& N, const uint8* Diffuse, const uint8* Ambient, int32 Boost = 0) const;
	void LightMenu(const lr::MenuLighting& L);
	// A material of the libraries for a section (the texture's flip applied to its uvs by the caller), made once.
	UMaterialInstanceDynamic* MaterialFor(const std::string& Name, const TArray<std::pair<std::string, std::string>>& Libraries, float Alpha, bool& bFlip, uint8 Diffuse[4],
		bool bForceTranslucent = false, uint8* Ambient = nullptr);
	UTexture2D* Texture(const std::string& List, const std::string& Name, bool bPoint, bool bClamp);

	// The track: its plain entities, its animated ones (by World::track.animated), the sky's world.
	TArray<FPlaced> Animated;
	TArray<FPlaced> Plain;
	TArray<FString> PlainNames;
	TArray<int32> PlainOfEntity; // Track::models' index -> Plain's (-1 none)
	TArray<int16> ShownFrames; // each material binding's frame last shown
	TArray<float> PlainDistances; // each plain model entity's farthest squared draw distance last applied (the core's)
	// The racers: each member (body, variant, car, driver), its ghost copies.
	struct FRacer
	{
		FPlaced Members[4]; // (a built driver's: its head, on the face node)
		TArray<TWeakObjectPtr<UProceduralMeshComponent>> Built; // a built car's body (lit once)
		TArray<FSkin> Driver; // a built driver's body
		TWeakObjectPtr<UTexture2D> Shadow;
	};
	TArray<FRacer> Racers;
	FPlaced Ghosts[2][4];
	TArray<FSkin> GhostDrivers[2];
	// A player's built driver from its looks: the body skinned, the head as a placed model.
	void BuildRaceDriver(AActor* Actor, const std::array<uint8_t, 4>& Looks, const lr::MenuData& Data, float Alpha, TArray<FSkin>& Body, FPlaced& Head);
	// Its body on the pelvis skeleton's nodes (placed: the core's figure nodes), its head on the head node.
	void PoseRaceDriver(TArray<FSkin>& Body, FPlaced& Head, const std::vector<lr::NodeMatrix>& PelvisNodes);
	// A minifig picture (MENUDATA/PARTDB/<file>.BMP) as a material, made once.
	UMaterialInterface* Picture(const std::string& File, bool bFlip, float Alpha = 1);
	// The power-ups' models (POWERUP.WDB), by role, made once each and shown as needed.
	TArray<FPlaced> Bricks, BrickGlows, Explosions;
	// The actions' models (turbo burns and flames, shields, the dynamite, the grapple, missiles), by action and role
	// (a slot is reused by other kinds and levels, whose models differ), made when first wanted; the frame each was last
	// shown in.
	struct FActionModel
	{
		std::string Model;
		FPlaced Placed;
		uint32 ShownAt = 0;
	};
	TMap<uint32, TArray<FActionModel>> ActionModels;
	uint32 ActionFrame = 0;
	FPlaced& ActionModel(int32 Action, int32 Role, const std::string& Model);
	void PoseActions(const lr::World& W);
	// A power-up model's skeleton and parts.
	struct FFigure
	{
		std::vector<lr::SkeletonNode> Nodes;
		lr::Animation Animation;
		bool bAnimated = false;
	};
	TMap<FString, TUniquePtr<FFigure>> Figures;
	const FFigure* Figure(const std::string& Model);
	FPlaced Tube[2];
	std::string TubeModels[2];
	lr::Pose TubePoses[2];
	FVector2D TubeScroll[2];

	const lr::Cache* Cache = nullptr;
	FString Track;
	bool bMirrored = false;
	TWeakObjectPtr<UWorld> WorldRef;
	TArray<TWeakObjectPtr<AActor>> Actors;
	std::vector<std::string> EmitterLibraries[2];

	AActor* NewActor(const FTransform& At);
	void BuildSky(const lr::World& W);
	void PoseSky(int32 View, const lr::World& W, const lr::Show& Show);
	void PoseRacers(const lr::World& W);
	void PoseScenery(const lr::World& W);
	void PosePowerups(const lr::World& W);
	void BuildEffects(int32 View, const lr::World& W, const lr::Show& Show);
	void MakeShadow(int32 Racer, const lr::World& W);

	// The menus' views (LRCoreMenus.cpp).
	struct FMenuThing
	{
		FString Key;
		TArray<FPlaced> Placed; // a minifig's head; a plain model's
		TArray<TWeakObjectPtr<UProceduralMeshComponent>> Built; // a built mesh (car, piece, marks)
		TArray<FSkin> Skins; // a minifig's body; an animated cinematic entity
		std::vector<lr::SkeletonNode> Nodes;
		const lr::Animation* Animation = nullptr;
		int32 FacePart = -1; // the body node the head rides on
		// A cinematic entity's: the libraries its materials resolve in (newest first) and the material
		// table swaps shown (slot, material).
		TArray<std::pair<std::string, std::string>> Libraries;
		std::vector<std::pair<int32_t, std::string>> Shown;
	};
	// A blended world's animated entity (racer select's turntable shadows), posed at the scene's frame.
	struct FBlended
	{
		FPlaced Placed;
		std::vector<lr::SkeletonNode> Nodes;
		const lr::Animation* Animation = nullptr;
		int32 Part = -1;
		lr::Pose Where;
		float Scale = 1;
	};
	struct FMenuView
	{
		TWeakObjectPtr<AActor> Actor;
		std::string World;
		bool bCinema = false;
		FMenuCamera Last; // (a cinematic with no camera keeps the last view)
		TArray<FMenuThing> Things;
		TArray<FBlended> Blended;
		TWeakObjectPtr<UProceduralMeshComponent> Sprites; // a cinematic's worlds'
	};
	TArray<FMenuView> MenuViews;
	void PoseMenuSprites(FMenuView& V, const lr::MenuScene& Scene, const lr::MenuData& Data);
	TMap<FString, TUniquePtr<lr::Animation>> MenuAnimations;
	bool InitBase(UWorld* World, const lr::Cache& InCache);
	void ClearThing(FMenuThing& T);
	void BuildThing(FMenuThing& T, AActor* Actor, const lr::MenuScene& Scene, const lr::MenuScene::Thing& Thing, const lr::Menus& Menus, const lr::Game& Game,
		const lr::MenuData& Data);
	void BuildMinifig(FMenuThing& T, AActor* Actor, const lr::MenuScene::Thing& Thing, const lr::MenuData& Data);
	void BuildEntity(FMenuThing& T, AActor* Actor, const lr::MenuScene& Scene, const lr::MenuScene::Thing& Thing, const lr::MenuData& Data);
	void BuildPart(FMenuThing& T, AActor* Actor, const lr::MenuScene::Thing& Thing, const lr::MenuData& Data);
	void BuildMenuCar(FMenuThing& T, AActor* Actor, const lr::MenuScene::Thing& Thing, const lr::Menus& Menus, const lr::Game& Game, const lr::MenuData& Data);
	void BuildChassis(FMenuThing& T, AActor* Actor, const lr::MenuScene::Thing& Thing, const lr::MenuData& Data);
	void BuildMark(FMenuThing& T, AActor* Actor, const lr::MenuScene::Thing& Thing);
	void PoseThing(FMenuThing& T, const lr::MenuScene& Scene, const lr::MenuScene::Thing& Thing, const lr::Menus& Menus, const FVector& At);
	FSkin BuildSkin(AActor* Actor, const lr::Model& Model, const TArray<std::pair<std::string, std::string>>& Libraries, const TMap<FString, UMaterialInterface*>* Overrides,
		bool bFaceless, float Alpha = 1);
	void PoseSkin(FSkin& S, const std::vector<lr::NodeMatrix>& Nodes, const FVector& At);
	// A cinematic entity's model table swaps and colour transform.
	void ShowTable(FMenuThing& T, const lr::MenuScene::Thing& Thing);
	void Recolour(FPlaced& P, const lr::MenuScene::Thing& Thing);
	UTexture2D* ImageTexture(const std::string& Path, bool bFlip);
	UProceduralMeshComponent* BuiltMesh(AActor* Actor, const lr::CarMesh& Mesh, const TArray<std::pair<std::string, std::string>>& Libraries, const std::vector<std::string>& Names,
		float Alpha, bool bBright);
	const lr::Animation* MenuAnimation(const std::string& Path);

	void ConfigureCapture(USceneCaptureComponent2D* Capture) const;
	// Frame smoothing: the components a pose moved, from and to.
	struct FMoved
	{
		TWeakObjectPtr<USceneComponent> Component;
		FTransform From, To;
	};
	TArray<FMoved> Moved;
	TMap<TWeakObjectPtr<USceneComponent>, FTransform> BeforePose;
	void SmoothBegin();
	void SmoothEnd();
	void CaptureViews();

	UPROPERTY()
	TObjectPtr<UMaterialInterface> MaskedBase;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> TranslucentBase;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> AdditiveBase;
	AActor* PowerupActor = nullptr;
	int32 ShowClockMs = 0;
	UPROPERTY()
	TMap<FString, TObjectPtr<UTexture2D>> Textures;
	UPROPERTY()
	TMap<FString, TObjectPtr<UMaterialInstanceDynamic>> Materials;
	// Each material library and texture list decoded once (MaterialFor runs per particle per frame); none if it doesn't
	// decode.
	// Per-frame scratch (posed vertices, relit colours), kept so a frame allocates nothing for them.
	TArray<FVector> ScratchPositions;
	TArray<FColor> ScratchColours;
	TArray<FTransform> ScratchNodes;
	std::vector<lr::NodeMatrix> ScratchMatrices;
	std::unordered_map<std::string, std::optional<lr::MaterialLibrary>> LibraryCache;
	std::unordered_map<std::string, std::optional<lr::TextureList>> TextureCache;
	const lr::MaterialLibrary* LibraryAt(const std::string& Path);
	bool LoadBases();
	void BuildLibraries(const lr::RaceFile& Race, const std::string& Dir);
	void BuildScenery(const lr::RaceFile& Race, const std::string& Dir, const lr::World& W);
	void BuildRacers(const lr::World& W, const lr::MenuData& Data);
	UProceduralMeshComponent* BuiltCar(AActor* Kart, const std::vector<lr::CarPiece>& Pieces, const lr::PieceLibrary& Low, const lr::MenuData& Data);
	void BuildPowerups(const lr::World& W);
	void BuildWarpTube();
	void ShowActionModel(int32 Action, int32 Role, const std::string& Model, const lr::Pose& At, float Scale, int32 Part, float TimeMs, float Alpha, FVector2D Scroll);
	void PoseTurbo(const lr::World& W, int32 i, const lr::Action& A, const lr::Pose& Car, float Seconds);
	void PoseRacerMember(int32 i, int32 m, const lr::World& W);
	void PoseGhosts(const lr::World& W);
	FMenuView& MenuView(UWorld* World, int32 v, const FVector& At);
	void ResetMenuView(FMenuView& View, const std::string& WorldName, bool bCinema);
	void BuildMenuWorld(FMenuView& View, const lr::MenuScene& Scene, const FVector& At);
	void AddBlendedWorld(FMenuView& View, const std::string& Folder, const TArray<std::pair<std::string, std::string>>& Libraries);
	void PoseBlended(FMenuView& View, const lr::MenuScene& Scene, const FVector& At);
	static FString ThingKey(const lr::MenuScene::Thing& Th, const lr::Menus& Menus, bool bRing);
	void PoseMenuThings(FMenuView& View, const lr::MenuScene& Scene, const lr::Menus& Menus, const lr::Game& Game, const lr::MenuData& Data, const FVector& At);
	void PoseAnimatedScenery(const lr::World& W);
	void ApplyDrawDistances(const lr::World& W);
	FPlaced* SceneryOf(bool bAnimated, int32 Entity);
	void AnimateMaterials(const lr::World& W);
	static void ForSections(FPlaced& P, const std::string* Only, TFunctionRef<void(UProceduralMeshComponent*, int32, const FLRModelMesh::FSection&)> Do);
	FVector2f ShadowSize(int32 Racer, const lr::World& W);
	void MakeSky(FView& V);
	void AddSkyWorld(FView& V, AActor* A);
	void PoseBrick(const lr::World& W, const lr::Brick& B, FPlaced& Placed, int32 m);
	void PoseExplosions(const lr::World& W);
	void AddViews(int32 Count);
	void PoseView(int32 v, const lr::World& W, float WindowScale, float WideAspect);
	void HideFromView(USceneCaptureComponent2D* Capture, int32 v, const lr::World& W);
	void PoseWarpView(USceneCaptureComponent2D* Capture, const lr::World& W, const lr::PlayerCamera& Cam, lr::Vec3& EyeAt, lr::Vec3& Look, lr::Vec3& Down, float& Fov);
	struct FEffects;
	void AddParticles(FEffects& E, const lr::Show& Show);
	void AddBillboards(FEffects& E, const lr::World& W);
	void AddSprites(FEffects& E, const lr::World& W);
	void AddDecals(FEffects& E, int32 v, const lr::Show& Show);
	UMaterialInterface* DecalMaterial(const lr::Decal& D, const TArray<std::pair<std::string, std::string>>& Shared, bool& bFlip);
	const lr::TextureList* TexturesAt(const std::string& Path);
	UPROPERTY()
	TArray<TObjectPtr<UTextureRenderTarget2D>> Targets;
	UPROPERTY()
	TArray<TObjectPtr<UTexture2D>> Shadows;
	TMap<FString, TUniquePtr<FLRModelMesh>> Meshes;
	TArray<std::pair<std::string, std::string>> TrackLibraries, CommonLibraries, PowerupLibraries, SkyLibraries;
};
