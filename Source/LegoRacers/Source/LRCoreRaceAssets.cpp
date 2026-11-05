// The race view's assets: materials and textures from the cache, models as UE meshes, placing and relighting them.

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

UTexture2D* ULRRaceView::Texture(const std::string& List, const std::string& Name, bool bPoint, bool bClamp)
{
	const FString Key = FString::Printf(TEXT("%hs#%hs#%d%d"), List.c_str(), Name.c_str(), bPoint, bClamp);
	if (TObjectPtr<UTexture2D>* Found = Textures.Find(Key)) {
		return *Found;
	}
	lr::Texture T;
	UTexture2D* Made = nullptr;
	if (Cache->texture(List, Name, T) && T.width && T.height) {
		Made = NewTexture(int32(T.width), int32(T.height), bPoint ? TF_Nearest : TF_Bilinear, bClamp ? TA_Clamp : TA_Wrap, [&](uint8* Mip) {
			for (size_t i = 0; i < size_t(T.width) * T.height; i++) {
				Mip[i * 4 + 0] = T.rgba[i * 4 + 2];
				Mip[i * 4 + 1] = T.rgba[i * 4 + 1];
				Mip[i * 4 + 2] = T.rgba[i * 4 + 0];
				Mip[i * 4 + 3] = T.rgba[i * 4 + 3];
			}
		});
	}
	Textures.Add(Key, Made);
	return Made;
}

// The Classic look's three base materials (masked, translucent, additive), loaded once.
bool ULRRaceView::LoadBases()
{
	if (!MaskedBase) {
		MaskedBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LR/Materials/M_ClassicMasked.M_ClassicMasked"));
		TranslucentBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LR/Materials/M_ClassicTranslucent.M_ClassicTranslucent"));
		AdditiveBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LR/Materials/M_ClassicAdditive.M_ClassicAdditive"));
	}
	return MaskedBase && TranslucentBase && AdditiveBase;
}

const lr::MaterialLibrary* ULRRaceView::LibraryAt(const std::string& Path)
{
	auto [It, bNew] = LibraryCache.try_emplace(Path);
	if (bNew) {
		lr::MaterialLibrary Lib;
		if (Cache->get(Path, Lib)) {
			It->second = std::move(Lib);
		}
	}
	return It->second ? &*It->second : nullptr;
}

const lr::TextureList* ULRRaceView::TexturesAt(const std::string& Path)
{
	auto [It, bNew] = TextureCache.try_emplace(Path);
	if (bNew) {
		lr::TextureList List;
		if (Cache->get(Path, List)) {
			It->second = std::move(List);
		}
	}
	return It->second ? &*It->second : nullptr;
}

UMaterialInstanceDynamic* ULRRaceView::MaterialFor(const std::string& Name, const TArray<std::pair<std::string, std::string>>& Libraries, float Alpha, bool& bFlip,
	uint8 Diffuse[4], bool bForceTranslucent, uint8* Ambient)
{
	bFlip = false;
	Diffuse[0] = Diffuse[1] = Diffuse[2] = Diffuse[3] = 255;
	if (Ambient) {
		Ambient[0] = Ambient[1] = Ambient[2] = Ambient[3] = 255;
	}
	const lr::MaterialLibrary::Material* M = nullptr;
	std::string List;
	for (const auto& [Mdb, Tdb] : Libraries) {
		const lr::MaterialLibrary* Lib = LibraryAt(Mdb);
		const int32 i = Lib ? Lib->find(Name) : -1;
		if (i >= 0) {
			M = &Lib->materials[size_t(i)];
			List = Tdb;
			break;
		}
	}
	bool bTextured = M && !M->texture.empty();
	if (bTextured) {
		// The library's own list, else the shared effects' (a track's EMITCxRx.MDB names COMMON/EMITTER.TDB's smoke;
		// drawn untextured it was a solid white cone over C1R3's smoke vent).
		for (const std::string& Candidate : {List, Common + "EMITTER.TDB"}) {
			const lr::TextureList* L = TexturesAt(Candidate);
			const int32 e = L ? L->find(M->texture) : -1;
			if (e >= 0) {
				bFlip = L->textures[size_t(e)].flip;
				List = Candidate;
				break;
			}
		}
	}
	if (M) {
		for (int32 c = 0; c < 4; c++) {
			Diffuse[c] = M->diffuse[size_t(c)];
			if (Ambient) {
				Ambient[c] = M->ambient[size_t(c)];
			}
		}
	}
	const bool bTranslucent = bForceTranslucent || Alpha < 1 || (M && (M->blend || M->constantAlpha));
	// Additive by the blend factors (src alpha, one), never by name (the flashes, the glows, the turbo
	// burns, the magnet dust, the missile flame, lightning, sbfan), in the menus and the races alike.
	const bool bAdd = M && M->additive();
	const float Constant = (M && M->constantAlpha ? float(M->alpha) / 255.0f : 1.0f) * Alpha;
	const FString Key = FString::Printf(TEXT("%hs|%hs|%g|%d|%d"), Name.c_str(), List.c_str(), Constant, bTranslucent, bAdd);
	if (TObjectPtr<UMaterialInstanceDynamic>* Found = Materials.Find(Key)) {
		return *Found;
	}
	UMaterialInterface* Parent = bAdd ? AdditiveBase.Get() : bTranslucent ? TranslucentBase.Get() : MaskedBase.Get();
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Parent, this);
	if (bTextured) {
		if (UTexture2D* T = Texture(List, M->texture, M->point, M->clamp)) {
			Mid->SetTextureParameterValue(TEXT("Tex"), T);
		}
	}
	Mid->SetScalarParameterValue(TEXT("Alpha"), Constant);
	Materials.Add(Key, Mid);
	return Mid;
}

// A lit vertex's colour by the view's lights (light 0, then the others), at an ambient raised by Boost.
FColor ULRRaceView::LitVertex(const lr::Vec3& N, const uint8* Diffuse, const uint8* Ambient, int32 Boost) const
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

const FLRModelMesh* ULRRaceView::Model(const FString& Path, bool bMirror)
{
	const FString Key = Path + (bMirror ? TEXT("#m") : TEXT(""));
	if (TUniquePtr<FLRModelMesh>* Found = Meshes.Find(Key)) {
		return Found->Get();
	}
	lr::Model M;
	const bool bHas = Cache->get(TCHAR_TO_UTF8(*Path), M);
	return ModelOf(Key, bHas ? &M : nullptr, bMirror);
}

const FLRModelMesh* ULRRaceView::ModelOf(const FString& Key, const lr::Model* Source, bool bMirror)
{
	if (TUniquePtr<FLRModelMesh>* Found = Meshes.Find(Key)) {
		return Found->Get();
	}
	TUniquePtr<FLRModelMesh> Made;
	if (Source) {
		const lr::Model& M = *Source;
		Made = MakeUnique<FLRModelMesh>();
		FLRModelMesh* Out = Made.Get();
		Out->Scale = M.scale;
		Out->bLit = M.kind == lr::Model::Vertices::Lit;
		Out->Bounds = lr::modelBounds(M);
		Out->MaterialNames = M.materials;
		Out->bJoined = lr::joinsNodes(M);
		const float Flip = bMirror ? -1.0f : 1.0f;
		for (const lr::Model::Section& S : M.sections) {
			// One section per part and material.
			const std::string Material = S.material < M.materials.size() ? M.materials[S.material] : std::string();
			const int32 Part = Out->bJoined ? 0 : int32(S.part);
			FLRModelMesh::FSection* Into = nullptr;
			for (FLRModelMesh::FSection& Have : Out->Sections) {
				Into = Have.Part == Part && Have.Material == Material ? &Have : Into;
			}
			if (!Into) {
				Into = &Out->Sections.AddDefaulted_GetRef();
				Into->Part = Part;
				Into->Material = Material;
				Out->Parts = FMath::Max(Out->Parts, Part + 1);
			}
			TMap<uint32, int32> Remap;
			for (size_t t = 0; t + 2 < S.indices.size(); t += 3) {
				int32 Corner[3];
				for (size_t c = 0; c < 3; c++) {
					const uint32 V = S.indices[t + c];
					if (const int32* Known = Remap.Find(V)) {
						Corner[c] = *Known;
						continue;
					}
					Corner[c] = Into->Positions.Num();
					Remap.Add(V, Corner[c]);
					const float* P = &M.positions[size_t(V) * 3];
					Into->Positions.Add(FVector(P[0], -P[1] * Flip, P[2]) * Cm);
					if (Out->bJoined) {
						Into->VertexNodes.Add(M.vertexParts[V]);
					}
					const lr::Vec3 N = M.normals.empty() ? lr::Vec3{0, 0, 1} : lr::Vec3{M.normals[V * 3], M.normals[V * 3 + 1] * Flip, M.normals[V * 3 + 2]};
					Into->GameNormals.Add(N);
					Into->Normals.Add(Along(N));
					Into->Uvs.Add(M.uvs.empty() ? FVector2D::ZeroVector : FVector2D(M.uvs[V * 2], M.uvs[V * 2 + 1]));
					const uint8* C = M.colors.empty() ? nullptr : &M.colors[size_t(V) * 4];
					Into->Colours.Add(C ? FColor(C[0], C[1], C[2], C[3]) : FColor::White);
				}
				// The game's winding is UE's front face; a mirrored model turns it round.
				Into->Triangles.Add(Corner[0]);
				Into->Triangles.Add(bMirror ? Corner[2] : Corner[1]);
				Into->Triangles.Add(bMirror ? Corner[1] : Corner[2]);
			}
		}
	}
	return Meshes.Add(Key, MoveTemp(Made)).Get();
}

ULRRaceView::FPlaced ULRRaceView::Place(AActor* Actor, const FString& Path, const TArray<std::pair<std::string, std::string>>& Libraries, bool bLit, bool bMirror, float Alpha)
{
	// In a race, a name the world's libraries don't hold is looked up in the model's own X.MDB beside it, then in the race's
	// libraries loaded before (the beach's water sits in a world that names no library). (The menus' views
	// keep their own lookup.)
	if (TrackLibraries.IsEmpty() && CommonLibraries.IsEmpty()) {
		return PlaceMesh(Actor, Model(Path, bMirror), Libraries, bLit, Alpha, nullptr);
	}
	TArray<std::pair<std::string, std::string>> With = Libraries;
	const std::string Own = std::string(TCHAR_TO_UTF8(*Path)).substr(0, Path.Len() - 3);
	const auto Add = [&](const std::pair<std::string, std::string>& L) {
		if (!With.ContainsByPredicate([&](const auto& Have) { return Have.first == L.first; })) {
			With.Add(L);
		}
	};
	if (Path.EndsWith(TEXT(".GDB")) && Cache->has(Own + "MDB")) {
		Add({Own + "MDB", Own + "TDB"});
	}
	for (const auto& L : TrackLibraries) {
		Add(L);
	}
	for (const auto& L : CommonLibraries) {
		Add(L);
	}
	return PlaceMesh(Actor, Model(Path, bMirror), With, bLit, Alpha, nullptr);
}

ULRRaceView::FPlaced ULRRaceView::PlaceMesh(AActor* Actor, const FLRModelMesh* Mesh, const TArray<std::pair<std::string, std::string>>& Libraries, bool bLit, float Alpha,
	const TMap<FString, UMaterialInterface*>* Overrides)
{
	FPlaced P;
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
		UMaterialInterface* Mid = MaterialFor(S.Material, Libraries, Alpha, bFlip, Diffuse, Alpha < 1, Ambient);
		if (UMaterialInterface* const* Over = Overrides ? Overrides->Find(UTF8_TO_TCHAR(S.Material.c_str())) : nullptr) {
			// (A minifig's face, torso and legs: their own pictures, ambient and diffuse 200)
			Mid = *Over;
			bFlip = false;
			for (int32 c = 0; c < 3; c++) {
				Diffuse[c] = Ambient[c] = 200;
			}
		}
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

void ULRRaceView::PoseNodes(FPlaced& P, const TArray<FTransform>& Nodes, const FTransform& Whole)
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
				ScratchPositions[v] = (Nodes.IsValidIndex(S.VertexNodes[v]) ? Nodes[S.VertexNodes[v]] : Whole).TransformPosition(S.Positions[v]);
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
void ULRRaceView::Relight(FPlaced& P, const TArray<FQuat>& Rotations)
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

// Each of P's sections (Only: those of that material) with its component and its index there.
void ULRRaceView::ForSections(FPlaced& P, const std::string* Only, TFunctionRef<void(UProceduralMeshComponent*, int32, const FLRModelMesh::FSection&)> Do)
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
