// The race view's power-ups (LRCoreRacePowerups.h): bricks, explosions, the actions' models and the warp tube, built and
// posed from the core.

#include "LRCoreRacePowerups.h"
#include "LRCoreRaceAssets.h"

#include "Components/SceneCaptureComponent2D.h"
#include "GameFramework/Actor.h"
#include "LRCore/Race/World.h"
#include "LRData/Cache.h"
#include "LRData/Scene.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

#include <string>

#include "LRCoreModelMesh.h"
#include "LRCoreRaceParts.h"

using namespace LRUe;

// The power-ups' models: a brick and its glow per brick, the explosions.
void FLRRacePowerups::Build(FLRRaceAssets& Assets, FLRModels& Models, const lr::World& W)
{
	AActor* Powerups = Models.NewActor(FTransform::Identity);
	PowerupActor = Powerups;
	const FString Brick = UTF8_TO_TCHAR((Common + "PUBRICKY.GDB").c_str()), Glow = UTF8_TO_TCHAR((Common + "PUTRAILY.GDB").c_str());
	const FString White = UTF8_TO_TCHAR((Common + "ENHABRIK.GDB").c_str()), WhiteGlow = UTF8_TO_TCHAR((Common + "ENHANER.GDB").c_str());
	for (const lr::Brick& B : W.powerUps.bricks) {
		Bricks.Add(Models.Place(Assets, Powerups, B.colour ? Brick : White, Assets.PowerupLibraries, true));
		BrickGlows.Add(Models.Place(Assets, Powerups, B.colour ? Glow : WhiteGlow, Assets.PowerupLibraries, false));
	}
	for (size_t e = 0; e < W.powerUps.explosions.size(); e++) {
		Explosions.Add(Models.Place(Assets, Powerups, UTF8_TO_TCHAR((Common + (e < 6 ? "HEDRA01" : "SPIKEXP") + ".GDB").c_str()),
			Assets.PowerupLibraries, true, false, 0.999f));
	}
	// The colour's material: the brick "pbrick" + P, M, S, T (red, yellow, blue, green), its streak "ptrail" + the same.
	static const char Letters[5] = {'P', 'P', 'S', 'T', 'M'};
	for (int32 m = 0; m < 2; m++) {
		for (int32 c = 0; c < 5; c++) {
			bool bFlip;
			uint8 D[4];
			BrickColours[m][c] = Assets.MaterialFor(std::string(m == 0 ? "pbrick" : "ptrail") + Letters[c], Assets.PowerupLibraries, 1,
				bFlip, D);
		}
	}
}

// The warp tube: TURBO3.WDB's "dtube" and "dbricks", away from the track, shown only to a warping
// player's view.
void FLRRacePowerups::BuildWarpTube(FLRRaceAssets& Assets, FLRModels& Models)
{
	lr::SceneFile Tubes;
	if (Assets.Cache->get(Common + "TURBO3.WDB", Tubes)) {
		const TArray<std::pair<std::string, std::string>> Libraries = LibrariesOf(Tubes, Common);
		AActor* A = Models.NewActor(FTransform::Identity);
		for (size_t e = 0; e < Tubes.animated.size() && e < 2; e++) {
			const lr::SceneFile::Entity& E = Tubes.animated[e];
			if (E.model >= 0 && size_t(E.model) < Tubes.models.size()) {
				Tube[e] = Models.Place(Assets, A, UTF8_TO_TCHAR((AssetPath(Common, Tubes.models[size_t(E.model)], ".GDB")).c_str()),
					Libraries, false);
				TubeModels[e] = Tubes.models[size_t(E.model)];
				TubePoses[e] = EntityPose(E, false);
			}
		}
	}
}

void FLRRacePowerups::Pose(FLRModels& Models, const lr::World& W)
{
	// Bricks: the four shared entities (the colour brick's cube and streak, the white brick's cube and
	// streaks), each playing its own part 0 from the race's load on the core's clock, posed by the core at each brick's
	// place and size; the colour's materials in slot 0 (P, M, S, T: red, yellow, blue, green).
	for (int32 b = 0; b < Bricks.Num() && size_t(b) < W.powerUps.bricks.size(); b++) {
		const lr::Brick& B = W.powerUps.bricks[size_t(b)];
		PoseBrick(Models, W, B, Bricks[b], 0);
		PoseBrick(Models, W, B, BrickGlows[b], 1);
	}
	PoseExplosions(W);
}

// A brick's cube (m 0) or streak (1).
void FLRRacePowerups::PoseBrick(FLRModels& Models, const lr::World& W, const lr::Brick& B, FLRPlaced& Placed, int32 m)
{
	FLRPlaced* P = &Placed;
	const float Size = B.size();
	if (!P->Mesh) {
		return;
	}
	const bool bShown = Size > 0;
	const lr::Track::BrickModel Which = B.colour ? (m == 0 ? lr::Track::BrickCube : lr::Track::BrickStreak)
												 : (m == 0 ? lr::Track::WhiteCube : lr::Track::WhiteStreaks);
	ScratchNodes.Reset();
	if (bShown) {
		W.powerUps.brickNodes(W, Which, B.position, Size, ScratchMatrices);
		FramesOf(ScratchMatrices, ScratchNodes);
	}
	for (TWeakObjectPtr<UProceduralMeshComponent>& C : P->Parts) {
		if (C.IsValid()) {
			C->SetVisibility(bShown);
		}
	}
	if (bShown) {
		Models.PoseLit(*P, ScratchNodes, FrameOf(lr::Basis{} * (Size * P->Mesh->Scale), B.position));
	}
	if (B.colour && bShown) {
		// The colour's material (Build's), put in slot 0 of the shared models' own tables (both files name ptrailM there:
		// the slot, not the name).
		UMaterialInstanceDynamic* Colour = BrickColours[m][FMath::Min<int32>(B.colour, 4)];
		static const std::string None;
		const std::string& Slot0 = P->Mesh->MaterialNames.empty() ? None : P->Mesh->MaterialNames[0];
		FLRModels::ForSections(*P, &Slot0,
			[&](UProceduralMeshComponent* C, int32 Index, const FLRModelMesh::FSection&) { C->SetMaterial(Index, Colour); });
	}
}

void FLRRacePowerups::PoseExplosions(const lr::World& W)
{
	// Explosions: scaled (1 - 0.05) g + 0.05 (at most 1), alpha (1 - g) 255.
	for (int32 e = 0; e < Explosions.Num() && size_t(e) < W.powerUps.explosions.size(); e++) {
		const lr::Explosion& X = W.powerUps.explosions[size_t(e)];
		FLRPlaced& P = Explosions[e];
		if (!P.Mesh) {
			continue;
		}
		const bool bShown = X.state == lr::Explosion::Exploding;
		const float S = FMath::Min(1.0f, (1 - 0.05f) * X.growth + 0.05f);
		for (TWeakObjectPtr<UProceduralMeshComponent>& C : P.Parts) {
			if (C.IsValid()) {
				C->SetVisibility(bShown);
				C->SetWorldTransform(FrameOf(lr::Basis{} * (S * P.Mesh->Scale), X.centre));
				for (int32 m = 0; bShown && m < C->GetNumMaterials(); m++) {
					if (UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(C->GetMaterial(m))) {
						if (Mid->GetOuter() != C) {
							Mid = C->CreateDynamicMaterialInstance(m, Mid);
						}
						Mid->SetScalarParameterValue(TEXT("Alpha"), 1 - X.growth);
					}
				}
			}
		}
	}
}

// Action's model in its role, shown this frame (made when first wanted).
FLRPlaced& FLRRacePowerups::ActionModel(FLRRaceAssets& Assets, FLRModels& Models, int32 Action, int32 Role, const std::string& Model)
{
	TArray<FActionModel>& Slot = ActionModels.FindOrAdd(uint32(Action) << 8 | uint32(Role));
	for (FActionModel& Each : Slot) {
		if (Each.Model == Model) {
			Each.ShownAt = ActionFrame;
			return Each.Placed;
		}
	}
	FActionModel& Made = Slot.AddDefaulted_GetRef();
	Made.Model = Model;
	Made.ShownAt = ActionFrame;
	Made.Placed = Models.Place(Assets, PowerupActor, UTF8_TO_TCHAR((AssetPath(Common, Model, ".GDB")).c_str()), Assets.PowerupLibraries,
		true);
	return Made.Placed;
}

// A model at a pose (a skeleton's part at a time, if it has one), with its parts' alpha; its uv offset. Shown collects the
// models shown this frame.
void FLRRacePowerups::ShowActionModel(FLRRaceAssets& Assets, FLRModels& Models, int32 Action, int32 Role, const std::string& Model,
	const lr::Pose& At, float Scale, int32 Part, float TimeMs, float Alpha, FVector2D Scroll)
{
	FLRPlaced& P = ActionModel(Assets, Models, Action, Role, Model);
	if (!P.Mesh) {
		return;
	}
	const FLRRaceAssets::FFigure* F = Assets.Figure(Model);
	TArray<FTransform> Nodes;
	if (F && !F->Nodes.empty()) {
		std::vector<lr::NodeMatrix> Out;
		const lr::Animation* A = F->bAnimated ? &F->Animation : nullptr;
		const float Frames = A && Part >= 0 && size_t(Part) < A->parts.size() ? A->parts[size_t(Part)].rate * TimeMs : 0;
		lr::poseSkeleton(F->Nodes, A, {A ? Part : -1, Frames, true}, At, Scale * P.Mesh->Scale, Out);
		FramesOf(Out, Nodes);
	}
	Models.PoseNodes(P, Nodes, FrameOf(At.rotation * (Scale * P.Mesh->Scale), At.position));
	for (TWeakObjectPtr<UProceduralMeshComponent>& C : P.Parts) {
		if (!C.IsValid()) {
			continue;
		}
		C->SetVisibility(true);
		for (int32 m = 0; m < C->GetNumMaterials(); m++) {
			UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(C->GetMaterial(m));
			if ((Alpha < 1 || !Scroll.IsZero()) && Mid && Mid->GetOuter() != C) {
				// (Its own instance, translucent when it fades.)
				UMaterialInstanceDynamic* Shared = Mid;
				Mid = UMaterialInstanceDynamic::Create(Alpha < 1 ? Assets.TranslucentBase.Get() : Shared->Parent.Get(), C.Get());
				Mid->CopyParameterOverrides(Shared);
				C->SetMaterial(m, Mid);
			}
			if (Mid && Mid->GetOuter() == C) {
				Mid->SetScalarParameterValue(TEXT("Alpha"), Alpha);
				Mid->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(float(Scroll.X), float(Scroll.Y), 0, 0));
			}
		}
	}
}

// A turbo: the burn on the car (part 0 lighting, 1 boosting, 2 fading) and two flames behind it.
void FLRRacePowerups::PoseTurbo(FLRRaceAssets& Assets, FLRModels& Models, const lr::World& W, int32 i, const lr::Action& A,
	const lr::Pose& Car, float Seconds)
{
	const int32 Level = FMath::Min<int32>(A.level, 2);
	static const char* Burns[3] = {"DT0BURN", "DT1BURN", "DT2CBURN"};
	static const char* Flames[3][2] = {{"DT0FLA1", "DT0FLA2"}, {"DT1FLA1", "DT1FLA2"}, {"DT2CFLA1", "DT2CFLA2"}};
	const int32 Part = A.state - 2;
	const float Alpha = A.state == 4 ? FMath::Max(0.0f, float(int32(A.ms)) - 350) / 350 * 191 / 255 : 1.0f;
	// The burn and both flames share one frame: the chassis car's (not
	// the leaned body's), turned a quarter about its up (rows left, back, up), 2 back and 3 up from it.
	lr::Pose Chassis;
	float ChassisScale = 1;
	const lr::Pose& C = W.raceView.member(W, size_t(A.owner), lr::RacerShape::Member(2), Chassis, ChassisScale) ? Chassis : Car;
	const lr::Basis& B = C.rotation;
	const lr::Pose At{C.position + B.f * -2 + B.u * 3, {B.s, B.f * -1, B.u}};
	// Burn and flames play one part (lit, boosting, fading) from the state's start.
	static const float Boosts[3] = {1000, 1500, 5000};
	const float Elapsed = (A.state == 2 ? 400 : A.state == 3 ? Boosts[Level] : 700) - float(A.ms);
	ShowActionModel(Assets, Models, i, 0, Burns[Level], At, 1, Part, Elapsed, Alpha, FVector2D::ZeroVector);
	const bool bFlames = A.state != 2 || A.ms < 50;
	if (bFlames) {
		ShowActionModel(Assets, Models, i, 1, Flames[Level][0], At, 1, Part, Elapsed, Alpha, FVector2D(0, 4 * Seconds));
		ShowActionModel(Assets, Models, i, 2, Flames[Level][1], At, 1, Part, Elapsed, Alpha, FVector2D(0, 2 * Seconds));
	}
}

void FLRRacePowerups::PoseActions(FLRRaceAssets& Assets, FLRModels& Models, const lr::World& W)
{
	ActionFrame++;
	const float Seconds = float(W.session.ms) / 1000;
	for (const uint8 Index : W.powerUps.actionList) {
		const lr::Action& A = W.powerUps.actions[Index];
		const int32 i = int32(Index);
		if (A.owner < 0 || size_t(A.owner) >= W.racers.visible.size()) {
			if (A.kind != lr::Action::Dynamite && A.kind != lr::Action::Missile && A.kind != lr::Action::Cannon) {
				continue;
			}
		}
		const lr::Pose* Car = A.owner >= 0 && size_t(A.owner) < W.racers.visible.size() ? &W.racers.visible[size_t(A.owner)] : nullptr;
		switch (A.kind) {
		case lr::Action::Turbo:
			if (Car && A.state >= 2 && A.state <= 4 && !(W.racers.race[size_t(A.owner)].flags & lr::RacerGhost)) {
				PoseTurbo(Assets, Models, W, i, A, *Car, Seconds);
			}
			break;
		case lr::Action::Shield: {
			if (!Car || A.state < 3 || A.state > 4 || (W.racers.race[size_t(A.owner)].flags & lr::RacerGhost)) {
				break;
			}
			const int32 Level = FMath::Clamp<int32>(A.level, 0, 3);
			const float Alpha = A.state == 4 ? float(int32(A.ms)) / 1000 * 191 / 255 : 1.0f;
			lr::Pose At{Car->position + lr::Vec3{0, 0, 2}, lr::basisFromForwardUp(Car->rotation.f, {0, 0, 1})};
			ShowActionModel(Assets, Models, i, 0, "SHEOUT" + std::to_string(Level), At, 1, 0, Seconds * 1000, Alpha,
				FVector2D(0, 4 * Seconds));
			ShowActionModel(Assets, Models, i, 1, "SHEIN" + std::to_string(Level), At, 1, 0, Seconds * 1000, Alpha,
				FVector2D(0, 4 * Seconds));
			break;
		}
		case lr::Action::Dynamite: {
			if (A.shot.state != lr::Projectile::Flying && A.state != 2) {
				break;
			}
			const float Angle = A.phase;
			const float Cs = FMath::Cos(Angle), Sn = FMath::Sin(Angle);
			lr::Pose At{A.state == 2 && Car ? Car->position + lr::Vec3{0, 0, 5} : A.shot.at, {{Cs, 0, -Sn}, {0, 1, 0}, {Sn, 0, Cs}}};
			ShowActionModel(Assets, Models, i, 0, "BARREL", At, 1, -1, 0, 1, FVector2D::ZeroVector);
			break;
		}
		case lr::Action::Hook:
			if (A.state >= 3 && A.state <= 5) {
				ShowActionModel(Assets, Models, i, 0, "GRAPPLE", W.powerUps.grapple, 1, -1, 0, 1, FVector2D::ZeroVector);
			}
			break;
		case lr::Action::Missile:
			if (A.state == 2 && Car) {
				ShowActionModel(Assets, Models, i, 0, "DMISSILE", A.ride, 1, 0, A.riseFrames / 0.03f, 1, FVector2D::ZeroVector);
			}
			else if (A.shot.state == lr::Projectile::Flying) {
				const lr::Vec3 F = lr::isZero(A.shot.velocity) ? lr::Vec3{1, 0, 0} : lr::normalize(A.shot.velocity);
				ShowActionModel(Assets, Models, i, 0, "DMISSILE", {A.shot.at, lr::basisFromForwardUp(F, {0, 0, 1})}, 1, -1, 0, 1,
					FVector2D::ZeroVector);
			}
			break;
		default: break;
		}
	}
	for (auto& [Slot, InSlot] : ActionModels) {
		for (const FActionModel& Each : InSlot) {
			if (Each.ShownAt != ActionFrame) {
				SetVisible(Each.Placed.Parts, false);
			}
		}
	}
}

// Warping: only the tube, its parts at 1500 - the ms left, the camera snapped behind a car at the origin facing
// (0, -1, 0) (the chase view's pose; FOV 45 cos(pi ms left / 1500) + base).
void FLRRacePowerups::PoseWarpView(FLRRaceAssets& Assets, FLRModels& Models, USceneCaptureComponent2D* Capture, const lr::World& W,
	const lr::PlayerCamera& Cam, lr::Vec3& EyeAt, lr::Vec3& Look, lr::Vec3& Down, float& Fov)
{
	int32 Left = 0;
	for (const uint8 Index : W.powerUps.actionList) {
		const lr::Action& A = W.powerUps.actions[Index];
		if (A.kind == lr::Action::Warp && A.owner == Cam.racer) {
			Left = int32(A.ms);
		}
	}
	for (int32 e = 0; e < 2; e++) {
		FLRPlaced& P = Tube[e];
		if (!P.Mesh) {
			continue;
		}
		const FLRRaceAssets::FFigure* F = Assets.Figure(TubeModels[e]);
		TArray<FTransform> Nodes;
		if (F && !F->Nodes.empty()) {
			std::vector<lr::NodeMatrix> Out;
			const lr::Animation* An = F->bAnimated && !F->Animation.parts.empty() ? &F->Animation : nullptr;
			const float Frames = An ? An->parts[0].rate * float(1500 - Left) : 0;
			lr::poseSkeleton(F->Nodes, An, {An ? 0 : -1, Frames, true}, TubePoses[e], P.Mesh->Scale, Out);
			FramesOf(Out, Nodes);
		}
		Models.PoseNodes(P, Nodes, FrameOf(TubePoses[e].rotation * P.Mesh->Scale, TubePoses[e].position));
		for (TWeakObjectPtr<UProceduralMeshComponent>& Part : P.Parts) {
			if (Part.IsValid()) {
				Capture->ShowOnlyComponents.Add(Part);
			}
		}
	}
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	const float SinP = FMath::Sin(FMath::DegreesToRadians(5.0f)), CosP = FMath::Cos(FMath::DegreesToRadians(5.0f));
	const float SinH = FMath::Sin(FMath::DegreesToRadians(35.0f));
	EyeAt = {0, CosP * 20, (SinP + SinH) * 20};
	Look = {0, -CosP, -SinP};
	Down = lr::normalize(lr::Vec3{0, 0, -1} - Look * lr::dot(lr::Vec3{0, 0, -1}, Look));
	Fov = 45 * FMath::Cos(PI * float(Left) / 1500) + Cam.fov;
}

void FLRRacePowerups::HideTube(USceneCaptureComponent2D* Capture) const
{
	for (const FLRPlaced& P : Tube) {
		HideParts(Capture, P.Parts);
	}
}

void FLRRacePowerups::HideBricks(USceneCaptureComponent2D* Capture, const lr::World& W, const lr::PlayerCamera& Cam) const
{
	// The bricks' shared entities' draw distance (400 ft from this camera's eye).
	for (int32 b = 0; b < Bricks.Num() && size_t(b) < W.powerUps.bricks.size(); b++) {
		const lr::Brick& B = W.powerUps.bricks[size_t(b)];
		const lr::Track::BrickModel Which = B.colour ? lr::Track::BrickCube : lr::Track::WhiteCube;
		if (!lr::World::withinDistance(W.track.brickModels[Which].distances, lr::lengthSquared(B.position - Cam.shownEye()))) {
			HideParts(Capture, Bricks[b].Parts);
			HideParts(Capture, BrickGlows[b].Parts);
		}
	}
}

void FLRRacePowerups::Clear()
{
	Bricks.Reset();
	BrickGlows.Reset();
	Explosions.Reset();
	ActionModels.Reset();
	PowerupActor = nullptr;
}
