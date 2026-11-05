// The race in the Classic look (LRCoreRace.h): building and clearing the view, the racers' drivers, the models' figures.

#include "LRCoreRace.h"
#include "LRCoreLog.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "GameFramework/Actor.h"
#include "LRCore/Kart/Car.h"
#include "LRCore/Common/Game.h"
#include "LRCore/Menus/Menus.h"
#include "LRCore/Show/ShowState.h"
#include "LRCore/Race/World.h"
#include "LRData/Archive.h"
#include "LRData/Cache.h"
#include "LRData/Cars.h"
#include "LRData/Look.h"
#include "LRData/Model.h"
#include "LRData/Pieces.h"
#include "LRData/Race.h"
#include "LRData/Scene.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

#include <algorithm>
#include <cmath>

#include "LRCoreModelMesh.h"
#include "LRCoreRaceParts.h"

using namespace LRUe;

void ULRRaceView::BeginDestroy()
{
	Meshes.Reset();
	Figures.Reset();
	MenuAnimations.Reset();
	Super::BeginDestroy();
}

AActor* ULRRaceView::NewActor(const FTransform& At)
{
	AActor* A = WorldRef->SpawnActor<AActor>(AActor::StaticClass(), At);
	USceneComponent* Root = NewObject<USceneComponent>(A, TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	A->SetRootComponent(Root);
	Root->RegisterComponent();
	A->SetActorTransform(At);
	Actors.Add(A);
	return A;
}

void ULRRaceView::Build(UWorld* World, const lr::Cache& InCache, const lr::World& W, const lr::Game& Game, const lr::MenuData& Data)
{
	Clear();
	SpriteLooks.Reset();
	WorldRef = World;
	Cache = &InCache;
	// The race's lights (every race the same): ambient 60, one white light.
	for (int32 c = 0; c < 3; c++) {
		LitAmbient[c] = 60, LitColour[c] = 255;
	}
	LitDirection = lr::normalize({-0.825175881f, -0.278099984f, -0.491675943f});
	LitMore = 0;
	if (!LoadBases()) {
		UE_LOG(LogLRCore, Error, TEXT("the Classic materials are missing (scripts/make_classic_materials.py)"));
		return;
	}
	Track = UTF8_TO_TCHAR(W.session.brief.track.c_str());
	bMirrored = W.track.mirrored;
	const std::string Dir = "GAMEDATA/" + W.session.brief.track + "/";
	lr::RaceFile Race;
	if (!Cache->get(Dir + W.session.brief.track + ".RAB", Race)) {
		UE_LOG(LogLRCore, Error, TEXT("%hs doesn't decode: no race view"), (Dir + W.session.brief.track + ".RAB").c_str());
		return;
	}
	BuildLibraries(Race, Dir);
	BuildScenery(Race, Dir, W);
	BuildRacers(W, Data);
	BuildPowerups(W);
	BuildSky(W);
	BuildWarpTube();
	bBuilt = true;
}

// The effects', the power-ups' and the local world's libraries.
void ULRRaceView::BuildLibraries(const lr::RaceFile& Race, const std::string& Dir)
{
	const auto Found = [&](const std::string& Name) { return Cache->has(Dir + Name) ? Dir + Name : Common + Name; };
	// The effects' and the power-ups' libraries.
	for (const std::string* Name : {&Race.globalWorld, &Race.powerupScene}) {
		lr::SceneFile Scene;
		const std::string Path = Found(lr::binaryName(*Name));
		if (!Name->empty() && Cache->get(Path, Scene)) {
			(Name == &Race.globalWorld ? CommonLibraries : PowerupLibraries) = LibrariesOf(Scene, FolderOf(Path));
		}
	}
	{
		lr::SceneFile Local;
		const std::string Path = Found(lr::binaryName(Race.localWorld));
		TArray<std::pair<std::string, std::string>> LocalLibraries;
		if (!Race.localWorld.empty() && Cache->get(Path, Local)) {
			LocalLibraries = LibrariesOf(Local, FolderOf(Path));
		}
		EmitterLibraries[1].clear();
		for (const auto& L : LocalLibraries) {
			EmitterLibraries[1].push_back(L.first);
		}
	}
}

void ULRRaceView::BuildScenery(const lr::RaceFile& Race, const std::string& Dir, const lr::World& W)
{
	// The track's worlds: plain entities where they stand (a model no entity places at the origin), animated ones posed
	// each frame from the core.
	AActor* Scenery = NewActor(FTransform::Identity);
	for (const std::string* Name : {&Race.scene, &Race.sharedScene}) {
		lr::SceneFile Scene;
		const std::string Path = Dir + lr::binaryName(*Name);
		if (Name->empty() || !Cache->size(Path) || !Cache->get(Path, Scene)) {
			continue; // (as the core: an empty shared database)
		}
		const std::string Folder = FolderOf(Path);
		// (A database naming no library finds its materials in those loaded before it, newest first)
		TArray<std::pair<std::string, std::string>> Libraries = LibrariesOf(Scene, Folder);
		if (Libraries.IsEmpty()) {
			for (const auto* Loaded : {&TrackLibraries, &PowerupLibraries, &CommonLibraries}) {
				for (int32 i = Loaded->Num(); i-- > 0;) {
					Libraries.Add((*Loaded)[i]);
				}
			}
		}
		if (Name == &Race.scene) {
			TrackLibraries = Libraries;
		}
		TArray<bool> Placed;
		Placed.Init(false, int32(Scene.models.size()));
		for (const lr::SceneFile::Entity& E : Scene.animated) {
			if (E.model >= 0 && size_t(E.model) < Scene.models.size()) {
				Placed[E.model] = true;
			}
		}
		const auto Add = [&](size_t Model, const lr::Pose& Pose) {
			FPlaced P = Place(Scenery, ModelPath(*Cache, Scene.models[Model], Folder), Libraries, false, bMirrored);
			if (!P.Mesh) {
				return -1;
			}
			const FTransform T = FrameOf(Pose.rotation * P.Mesh->Scale, Pose.position);
			for (TWeakObjectPtr<UProceduralMeshComponent>& C : P.Parts) {
				if (C.IsValid()) {
					C->SetWorldTransform(T);
				}
			}
			PlainNames.Add(UTF8_TO_TCHAR(Scene.models[Model].c_str()));
			return Plain.Add(P);
		};
		for (const lr::SceneFile::Entity& E : Scene.entities) {
			int32 Index = -1;
			if (E.model >= 0 && size_t(E.model) < Scene.models.size()) {
				lr::Pose Pose = EntityPose(E, bMirrored);
				Pose.rotation = Pose.rotation * E.scale;
				Index = Add(size_t(E.model), Pose); // (its distances are the core's, as they change: PoseScenery)
				Placed[E.model] = true;
			}
			PlainOfEntity.Add(Index);
		}
		for (size_t m = 0; m < Scene.models.size(); m++) {
			if (!Placed[int32(m)]) {
				Add(m, lr::Pose{});
			}
		}
	}
	for (const lr::AnimatedModel& A : W.track.animated) {
		FPlaced P;
		if (!A.model.empty()) {
			P = Place(Scenery, ModelPath(*Cache, A.model, Dir), TrackLibraries.Num() ? TrackLibraries : CommonLibraries, false, bMirrored);
		}
		Animated.Add(P);
	}
}

void ULRRaceView::BuildRacers(const lr::World& W, const lr::MenuData& Data)
{
	// The racers: the body (a champion's model, lit each frame; or the built car, lit once), the chassis' second variant,
	// the chassis car and the driver, each on its skeleton.
	lr::ChassisTable ChassisTable;
	lr::ChampionTable Champions;
	lr::DriverTable Drivers;
	WarnUnless(Cache->get(Common + "CHASSIS.CMB", ChassisTable), "CHASSIS.CMB");
	WarnUnless(Cache->get(Common + "CHAMPS.CCB", Champions), "CHAMPS.CCB");
	WarnUnless(Cache->get(Common + "DRIVERS.DDB", Drivers), "DRIVERS.DDB");
	lr::PieceLibrary Low;
	WarnUnless(Cache->get("MENUDATA/PIECEDB/LPIECELO.LEB", Low), "LPIECELO.LEB");
	Racers.SetNum(int32(W.racers.karts.size()));
	for (size_t i = 0; i < W.racers.karts.size() && i < W.session.brief.slots.size(); i++) {
		const lr::RaceBrief::Slot& Slot = W.session.brief.slots[i];
		FRacer& R = Racers[int32(i)];
		AActor* Kart = NewActor(FTransform::Identity);
		// (A player's slot races its record's built minifig; DRIVERS.DDB's "bb" only if none is built.)
		if (Slot.built()) {
			BuildRaceDriver(Kart, Slot.looks, Data, 1, R.Driver, R.Members[3]);
		}
		const lr::DriverTable::Driver* Driver = Slot.driver.empty() ? (R.Driver.IsEmpty() ? Drivers.find("BB") : nullptr) : Drivers.find(Slot.driver);
		const lr::ChampionTable::Champion* Champion = !Slot.driver.empty() && Driver ? Champions.find(Driver->champion) : nullptr;
		const lr::ChassisTable::Chassis* Chassis = ChassisTable.find(Champion ? Champion->chassis : Slot.chassis);
		if (Champion) {
			R.Members[0] = Place(Kart, ModelPath(*Cache, Champion->model, Common), Libs(Champion->materials, Champion->textures), true);
		}
		else if (!Slot.car.empty()) {
			R.Built.Add(BuiltCar(Kart, Slot.car, Low, Data));
			R.Members[0].Actor = Kart;
		}
		if (Chassis) {
			if (!Chassis->second.model.empty()) {
				R.Members[1] = Place(Kart, ModelPath(*Cache, Chassis->second.model, Common), Libs(Chassis->second.materials, Chassis->second.textures), true);
			}
			R.Members[2] = Place(Kart, ModelPath(*Cache, Chassis->model, Common), Libs(Chassis->materials, Chassis->textures), true);
		}
		if (Driver) {
			R.Members[3] = Place(Kart, ModelPath(*Cache, Driver->model, Common), Libs(Driver->materials, Driver->textures), true);
		}
		MakeShadow(int32(i), W);
	}
	// A time race's ghosts: player 0's members again, translucent.
	if (W.session.brief.mode == lr::RaceBrief::Mode::TimeRace && !Racers.IsEmpty()) {
		for (int32 g = 0; g < 2; g++) {
			AActor* A = NewActor(FTransform::Identity);
			const lr::RaceBrief::Slot& Slot = W.session.brief.slots[0];
			if (Slot.built()) {
				BuildRaceDriver(A, Slot.looks, Data, 127.0f / 255, GhostDrivers[g], Ghosts[g][3]);
			}
			const lr::DriverTable::Driver* Driver = Slot.driver.empty() ? (GhostDrivers[g].IsEmpty() ? Drivers.find("BB") : nullptr) : Drivers.find(Slot.driver);
			const lr::ChampionTable::Champion* Champion = !Slot.driver.empty() && Driver ? Champions.find(Driver->champion) : nullptr;
			const lr::ChassisTable::Chassis* Chassis = ChassisTable.find(Champion ? Champion->chassis : Slot.chassis);
			if (Champion) {
				Ghosts[g][0] = Place(A, ModelPath(*Cache, Champion->model, Common), Libs(Champion->materials, Champion->textures), true, false, 127.0f / 255);
			}
			if (Chassis) {
				Ghosts[g][2] = Place(A, ModelPath(*Cache, Chassis->model, Common), Libs(Chassis->materials, Chassis->textures), true, false, 127.0f / 255);
			}
			if (Driver) {
				Ghosts[g][3] = Place(A, ModelPath(*Cache, Driver->model, Common), Libs(Driver->materials, Driver->textures), true, false, 127.0f / 255);
			}
			A->SetActorHiddenInGame(true);
		}
	}
}

// The built car: the low pieces, lit once by ambient 0x80 and a white light along (-1, 0, -1).
UProceduralMeshComponent* ULRRaceView::BuiltCar(AActor* Kart, const std::vector<lr::CarPiece>& Pieces, const lr::PieceLibrary& Low, const lr::MenuData& Data)
{
	lr::Car Car;
	Car.load(Low, Pieces);
	Car.chooseOffset(Low);
	const lr::CarMesh Mesh = Car.mesh(Low, Data.sets.materials, {false, true, true, true}, Car.entries.size());
	lr::MaterialLibrary Mats;
	WarnUnless(Cache->get("MENUDATA/PIECEDB/LPIECELO.MDB", Mats), "LPIECELO.MDB");
	const lr::Vec3 Light = lr::normalize({-1, 0, -1});
	UProceduralMeshComponent* C = NewMesh(Kart);
	TMap<int32, int32> SectionOf;
	TArray<TArray<FVector>> Positions, Normals;
	TArray<TArray<FVector2D>> Uvs;
	TArray<TArray<FColor>> Colours;
	TArray<TArray<int32>> Triangles;
	TArray<int32> MaterialOf;
	for (const lr::CarMesh::Triangle& T : Mesh.triangles) {
		int32* S = SectionOf.Find(T.material);
		if (!S) {
			S = &SectionOf.Add(T.material, Positions.Num());
			Positions.AddDefaulted(), Normals.AddDefaulted(), Uvs.AddDefaulted(), Colours.AddDefaulted(), Triangles.AddDefaulted();
			MaterialOf.Add(T.material);
		}
		const std::string Name = T.material >= 0 && size_t(T.material) < Low.materials.size() ? Low.materials[size_t(T.material)] : std::string();
		const int32 m = Mats.find(Name);
		const std::array<uint8_t, 4> D = m >= 0 ? Mats.materials[size_t(m)].diffuse : std::array<uint8_t, 4>{200, 200, 200, 255};
		const std::array<uint8_t, 4> Amb = m >= 0 ? Mats.materials[size_t(m)].ambient : std::array<uint8_t, 4>{200, 200, 200, 255};
		for (int32 c = 0; c < 3; c++) {
			const lr::CarMesh::Vertex& V = Mesh.vertices[T.vertices[size_t(c)]];
			const lr::Vec3 N = lr::normalize({V.normal[0] / 127.0f, V.normal[1] / 127.0f, V.normal[2] / 127.0f});
			const float NdotL = lr::dot(N, Light);
			Triangles[*S].Add(Positions[*S].Num());
			Positions[*S].Add(FVector(V.position[0], -V.position[1], V.position[2]) * Cm);
			Normals[*S].Add(Along(N));
			Uvs[*S].Add(FVector2D(V.uv[0], V.uv[1]));
			Colours[*S].Add(FColor(LitChannel(0x80, D[0], 255, NdotL, Amb[0]), LitChannel(0x80, D[1], 255, NdotL, Amb[1]), LitChannel(0x80, D[2], 255, NdotL, Amb[2]), 255));
		}
	}
	TArray<std::pair<std::string, std::string>> PieceLibs;
	PieceLibs.Add({"MENUDATA/PIECEDB/LPIECELO.MDB", "MENUDATA/PIECEDB/LPIECELO.TDB"});
	for (int32 S = 0; S < Positions.Num(); S++) {
		C->CreateMeshSection(S, Positions[S], Triangles[S], Normals[S], Uvs[S], Colours[S], {}, false);
		bool bFlip;
		uint8 D[4];
		const std::string Name = MaterialOf[S] >= 0 && size_t(MaterialOf[S]) < Low.materials.size() ? Low.materials[size_t(MaterialOf[S])] : std::string();
		C->SetMaterial(S, MaterialFor(Name, PieceLibs, 1, bFlip, D));
	}
	C->SetCastShadow(false);
	C->SetReceivesDecals(false);
	return C;
}

// The power-ups' models: a brick and its glow per brick, the explosions.
void ULRRaceView::BuildPowerups(const lr::World& W)
{
	AActor* Powerups = NewActor(FTransform::Identity);
	PowerupActor = Powerups;
	const FString Brick = UTF8_TO_TCHAR((Common + "PUBRICKY.GDB").c_str()), Glow = UTF8_TO_TCHAR((Common + "PUTRAILY.GDB").c_str());
	const FString White = UTF8_TO_TCHAR((Common + "ENHABRIK.GDB").c_str()), WhiteGlow = UTF8_TO_TCHAR((Common + "ENHANER.GDB").c_str());
	for (const lr::Brick& B : W.powerUps.bricks) {
		Bricks.Add(Place(Powerups, B.colour ? Brick : White, PowerupLibraries, true));
		BrickGlows.Add(Place(Powerups, B.colour ? Glow : WhiteGlow, PowerupLibraries, false));
	}
	for (size_t e = 0; e < W.powerUps.explosions.size(); e++) {
		Explosions.Add(Place(Powerups, UTF8_TO_TCHAR((Common + (e < 6 ? "HEDRA01" : "SPIKEXP") + ".GDB").c_str()), PowerupLibraries, true, false, 0.999f));
	}
}

// The warp tube: TURBO3.WDB's "dtube" and "dbricks", away from the track, shown only to a warping
// player's view.
void ULRRaceView::BuildWarpTube()
{
	lr::SceneFile Tubes;
	if (Cache->get(Common + "TURBO3.WDB", Tubes)) {
		const TArray<std::pair<std::string, std::string>> Libraries = LibrariesOf(Tubes, Common);
		AActor* A = NewActor(FTransform::Identity);
		for (size_t e = 0; e < Tubes.animated.size() && e < 2; e++) {
			const lr::SceneFile::Entity& E = Tubes.animated[e];
			if (E.model >= 0 && size_t(E.model) < Tubes.models.size()) {
				Tube[e] = Place(A, UTF8_TO_TCHAR((AssetPath(Common, Tubes.models[size_t(E.model)], ".GDB")).c_str()), Libraries, false);
				TubeModels[e] = Tubes.models[size_t(E.model)];
				TubePoses[e] = EntityPose(E, false);
				TubeScroll[e] = FVector2D(E.scroll[0], E.scroll[1]);
			}
		}
	}
}

void ULRRaceView::Clear()
{
	for (const TWeakObjectPtr<AActor>& A : Actors) {
		if (A.IsValid()) {
			A->Destroy();
		}
	}
	Actors.Reset();
	Views.Reset();
	Animated.Reset();
	Plain.Reset();
	PlainNames.Reset();
	PlainOfEntity.Reset(); // (a race's own: a second race's entities would follow the first's)
	PlainDistances.Reset();
	ShownFrames.Reset();
	Racers.Reset();
	Bricks.Reset();
	BrickGlows.Reset();
	Explosions.Reset();
	ActionModels.Reset();
	PowerupActor = nullptr;
	for (auto& G : Ghosts) {
		for (FPlaced& P : G) {
			P = {};
		}
	}
	Shadows.Reset();
	TrackLibraries.Reset();
	CommonLibraries.Reset();
	Moved.Reset();
	BeforePose.Reset();
	bBuilt = false;
}

// ---- A player's built driver ----

// The pelvis skeleton's node for each of GAMEPART's leg box nodes (leg_box, chest, arm_l, arm_r, face): decided from the
// files, LEG_BOX.SDB's first four nodes stand where PELVIS.SDB's alpelvis, torso, arm_l, arm_r do; its face (the head's
// place) within 0.06 ft of PELVIS's head.
static constexpr int32 LegBoxOnPelvis[5] = {0, 1, 2, 3, 5};

void ULRRaceView::BuildRaceDriver(AActor* Actor, const std::array<uint8_t, 4>& Looks, const lr::MenuData& Data, float Alpha, TArray<FSkin>& Body, FPlaced& Head)
{
	const lr::PartCatalog& Cat = Data.parts;
	const auto Name = [](const std::vector<lr::PartCatalog::Part>& Ring, int32 i) { return i >= 0 && size_t(i) < Ring.size() ? Ring[size_t(i)].name : std::string(); };
	const uint8 Face = Looks[0], Hat = Looks[1], Legs = Looks[2], Torso = Looks[3];
	// The pictures: the face's dflt (the race always shows it), the torso's, the legs' (flipped as the menus' are).
	TMap<FString, UMaterialInterface*> Overrides;
	Overrides.Add(TEXT("face"), Picture(Name(Cat.rings[1], Face) + "DFLT", true, Alpha));
	Overrides.Add(TEXT("torso"), Picture(Name(Cat.rings[2], Torso), true, Alpha));
	Overrides.Add(TEXT("legs"), Picture(Name(Cat.rings[3], Legs), true, Alpha));
	TArray<std::pair<std::string, std::string>> BodyParts;
	BodyParts.Add({"MENUDATA/PARTDB/BODYPART.MDB", "MENUDATA/PARTDB/BODYPART.TDB"});
	lr::Model M;
	if (!Cache->get("MENUDATA/PARTDB/GAMEPART/LEG_BOX.GDB", M)) {
		return;
	}
	FSkin& S = Body.Add_GetRef(BuildSkin(Actor, M, BodyParts, &Overrides, true, Alpha));
	// Lit once: ambient 0x80 and a white light along (-1, 0, -1), on the model's own normals.
	const lr::Vec3 Light = lr::normalize({-1, 0, -1});
	for (const FSkin::FSection& Sec : S.Sections) {
		TArray<FColor>& Out = S.Prelit.AddDefaulted_GetRef();
		for (const int32 V : Sec.Vertices) {
			const float NdotL = M.normals.empty() ? 0 : lr::dot(lr::normalize({M.normals[size_t(V) * 3], M.normals[size_t(V) * 3 + 1], M.normals[size_t(V) * 3 + 2]}), Light);
			Out.Add(FColor(LitChannel(0x80, Sec.Diffuse.R, 255, NdotL, Sec.Ambient.R), LitChannel(0x80, Sec.Diffuse.G, 255, NdotL, Sec.Ambient.G),
				LitChannel(0x80, Sec.Diffuse.B, 255, NdotL, Sec.Ambient.B), 255));
		}
	}
	// The head with the hat, from ICB_CHAR.GCB (body set 1's), riding the face node.
	const std::string HatModel = size_t(Hat) < Cat.hatModels.size() ? Cat.hatModels[Hat] : std::string("head");
	if (const lr::HeadLibrary::Head* H = Data.heads[1].find(HatModel)) {
		Head = PlaceMesh(Actor, ModelOf(FString::Printf(TEXT("head#1#%hs"), HatModel.c_str()), &H->model, false), BodyParts, true, Alpha, &Overrides);
	}
}

void ULRRaceView::PoseRaceDriver(TArray<FSkin>& Body, FPlaced& Head, const std::vector<lr::NodeMatrix>& PelvisNodes)
{
	if (PelvisNodes.size() < 6) {
		return;
	}
	std::vector<lr::NodeMatrix> Nodes;
	for (const int32 n : LegBoxOnPelvis) {
		Nodes.push_back(PelvisNodes[size_t(n)]);
	}
	for (FSkin& S : Body) {
		PoseSkin(S, Nodes, FVector::ZeroVector);
	}
	const FTransform T = FrameOf(Nodes[4].rows, Nodes[4].position);
	TArray<FTransform> HeadNodes;
	HeadNodes.Init(T, Head.Parts.Num());
	PoseNodes(Head, HeadNodes, T);
	TArray<FQuat> R;
	R.Init(T.GetRotation(), FMath::Max(1, Head.Parts.Num()));
	Relight(Head, R);
}

// ---- The actions' models ----

const ULRRaceView::FFigure* ULRRaceView::Figure(const std::string& Model)
{
	const FString Key = UTF8_TO_TCHAR(Model.c_str());
	if (TUniquePtr<FFigure>* Found = Figures.Find(Key)) {
		return Found->Get();
	}
	FFigure* F = Figures.Add(Key, MakeUnique<FFigure>()).Get();
	lr::SkeletonFile Nodes;
	lr::AnimationFile Anim;
	if (Cache->get(AssetPath(Common, Model, ".SDB"), Nodes)) {
		F->Nodes = lr::nodesOf(Nodes);
	}
	if (Cache->get(AssetPath(Common, Model, ".ADB"), Anim)) {
		F->Animation = lr::animationOf(Anim, false);
		F->bAnimated = !F->Animation.parts.empty();
	}
	return F;
}
