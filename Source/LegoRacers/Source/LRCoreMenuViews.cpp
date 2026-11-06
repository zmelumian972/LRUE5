// The menus' 3D in the Classic look (LRCoreMenuViews.h): each
// view's world and things built from the data cache with the race view's materials and posed only from the core's
// MenuScene (its camera, lights, things' world matrices, a minifig's skeleton, part and frame through the shared skeleton
// sampler, its body skinned per vertex; a cinematic's entities on their nodes).

#include "LRCoreMenuViews.h"
#include "LRCoreRaceAssets.h"

#include "LRCoreModelMesh.h"
#include "LRCoreRaceParts.h"

#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "LRCore/Kart/Car.h"
#include "LRCore/Common/Game.h"
#include "LRCore/Menus/Menus.h"
#include "LRCore/Race/Track.h"
#include "LRData/Archive.h"
#include "LRData/Cache.h"
#include "LRData/Image.h"
#include "LRData/Look.h"
#include "LRData/Model.h"
#include "LRData/Scene.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

using namespace LRUe;

namespace
{
const char* const Expressions[6] = {"DFLT", "ANGRY", "BLINK", "HAPPY", "SAD", "SUPRZ"};
const char* const PartFiles[5] = {"", "MENUDATA/LEGOMAN.ADB", "MENUDATA/RSANIM.ADB", "MENUDATA/CBANIM.ADB", "MENUDATA/CMAMAN.ADB"};
// A cinematic's libraries, newest first.
TArray<std::pair<std::string, std::string>> CinemaLibraries(const lr::MenuData::Screen& Sc)
{
	TArray<std::pair<std::string, std::string>> L;
	for (size_t i = Sc.cinemaLibraries.size(); i-- > 0;) {
		L.Add({Sc.cinemaLibraries[i].materials, Sc.cinemaLibraries[i].textures});
	}
	return L;
}
} // namespace

// A built mesh (a car, the held piece, a carousel item, the marks) as one component with a section per material: the
// library's materials, vertex colours lit by the view's lights, at the alpha given (translucent below 1).
UProceduralMeshComponent* FLRMenuViews::BuiltMesh(FLRRaceAssets& Assets, FLRModels& Models, AActor* Actor, const lr::CarMesh& Mesh,
	const TArray<std::pair<std::string, std::string>>& Libraries, const std::vector<std::string>& Names, float Alpha, bool bBright)
{
	UProceduralMeshComponent* MeshComponent = NewMesh(Actor);
	static const lr::MaterialLibrary None;
	const lr::MaterialLibrary* Found = Libraries.Num() ? Assets.LibraryAt(Libraries[0].first) : nullptr;
	const lr::MaterialLibrary& Lib = Found ? *Found : None; // (none: the default colours below)
	TMap<int32, int32> SectionOf;
	struct FSection
	{
		TArray<FVector> P, N;
		TArray<FVector2D> Uv;
		TArray<FColor> Colour;
		TArray<int32> T;
		int32 Material = -1;
	};
	TArray<FSection> Sections;
	// A hovered car or held piece is lit with ambient 180 instead of 120.
	const int32 Boost = bBright ? 60 : 0;
	for (const lr::CarMesh::Triangle& T : Mesh.triangles) {
		if (T.vertices[0] >= Mesh.vertices.size() || T.vertices[1] >= Mesh.vertices.size() || T.vertices[2] >= Mesh.vertices.size()) {
			continue; // (a bad vertex index: the triangle left out)
		}
		int32* SectionIndex = SectionOf.Find(T.material);
		if (!SectionIndex) {
			SectionIndex = &SectionOf.Add(T.material, Sections.Num());
			Sections.AddDefaulted_GetRef().Material = T.material;
		}
		const std::string Name = T.material >= 0 && size_t(T.material) < Names.size() ? Names[size_t(T.material)] : std::string();
		const int32 MaterialIndex = Lib.find(Name);
		const std::array<uint8_t, 4> DiffuseColour = MaterialIndex >= 0 ? Lib.materials[size_t(MaterialIndex)].diffuse
																		: std::array<uint8_t, 4>{200, 200, 200, 255};
		const std::array<uint8_t, 4> AmbientColour = MaterialIndex >= 0 ? Lib.materials[size_t(MaterialIndex)].ambient
																		: std::array<uint8_t, 4>{200, 200, 200, 255};
		FSection& Sec = Sections[*SectionIndex];
		for (int32 c = 0; c < 3; c++) {
			const lr::CarMesh::Vertex& V = Mesh.vertices[T.vertices[size_t(c)]];
			const lr::Vec3 N = lr::normalize({V.normal[0] / 127.0f, V.normal[1] / 127.0f, V.normal[2] / 127.0f});
			Sec.T.Add(Sec.P.Num());
			Sec.P.Add(FVector(V.position[0], -V.position[1], V.position[2]) * Cm);
			Sec.N.Add(Along(N));
			Sec.Uv.Add(FVector2D(V.uv[0], V.uv[1]));
			Sec.Colour.Add(Models.LitVertex(N, DiffuseColour.data(), AmbientColour.data(), Boost));
		}
	}
	for (int32 s = 0; s < Sections.Num(); s++) {
		MeshComponent->CreateMeshSection(s, Sections[s].P, Sections[s].T, Sections[s].N, Sections[s].Uv, Sections[s].Colour, {}, false);
		bool bFlip;
		uint8 DiffuseColour[4];
		const int32 MaterialIndex = Sections[s].Material;
		const std::string Name = MaterialIndex >= 0 && size_t(MaterialIndex) < Names.size() ? Names[size_t(MaterialIndex)] : std::string();
		MeshComponent->SetMaterial(s, Assets.MaterialFor(Name, Libraries, Alpha, bFlip, DiffuseColour, Alpha < 1));
	}
	return MeshComponent;
}

// A cinematic entity's model table as the material tracks left it: each slot's sections drawn
// with the material written there (found across the cinematic's libraries), the model's own where nothing is written.
void FLRMenuViews::ShowTable(FLRRaceAssets& Assets, FMenuThing& T, const lr::MenuScene::Thing& Thing)
{
	if (Thing.materials == T.Shown) {
		return;
	}
	const auto Written = [&](const std::vector<std::pair<int32_t, std::string>>& Table, int32 Slot) -> const std::string* {
		for (const auto& [S, Name] : Table) {
			if (S == Slot) {
				return &Name;
			}
		}
		return nullptr;
	};
	// Every slot written now or before (one no longer written goes back to the model's own).
	TArray<int32> Slots;
	using FTable = std::vector<std::pair<int32_t, std::string>>;
	for (const FTable* Table : {&Thing.materials, static_cast<const FTable*>(&T.Shown)}) {
		for (const auto& [S, Name] : *Table) {
			Slots.AddUnique(S);
		}
	}
	for (const int32 Slot : Slots) {
		const std::string* Now = Written(Thing.materials, Slot);
		for (FLRPlaced& P : T.Placed) {
			if (!P.Mesh || Slot < 0 || size_t(Slot) >= P.Mesh->MaterialNames.size()) {
				continue;
			}
			const std::string& Own = P.Mesh->MaterialNames[size_t(Slot)];
			bool bFlip;
			uint8 D[4];
			UMaterialInstanceDynamic* Shown = Assets.MaterialFor(Now ? *Now : Own, T.Libraries, 1, bFlip, D);
			FLRModels::ForSections(P, &Own,
				[&](UProceduralMeshComponent* C, int32 Index, const FLRModelMesh::FSection&) { C->SetMaterial(Index, Shown); });
		}
		for (FLRSkin& S : T.Skins) {
			if (!S.Component.IsValid() || Slot < 0 || size_t(Slot) >= S.Model.materials.size()) {
				continue;
			}
			const std::string& Own = S.Model.materials[size_t(Slot)];
			bool bFlip;
			uint8 D[4];
			UMaterialInstanceDynamic* Shown = Assets.MaterialFor(Now ? *Now : Own, T.Libraries, 1, bFlip, D);
			for (int32 s = 0; s < S.Sections.Num(); s++) {
				if (S.Sections[s].Material == Own) {
					S.Component->SetMaterial(s, Shown);
				}
			}
		}
	}
	T.Shown = Thing.materials;
}

// A plain cinematic entity's prelit colours under its model's colour transform (the caller
// leaves out models without vertex colours).
void FLRMenuViews::Recolour(FLRPlaced& P, const lr::MenuScene::Thing& Thing)
{
	if (!Thing.coloured || !P.Mesh || P.bLit) {
		return;
	}
	const int32 Shift[3] = {Thing.colourShift[0], Thing.colourShift[1], Thing.colourShift[2]};
	const int32 Offset[3] = {Thing.colourOffset[0], Thing.colourOffset[1], Thing.colourOffset[2]};
	FLRModels::ForSections(P, nullptr, [&](UProceduralMeshComponent* Part, int32 Index, const FLRModelMesh::FSection& S) {
		TArray<FColor> Colours;
		for (const FColor& C : S.Colours) {
			Colours.Add(Transformed(C, Shift, Offset));
		}
		Part->UpdateMeshSection(Index, S.Positions, {}, {}, Colours, {});
	});
}

void FLRMenuViews::ClearThing(FMenuThing& T)
{
	for (FLRPlaced& P : T.Placed) {
		for (TWeakObjectPtr<UProceduralMeshComponent>& C : P.Parts) {
			if (C.IsValid()) {
				C->DestroyComponent();
			}
		}
	}
	for (TWeakObjectPtr<UProceduralMeshComponent>& C : T.Built) {
		if (C.IsValid()) {
			C->DestroyComponent();
		}
	}
	for (FLRSkin& S : T.Skins) {
		if (S.Component.IsValid()) {
			S.Component->DestroyComponent();
		}
	}
	T = {};
}

// A menu scene's thing built: its components, skins, nodes and animation in T.
void FLRMenuViews::BuildThing(const FBuildContext& Ctx, const lr::MenuScene& Scene, const lr::Menus& Menus, const lr::Game& Game,
	const lr::MenuData& Data)
{
	const auto& [Assets, Models, T, Actor, Thing] = Ctx;
	using Kind = lr::MenuScene::Thing;
	switch (Thing.kind) {
	case Kind::Minifig: BuildMinifig(Ctx, Data); break;
	case Kind::Entity: BuildEntity(Ctx, Scene, Data); break;
	case Kind::Part: BuildPart(Ctx, Data); break;
	case Kind::Car: BuildMenuCar(Ctx, Menus, Game, Data); break;
	case Kind::Chassis: BuildChassis(Ctx, Data); break;
	case Kind::Piece:
		// A piece carousel's item, or the builder's held piece.
		T.Built.Add(Scene.world.empty() ? BuiltMesh(Assets, Models, Actor, Menus.carouselItem(Thing.type, Thing.colour),
											  Library("MENUDATA/PIECEDB/LPIECEHI"), Data.pieces.materials, 1, false)
										: BuiltMesh(Assets, Models, Actor, Menus.heldMesh, Library("MENUDATA/PIECEDB/LPIECEHI"),
											  Data.pieces.materials, float(Thing.alpha) / 255.0f, Thing.lit));
		break;
	case Kind::Mark: BuildMark(Ctx); break;
	default: break;
	}
}

// A minifig: the menus' body (the seated leg box in a car) skinned per vertex on the skeleton the core names, the hat's
// head from CBBODIES.GCB on the body's face node, and its pictures, flipped as the face's: the face's for an expression,
// the torso's, the legs' (the torso flipped too: decided from the menus shots, its hands and print).
void FLRMenuViews::BuildMinifig(const FBuildContext& Ctx, const lr::MenuData& Data)
{
	const auto& [Assets, Models, T, Actor, Thing] = Ctx;
	const lr::PartCatalog& Cat = Data.parts;
	const TArray<std::pair<std::string, std::string>> BodyParts = Library("MENUDATA/PARTDB/BODYPART");
	TMap<FString, UMaterialInterface*> Overrides;
	const uint8 Face = Thing.looks[0], Hat = Thing.looks[1], Legs = Thing.looks[2], Torso = Thing.looks[3];
	const int32 Body = Thing.seated ? 4 : FMath::Clamp(Thing.body, 0, 4);
	const std::string Folder = "MENUDATA/PARTDB/MENUPART/";
	Overrides.Add(TEXT("face"), Assets.Picture(PartName(Cat.rings[1], Face) + Expressions[FMath::Min<int32>(Thing.looks[4], 5)], true));
	Overrides.Add(TEXT("torso"), Assets.Picture(PartName(Cat.rings[2], Torso), true));
	Overrides.Add(TEXT("legs"), Assets.Picture(PartName(Cat.rings[3], Legs), true));
	lr::Model M;
	if (size_t(Body) < Cat.bodies.size() && Assets.Cache->get(AssetPath(Folder, Cat.bodies[size_t(Body)], ".GDB"), M)) {
		for (const lr::Model::Section& Sec : M.sections) {
			if (Sec.material < M.materials.size() && M.materials[Sec.material] == "face") {
				T.FacePart = Sec.part;
			}
		}
		T.Skins.Add(Models.BuildSkin(Assets, Actor, M, BodyParts, &Overrides, true));
	}
	const std::string HatModel = size_t(Hat) < Cat.hatModels.size() ? Cat.hatModels[Hat] : std::string("head");
	if (const lr::HeadLibrary::Head* H = Data.heads[0].find(HatModel)) {
		T.Placed.Add(Models.PlaceMesh(Assets, Actor,
			Assets.ModelOf(FString::Printf(TEXT("head#0#%hs"), HatModel.c_str()), &H->model, false), BodyParts, true, 1, &Overrides));
	}
	lr::SkeletonFile S;
	if (Assets.Cache->get(Thing.skeleton.empty() && size_t(Body) < Cat.bodies.size() ? AssetPath(Folder, Cat.bodies[size_t(Body)], ".SDB")
																					 : Thing.skeleton,
			S)) {
		T.Nodes = lr::nodesOf(S);
	}
	T.Animation = Assets.MenuAnimation(!Thing.partFile.empty() ? Thing.partFile : std::string(PartFiles[Thing.parts]));
}

// A cinematic's entity: its model through the cinematic's folder, then MENUDATA/, its materials
// found by name across every library the cinematic's worlds loaded, newest first (the blended world names
// none); an animated one skinned on its nodes; its model's colour transform while a colour effect runs on it.
void FLRMenuViews::BuildEntity(const FBuildContext& Ctx, const lr::MenuScene& Scene, const lr::MenuData& Data)
{
	const auto& [Assets, Models, T, Actor, Thing] = Ctx;
	if (Scene.cinemaScreen >= Data.screens.size()) {
		return;
	}
	const lr::MenuData::Screen& Sc = Data.screens[Scene.cinemaScreen];
	if (Thing.entity[0] < 0 || size_t(Thing.entity[0]) >= Sc.cinemaWorlds.size()) {
		return;
	}
	const auto Path = [&](const std::string& Base, const char* Ext) {
		const std::string Own = Sc.cinemaFolder + lr::upper(Base) + Ext;
		return Assets.Cache->has(Own) ? Own : "MENUDATA/" + lr::upper(Base) + Ext;
	};
	T.Libraries = CinemaLibraries(Sc);
	lr::Model M;
	if (!Assets.Cache->get(Path(Thing.model, ".GDB"), M)) {
		return;
	}
	if (Thing.entity[1]) {
		FLRSkin& S = T.Skins.Add_GetRef(Models.BuildSkin(Assets, Actor, M, T.Libraries, nullptr, false));
		S.bColoured = Thing.coloured && M.kind == lr::Model::Vertices::Colored;
		for (int32 c = 0; c < 3; c++) {
			S.Shift[c] = Thing.colourShift[size_t(c)], S.Offset[c] = Thing.colourOffset[size_t(c)];
		}
	}
	else {
		FLRPlaced& P = T.Placed.Add_GetRef(Models.PlaceMesh(Assets, Actor,
			Assets.ModelOf(UTF8_TO_TCHAR((Path(Thing.model, ".GDB")).c_str()), &M, false), T.Libraries, true, 1, nullptr));
		if (M.kind == lr::Model::Vertices::Colored) {
			Recolour(P, Thing);
		}
	}
}

// A driver ring's item: a hat's own entry, or the ring's body part model with its picture.
void FLRMenuViews::BuildPart(const FBuildContext& Ctx, const lr::MenuData& Data)
{
	const auto& [Assets, Models, T, Actor, Thing] = Ctx;
	const lr::PartCatalog& Cat = Data.parts;
	const TArray<std::pair<std::string, std::string>> BodyParts = Library("MENUDATA/PARTDB/BODYPART");
	TMap<FString, UMaterialInterface*> Overrides;
	const int32 Ring = Thing.colour;
	if (Ring == 0) {
		if (const lr::HeadLibrary::Head* H = Data.heads[0].find(Thing.model)) {
			T.Placed.Add(Models.PlaceMesh(Assets, Actor,
				Assets.ModelOf(FString::Printf(TEXT("head#0#%hs"), Thing.model.c_str()), &H->model, false), BodyParts, true, 1, nullptr));
		}
		return;
	}
	if (Ring == 1) {
		Overrides.Add(TEXT("face"), Assets.Picture(PartName(Cat.rings[1], Thing.type) + "DFLT", true));
	}
	else if (Ring == 2) {
		Overrides.Add(TEXT("torso"), Assets.Picture(PartName(Cat.rings[2], Thing.type), true));
	}
	else {
		Overrides.Add(TEXT("legs"), Assets.Picture(PartName(Cat.rings[3], Thing.type), true));
	}
	const FLRModelMesh* M = Assets.Model(UTF8_TO_TCHAR((AssetPath("MENUDATA/PARTDB/MENUPART/", Thing.model, ".GDB")).c_str()), false);
	T.Placed.Add(Models.PlaceMesh(Assets, Actor, M, BodyParts, true, 1, &Overrides));
}

// The builders' and racer select's car is the menus' mesh; a cinematic's its swapped one; the garage's is its racer's,
// built here.
void FLRMenuViews::BuildMenuCar(const FBuildContext& Ctx, const lr::Menus& Menus, const lr::Game& Game, const lr::MenuData& Data)
{
	const auto& [Assets, Models, T, Actor, Thing] = Ctx;
	lr::CarMesh Built;
	const lr::CarMesh* Mesh = Thing.model == "cinema" ? &Menus.cinemaCar : &Menus.carMesh;
	if (Menus.screen == lr::ScreenGarage) {
		const int32 n = int32(Menus.screens.state.racers.size());
		if (const lr::RacerRecord* R = n && Menus.screens.state.shown >= 0
				? Game.racer(Menus.screens.state.racers[size_t(Menus.screens.state.shown % n)])
				: nullptr) {
			lr::Car Car;
			Car.load(Data.pieces, R->pieces);
			Car.chooseOffset(Data.pieces);
			Built = Car.menuMesh(Data.pieces, Data.sets.materials);
			Mesh = &Built;
		}
	}
	T.Built.Add(BuiltMesh(Assets, Models, Actor, *Mesh, Library("MENUDATA/PIECEDB/LPIECEHI"), Data.pieces.materials, 1, Thing.lit));
}

// A chassis on its own skeleton and animation (MENUDATA/PIECEDB/).
void FLRMenuViews::BuildChassis(const FBuildContext& Ctx, const lr::MenuData& Data)
{
	const auto& [Assets, Models, T, Actor, Thing] = Ctx;
	const lr::ChassisTable::Chassis* C = nullptr;
	for (const lr::ChassisTable::Chassis& Each : Data.menuChassis.chassis) {
		C = Each.model == Thing.model ? &Each : C;
	}
	if (!C) {
		return;
	}
	const std::string Base = "MENUDATA/PIECEDB/";
	TArray<std::pair<std::string, std::string>> Libs;
	Libs.Add({AssetPath(Base, C->materials.empty() ? C->model : C->materials, ".MDB"),
		AssetPath(Base, C->textures.empty() ? C->model : C->textures, ".TDB")});
	T.Placed.Add(Models.Place(Assets, Actor, UTF8_TO_TCHAR((AssetPath(Base, C->model, ".GDB")).c_str()), Libs, true));
	lr::SkeletonFile S;
	if (Assets.Cache->get(AssetPath(Base, C->skeleton.empty() ? C->model : C->skeleton, ".SDB"), S)) {
		T.Nodes = lr::nodesOf(S);
	}
	T.Animation = Assets.MenuAnimation(AssetPath(Base, C->animation.empty() ? C->model : C->animation, ".ADB"));
}

// A builder's mark: one 1x1 quad in the car's frame with the overlay's material (BLDRGRID's second), uvs (0.5, 0)-(0.99, 0.49).
void FLRMenuViews::BuildMark(const FBuildContext& Ctx)
{
	const auto& [Assets, Models, T, Actor, Thing] = Ctx;
	static const lr::MaterialLibrary None;
	const lr::MaterialLibrary* Found = Assets.LibraryAt("MENUDATA/PIECEDB/BLDRGRID.MDB");
	const lr::MaterialLibrary& Grid = Found ? *Found : None;
	lr::CarMesh Quad;
	const float X = Thing.at[0], Y = Thing.at[1], Z = Thing.at[2];
	const float Corners[4][4] = {{X, Y, 0.5f, 0}, {X + 1, Y, 0.99f, 0}, {X + 1, Y + 1, 0.99f, 0.49f}, {X, Y + 1, 0.5f, 0.49f}};
	for (const auto& Cn : Corners) {
		Quad.vertices.push_back({{Cn[0], Cn[1], Z}, {0, 0, 127}, {Cn[2], Cn[3]}});
	}
	Quad.triangles.push_back({{0, 1, 2}, 0});
	Quad.triangles.push_back({{0, 2, 3}, 0});
	Quad.triangles.push_back({{0, 2, 1}, 0});
	Quad.triangles.push_back({{0, 3, 2}, 0});
	std::vector<std::string> Names = {Grid.materials.size() > 1 ? Grid.materials[1].name : std::string()};
	T.Built.Add(BuiltMesh(Assets, Models, Actor, Quad, Library("MENUDATA/PIECEDB/BLDRGRID"), Names, 1, false));
}

void FLRMenuViews::PoseThing(FLRRaceAssets& Assets, FLRModels& Models, FMenuThing& T, const lr::MenuScene& Scene,
	const lr::MenuScene::Thing& Thing, const lr::Menus& Menus, const FVector& At)
{
	const lr::NodeMatrix& W = Thing.world;
	const float Scale = lr::length(W.rows.f);
	const FTransform Offset(At);
	const FTransform Whole = FrameOf(W.rows, W.position) * Offset;
	for (TWeakObjectPtr<UProceduralMeshComponent>& C : T.Built) {
		if (C.IsValid()) {
			C->SetWorldTransform(Whole);
		}
	}
	if (Scale <= 0) {
		return;
	}
	// The nodes: a cinematic entity's from the core, a minifig's and a chassis' sampled here on their skeleton.
	std::vector<lr::NodeMatrix> Out;
	if (Thing.kind == lr::MenuScene::Thing::Entity) {
		lr::entityNodes(*Menus.data, Scene, Thing, Out);
		ShowTable(Assets, T, Thing);
	}
	else if (!T.Nodes.empty()) {
		const lr::Basis Unit = {W.rows.f * (1 / Scale), W.rows.s * (1 / Scale), W.rows.u * (1 / Scale)};
		const bool bChassis = Thing.kind == lr::MenuScene::Thing::Chassis;
		const int32 Part = bChassis ? 0 : Thing.animation;
		const lr::Animation* A = T.Animation && Part >= 0 && size_t(Part) < T.Animation->parts.size() ? T.Animation : nullptr;
		lr::poseSkeleton(T.Nodes, A, {A ? Part : -1, Thing.frame, true}, lr::Pose{W.position, Unit}, Scale, Out);
	}
	for (FLRSkin& S : T.Skins) {
		Models.PoseSkin(S, Out, At);
	}
	TArray<FTransform> Nodes;
	FramesOf(Out, Nodes, Offset);
	const TArray<FTransform> NoNodes;
	for (FLRPlaced& P : T.Placed) {
		if (Thing.kind == lr::MenuScene::Thing::Minifig) {
			// The head: its whole mesh rides the face node.
			Models.PoseOn(P, Nodes.IsValidIndex(T.FacePart) ? Nodes[T.FacePart] : Whole);
			continue;
		}
		// (A chassis on its nodes, anything else whole.)
		Models.PoseLit(P, Thing.kind == lr::MenuScene::Thing::Chassis ? Nodes : NoNodes, Whole);
	}
}

// A cinematic's worlds' sprites, turned to its camera each frame.
void FLRMenuViews::PoseMenuSprites(FLRRaceAssets& Assets, FMenuView& V, const lr::MenuScene& Scene, const lr::MenuData& Data)
{
	if (Scene.cinemaScreen >= Data.screens.size()) {
		return;
	}
	const lr::MenuData::Screen& Sc = Data.screens[Scene.cinemaScreen];
	const TArray<std::pair<std::string, std::string>> Libraries = CinemaLibraries(Sc);
	const lr::Vec3 Eye = lr::vec(Scene.eye), Look = lr::normalize(lr::vec(Scene.target) - Eye);
	const lr::Vec3 Right = lr::normalize(lr::cross(Look, lr::vec(Scene.up)));
	const lr::Vec3 Down = lr::cross(Look, Right);
	int32 Section = 0;
	for (const lr::MenuData::CinemaWorld& World : Sc.cinemaWorlds) {
		for (const lr::SceneFile::Sprite& File : World.scene.sprites) {
			if (!V.Sprites.IsValid()) {
				UProceduralMeshComponent* C = NewMesh(V.Actor.Get());
				V.Sprites = C;
			}
			const lr::Sprite S = lr::Sprite::of(File, false);
			if (!S.inReach(Eye)) {
				continue;
			}
			bool bFlip;
			uint8 D[4];
			UMaterialInstanceDynamic* Mid = Assets.MaterialFor(S.material, Libraries, 1, bFlip, D);
			TArray<FVector> P;
			TArray<FVector2D> Uv;
			TArray<FColor> Colour;
			TArray<int32> T;
			AddSprite(S.corners(Right, Down, Look), bFlip, P, Uv, Colour, T);
			V.Sprites->CreateMeshSection(Section, P, T, {}, Uv, Colour, {}, false);
			V.Sprites->SetMaterial(Section++, Mid);
		}
	}
	for (int32 Extra = V.Sprites.IsValid() ? V.Sprites->GetNumSections() : 0; Extra-- > Section;) {
		V.Sprites->ClearMeshSection(Extra);
	}
}

FLRMenuCamera FLRMenuViews::Pose(FLRRaceAssets& Assets, FLRModels& Models, UWorld* World, const lr::MenuData& Data, int32 v,
	const FVector& At, const lr::MenuScene& Scene, const lr::Menus& Menus, const lr::Game& Game, AActor*& OutActor)
{
	FMenuView& View = MenuView(World, v, At);
	AActor* Actor = View.Actor.Get();
	OutActor = Actor;
	// The view's lights (from the core).
	Models.LightMenu(Scene.lighting);
	// Its world (a menu .WDB of that name: its entities where they stand, at rest), rebuilt when it changes; a
	// cinematic's or preview's draws only what its cues show.
	const bool bCinema = Scene.cinemaScreen != 0;
	if (View.World != Scene.world || View.bCinema != bCinema) {
		ResetMenuView(View, Scene.world, bCinema);
		if (!bCinema && Scene.drawWorld) {
			BuildMenuWorld(Assets, Models, View, Scene, At);
		}
	}
	PoseBlended(Models, View, Scene, At);
	PoseMenuThings(Assets, Models, View, Scene, Menus, Game, Data, At);
	if (bCinema && Scene.camera) {
		PoseMenuSprites(Assets, View, Scene, Data);
	}
	// The camera: the core's (the world's when it has one; a cinematic's stack), else the last.
	const FVector Ahead = Along({Scene.target[0] - Scene.eye[0], Scene.target[1] - Scene.eye[1], Scene.target[2] - Scene.eye[2]});
	if (!Scene.camera || Ahead.IsNearlyZero()) {
		return View.Last;
	}
	FLRMenuCamera Out;
	Out.Eye = Ue(lr::vec(Scene.eye)) + At;
	Out.Rotation = FRotationMatrix::MakeFromXZ(Ahead, Along(lr::vec(Scene.up))).Rotator();
	Out.Fov = Scene.fov;
	Out.bValid = true;
	View.Last = Out;
	return Out;
}

// Menu view v and its actor, made when first asked for.
FLRMenuViews::FMenuView& FLRMenuViews::MenuView(UWorld* World, int32 v, const FVector& At)
{
	while (MenuViews.Num() <= v) {
		MenuViews.AddDefaulted();
	}
	FMenuView& View = MenuViews[v];
	if (!View.Actor.IsValid()) {
		View = {};
		View.Actor = NewRootedActor(World, FTransform(At));
	}
	return View;
}

// Everything the view drew goes; it now shows WorldName (a cinematic's or not).
void FLRMenuViews::ResetMenuView(FMenuView& View, const std::string& WorldName, bool bCinema)
{
	for (FMenuThing& Thing : View.Things) {
		ClearThing(Thing);
	}
	View.Things.Reset();
	AActor* Actor = View.Actor.Get();
	TArray<UActorComponent*> Old;
	Actor->GetComponents(Old);
	for (UActorComponent* Component : Old) {
		if (Component != Actor->GetRootComponent()) {
			Component->DestroyComponent();
		}
	}
	View.World = WorldName;
	View.bCinema = bCinema;
	View.Last = {};
	View.Blended.Reset();
}

// The scene's world: the first MENUDATA folder's .WDB of its name, its entities where they stand, lit once; then its
// blended world.
void FLRMenuViews::BuildMenuWorld(FLRRaceAssets& Assets, FLRModels& Models, FMenuView& View, const lr::MenuScene& Scene, const FVector& At)
{
	if (Scene.world.empty()) {
		return;
	}
	const std::string Wanted = "/" + lr::upper(Scene.world) + ".WDB";
	for (const std::string& File : Assets.Cache->paths()) {
		if (!File.starts_with("MENUDATA/") || !File.ends_with(Wanted)) {
			continue;
		}
		lr::SceneFile World;
		if (!Assets.Cache->get(File, World)) {
			return;
		}
		const std::string Folder = FolderOf(File);
		const TArray<std::pair<std::string, std::string>> Libraries = LibrariesOf(World, Folder);
		for (const std::vector<lr::SceneFile::Entity>* List : {&World.entities, &World.animated}) {
			for (const lr::SceneFile::Entity& E : *List) {
				if (E.model < 0 || size_t(E.model) >= World.models.size()) {
					continue;
				}
				FLRPlaced Placed = Models.Place(Assets, View.Actor.Get(),
					UTF8_TO_TCHAR((AssetPath(Folder, World.models[size_t(E.model)], ".GDB")).c_str()), Libraries, true);
				if (!Placed.Mesh) {
					continue;
				}
				const lr::Basis Rows = lr::basisFromForwardUp(lr::normalize(lr::vec(E.forward)), lr::normalize(lr::vec(E.up)));
				Models.PoseLit(Placed, {}, FrameOf(Rows * (E.scale * Placed.Mesh->Scale), lr::vec(E.position)) * FTransform(At));
			}
		}
		if (Scene.blendedWorld) {
			AddBlendedWorld(Assets, Models, View, Folder, Libraries);
		}
		return;
	}
}

// The view's blended world (the driver builder's spotlight cone and the stand's shadow, racer select's
// turntable shadows): the folder's BLENDED.WDB drawn after the world and the view's things (its translucent sort later),
// its materials found by name in the libraries the world loaded, newest first (it names none); its
// animated entities play their part at the scene's frame.
void FLRMenuViews::AddBlendedWorld(FLRRaceAssets& Assets, FLRModels& Models, FMenuView& View, const std::string& Folder,
	const TArray<std::pair<std::string, std::string>>& Libraries)
{
	lr::SceneFile Blended;
	if (!Assets.Cache->get(Folder + "BLENDED.WDB", Blended)) {
		return;
	}
	TArray<std::pair<std::string, std::string>> Newest;
	for (int32 i = Libraries.Num(); i-- > 0;) {
		Newest.Add(Libraries[i]);
	}
	for (const std::vector<lr::SceneFile::Entity>* List : {&Blended.entities, &Blended.animated}) {
		for (const lr::SceneFile::Entity& E : *List) {
			if (E.model < 0 || size_t(E.model) >= Blended.models.size()) {
				continue;
			}
			FBlended& Entry = View.Blended.AddDefaulted_GetRef();
			Entry.Placed = Models.Place(Assets, View.Actor.Get(),
				UTF8_TO_TCHAR((AssetPath(Folder, Blended.models[size_t(E.model)], ".GDB")).c_str()), Newest, true);
			if (!Entry.Placed.Mesh) {
				View.Blended.Pop();
				continue;
			}
			for (TWeakObjectPtr<UProceduralMeshComponent>& Part : Entry.Placed.Parts) {
				if (Part.IsValid()) {
					Part->SetTranslucentSortPriority(1);
				}
			}
			Entry.Where = {lr::vec(E.position), lr::basisFromForwardUp(lr::normalize(lr::vec(E.forward)), lr::normalize(lr::vec(E.up)))};
			Entry.Scale = E.scale * Entry.Placed.Mesh->Scale;
			lr::SkeletonFile Skeleton;
			if (List == &Blended.animated && E.skeleton >= 0 && size_t(E.skeleton) < Blended.skeletons.size() &&
				Assets.Cache->get(AssetPath(Folder, Blended.skeletons[size_t(E.skeleton)], ".SDB"), Skeleton)) {
				Entry.Nodes = lr::nodesOf(Skeleton);
				Entry.Animation = E.animation >= 0 && size_t(E.animation) < Blended.animations.size()
					? Assets.MenuAnimation(AssetPath(Folder, Blended.animations[size_t(E.animation)], ".ADB"))
					: nullptr;
				Entry.Part = E.part;
			}
		}
	}
}

// The blended world's entities posed: plain ones where they stand, animated ones on their nodes at the scene's frame.
void FLRMenuViews::PoseBlended(FLRModels& Models, FMenuView& View, const lr::MenuScene& Scene, const FVector& At)
{
	for (FBlended& Entry : View.Blended) {
		const FTransform Whole = FrameOf(Entry.Where.rotation * Entry.Scale, Entry.Where.position) * FTransform(At);
		TArray<FTransform> Nodes;
		if (!Entry.Nodes.empty()) {
			const lr::Animation* Anim = Entry.Animation && Entry.Part >= 0 && size_t(Entry.Part) < Entry.Animation->parts.size()
				? Entry.Animation
				: nullptr;
			std::vector<lr::NodeMatrix> Posed;
			lr::poseSkeleton(Entry.Nodes, Anim, {Anim ? Entry.Part : -1, Scene.blendedFrame, true}, Entry.Where, Entry.Scale, Posed);
			FramesOf(Posed, Nodes, FTransform(At));
		}
		Models.PoseLit(Entry.Placed, Nodes, Whole);
	}
}

// What a thing is, as a key: when it changes the thing is built again.
FString FLRMenuViews::ThingKey(const lr::MenuScene::Thing& Th, const lr::Menus& Menus, bool bRing)
{
	switch (Th.kind) {
	case lr::MenuScene::Thing::Minifig:
		return FString::Printf(TEXT("M%d %d %d %d %d %d %d %d %hs %hs"), Th.seated, Th.body, Th.looks[0], Th.looks[1], Th.looks[2],
			Th.looks[3], Th.looks[4], int32(Th.parts), Th.skeleton.c_str(), Th.partFile.c_str());
	case lr::MenuScene::Thing::Entity:
		// (Built again when a colour effect starts, moves or stops on its model.)
		return FString::Printf(TEXT("E%d %d %d %hs %d %d %d %d %d %d %d"), Th.entity[0], Th.entity[1], Th.entity[2], Th.model.c_str(),
			Th.coloured, Th.colourShift[0], Th.colourShift[1], Th.colourShift[2], Th.colourOffset[0], Th.colourOffset[1],
			Th.colourOffset[2]);
	case lr::MenuScene::Thing::Part: return FString::Printf(TEXT("P%d %d %hs"), Th.colour, Th.type, Th.model.c_str());
	case lr::MenuScene::Thing::Car: {
		const lr::CarMesh& Mesh = Th.model == "cinema" ? Menus.cinemaCar : Menus.carMesh;
		return FString::Printf(TEXT("C%hs %d %d %u %d"), Th.model.c_str(), int32(Mesh.vertices.size()), Menus.screens.state.shown,
			FCrc::MemCrc32(Mesh.vertices.data(), int32(Mesh.vertices.size() * sizeof(lr::CarMesh::Vertex))), Th.lit);
	}
	case lr::MenuScene::Thing::Chassis: return FString::Printf(TEXT("H%hs"), Th.model.c_str());
	case lr::MenuScene::Thing::Piece:
		return bRing
			? FString::Printf(TEXT("R%d %d %08x %08x"), Th.type, Th.colour, Th.primary, Th.secondary)
			: FString::Printf(TEXT("K%d %d %d %d %u %d"), Th.type, Th.colour, Th.alpha, Th.lit,
				  FCrc::MemCrc32(Menus.heldMesh.vertices.data(), int32(Menus.heldMesh.vertices.size() * sizeof(lr::CarMesh::Vertex))),
				  int32(Menus.heldMesh.vertices.size()));
	case lr::MenuScene::Thing::Mark: return FString::Printf(TEXT("X%g %g %g"), Th.at[0], Th.at[1], Th.at[2]);
	default: return FString();
	}
}

// Its things, rebuilt when what they are changes, posed every frame; a ring's items lit each by its colours (ambient the
// primary, the light the secondary along the scene's light).
void FLRMenuViews::PoseMenuThings(FLRRaceAssets& Assets, FLRModels& Models, FMenuView& View, const lr::MenuScene& Scene,
	const lr::Menus& Menus, const lr::Game& Game, const lr::MenuData& Data, const FVector& At)
{
	const bool bRing = Scene.world.empty() && Scene.cinemaScreen == 0;
	View.Things.SetNum(FMath::Max(View.Things.Num(), int32(Scene.things.size())));
	for (int32 i = 0; i < View.Things.Num(); i++) {
		FMenuThing& Thing = View.Things[i];
		if (size_t(i) >= Scene.things.size()) {
			if (!Thing.Key.IsEmpty()) {
				ClearThing(Thing);
			}
			continue;
		}
		const lr::MenuScene::Thing& Th = Scene.things[size_t(i)];
		if (bRing) {
			lr::MenuLighting Item = {Th.primary | 0xff000000u,
				{{Scene.lighting.lights.empty() ? lr::normalize(lr::Vec3{-1, -1, -1}) : Scene.lighting.lights[0].first,
					Th.secondary | 0xff000000u}}};
			Models.LightMenu(Item);
		}
		const FString Key = ThingKey(Th, Menus, bRing);
		if (Key != Thing.Key) {
			ClearThing(Thing);
			Thing.Key = Key;
			BuildThing({Assets, Models, Thing, View.Actor.Get(), Th}, Scene, Menus, Game, Data);
		}
		PoseThing(Assets, Models, Thing, Scene, Th, Menus, At);
	}
}
