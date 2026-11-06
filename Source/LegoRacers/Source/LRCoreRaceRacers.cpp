// The race view's racers (LRCoreRaceRacers.h): built from the race's slots, posed from the core, their shadows drawn.

#include "LRCoreRaceRacers.h"
#include "LRCoreRaceAssets.h"
#include "LRCoreLog.h"

#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "LRCore/Kart/Car.h"
#include "LRCore/Menus/Menus.h"
#include "LRCore/Race/World.h"
#include "LRData/Cache.h"
#include "LRData/Cars.h"
#include "LRData/Look.h"
#include "LRData/Model.h"
#include "LRData/Pieces.h"
#include "ProceduralMeshComponent.h"
#include "UObject/UObjectGlobals.h"

#include "LRCoreModelMesh.h"
#include "LRCoreRaceParts.h"

using namespace LRUe;

namespace
{
// The tables a racer's members are found in (COMMON's).
struct FMemberTables
{
	lr::ChassisTable Chassis;
	lr::ChampionTable Champions;
	lr::DriverTable Drivers;
};

// A slot's members placed on Actor at Alpha: a champion's body, the chassis' second variant (a ghost, translucent, has
// none) and car, the driver (DRIVERS.DDB's "BB" for a slot naming none with no built driver); its chassis (a champion's
// own).
const lr::ChassisTable::Chassis* PlaceMembers(FLRRaceAssets& Assets, FLRModels& Models, const FMemberTables& Tables, AActor* Actor,
	const lr::RaceBrief::Slot& Slot, bool bBuiltDriver, float Alpha, FLRPlaced (&Members)[4])
{
	const lr::DriverTable::Driver* Driver = Slot.driver.empty() ? (bBuiltDriver ? nullptr : Tables.Drivers.find("BB"))
																: Tables.Drivers.find(Slot.driver);
	const lr::ChampionTable::Champion* Champion = !Slot.driver.empty() && Driver ? Tables.Champions.find(Driver->champion) : nullptr;
	const lr::ChassisTable::Chassis* Chassis = Tables.Chassis.find(Champion ? Champion->chassis : Slot.chassis);
	const auto Place = [&](int32 m, const std::string& Model, const std::string& Mdb, const std::string& Tdb) {
		Members[m] = Models.Place(Assets, Actor, ModelPath(*Assets.Cache, Model, Common), Libs(Mdb, Tdb), true, false, Alpha);
	};
	if (Champion) {
		Place(0, Champion->model, Champion->materials, Champion->textures);
	}
	if (Chassis && Alpha >= 1 && !Chassis->second.model.empty()) {
		Place(1, Chassis->second.model, Chassis->second.materials, Chassis->second.textures);
	}
	if (Chassis) {
		Place(2, Chassis->model, Chassis->materials, Chassis->textures);
	}
	if (Driver) {
		Place(3, Driver->model, Driver->materials, Driver->textures);
	}
	return Chassis;
}
} // namespace

void FLRRaceRacers::Build(FLRRaceAssets& Assets, FLRModels& Models, const lr::World& W, const lr::MenuData& Data)
{
	// The racers: the body (a champion's model, lit each frame; or the built car, lit once), the chassis' second variant,
	// the chassis car and the driver, each on its skeleton.
	FMemberTables Tables;
	WarnUnless(Assets.Cache->get(Common + "CHASSIS.CMB", Tables.Chassis), "CHASSIS.CMB");
	WarnUnless(Assets.Cache->get(Common + "CHAMPS.CCB", Tables.Champions), "CHAMPS.CCB");
	WarnUnless(Assets.Cache->get(Common + "DRIVERS.DDB", Tables.Drivers), "DRIVERS.DDB");
	lr::PieceLibrary Low;
	WarnUnless(Assets.Cache->get("MENUDATA/PIECEDB/LPIECELO.LEB", Low), "LPIECELO.LEB");
	Racers.SetNum(int32(W.racers.karts.size()));
	for (size_t i = 0; i < W.racers.karts.size() && i < W.session.brief.slots.size(); i++) {
		const lr::RaceBrief::Slot& Slot = W.session.brief.slots[i];
		FRacer& R = Racers[int32(i)];
		AActor* Kart = Models.NewActor(FTransform::Identity);
		// (A player's slot races its record's built minifig; DRIVERS.DDB's "bb" only if none is built.)
		if (Slot.built()) {
			BuildRaceDriver(Assets, Models, Kart, Slot.looks, Data, 1, R.Driver, R.Members[3]);
		}
		const lr::ChassisTable::Chassis* Chassis = PlaceMembers(Assets, Models, Tables, Kart, Slot, !R.Driver.IsEmpty(), 1, R.Members);
		if (!R.Members[0].Actor.IsValid() && !Slot.car.empty()) { // (no champion: the slot's built car)
			R.Built.Add(BuiltCar(Assets, Kart, Slot.car, Low, Data));
			R.Members[0].Actor = Kart;
		}
		// A racer's shadow's size: its chassis' (a champion's own), else 8 by 9 ft.
		const FVector2f Size(Chassis && Chassis->shadowWidth > 0 ? Chassis->shadowWidth : 8,
			Chassis && Chassis->shadowLength > 0 ? Chassis->shadowLength : 9);
		MakeShadow(int32(i), Size, W);
	}
	// A time race's ghosts: player 0's members again, translucent.
	if (W.session.brief.mode == lr::RaceBrief::Mode::TimeRace && !Racers.IsEmpty()) {
		for (int32 g = 0; g < 2; g++) {
			AActor* A = Models.NewActor(FTransform::Identity);
			const lr::RaceBrief::Slot& Slot = W.session.brief.slots[0];
			if (Slot.built()) {
				BuildRaceDriver(Assets, Models, A, Slot.looks, Data, 127.0f / 255, GhostDrivers[g], Ghosts[g][3]);
			}
			PlaceMembers(Assets, Models, Tables, A, Slot, !GhostDrivers[g].IsEmpty(), 127.0f / 255, Ghosts[g]);
			A->SetActorHiddenInGame(true);
		}
	}
}

// The built car: the low pieces, lit once by ambient 0x80 and a white light along (-1, 0, -1).
UProceduralMeshComponent* FLRRaceRacers::BuiltCar(FLRRaceAssets& Assets, AActor* Kart, const std::vector<lr::CarPiece>& Pieces,
	const lr::PieceLibrary& Low, const lr::MenuData& Data)
{
	lr::Car Car;
	Car.load(Low, Pieces);
	Car.chooseOffset(Low);
	const lr::CarMesh Mesh = Car.mesh(Low, Data.sets.materials, {false, true, true, true}, Car.entries.size());
	lr::MaterialLibrary Mats;
	WarnUnless(Assets.Cache->get("MENUDATA/PIECEDB/LPIECELO.MDB", Mats), "LPIECELO.MDB");
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
		const std::string Name = T.material >= 0 && size_t(T.material) < Low.materials.size() ? Low.materials[size_t(T.material)]
																							  : std::string();
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
			Colours[*S].Add(FColor(LitChannel(0x80, D[0], 255, NdotL, Amb[0]), LitChannel(0x80, D[1], 255, NdotL, Amb[1]),
				LitChannel(0x80, D[2], 255, NdotL, Amb[2]), 255));
		}
	}
	const TArray<std::pair<std::string, std::string>> PieceLibs = Library("MENUDATA/PIECEDB/LPIECELO");
	for (int32 S = 0; S < Positions.Num(); S++) {
		C->CreateMeshSection(S, Positions[S], Triangles[S], Normals[S], Uvs[S], Colours[S], {}, false);
		bool bFlip;
		uint8 D[4];
		const std::string Name = MaterialOf[S] >= 0 && size_t(MaterialOf[S]) < Low.materials.size() ? Low.materials[size_t(MaterialOf[S])]
																									: std::string();
		C->SetMaterial(S, Assets.MaterialFor(Name, PieceLibs, 1, bFlip, D));
	}
	C->SetCastShadow(false);
	C->SetReceivesDecals(false);
	return C;
}

// ---- A player's built driver ----

// The pelvis skeleton's node for each of GAMEPART's leg box nodes (leg_box, chest, arm_l, arm_r, face): decided from the
// files, LEG_BOX.SDB's first four nodes stand where PELVIS.SDB's alpelvis, torso, arm_l, arm_r do; its face (the head's
// place) within 0.06 ft of PELVIS's head.
static constexpr int32 LegBoxOnPelvis[5] = {0, 1, 2, 3, 5};

void FLRRaceRacers::BuildRaceDriver(FLRRaceAssets& Assets, FLRModels& Models, AActor* Actor, const std::array<uint8_t, 4>& Looks,
	const lr::MenuData& Data, float Alpha, TArray<FLRSkin>& Body, FLRPlaced& Head)
{
	const lr::PartCatalog& Cat = Data.parts;
	const uint8 Face = Looks[0], Hat = Looks[1], Legs = Looks[2], Torso = Looks[3];
	// The pictures: the face's dflt (the race always shows it), the torso's, the legs' (flipped as the menus' are).
	TMap<FString, UMaterialInterface*> Overrides;
	Overrides.Add(TEXT("face"), Assets.Picture(PartName(Cat.rings[1], Face) + "DFLT", true, Alpha));
	Overrides.Add(TEXT("torso"), Assets.Picture(PartName(Cat.rings[2], Torso), true, Alpha));
	Overrides.Add(TEXT("legs"), Assets.Picture(PartName(Cat.rings[3], Legs), true, Alpha));
	const TArray<std::pair<std::string, std::string>> BodyParts = Library("MENUDATA/PARTDB/BODYPART");
	lr::Model M;
	if (!Assets.Cache->get("MENUDATA/PARTDB/GAMEPART/LEG_BOX.GDB", M)) {
		return;
	}
	FLRSkin& S = Body.Add_GetRef(Models.BuildSkin(Assets, Actor, M, BodyParts, &Overrides, true, Alpha));
	// Lit once: ambient 0x80 and a white light along (-1, 0, -1), on the model's own normals.
	const lr::Vec3 Light = lr::normalize({-1, 0, -1});
	for (const FLRSkin::FSection& Sec : S.Sections) {
		TArray<FColor>& Out = S.Prelit.AddDefaulted_GetRef();
		for (const int32 V : Sec.Vertices) {
			const float NdotL = M.normals.empty()
				? 0
				: lr::dot(lr::normalize({M.normals[size_t(V) * 3], M.normals[size_t(V) * 3 + 1], M.normals[size_t(V) * 3 + 2]}), Light);
			Out.Add(FColor(LitChannel(0x80, Sec.Diffuse.R, 255, NdotL, Sec.Ambient.R),
				LitChannel(0x80, Sec.Diffuse.G, 255, NdotL, Sec.Ambient.G), LitChannel(0x80, Sec.Diffuse.B, 255, NdotL, Sec.Ambient.B),
				255));
		}
	}
	// The head with the hat, from ICB_CHAR.GCB (body set 1's), riding the face node.
	const std::string HatModel = size_t(Hat) < Cat.hatModels.size() ? Cat.hatModels[Hat] : std::string("head");
	if (const lr::HeadLibrary::Head* H = Data.heads[1].find(HatModel)) {
		Head = Models.PlaceMesh(Assets, Actor, Assets.ModelOf(FString::Printf(TEXT("head#1#%hs"), HatModel.c_str()), &H->model, false),
			BodyParts, true, Alpha, &Overrides);
	}
}

void FLRRaceRacers::PoseRaceDriver(FLRModels& Models, TArray<FLRSkin>& Body, FLRPlaced& Head,
	const std::vector<lr::NodeMatrix>& PelvisNodes)
{
	if (PelvisNodes.size() < 6) {
		return;
	}
	std::vector<lr::NodeMatrix> Nodes;
	for (const int32 n : LegBoxOnPelvis) {
		Nodes.push_back(PelvisNodes[size_t(n)]);
	}
	for (FLRSkin& S : Body) {
		Models.PoseSkin(S, Nodes, FVector::ZeroVector);
	}
	Models.PoseOn(Head, FrameOf(Nodes[4].rows, Nodes[4].position));
}

void FLRRaceRacers::Pose(FLRModels& Models, const lr::World& W)
{
	for (int32 i = 0; i < Racers.Num(); i++) {
		for (int32 m = 0; m < 4; m++) {
			PoseMember(Models, i, m, W);
		}
	}
	PoseGhosts(Models, W);
}

// Racer i's member m (0 the body, 1 the chassis' second variant, 2 the chassis car, 3 the driver) at the core's pose.
void FLRRaceRacers::PoseMember(FLRModels& Models, int32 i, int32 m, const lr::World& W)
{
	FRacer& R = Racers[i];
	const bool bHidden = W.racers.race[size_t(i)].flags & lr::RacerGhost; // (warping: hidden)
	const lr::RacerShape& Shape = W.raceView.shapes[size_t(i)];
	const lr::RacerLook& Look = W.raceView.looks[size_t(i)];
	FLRPlaced& P = R.Members[m];
	lr::Pose Pose;
	float Scale = 1;
	const bool bPresent = W.raceView.member(W, size_t(i), lr::RacerShape::Member(m), Pose, Scale) && !bHidden;
	SetVisible(P.Parts, bPresent);
	if (m == 0) {
		SetVisible(R.Built, bPresent);
	}
	if (m == 3) {
		for (const FLRSkin& S : R.Driver) {
			SetVisible({S.Component}, bPresent);
		}
	}
	if (!bPresent) {
		return;
	}
	const int16 Figure = m == 1 ? Shape.variantAnimation : m == 2 ? Shape.carAnimation : m == 3 ? 0 : -1;
	const std::vector<lr::Pose>& Locals = m == 1 ? Look.variantNodes : m == 2 ? Look.carNodes : Look.figureNodes;
	TArray<FTransform> Nodes;
	if (Figure >= 0 && size_t(Figure) < W.raceView.skeletons.size() && !Locals.empty()) {
		std::vector<lr::NodeMatrix> Out;
		lr::placeNodes(W.raceView.skeletons[size_t(Figure)], Locals, Pose, Scale, Out);
		if (m == 3 && !R.Driver.IsEmpty()) {
			PoseRaceDriver(Models, R.Driver, P, Out);
			return;
		}
		FramesOf(Out, Nodes);
	}
	// A body without a skeleton: the model's scale too (the built car is drawn at 1: the core's bounds).
	const float Whole = m == 0 && P.Mesh ? Scale * P.Mesh->Scale : Scale;
	const FTransform T = FrameOf(Pose.rotation * Whole, Pose.position);
	Models.PoseLit(P, Nodes, T);
	if (m == 0) {
		for (TWeakObjectPtr<UProceduralMeshComponent>& C : R.Built) {
			if (C.IsValid()) {
				C->SetWorldTransform(FrameOf(Pose.rotation * Scale, Pose.position));
			}
		}
	}
}

// A time race's ghosts at their runs' poses: the car, its body on it, the driver at its mount.
void FLRRaceRacers::PoseGhosts(FLRModels& Models, const lr::World& W)
{
	for (int32 g = 0; g < 2; g++) {
		const lr::GhostRun& Run = g == 0 ? W.session.timeRace.record : W.session.timeRace.best;
		const bool bValid = g == 0 ? W.session.timeRace.recordValid : W.session.timeRace.bestValid;
		lr::Vec3 At;
		lr::Quat Q;
		const bool bShown = bValid && !Run.finished && W.session.ghostAt(Run, At, Q);
		for (int32 m = 0; m < 4; m++) {
			FLRPlaced& P = Ghosts[g][m];
			if (!P.Mesh) {
				continue;
			}
			SetVisible(P.Parts, bShown);
			if (m == 3) {
				for (const FLRSkin& S : GhostDrivers[g]) {
					SetVisible({S.Component}, bShown);
				}
			}
			if (P.Actor.IsValid()) {
				P.Actor->SetActorHiddenInGame(!bShown);
			}
			if (!bShown || Racers.IsEmpty()) {
				continue;
			}
			const lr::Basis B = lr::basisOf(Q);
			const lr::RacerShape& Shape = W.raceView.shapes[0];
			lr::Pose Pose{At, B};
			float Scale = m == 2 ? Shape.carScale : m == 3 ? Shape.driverScale : P.Mesh->Scale;
			if (m == 3) {
				Pose.position = lr::toWorld(B, Shape.mount) + At;
			}
			TArray<FTransform> Nodes;
			const int16 Figure = m == 2 ? Shape.carAnimation : m == 3 ? 0 : -1;
			if (Figure >= 0 && size_t(Figure) < W.raceView.skeletons.size() && size_t(Figure) < W.raceView.figures.size()) {
				std::vector<lr::NodeMatrix> Out;
				lr::poseSkeleton(W.raceView.skeletons[size_t(Figure)], &W.raceView.figures[size_t(Figure)], {0, 0, true}, Pose, Scale, Out);
				if (m == 3 && !GhostDrivers[g].IsEmpty()) {
					PoseRaceDriver(Models, GhostDrivers[g], P, Out);
					continue;
				}
				FramesOf(Out, Nodes);
			}
			Models.PoseNodes(P, Nodes, FrameOf(B * Scale, At));
		}
	}
}

namespace
{
// A car's shadow drawn into Size x Size BGRA: dark where any triangle covers a pixel's centre. Points are UE's local cm
// in the car's frame; the decal's u runs across (left), v along (forward): Show::project.
struct FShadowRaster
{
	static constexpr int32 Size = 64;
	float HalfW, HalfL;
	TArray<uint8> Pixels;

	FShadowRaster(float Width, float Length)
		: HalfW(Width * 1.15f * 0.5f)
		, HalfL(Length * 1.15f * 0.5f)
	{
		Pixels.Init(0, Size * Size * 4);
	}
	void Triangle(const FVector& A, const FVector& B, const FVector& C)
	{
		FVector2D P[3];
		const FVector* V[3] = {&A, &B, &C};
		for (int32 k = 0; k < 3; k++) {
			const float Fwd = float(V[k]->X) / Cm, Left = -float(V[k]->Y) / Cm; // (UE's y right = -left)
			P[k] = FVector2D((Left / (2 * HalfW) + 0.5f) * Size, (Fwd / (2 * HalfL) + 0.5f) * Size);
		}
		const float MinX = FMath::Max(0.0f, float(FMath::Min3(P[0].X, P[1].X, P[2].X))),
					MaxX = FMath::Min(float(Size - 1), float(FMath::Max3(P[0].X, P[1].X, P[2].X)));
		const float MinY = FMath::Max(0.0f, float(FMath::Min3(P[0].Y, P[1].Y, P[2].Y))),
					MaxY = FMath::Min(float(Size - 1), float(FMath::Max3(P[0].Y, P[1].Y, P[2].Y)));
		const float Area = float((P[1] - P[0]) ^ (P[2] - P[0]));
		if (FMath::Abs(Area) < 1e-6f) {
			return;
		}
		for (int32 Y = int32(MinY); Y <= int32(MaxY); Y++) {
			for (int32 X = int32(MinX); X <= int32(MaxX); X++) {
				const FVector2D Q(X + 0.5f, Y + 0.5f);
				const float W0 = float((P[1] - Q) ^ (P[2] - Q)) / Area, W1 = float((P[2] - Q) ^ (P[0] - Q)) / Area;
				if (W0 >= 0 && W1 >= 0 && W0 + W1 <= 1) {
					uint8* Px = &Pixels[(Y * Size + X) * 4];
					Px[0] = Px[1] = Px[2] = 8;
					Px[3] = 255;
				}
			}
		}
	}
};
} // namespace

// A racer's shadow texture: every member's triangles in the car's frame at rest (the body at the car, the driver at its
// mount), point filtered (carshadN).
void FLRRaceRacers::MakeShadow(int32 Racer, FVector2f Size, const lr::World& W)
{
	FRacer& R = Racers[Racer];
	FShadowRaster Raster(Size.X, Size.Y);
	for (int32 m = 0; m < 4; m++) {
		const FLRPlaced& P = R.Members[m];
		if (!P.Mesh) {
			continue;
		}
		const float S = P.Mesh->Scale;
		const FVector Offset = m == 3 ? Ue(W.raceView.shapes[size_t(Racer)].mount) : FVector::ZeroVector;
		for (const FLRModelMesh::FSection& Sec : P.Mesh->Sections) {
			for (int32 t = 0; t + 2 < Sec.Triangles.Num(); t += 3) {
				Raster.Triangle(Sec.Positions[Sec.Triangles[t]] * S + Offset, Sec.Positions[Sec.Triangles[t + 1]] * S + Offset,
					Sec.Positions[Sec.Triangles[t + 2]] * S + Offset);
			}
		}
	}
	for (const TWeakObjectPtr<UProceduralMeshComponent>& C : R.Built) {
		for (int32 s = 0; C.IsValid() && s < C->GetNumSections(); s++) {
			const FProcMeshSection* Sec = C->GetProcMeshSection(s);
			for (int32 t = 0; Sec && t + 2 < Sec->ProcIndexBuffer.Num(); t += 3) {
				Raster.Triangle(Sec->ProcVertexBuffer[Sec->ProcIndexBuffer[t]].Position,
					Sec->ProcVertexBuffer[Sec->ProcIndexBuffer[t + 1]].Position,
					Sec->ProcVertexBuffer[Sec->ProcIndexBuffer[t + 2]].Position);
			}
		}
	}
	UTexture2D* T = NewTexture(FShadowRaster::Size, FShadowRaster::Size, TF_Nearest, TA_Clamp,
		[&](uint8* Mip) { FMemory::Memcpy(Mip, Raster.Pixels.GetData(), Raster.Pixels.Num()); });
	Shadows.Add(T);
	R.Shadow = T;
}

UTexture2D* FLRRaceRacers::ShadowOf(int32 i) const { return Racers.IsValidIndex(i) ? Racers[i].Shadow.Get() : nullptr; }

void FLRRaceRacers::Clear()
{
	Racers.Reset();
	for (auto& G : Ghosts) {
		for (FLRPlaced& P : G) {
			P = {};
		}
	}
	for (TArray<FLRSkin>& Driver : GhostDrivers) {
		Driver.Reset(); // (a race's own: the next time race's would add to them)
	}
	Shadows.Reset();
}

void FLRRaceRacers::AddReferencedObjects(FReferenceCollector& Collector) { Collector.AddReferencedObjects(Shadows); }
