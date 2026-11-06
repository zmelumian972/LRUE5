// The race view's scenery (LRCoreRaceScenery.h): the track's worlds built, posed from the core, animated and hidden per
// camera.

#include "LRCoreRaceScenery.h"
#include "LRCoreRaceAssets.h"

#include "Components/SceneCaptureComponent2D.h"
#include "GameFramework/Actor.h"
#include "LRCore/Race/World.h"
#include "LRData/Cache.h"
#include "LRData/Race.h"
#include "LRData/Scene.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

#include "LRCoreModelMesh.h"
#include "LRCoreRaceParts.h"

using namespace LRUe;

void FLRRaceScenery::Build(FLRRaceAssets& Assets, FLRModels& Models, const lr::RaceFile& Race, const std::string& Dir, const lr::World& W)
{
	bMirrored = W.track.mirrored;
	// The track's worlds: plain entities where they stand (a model no entity places at the origin), animated ones posed
	// each frame from the core.
	AActor* Scenery = Models.NewActor(FTransform::Identity);
	for (const std::string* Name : {&Race.scene, &Race.sharedScene}) {
		lr::SceneFile Scene;
		const std::string Path = Dir + lr::binaryName(*Name);
		if (Name->empty() || !Assets.Cache->size(Path) || !Assets.Cache->get(Path, Scene)) {
			continue; // (as the core: an empty shared database)
		}
		const std::string Folder = FolderOf(Path);
		// (A database naming no library finds its materials in those loaded before it, newest first)
		TArray<std::pair<std::string, std::string>> Libraries = LibrariesOf(Scene, Folder);
		if (Libraries.IsEmpty()) {
			for (const auto* Loaded : {&Assets.TrackLibraries, &Assets.PowerupLibraries, &Assets.CommonLibraries}) {
				for (int32 i = Loaded->Num(); i-- > 0;) {
					Libraries.Add((*Loaded)[i]);
				}
			}
		}
		if (Name == &Race.scene) {
			Assets.TrackLibraries = Libraries;
		}
		TArray<bool> Placed;
		Placed.Init(false, int32(Scene.models.size()));
		for (const lr::SceneFile::Entity& E : Scene.animated) {
			if (E.model >= 0 && size_t(E.model) < Scene.models.size()) {
				Placed[E.model] = true;
			}
		}
		const auto Add = [&](size_t Model, const lr::Pose& Pose) {
			FLRPlaced P = Models.Place(Assets, Scenery, ModelPath(*Assets.Cache, Scene.models[Model], Folder), Libraries, false, bMirrored);
			if (!P.Mesh) {
				return -1;
			}
			const FTransform T = FrameOf(Pose.rotation * P.Mesh->Scale, Pose.position);
			for (TWeakObjectPtr<UProceduralMeshComponent>& C : P.Parts) {
				if (C.IsValid()) {
					C->SetWorldTransform(T);
				}
			}
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
		FLRPlaced P;
		if (!A.model.empty()) {
			P = Models.Place(Assets, Scenery, ModelPath(*Assets.Cache, A.model, Dir),
				Assets.TrackLibraries.Num() ? Assets.TrackLibraries : Assets.CommonLibraries, false, bMirrored);
		}
		Animated.Add(P);
	}
}

void FLRRaceScenery::Pose(FLRRaceAssets& Assets, FLRModels& Models, const lr::World& W)
{
	PoseAnimated(Models, W);
	ApplyDrawDistances(W);
	AnimateMaterials(Assets, W);
}

// The animated scenery on its clocks (the core's).
void FLRRaceScenery::PoseAnimated(FLRModels& Models, const lr::World& W)
{
	for (int32 k = 0; k < Animated.Num() && size_t(k) < W.track.animated.size(); k++) {
		FLRPlaced& P = Animated[k];
		if (!P.Mesh) {
			continue;
		}
		const lr::AnimatedModel& A = W.track.animated[size_t(k)];
		const lr::Clock& Clock = W.scenery.clocks[size_t(k)];
		ScratchNodes.Reset();
		if (!A.nodes.empty()) {
			const lr::Animation* Anim = W.track.animationOf(A);
			lr::poseSkeleton(A.nodes, Anim, {Clock.part, Clock.time, Clock.loop}, A.pose, A.scale, ScratchMatrices);
			FramesOf(ScratchMatrices, ScratchNodes);
		}
		Models.PoseNodes(P, ScratchNodes, FrameOf(A.pose.rotation * A.scale, A.pose.position));
	}
}

void FLRRaceScenery::ApplyDrawDistances(const lr::World& W)
{
	// The plain models' draw distances as the core has them now: a model the core
	// hides (-1) is drawn by no camera (the rocket's two looks, C0R3's laser columns, C2R3's beamdumy); the others are
	// shown or hidden per camera by the core's rule as each view is captured (CaptureViews: World::plainInReach), never by
	// UE's cull distance, which the view distance scale shortens at lower qualities (the rocket from 400 ft at 0.8).
	if (PlainDistances.Num() != int32(W.scenery.modelDistances.size())) {
		PlainDistances.Init(-2, int32(W.scenery.modelDistances.size())); // (none applied yet)
	}
	for (int32 m = 0; m < PlainDistances.Num() && m < PlainOfEntity.Num(); m++) {
		const std::array<float, 3>& D = W.scenery.modelDistances[size_t(m)];
		const float Far = FMath::Max3(D[0], D[1], D[2]);
		if (PlainDistances[m] == Far || !Plain.IsValidIndex(PlainOfEntity[m])) {
			continue;
		}
		PlainDistances[m] = Far;
		for (TWeakObjectPtr<UProceduralMeshComponent>& C : Plain[PlainOfEntity[m]].Parts) {
			if (C.IsValid()) {
				C->SetVisibility(Far > 0);
				C->SetCachedMaxDrawDistance(0);
				C->LDMaxDrawDistance = 0;
			}
		}
	}
}

// A scenery entity's placed model: animated (by Track::animated) or plain (by entity); none if it has none.
FLRPlaced* FLRRaceScenery::SceneryOf(bool bAnimated, int32 Entity)
{
	if (bAnimated) {
		return Animated.IsValidIndex(Entity) ? &Animated[Entity] : nullptr;
	}
	return PlainOfEntity.IsValidIndex(Entity) && Plain.IsValidIndex(PlainOfEntity[Entity]) ? &Plain[PlainOfEntity[Entity]] : nullptr;
}

void FLRRaceScenery::AnimateMaterials(FLRRaceAssets& Assets, const lr::World& W)
{
	// Material animation: a binding's shown frame swaps its slot's material (by texture, on the entity's own instances);
	// texture scroll: the entity's uv offset.
	if (ShownFrames.Num() != int32(W.track.materialBindings.size())) {
		ShownFrames.Init(0, int32(W.track.materialBindings.size()));
	}
	for (size_t b = 0; b < W.track.materialBindings.size(); b++) {
		const lr::Track::MaterialBinding& Bind = W.track.materialBindings[b];
		// (A binding shows its track's frame, the track's cursor; the code display's are re-rated and
		// stopped by the core)
		const int16 Frame = size_t(Bind.track) < W.raceView.materials.size() ? W.raceView.materials[size_t(Bind.track)].shown : int16(-1);
		if (ShownFrames[int32(b)] == Frame + 1) {
			continue;
		}
		ShownFrames[int32(b)] = Frame + 1;
		FLRPlaced* P = SceneryOf(Bind.animated, Bind.entity);
		const lr::Track::MaterialTrack* T = size_t(Bind.track) < W.track.materialTracks.size() ? &W.track.materialTracks[size_t(Bind.track)]
																							   : nullptr;
		if (!P || !P->Mesh || !T || Bind.slot < 0 || size_t(Bind.slot) >= P->Mesh->MaterialNames.size() || Frame < 0 ||
			size_t(Frame) >= T->frames.size()) {
			continue;
		}
		const std::string& Slot = P->Mesh->MaterialNames[size_t(Bind.slot)];
		bool bFlip;
		uint8 D[4];
		UMaterialInstanceDynamic* Shown = Assets.MaterialFor(T->frames[size_t(Frame)].first, Assets.TrackLibraries, 1, bFlip, D);
		FLRModels::ForSections(*P, &Slot,
			[&](UProceduralMeshComponent* C, int32 Index, const FLRModelMesh::FSection&) { C->SetMaterial(Index, Shown); });
	}
	for (size_t s = 0; s < W.raceView.scrollOffsets.size() && s < W.track.scrolls.size(); s++) {
		const lr::Track::Scroll& Sc = W.track.scrolls[s];
		FLRPlaced* P = SceneryOf(Sc.animated, Sc.entity);
		if (!P || !P->Mesh) {
			continue;
		}
		const FLinearColor Offset(float(W.raceView.scrollOffsets[s][0]) / 65536.0f, float(W.raceView.scrollOffsets[s][1]) / 65536.0f, 0, 0);
		FLRModels::ForSections(*P, nullptr, [&](UProceduralMeshComponent* C, int32 Index, const FLRModelMesh::FSection&) {
			UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(C->GetMaterial(Index));
			if (!Mid || Mid->GetOuter() != C) {
				Mid = C->CreateDynamicMaterialInstance(Index, C->GetMaterial(Index));
			}
			if (Mid) {
				Mid->SetVectorParameterValue(TEXT("UVOffset"), Offset);
			}
		});
	}
}

void FLRRaceScenery::HidePlain(USceneCaptureComponent2D* Capture, const lr::World& W, const lr::PlayerCamera& Cam) const
{
	// The plain models out of this camera's reach (the eye to the entity's position, squared, against
	// the core's stored levels; UE's frustum culling does the rest).
	for (int32 m = 0; m < PlainOfEntity.Num() && size_t(m) < W.scenery.modelDistances.size(); m++) {
		if (Plain.IsValidIndex(PlainOfEntity[m]) && !W.plainInReach(size_t(m), Cam.shownEye())) {
			HideParts(Capture, Plain[PlainOfEntity[m]].Parts);
		}
	}
}

void FLRRaceScenery::HideAnimated(USceneCaptureComponent2D* Capture, const lr::World& W, int32 v) const
{
	for (int32 k = 0; k < Animated.Num() && size_t(k) < W.raceView.drawnBy.size(); k++) {
		if (!(W.raceView.drawnBy[size_t(k)] & (1u << v))) {
			HideParts(Capture, Animated[k].Parts);
		}
	}
}

void FLRRaceScenery::Clear()
{
	Animated.Reset();
	Plain.Reset();
	PlainOfEntity.Reset(); // (a race's own: a second race's entities would follow the first's)
	PlainDistances.Reset();
	ShownFrames.Reset();
}
