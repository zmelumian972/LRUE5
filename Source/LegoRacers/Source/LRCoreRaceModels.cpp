// The race view's models as components (LRCoreRaceModels.h): the race's actors, the lights, models placed, posed and
// relit.

#include "LRCoreRaceModels.h"
#include "LRCoreRaceAssets.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "LRData/Cache.h"
#include "LRData/Model.h"
#include "ProceduralMeshComponent.h"

#include "LRCoreModelMesh.h"
#include "LRCoreRaceParts.h"

using namespace LRUe;

namespace
{
// A section's material by name in the libraries, its flip and colours; an override's in its place (a minifig's face,
// torso and legs: their own pictures, ambient and diffuse 200).
UMaterialInterface* SectionMaterial(FLRRaceAssets& Assets, const std::string& Name,
	const TArray<std::pair<std::string, std::string>>& Libraries, float Alpha, const TMap<FString, UMaterialInterface*>* Overrides,
	bool& bFlip, uint8 Diffuse[4], uint8 Ambient[4])
{
	UMaterialInterface* Mid = Assets.MaterialFor(Name, Libraries, Alpha, bFlip, Diffuse, Alpha < 1, Ambient);
	UMaterialInterface* const* Over = Overrides ? Overrides->Find(UTF8_TO_TCHAR(Name.c_str())) : nullptr;
	if (!Over) {
		return Mid;
	}
	bFlip = false;
	for (int32 c = 0; c < 3; c++) {
		Diffuse[c] = Ambient[c] = 200;
	}
	return *Over;
}
} // namespace

AActor* FLRModels::NewActor(const FTransform& At)
{
	AActor* A = NewRootedActor(WorldRef.Get(), At);
	Actors.Add(A);
	return A;
}

void FLRModels::DestroyActors()
{
	for (const TWeakObjectPtr<AActor>& A : Actors) {
		if (A.IsValid()) {
			A->Destroy();
		}
	}
	Actors.Reset();
}

// The race's lights (every race the same): ambient 60, one white light.
void FLRModels::LightRace()
{
	for (int32 c = 0; c < 3; c++) {
		LitAmbient[c] = 60, LitColour[c] = 255;
	}
	LitDirection = lr::normalize({-0.825175881f, -0.278099984f, -0.491675943f});
	LitMore = 0;
}

// The menus' lights: an ambient (none: the device's 255) and the directional lights (none: no
// light terms).
void FLRModels::LightMenu(const lr::MenuLighting& L)
{
	for (int32 c = 0; c < 3; c++) {
		LitAmbient[c] = L.ambient >> 24 ? int32((L.ambient >> (16 - 8 * c)) & 255) : 255;
		LitColour[c] = L.lights.empty() ? 0 : int32((L.lights[0].second >> (16 - 8 * c)) & 255);
	}
	LitDirection = L.lights.empty() ? lr::Vec3{0, 0, -1} : L.lights[0].first;
	LitMore = FMath::Min(6, int32(L.lights.size()) - 1);
	for (int32 i = 0; i < LitMore; i++) {
		LitMoreDirections[i] = L.lights[size_t(i + 1)].first;
		for (int32 c = 0; c < 3; c++) {
			LitMoreColours[i][c] = int32((L.lights[size_t(i + 1)].second >> (16 - 8 * c)) & 255);
		}
	}
	LitMore = FMath::Max(0, LitMore);
}

// A lit vertex's colour by the view's lights (light 0, then the others), at an ambient raised by Boost.
FColor FLRModels::LitVertex(const lr::Vec3& N, const uint8* Diffuse, const uint8* Ambient, int32 Boost) const
{
	uint8 Out[3];
	for (int32 c = 0; c < 3; c++) {
		const int32 Base = (FMath::Min(255, LitAmbient[c] + Boost) * Ambient[c]) >> 8;
		int32 V = Base - int32(float((Diffuse[c] * LitColour[c]) >> 8) * lr::dot(N, LitDirection));
		for (int32 i = 0; i < LitMore; i++) {
			V -= int32(float((Diffuse[c] * LitMoreColours[i][c]) >> 8) * lr::dot(N, LitMoreDirections[i]));
		}
		Out[c] = uint8(FMath::Clamp(V, Base, 255));
	}
	return FColor(Out[0], Out[1], Out[2], 255);
}

FLRPlaced FLRModels::Place(FLRRaceAssets& Assets, AActor* Actor, const FString& Path,
	const TArray<std::pair<std::string, std::string>>& Libraries, bool bLit, bool bMirror, float Alpha)
{
	// In a race, a name the world's libraries don't hold is looked up in the model's own X.MDB beside it, then in the race's
	// libraries loaded before (the beach's water sits in a world that names no library). (The menus' views
	// keep their own lookup.)
	if (Assets.TrackLibraries.IsEmpty() && Assets.CommonLibraries.IsEmpty()) {
		return PlaceMesh(Assets, Actor, Assets.Model(Path, bMirror), Libraries, bLit, Alpha, nullptr);
	}
	TArray<std::pair<std::string, std::string>> With = Libraries;
	const std::string Own = std::string(TCHAR_TO_UTF8(*Path)).substr(0, Path.Len() - 3);
	const auto Add = [&](const std::pair<std::string, std::string>& L) {
		if (!With.ContainsByPredicate([&](const auto& Have) { return Have.first == L.first; })) {
			With.Add(L);
		}
	};
	if (Path.EndsWith(TEXT(".GDB")) && Assets.Cache->has(Own + "MDB")) {
		Add({Own + "MDB", Own + "TDB"});
	}
	for (const auto& L : Assets.TrackLibraries) {
		Add(L);
	}
	for (const auto& L : Assets.CommonLibraries) {
		Add(L);
	}
	return PlaceMesh(Assets, Actor, Assets.Model(Path, bMirror), With, bLit, Alpha, nullptr);
}

FLRPlaced FLRModels::PlaceMesh(FLRRaceAssets& Assets, AActor* Actor, const FLRModelMesh* Mesh,
	const TArray<std::pair<std::string, std::string>>& Libraries, bool bLit, float Alpha,
	const TMap<FString, UMaterialInterface*>* Overrides)
{
	FLRPlaced P;
	P.Actor = Actor;
	P.Mesh = Mesh;
	if (!P.Mesh) {
		return P;
	}
	P.bLit = bLit && P.Mesh->bLit;
	P.Parts.SetNum(P.Mesh->Parts);
	TArray<int32> SectionsOf;
	SectionsOf.Init(0, P.Mesh->Parts);
	for (const FLRModelMesh::FSection& S : P.Mesh->Sections) {
		TWeakObjectPtr<UProceduralMeshComponent>& C = P.Parts[S.Part];
		if (!C.IsValid()) {
			UProceduralMeshComponent* Made = NewMesh(Actor);
			C = Made;
		}
		bool bFlip = false;
		uint8 Diffuse[4], Ambient[4];
		UMaterialInterface* Mid = SectionMaterial(Assets, S.Material, Libraries, Alpha, Overrides, bFlip, Diffuse, Ambient);
		TArray<FVector2D> Uvs = S.Uvs;
		if (bFlip) {
			for (FVector2D& Uv : Uvs) {
				Uv.Y = 1 - Uv.Y;
			}
		}
		TArray<FColor> Colours = S.Colours;
		if (P.bLit) {
			// Lit at identity until posed.
			for (int32 v = 0; v < Colours.Num(); v++) {
				Colours[v] = LitVertex(S.GameNormals[v], Diffuse, Ambient);
			}
		}
		P.Diffuse.Add(FColor(Diffuse[0], Diffuse[1], Diffuse[2], Diffuse[3]));
		P.Ambient.Add(FColor(Ambient[0], Ambient[1], Ambient[2], Ambient[3]));
		const int32 Index = SectionsOf[S.Part]++;
		C->CreateMeshSection(Index, S.Positions, S.Triangles, S.Normals, Uvs, Colours, {}, false);
		C->SetMaterial(Index, Mid);
	}
	return P;
}

void FLRModels::PoseNodes(FLRPlaced& P, const TArray<FTransform>& Nodes, const FTransform& Whole)
{
	if (P.Mesh && P.Mesh->bJoined && !Nodes.IsEmpty() && P.Parts.Num() && P.Parts[0].IsValid()) {
		if (Whole.ContainsNaN() || Nodes.ContainsByPredicate([](const FTransform& T) { return T.ContainsNaN(); })) {
			return;
		}
		UProceduralMeshComponent* C = P.Parts[0].Get();
		C->SetWorldTransform(FTransform::Identity);
		for (int32 s = 0; s < P.Mesh->Sections.Num(); s++) {
			const FLRModelMesh::FSection& S = P.Mesh->Sections[s];
			ScratchPositions.SetNumUninitialized(S.Positions.Num(), EAllowShrinking::No);
			for (int32 v = 0; v < ScratchPositions.Num(); v++) {
				ScratchPositions[v] =
					(Nodes.IsValidIndex(S.VertexNodes[v]) ? Nodes[S.VertexNodes[v]] : Whole).TransformPosition(S.Positions[v]);
			}
			C->UpdateMeshSection(s, ScratchPositions, {}, {}, {}, {});
		}
		return;
	}
	for (int32 i = 0; i < P.Parts.Num(); i++) {
		if (P.Parts[i].IsValid()) {
			const FTransform& T = Nodes.IsValidIndex(i) ? Nodes[i] : Whole;
			if (!T.ContainsNaN()) {
				P.Parts[i]->SetWorldTransform(T);
			}
		}
	}
}

// A lit model's vertex colours for its parts' world rotations now (the race's ambient and light).
void FLRModels::Relight(FLRPlaced& P, const TArray<FQuat>& Rotations)
{
	if (!P.bLit || !P.Mesh) {
		return;
	}
	TArray<int32> SectionsOf;
	SectionsOf.Init(0, P.Mesh->Parts);
	for (int32 s = 0; s < P.Mesh->Sections.Num(); s++) {
		const FLRModelMesh::FSection& S = P.Mesh->Sections[s];
		const int32 Index = SectionsOf[S.Part]++;
		UProceduralMeshComponent* C = P.Parts[S.Part].Get();
		if (!C || !P.Diffuse.IsValidIndex(s)) {
			continue;
		}
		const FQuat Q = Rotations.IsValidIndex(S.Part) ? Rotations[S.Part] : Rotations.Num() ? Rotations[0] : FQuat::Identity;
		const uint8 Diffuse[4] = {P.Diffuse[s].R, P.Diffuse[s].G, P.Diffuse[s].B, P.Diffuse[s].A};
		const uint8 Ambient[4] = {P.Ambient[s].R, P.Ambient[s].G, P.Ambient[s].B, P.Ambient[s].A};
		ScratchColours.SetNumUninitialized(S.Positions.Num(), EAllowShrinking::No);
		for (int32 v = 0; v < ScratchColours.Num(); v++) {
			const FVector N = Q.RotateVector(Along(S.GameNormals[v])).GetSafeNormal();
			ScratchColours[v] = LitVertex({float(N.X), float(-N.Y), float(N.Z)}, Diffuse, Ambient);
		}
		C->UpdateMeshSection(Index, S.Positions, S.Normals, {}, ScratchColours, {});
	}
}

void FLRModels::PoseLit(FLRPlaced& P, const TArray<FTransform>& Nodes, const FTransform& Whole)
{
	PoseNodes(P, Nodes, Whole);
	if (!P.bLit || !P.Mesh) {
		return;
	}
	ScratchRotations.Reset();
	for (const FTransform& N : Nodes) {
		ScratchRotations.Add(N.GetRotation());
	}
	if (ScratchRotations.IsEmpty()) {
		ScratchRotations.Add(Whole.GetRotation());
	}
	Relight(P, ScratchRotations);
}

void FLRModels::PoseOn(FLRPlaced& P, const FTransform& At)
{
	ScratchOn.Init(At, P.Parts.Num());
	PoseLit(P, ScratchOn, At);
}

// Each of P's sections (Only: those of that material) with its component and its index there.
void FLRModels::ForSections(FLRPlaced& P, const std::string* Only,
	TFunctionRef<void(UProceduralMeshComponent*, int32, const FLRModelMesh::FSection&)> Do)
{
	TArray<int32, TInlineAllocator<8>> SectionsOf;
	SectionsOf.Init(0, P.Mesh->Parts);
	for (const FLRModelMesh::FSection& S : P.Mesh->Sections) {
		const int32 Index = SectionsOf[S.Part]++;
		if (P.Parts[S.Part].IsValid() && (!Only || S.Material == *Only)) {
			Do(P.Parts[S.Part].Get(), Index, S);
		}
	}
}

FLRSkin FLRModels::BuildSkin(FLRRaceAssets& Assets, AActor* Actor, const lr::Model& M,
	const TArray<std::pair<std::string, std::string>>& Libraries, const TMap<FString, UMaterialInterface*>* Overrides, bool bFaceless,
	float Alpha)
{
	FLRSkin S;
	S.Model = M;
	UProceduralMeshComponent* C = NewMesh(Actor);
	S.Component = C;
	// A section per material (the body's "face" group only marks the head's place: left out).
	TMap<uint16, int32> SectionOf;
	TArray<TArray<int32>> Triangles;
	TArray<TMap<uint32, int32>> Remaps;
	TArray<std::string> SectionMaterials;
	for (const lr::Model::Section& Sec : M.sections) {
		const std::string Name = Sec.material < M.materials.size() ? M.materials[Sec.material] : std::string();
		if (bFaceless && Name == "face") {
			continue;
		}
		int32* At = SectionOf.Find(Sec.material);
		if (!At) {
			At = &SectionOf.Add(Sec.material, S.Sections.Num());
			S.Sections.AddDefaulted();
			Triangles.AddDefaulted();
			Remaps.AddDefaulted();
			SectionMaterials.Add(Name);
		}
		FLRSkin::FSection& Into = S.Sections[*At];
		for (size_t t = 0; t + 2 < Sec.indices.size(); t += 3) {
			for (size_t c = 0; c < 3; c++) {
				const uint32 V = Sec.indices[t + c];
				int32* Known = Remaps[*At].Find(V);
				if (!Known) {
					Known = &Remaps[*At].Add(V, Into.Vertices.Num());
					Into.Vertices.Add(int32(V));
				}
				Triangles[*At].Add(*Known);
			}
		}
	}
	for (int32 s = 0; s < S.Sections.Num(); s++) {
		FLRSkin::FSection& Sec = S.Sections[s];
		bool bFlip = false;
		uint8 Diffuse[4], Ambient[4];
		UMaterialInterface* Mid = SectionMaterial(Assets, SectionMaterials[s], Libraries, Alpha, Overrides, bFlip, Diffuse, Ambient);
		Sec.Diffuse = FColor(Diffuse[0], Diffuse[1], Diffuse[2], Diffuse[3]);
		Sec.Ambient = FColor(Ambient[0], Ambient[1], Ambient[2], Ambient[3]);
		Sec.Material = SectionMaterials[s];
		TArray<FVector> P, N;
		TArray<FVector2D> Uv;
		TArray<FColor> Colours;
		for (const int32 V : Sec.Vertices) {
			P.Add(FVector(M.positions[size_t(V) * 3], -M.positions[size_t(V) * 3 + 1], M.positions[size_t(V) * 3 + 2]) * Cm);
			N.Add(FVector::UpVector);
			const float U = M.uvs.empty() ? 0 : M.uvs[size_t(V) * 2], W = M.uvs.empty() ? 0 : M.uvs[size_t(V) * 2 + 1];
			Uv.Add(FVector2D(U, bFlip ? 1 - W : W));
			Colours.Add(FColor::White);
		}
		C->CreateMeshSection(s, P, Triangles[s], N, Uv, Colours, {}, false);
		C->SetMaterial(s, Mid);
	}
	return S;
}

void FLRModels::PoseSkin(FLRSkin& S, const std::vector<lr::NodeMatrix>& Nodes, const FVector& At)
{
	UProceduralMeshComponent* C = S.Component.Get();
	if (!C || Nodes.empty()) {
		return;
	}
	std::vector<lr::Vec3> Placed;
	lr::skinVertices(S.Model, Nodes, Placed);
	C->SetWorldTransform(FTransform::Identity);
	const bool bLit = S.Model.kind == lr::Model::Vertices::Lit && !S.Model.normals.empty();
	for (int32 s = 0; s < S.Sections.Num(); s++) {
		const FLRSkin::FSection& Sec = S.Sections[s];
		TArray<FVector> P, N;
		TArray<FColor> Colours;
		const uint8 Diffuse[4] = {Sec.Diffuse.R, Sec.Diffuse.G, Sec.Diffuse.B, Sec.Diffuse.A};
		const uint8 Ambient[4] = {Sec.Ambient.R, Sec.Ambient.G, Sec.Ambient.B, Sec.Ambient.A};
		for (const int32 V : Sec.Vertices) {
			P.Add(Ue(Placed[size_t(V)]) + At);
			const size_t Node = size_t(V) < S.Model.vertexParts.size() ? S.Model.vertexParts[size_t(V)] : 0;
			const lr::Basis& R = Nodes[FMath::Min(Node, Nodes.size() - 1)].rows;
			lr::Vec3 Normal = {0, 0, 1};
			if (bLit) {
				Normal = lr::normalize(lr::toWorld(R,
					{S.Model.normals[size_t(V) * 3], S.Model.normals[size_t(V) * 3 + 1], S.Model.normals[size_t(V) * 3 + 2]}));
			}
			N.Add(Along(Normal));
			if (S.Prelit.IsValidIndex(s) && S.Prelit[s].IsValidIndex(Colours.Num())) {
				Colours.Add(S.Prelit[s][Colours.Num()]);
			}
			else if (bLit) {
				Colours.Add(LitVertex(Normal, Diffuse, Ambient));
			}
			else {
				const uint8* Col = S.Model.colors.empty() ? nullptr : &S.Model.colors[size_t(V) * 4];
				FColor Out = Col ? FColor(Col[0], Col[1], Col[2], Col[3]) : FColor::White;
				if (Col && S.bColoured) {
					Out = Transformed(Out, S.Shift, S.Offset);
				}
				Colours.Add(Out);
			}
		}
		C->UpdateMeshSection(s, P, N, {}, Colours, {});
	}
}

void LRUe::FramesOf(const std::vector<lr::NodeMatrix>& Nodes, TArray<FTransform>& Out, const FTransform& Offset)
{
	Out.Reset();
	for (const lr::NodeMatrix& N : Nodes) {
		Out.Add(FrameOf(N.rows, N.position) * Offset);
	}
}

void LRUe::SetVisible(const TArray<TWeakObjectPtr<UProceduralMeshComponent>>& Parts, bool bVisible)
{
	for (const TWeakObjectPtr<UProceduralMeshComponent>& C : Parts) {
		if (C.IsValid()) {
			C->SetVisibility(bVisible);
		}
	}
}

void LRUe::HideParts(USceneCaptureComponent2D* Capture, const TArray<TWeakObjectPtr<UProceduralMeshComponent>>& Parts)
{
	for (const TWeakObjectPtr<UProceduralMeshComponent>& Part : Parts) {
		if (Part.IsValid()) {
			Capture->HiddenComponents.Add(Part);
		}
	}
}

void LRUe::AddSprite(const std::array<lr::Vec3, 4>& Corners, bool bFlip, TArray<FVector>& P, TArray<FVector2D>& Uv, TArray<FColor>& Colour,
	TArray<int32>& T)
{
	const int32 First = P.Num();
	const FVector2D Uvs[4] = {{0.01, 0.01}, {0.99, 0.01}, {0.99, 0.99}, {0.01, 0.99}}; // (inside the wrap's bleed)
	for (int32 k = 0; k < 4; k++) {
		P.Add(Ue(Corners[size_t(k)]));
		Uv.Add(bFlip ? FVector2D(Uvs[k].X, 1 - Uvs[k].Y) : Uvs[k]);
		Colour.Add(FColor::White);
	}
	for (const int32 k : {0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2}) {
		T.Add(First + k);
	}
}

// A colour effect's transform of a prelit colour: red, green, blue (byte >> shift) + offset,
// capped at 255; alpha kept.
FColor LRUe::Transformed(const FColor& C, const int32 Shift[3], const int32 Offset[3])
{
	const auto Channel = [&](uint8 V, int32 i) { return uint8(FMath::Min(255, (int32(V) >> Shift[i]) + Offset[i])); };
	return FColor(Channel(C.R, 0), Channel(C.G, 1), Channel(C.B, 2), C.A);
}
