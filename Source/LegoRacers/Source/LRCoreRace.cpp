// The race in the Classic look, from the core and the show (LRCoreRace.h).

#include "LRCoreRace.h"

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

namespace
{
constexpr float Cm = 30.48f;
const std::string Common = "GAMEDATA/COMMON/";

FVector Ue(const lr::Vec3& V) { return FVector(V.x, -V.y, V.z) * Cm; }
FVector Along(const lr::Vec3& V) { return FVector(V.x, -V.y, V.z); }

// A frame of the game's rows (forward, left, up; scaled) and a place as UE's transform.
FTransform FrameOf(const lr::Basis& B, const lr::Vec3& At)
{
	const FMatrix M(Along(B.f), -Along(B.s), Along(B.u), Ue(At));
	return FTransform(M);
}

std::string Upper(std::string S)
{
	for (char& C : S) {
		C = char(std::toupper(uint8_t(C)));
	}
	return S;
}
std::string FolderOf(const std::string& Path) { return Path.substr(0, Path.rfind('/') + 1); }

// The material libraries a world names, with their texture lists, as cache paths.
TArray<std::pair<std::string, std::string>> LibrariesOf(const lr::SceneFile& Scene, const std::string& Folder)
{
	TArray<std::pair<std::string, std::string>> Out;
	for (size_t i = 0; i < Scene.materialLibraries.size(); i++) {
		const std::string Mdb = Folder + Upper(Scene.materialLibraries[i]) + ".MDB";
		const std::string Tdb = Folder + Upper(i < Scene.textureLists.size() ? Scene.textureLists[i] : Scene.materialLibraries[i]) + ".TDB";
		Out.Add({Mdb, Tdb});
	}
	return Out;
}

// A lit vertex's colour channel. The base is the world's ambient times the material's ambient (a black built car is
// dark, which 255 in place of the material's would not give).
uint8 LitChannel(int32 Ambient, int32 Diffuse, int32 Light, float NdotL, int32 MaterialAmbient)
{
	const int32 Base = (Ambient * MaterialAmbient) >> 8;
	const int32 P = (Diffuse * Light) >> 8;
	return uint8(FMath::Clamp(Base - int32(float(P) * NdotL), Base, 255));
}
} // namespace

struct ULRRaceView::FFigure
{
	std::vector<lr::SkeletonNode> Nodes;
	lr::Animation Animation;
	bool bAnimated = false;
};

void ULRRaceView::BeginDestroy()
{
	for (auto& [Key, Mesh] : Meshes) {
		delete Mesh;
	}
	Meshes.Reset();
	for (auto& [Key, F] : Figures) {
		delete F;
	}
	Figures.Reset();
	for (auto& [Key, A] : MenuAnimations) {
		delete A;
	}
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

UTexture2D* ULRRaceView::Texture(const std::string& List, const std::string& Name, bool bPoint, bool bClamp)
{
	const FString Key = FString::Printf(TEXT("%hs#%hs#%d%d"), List.c_str(), Name.c_str(), bPoint, bClamp);
	if (TObjectPtr<UTexture2D>* Found = Textures.Find(Key)) {
		return *Found;
	}
	lr::Texture T;
	UTexture2D* Made = nullptr;
	if (Cache->texture(List, Name, T) && T.width && T.height) {
		Made = UTexture2D::CreateTransient(int32(T.width), int32(T.height), PF_B8G8R8A8);
		Made->SRGB = true;
		Made->Filter = bPoint ? TF_Nearest : TF_Bilinear;
		Made->AddressX = Made->AddressY = bClamp ? TA_Clamp : TA_Wrap;
		Made->NeverStream = true;
		uint8* Mip = static_cast<uint8*>(Made->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
		for (size_t i = 0; i < size_t(T.width) * T.height; i++) {
			Mip[i * 4 + 0] = T.rgba[i * 4 + 2];
			Mip[i * 4 + 1] = T.rgba[i * 4 + 1];
			Mip[i * 4 + 2] = T.rgba[i * 4 + 0];
			Mip[i * 4 + 3] = T.rgba[i * 4 + 3];
		}
		Made->GetPlatformData()->Mips[0].BulkData.Unlock();
		Made->UpdateResource();
	}
	Textures.Add(Key, Made);
	return Made;
}

UMaterialInstanceDynamic* ULRRaceView::MaterialFor(const std::string& Name, const TArray<std::pair<std::string, std::string>>& Libraries,
	float Alpha, bool& bFlip, uint8 Diffuse[4], bool bForceTranslucent, uint8* Ambient)
{
	bFlip = false;
	Diffuse[0] = Diffuse[1] = Diffuse[2] = Diffuse[3] = 255;
	if (Ambient) {
		Ambient[0] = Ambient[1] = Ambient[2] = Ambient[3] = 255;
	}
	const lr::MaterialLibrary::Material* M = nullptr;
	std::string List;
	for (const auto& [Mdb, Tdb] : Libraries) {
		lr::MaterialLibrary Lib;
		if (Cache->get(Mdb, Lib)) {
			const int32 i = Lib.find(Name);
			if (i >= 0) {
				static thread_local lr::MaterialLibrary::Material Kept;
				Kept = Lib.materials[size_t(i)];
				M = &Kept;
				List = Tdb;
				break;
			}
		}
	}
	bool bTextured = M && !M->texture.empty();
	if (bTextured) {
		// The library's own list, else the shared effects' (a track's EMITCxRx.MDB names COMMON/EMITTER.TDB's smoke;
		// drawn untextured it was a solid white cone over C1R3's smoke vent).
		for (const std::string& Candidate : {List, Common + "EMITTER.TDB"}) {
			lr::TextureList L;
			const int32 e = Cache->get(Candidate, L) ? L.find(M->texture) : -1;
			if (e >= 0) {
				bFlip = L.textures[size_t(e)].flip;
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
	if (FLRModelMesh** Found = Meshes.Find(Key)) {
		return *Found;
	}
	lr::Model M;
	const bool bHas = Cache->get(TCHAR_TO_UTF8(*Path), M);
	return ModelOf(Key, bHas ? &M : nullptr, bMirror);
}

const FLRModelMesh* ULRRaceView::ModelOf(const FString& Key, const lr::Model* Source, bool bMirror)
{
	if (FLRModelMesh** Found = Meshes.Find(Key)) {
		return *Found;
	}
	FLRModelMesh* Out = nullptr;
	if (Source) {
		const lr::Model& M = *Source;
		Out = new FLRModelMesh;
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
	Meshes.Add(Key, Out);
	return Out;
}

ULRRaceView::FPlaced ULRRaceView::Place(AActor* Actor, const FString& Path, const TArray<std::pair<std::string, std::string>>& Libraries, bool bLit,
	bool bMirror, float Alpha)
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

ULRRaceView::FPlaced ULRRaceView::PlaceMesh(AActor* Actor, const FLRModelMesh* Mesh, const TArray<std::pair<std::string, std::string>>& Libraries, bool bLit,
	float Alpha, const TMap<FString, UMaterialInterface*>* Overrides)
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
			UProceduralMeshComponent* Made = NewObject<UProceduralMeshComponent>(Actor);
			Made->SetMobility(EComponentMobility::Movable);
			Made->SetCastShadow(false);
			Made->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Made->bUseAsyncCooking = false;
			Made->SetupAttachment(Actor->GetRootComponent());
			Made->RegisterComponent();
			Actor->AddInstanceComponent(Made);
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
			TArray<FVector> Placed;
			Placed.SetNumUninitialized(S.Positions.Num());
			for (int32 v = 0; v < Placed.Num(); v++) {
				Placed[v] = (Nodes.IsValidIndex(S.VertexNodes[v]) ? Nodes[S.VertexNodes[v]] : Whole).TransformPosition(S.Positions[v]);
			}
			C->UpdateMeshSection(s, Placed, {}, {}, {}, {});
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
		TArray<FColor> Colours;
		Colours.SetNum(S.Positions.Num());
		for (int32 v = 0; v < Colours.Num(); v++) {
			const FVector N = Q.RotateVector(Along(S.GameNormals[v])).GetSafeNormal();
			Colours[v] = LitVertex({float(N.X), float(-N.Y), float(N.Z)}, Diffuse, Ambient);
		}
		C->UpdateMeshSection(Index, S.Positions, S.Normals, {}, Colours, {});
	}
}

// ---- Building ----

namespace
{
// A plain entity's frame (forward, up and the left between them; the model's and the entity's scale), mirrored as the
// track is (the mesh mirrored too: f' = Sf, s' = -Ss, u' = Su, p' = Sp with S the y flip).
lr::Pose EntityPose(const lr::SceneFile::Entity& E, bool bMirror)
{
	lr::Vec3 F = lr::normalize(lr::vec(E.forward)), U = lr::normalize(lr::vec(E.up));
	lr::Vec3 S = lr::cross(U, F);
	lr::Vec3 P = lr::vec(E.position);
	if (bMirror) {
		F.y = -F.y, U.y = -U.y, S = {-S.x, S.y, -S.z}, P.y = -P.y;
	}
	return {P, {F, S, U}};
}

lr::Basis Scaled(const lr::Basis& B, float S) { return {B.f * S, B.s * S, B.u * S}; }
} // namespace

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
	MaskedBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LR/Materials/M_ClassicMasked.M_ClassicMasked"));
	TranslucentBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LR/Materials/M_ClassicTranslucent.M_ClassicTranslucent"));
	AdditiveBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LR/Materials/M_ClassicAdditive.M_ClassicAdditive"));
	if (!MaskedBase || !TranslucentBase || !AdditiveBase) {
		UE_LOG(LogTemp, Error, TEXT("LRCore: the Classic materials are missing (scripts/make_classic_materials.py)"));
		return;
	}
	Track = UTF8_TO_TCHAR(W.brief.track.c_str());
	bMirrored = W.track.mirrored;
	const std::string Dir = "GAMEDATA/" + W.brief.track + "/";
	lr::RaceFile Race;
	if (!Cache->get(Dir + W.brief.track + ".RAB", Race)) {
		return;
	}
	const auto Found = [&](const std::string& Name) { return Cache->has(Dir + Name) ? Dir + Name : Common + Name; };
	const auto ModelPath = [&](const std::string& Name, const std::string& Folder) {
		const std::string Own = Folder + Upper(Name) + ".GDB";
		return FString(UTF8_TO_TCHAR((Cache->has(Own) ? Own : Common + Upper(Name) + ".GDB").c_str()));
	};
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
		const auto Add = [&](size_t Model, const lr::Pose& Pose, float Distance) {
			FPlaced P = Place(Scenery, ModelPath(Scene.models[Model], Folder), Libraries, false, bMirrored);
			if (!P.Mesh) {
				return -1;
			}
			const FTransform T = FrameOf(Scaled(Pose.rotation, P.Mesh->Scale), Pose.position);
			for (TWeakObjectPtr<UProceduralMeshComponent>& C : P.Parts) {
				if (C.IsValid()) {
					C->SetWorldTransform(T);
				}
			}
			(void) Distance; // (an entity's distances are the core's, as they change: PoseScenery)
			PlainNames.Add(UTF8_TO_TCHAR(Scene.models[Model].c_str()));
			return Plain.Add(P);
		};
		for (const lr::SceneFile::Entity& E : Scene.entities) {
			int32 Index = -1;
			if (E.model >= 0 && size_t(E.model) < Scene.models.size()) {
				lr::Pose Pose = EntityPose(E, bMirrored);
				Pose.rotation = Scaled(Pose.rotation, E.scale);
				Index = Add(size_t(E.model), Pose, E.drawDistance);
				Placed[E.model] = true;
			}
			PlainOfEntity.Add(Index);
		}
		for (size_t m = 0; m < Scene.models.size(); m++) {
			if (!Placed[int32(m)]) {
				Add(m, lr::Pose{}, -1);
			}
		}
	}
	for (const lr::AnimatedModel& A : W.track.animated) {
		FPlaced P;
		if (!A.model.empty()) {
			P = Place(Scenery, ModelPath(A.model, Dir), TrackLibraries.Num() ? TrackLibraries : CommonLibraries, false, bMirrored);
		}
		Animated.Add(P);
	}

	// The racers: the body (a champion's model, lit each frame; or the built car, lit once), the chassis' second variant,
	// the chassis car and the driver, each on its skeleton.
	lr::ChassisTable ChassisTable;
	lr::ChampionTable Champions;
	lr::DriverTable Drivers;
	Cache->get(Common + "CHASSIS.CMB", ChassisTable);
	Cache->get(Common + "CHAMPS.CCB", Champions);
	Cache->get(Common + "DRIVERS.DDB", Drivers);
	lr::PieceLibrary Low;
	Cache->get("MENUDATA/PIECEDB/LPIECELO.LEB", Low);
	const auto Libs = [](const std::string& Mdb, const std::string& Tdb) {
		TArray<std::pair<std::string, std::string>> L;
		L.Add({Common + Upper(Mdb) + ".MDB", Common + Upper(Tdb.empty() ? Mdb : Tdb) + ".TDB"});
		return L;
	};
	Racers.SetNum(int32(W.karts.size()));
	for (size_t i = 0; i < W.karts.size() && i < W.brief.slots.size(); i++) {
		const lr::RaceBrief::Slot& Slot = W.brief.slots[i];
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
			R.Members[0] = Place(Kart, ModelPath(Champion->model, Common), Libs(Champion->materials, Champion->textures), true);
		}
		else if (!Slot.car.empty()) {
			// The built car: the low pieces, lit once by ambient 0x80 and a white light along (-1, 0, -1).
			lr::Car Car;
			Car.load(Low, Slot.car);
			Car.chooseOffset(Low);
			const lr::CarMesh Mesh = Car.mesh(Low, Data.sets.materials, {false, true, true, true}, Car.entries.size());
			lr::MaterialLibrary Mats;
			Cache->get("MENUDATA/PIECEDB/LPIECELO.MDB", Mats);
			const lr::Vec3 Light = lr::normalize({-1, 0, -1});
			UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(Kart);
			C->SetMobility(EComponentMobility::Movable);
			C->SetCastShadow(false);
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetupAttachment(Kart->GetRootComponent());
			C->RegisterComponent();
			Kart->AddInstanceComponent(C);
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
					Colours[*S].Add(FColor(LitChannel(0x80, D[0], 255, NdotL, Amb[0]), LitChannel(0x80, D[1], 255, NdotL, Amb[1]),
						LitChannel(0x80, D[2], 255, NdotL, Amb[2]), 255));
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
			R.Built.Add(C);
			R.Members[0].Actor = Kart;
		}
		if (Chassis) {
			if (!Chassis->second.model.empty()) {
				R.Members[1] = Place(Kart, ModelPath(Chassis->second.model, Common), Libs(Chassis->second.materials, Chassis->second.textures), true);
			}
			R.Members[2] = Place(Kart, ModelPath(Chassis->model, Common), Libs(Chassis->materials, Chassis->textures), true);
		}
		if (Driver) {
			R.Members[3] = Place(Kart, ModelPath(Driver->model, Common), Libs(Driver->materials, Driver->textures), true);
		}
		MakeShadow(int32(i), W);
	}
	// A time race's ghosts: player 0's members again, translucent.
	if (W.brief.mode == lr::RaceBrief::Mode::TimeRace && !Racers.IsEmpty()) {
		for (int32 g = 0; g < 2; g++) {
			AActor* A = NewActor(FTransform::Identity);
			const lr::RaceBrief::Slot& Slot = W.brief.slots[0];
			if (Slot.built()) {
				BuildRaceDriver(A, Slot.looks, Data, 127.0f / 255, GhostDrivers[g], Ghosts[g][3]);
			}
			const lr::DriverTable::Driver* Driver = Slot.driver.empty() ? (GhostDrivers[g].IsEmpty() ? Drivers.find("BB") : nullptr) : Drivers.find(Slot.driver);
			const lr::ChampionTable::Champion* Champion = !Slot.driver.empty() && Driver ? Champions.find(Driver->champion) : nullptr;
			const lr::ChassisTable::Chassis* Chassis = ChassisTable.find(Champion ? Champion->chassis : Slot.chassis);
			if (Champion) {
				Ghosts[g][0] = Place(A, ModelPath(Champion->model, Common), Libs(Champion->materials, Champion->textures), true, false, 127.0f / 255);
			}
			if (Chassis) {
				Ghosts[g][2] = Place(A, ModelPath(Chassis->model, Common), Libs(Chassis->materials, Chassis->textures), true, false, 127.0f / 255);
			}
			if (Driver) {
				Ghosts[g][3] = Place(A, ModelPath(Driver->model, Common), Libs(Driver->materials, Driver->textures), true, false, 127.0f / 255);
			}
			A->SetActorHiddenInGame(true);
		}
	}

	// The power-ups' models: a brick and its glow per brick, the explosions.
	{
		AActor* Powerups = NewActor(FTransform::Identity);
		PowerupActor = Powerups;
		const FString Brick = UTF8_TO_TCHAR((Common + "PUBRICKY.GDB").c_str()), Glow = UTF8_TO_TCHAR((Common + "PUTRAILY.GDB").c_str());
		const FString White = UTF8_TO_TCHAR((Common + "ENHABRIK.GDB").c_str()), WhiteGlow = UTF8_TO_TCHAR((Common + "ENHANER.GDB").c_str());
		for (const lr::Brick& B : W.bricks) {
			Bricks.Add(Place(Powerups, B.colour ? Brick : White, PowerupLibraries, true));
			BrickGlows.Add(Place(Powerups, B.colour ? Glow : WhiteGlow, PowerupLibraries, false));
		}
		for (size_t e = 0; e < W.explosions.size(); e++) {
			Explosions.Add(Place(Powerups, UTF8_TO_TCHAR((Common + (e < 6 ? "HEDRA01" : "SPIKEXP") + ".GDB").c_str()), PowerupLibraries, true, false, 0.999f));
		}
	}
	BuildSky(W);
	// The warp tube: TURBO3.WDB's "dtube" and "dbricks", away from the track, shown only to a warping
	// player's view.
	{
		lr::SceneFile Tubes;
		if (Cache->get(Common + "TURBO3.WDB", Tubes)) {
			const TArray<std::pair<std::string, std::string>> Libraries = LibrariesOf(Tubes, Common);
			AActor* A = NewActor(FTransform::Identity);
			for (size_t e = 0; e < Tubes.animated.size() && e < 2; e++) {
				const lr::SceneFile::Entity& E = Tubes.animated[e];
				if (E.model >= 0 && size_t(E.model) < Tubes.models.size()) {
					Tube[e] = Place(A, UTF8_TO_TCHAR((Common + Upper(Tubes.models[size_t(E.model)]) + ".GDB").c_str()), Libraries, false);
					TubeModels[e] = Tubes.models[size_t(E.model)];
					TubePoses[e] = EntityPose(E, false);
					TubeScroll[e] = FVector2D(E.scroll[0], E.scroll[1]);
				}
			}
		}
	}
	bBuilt = true;
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
	ActionShown.Reset();
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

// The car's shadow texture: its models seen from above, flattened, in (8, 8, 8) on transparent black,
// over the chassis' shadow size x 1.15. (No file gives its size: 64 x 64, decided.)
void ULRRaceView::MakeShadow(int32 Racer, const lr::World& W)
{
	constexpr int32 Size = 64;
	const lr::RacerShape& Shape = W.shapes[size_t(Racer)];
	(void) Shape;
	TArray<uint8> Pixels;
	Pixels.Init(0, Size * Size * 4);
	FRacer& R = Racers[Racer];
	// Every member's triangles in the car's frame at rest (the body at the car, the driver at its mount).
	float Width = 8, Length = 9;
	{
		lr::ChassisTable Table;
		Cache->get(Common + "CHASSIS.CMB", Table);
		const lr::RaceBrief::Slot& Slot = W.brief.slots[size_t(Racer)];
		lr::ChampionTable Champions;
		lr::DriverTable Drivers;
		Cache->get(Common + "CHAMPS.CCB", Champions);
		Cache->get(Common + "DRIVERS.DDB", Drivers);
		const lr::DriverTable::Driver* Driver = Slot.driver.empty() ? nullptr : Drivers.find(Slot.driver);
		const lr::ChampionTable::Champion* Champion = Driver ? Champions.find(Driver->champion) : nullptr;
		if (const lr::ChassisTable::Chassis* C = Table.find(Champion ? Champion->chassis : Slot.chassis)) {
			Width = C->shadowWidth > 0 ? C->shadowWidth : Width;
			Length = C->shadowLength > 0 ? C->shadowLength : Length;
		}
	}
	const float HalfW = Width * 1.15f * 0.5f, HalfL = Length * 1.15f * 0.5f;
	const auto Splat = [&](const FVector& A, const FVector& B, const FVector& C) {
		// UE's local cm back to the car's feet: x forward, y right (UE) = -left.
		FVector2D P[3];
		const FVector* V[3] = {&A, &B, &C};
		for (int32 k = 0; k < 3; k++) {
			const float Fwd = float(V[k]->X) / Cm, Left = -float(V[k]->Y) / Cm;
			// The decal's u runs across (left), v along (forward): Show::project.
			P[k] = FVector2D((Left / (2 * HalfW) + 0.5f) * Size, (Fwd / (2 * HalfL) + 0.5f) * Size);
		}
		const float MinX = FMath::Max(0.0f, float(FMath::Min3(P[0].X, P[1].X, P[2].X))), MaxX = FMath::Min(float(Size - 1), float(FMath::Max3(P[0].X, P[1].X, P[2].X)));
		const float MinY = FMath::Max(0.0f, float(FMath::Min3(P[0].Y, P[1].Y, P[2].Y))), MaxY = FMath::Min(float(Size - 1), float(FMath::Max3(P[0].Y, P[1].Y, P[2].Y)));
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
	};
	for (int32 m = 0; m < 4; m++) {
		const FPlaced& P = R.Members[m];
		if (!P.Mesh) {
			continue;
		}
		const float S = P.Mesh->Scale;
		FVector Offset = FVector::ZeroVector;
		if (m == 3) {
			Offset = Ue(W.shapes[size_t(Racer)].mount);
		}
		for (const FLRModelMesh::FSection& Sec : P.Mesh->Sections) {
			for (int32 t = 0; t + 2 < Sec.Triangles.Num(); t += 3) {
				Splat(Sec.Positions[Sec.Triangles[t]] * S + Offset, Sec.Positions[Sec.Triangles[t + 1]] * S + Offset, Sec.Positions[Sec.Triangles[t + 2]] * S + Offset);
			}
		}
	}
	for (const TWeakObjectPtr<UProceduralMeshComponent>& C : R.Built) {
		if (!C.IsValid()) {
			continue;
		}
		for (int32 s = 0; s < C->GetNumSections(); s++) {
			const FProcMeshSection* Sec = C->GetProcMeshSection(s);
			for (int32 t = 0; Sec && t + 2 < Sec->ProcIndexBuffer.Num(); t += 3) {
				Splat(Sec->ProcVertexBuffer[Sec->ProcIndexBuffer[t]].Position, Sec->ProcVertexBuffer[Sec->ProcIndexBuffer[t + 1]].Position,
					Sec->ProcVertexBuffer[Sec->ProcIndexBuffer[t + 2]].Position);
			}
		}
	}
	UTexture2D* T = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8);
	T->SRGB = true;
	T->Filter = TF_Nearest; // (carshadN: point filtered)
	T->AddressX = T->AddressY = TA_Clamp;
	uint8* Mip = static_cast<uint8*>(T->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
	FMemory::Memcpy(Mip, Pixels.GetData(), Pixels.Num());
	T->GetPlatformData()->Mips[0].BulkData.Unlock();
	T->UpdateResource();
	Shadows.Add(T);
	R.Shadow = T;
}

// ---- The sky ----

namespace
{
// The dome: radius 100, 11 segments, rings at the horizon, 65 degrees from the zenith and about 33 (no top cap).
constexpr int32 SkySegments = 11;
constexpr float SkyRings[3] = {0, 41.5f, 84};

void SkyGeometry(TArray<FVector>& Positions, TArray<int32>& Triangles)
{
	for (int32 r = 0; r < 3; r++) {
		const float Radius = FMath::Sqrt(100 * 100 - SkyRings[r] * SkyRings[r]);
		for (int32 s = 0; s < SkySegments; s++) {
			const float A = 2 * PI * float(s) / SkySegments;
			Positions.Add(FVector(FMath::Cos(A) * Radius, -FMath::Sin(A) * Radius, SkyRings[r]) * Cm);
		}
	}
	for (int32 r = 0; r < 2; r++) {
		for (int32 s = 0; s < SkySegments; s++) {
			const int32 A0 = r * SkySegments + s, A1 = r * SkySegments + (s + 1) % SkySegments;
			const int32 B0 = A0 + SkySegments, B1 = A1 + SkySegments;
			// Both faces (seen from inside).
			for (const int32 V : {A0, A1, B1, A0, B1, B0, A0, B1, A1, A0, B0, B1}) {
				Triangles.Add(V);
			}
		}
	}
}
} // namespace

void ULRRaceView::BuildSky(const lr::World& W)
{
	(void) W;
	// The sky's libraries: its world's (the sky material: RACERFX's skymat).
	lr::RaceFile Race;
	const std::string Dir = "GAMEDATA/" + std::string(TCHAR_TO_UTF8(*Track)) + "/";
	Cache->get(Dir + std::string(TCHAR_TO_UTF8(*Track)) + ".RAB", Race);
	lr::SceneFile SkyWorld;
	if (!Race.skyWorld.empty() && Cache->size(Dir + lr::binaryName(Race.skyWorld)) && Cache->get(Dir + lr::binaryName(Race.skyWorld), SkyWorld)) {
		SkyLibraries = LibrariesOf(SkyWorld, Dir);
	}
	// (Made per view in Pose: each view's sky is re-centred on its eye.)
}

void ULRRaceView::PoseSky(int32 v, const lr::World& W, const lr::Show& Show)
{
	FView& V = Views[v];
	const lr::PlayerCamera& C = W.cameras[size_t(v)];
	if (!V.Sky.IsValid()) {
		AActor* A = NewActor(FTransform::Identity);
		V.Sky = A;
		UProceduralMeshComponent* Dome = NewObject<UProceduralMeshComponent>(A);
		Dome->SetMobility(EComponentMobility::Movable);
		Dome->SetCastShadow(false);
		Dome->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Dome->SetupAttachment(A->GetRootComponent());
		Dome->RegisterComponent();
		A->AddInstanceComponent(Dome);
		TArray<FVector> Positions;
		TArray<int32> Triangles;
		SkyGeometry(Positions, Triangles);
		TArray<FColor> Colours;
		Colours.Init(FColor::Black, Positions.Num());
		Dome->CreateMeshSection(0, Positions, Triangles, {}, {}, Colours, {}, false);
		bool bFlip;
		uint8 D[4];
		Dome->SetMaterial(0, MaterialFor("skymat", CommonLibraries, 1, bFlip, D));
		V.SkyDome.Add(Dome);
		// The sky's world: its models at the eye, 30 up.
		lr::RaceFile Race;
		const std::string Dir = "GAMEDATA/" + std::string(TCHAR_TO_UTF8(*Track)) + "/";
		lr::SceneFile SkyWorld;
		if (Cache->get(Dir + std::string(TCHAR_TO_UTF8(*Track)) + ".RAB", Race) && !Race.skyWorld.empty() &&
			Cache->size(Dir + lr::binaryName(Race.skyWorld)) && Cache->get(Dir + lr::binaryName(Race.skyWorld), SkyWorld)) {
			TArray<bool> Placed;
			Placed.Init(false, int32(SkyWorld.models.size()));
			const auto Add = [&](size_t m, const lr::Pose& Pose) {
				const std::string Own = Dir + Upper(SkyWorld.models[m]) + ".GDB";
				FPlaced P = Place(A, UTF8_TO_TCHAR((Cache->has(Own) ? Own : Common + Upper(SkyWorld.models[m]) + ".GDB").c_str()), SkyLibraries, false, bMirrored);
				if (!P.Mesh) {
					return;
				}
				lr::Pose At = Pose;
				At.position.z += 30;
				for (TWeakObjectPtr<UProceduralMeshComponent>& Part : P.Parts) {
					if (Part.IsValid()) {
						Part->SetRelativeTransform(FrameOf(Scaled(At.rotation, P.Mesh->Scale), At.position));
						V.SkyDome.Add(Part);
					}
				}
			};
			for (const lr::SceneFile::Entity& E : SkyWorld.entities) {
				if (E.model >= 0 && size_t(E.model) < SkyWorld.models.size()) {
					Add(size_t(E.model), EntityPose(E, bMirrored));
					Placed[E.model] = true;
				}
			}
			for (size_t m = 0; m < SkyWorld.models.size(); m++) {
				if (!Placed[int32(m)]) {
					Add(m, {});
				}
			}
		}
	}
	// Drawn behind everything: the whole sky scaled 1000 times about the eye; the dome 10 below it plus
	// the height offset.
	// Each view's dome and sky world around its own eye (split screen has no sky quirk).
	const FVector Eye = Ue(C.shownEye());
	V.Sky->SetActorTransform(FTransform(FQuat::Identity, Eye, FVector(1000)));
	V.Sky->SetActorHiddenInGame(!Show.hasSky);
	if (V.SkyDome.Num() && V.SkyDome[0].IsValid()) {
		UProceduralMeshComponent* Dome = V.SkyDome[0].Get();
		Dome->SetRelativeLocation(FVector(0, 0, (-10 + Show.sky.height) * Cm));
		Dome->SetVisibility(Show.domeShown);
		TArray<FColor> Colours;
		for (int32 r = 0; r < 3; r++) {
			const std::array<float, 3>& K = Show.skyColours[size_t(r)];
			for (int32 s = 0; s < SkySegments; s++) {
				Colours.Add(FColor(uint8(K[0] + 0.5f), uint8(K[1] + 0.5f), uint8(K[2] + 0.5f), 255));
			}
		}
		const FProcMeshSection* Sec = Dome->GetProcMeshSection(0);
		if (Sec) {
			TArray<FVector> Positions;
			for (const FProcMeshVertex& Pv : Sec->ProcVertexBuffer) {
				Positions.Add(Pv.Position);
			}
			Dome->UpdateMeshSection(0, Positions, {}, {}, Colours, {});
		}
		for (int32 k = 1; k < V.SkyDome.Num(); k++) {
			if (V.SkyDome[k].IsValid()) {
				V.SkyDome[k]->SetVisibility(Show.worldShown);
			}
		}
	}
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

// ---- Posing ----

void ULRRaceView::PoseRacers(const lr::World& W)
{
	for (int32 i = 0; i < Racers.Num(); i++) {
		FRacer& R = Racers[i];
		const bool bHidden = W.racers[size_t(i)].flags & lr::RacerGhost; // (warping: hidden)
		const lr::RacerShape& Shape = W.shapes[size_t(i)];
		const lr::RacerLook& Look = W.looks[size_t(i)];
		for (int32 m = 0; m < 4; m++) {
			FPlaced& P = R.Members[m];
			lr::Pose Pose;
			float Scale = 1;
			const bool bPresent = W.member(size_t(i), lr::RacerShape::Member(m), Pose, Scale) && !bHidden;
			TArray<TWeakObjectPtr<UProceduralMeshComponent>> All = P.Parts;
			if (m == 0) {
				All.Append(R.Built);
			}
			if (m == 3) {
				for (const FSkin& S : R.Driver) {
					All.Add(S.Component);
				}
			}
			for (TWeakObjectPtr<UProceduralMeshComponent>& C : All) {
				if (C.IsValid()) {
					C->SetVisibility(bPresent);
				}
			}
			if (!bPresent) {
				continue;
			}
			const int16 Figure = m == 1 ? Shape.variantAnimation : m == 2 ? Shape.carAnimation : m == 3 ? 0 : -1;
			const std::vector<lr::Pose>& Locals = m == 1 ? Look.variantNodes : m == 2 ? Look.carNodes : Look.figureNodes;
			TArray<FTransform> Nodes;
			TArray<FQuat> Rotations;
			if (Figure >= 0 && size_t(Figure) < W.skeletons.size() && !Locals.empty()) {
				std::vector<lr::NodeMatrix> Out;
				lr::placeNodes(W.skeletons[size_t(Figure)], Locals, Pose, Scale, Out);
				if (m == 3 && !R.Driver.IsEmpty()) {
					PoseRaceDriver(R.Driver, P, Out);
					continue;
				}
				for (const lr::NodeMatrix& N : Out) {
					Nodes.Add(FrameOf(N.rows, N.position));
					Rotations.Add(Nodes.Last().GetRotation());
				}
			}
			// A body without a skeleton: the model's scale too (the built car is drawn at 1: the core's bounds).
			const float Whole = m == 0 && P.Mesh ? Scale * P.Mesh->Scale : Scale;
			const FTransform T = FrameOf(Scaled(Pose.rotation, Whole), Pose.position);
			PoseNodes(P, Nodes, T);
			if (m == 0) {
				for (TWeakObjectPtr<UProceduralMeshComponent>& C : R.Built) {
					if (C.IsValid()) {
						C->SetWorldTransform(FrameOf(Scaled(Pose.rotation, Scale), Pose.position));
					}
				}
			}
			if (!Rotations.Num()) {
				Rotations.Add(T.GetRotation());
			}
			Relight(P, Rotations);
		}
	}
	// A time race's ghosts at their runs' poses: the car, its body on it, the driver at its mount.
	for (int32 g = 0; g < 2; g++) {
		const lr::GhostRun& Run = g == 0 ? W.timeRace.record : W.timeRace.best;
		const bool bValid = g == 0 ? W.timeRace.recordValid : W.timeRace.bestValid;
		lr::Vec3 At;
		lr::Quat Q;
		const bool bShown = bValid && !Run.finished && W.ghostAt(Run, At, Q);
		for (int32 m = 0; m < 4; m++) {
			FPlaced& P = Ghosts[g][m];
			if (!P.Mesh) {
				continue;
			}
			TArray<TWeakObjectPtr<UProceduralMeshComponent>> All = P.Parts;
			if (m == 3) {
				for (const FSkin& S : GhostDrivers[g]) {
					All.Add(S.Component);
				}
			}
			for (TWeakObjectPtr<UProceduralMeshComponent>& C : All) {
				if (C.IsValid()) {
					C->SetVisibility(bShown);
				}
			}
			if (P.Actor.IsValid()) {
				P.Actor->SetActorHiddenInGame(!bShown);
			}
			if (!bShown || Racers.IsEmpty()) {
				continue;
			}
			const lr::Basis B = lr::basisOf(Q);
			const lr::RacerShape& Shape = W.shapes[0];
			lr::Pose Pose{At, B};
			float Scale = m == 2 ? Shape.carScale : m == 3 ? Shape.driverScale : P.Mesh->Scale;
			if (m == 3) {
				Pose.position = lr::toWorld(B, Shape.mount) + At;
			}
			TArray<FTransform> Nodes;
			const int16 Figure = m == 2 ? Shape.carAnimation : m == 3 ? 0 : -1;
			if (Figure >= 0 && size_t(Figure) < W.skeletons.size()) {
				std::vector<lr::NodeMatrix> Out;
				lr::poseSkeleton(W.skeletons[size_t(Figure)], &W.figures[size_t(Figure)], {0, 0, true}, Pose, Scale, Out);
				if (m == 3 && !GhostDrivers[g].IsEmpty()) {
					PoseRaceDriver(GhostDrivers[g], P, Out);
					continue;
				}
				for (const lr::NodeMatrix& N : Out) {
					Nodes.Add(FrameOf(N.rows, N.position));
				}
			}
			PoseNodes(P, Nodes, FrameOf(Scaled(B, Scale), At));
		}
	}
}

void ULRRaceView::PoseScenery(const lr::World& W)
{
	for (int32 k = 0; k < Animated.Num() && size_t(k) < W.track.animated.size(); k++) {
		FPlaced& P = Animated[k];
		if (!P.Mesh) {
			continue;
		}
		const lr::AnimatedModel& A = W.track.animated[size_t(k)];
		const lr::Clock& Clock = W.clocks[size_t(k)];
		TArray<FTransform> Nodes;
		if (!A.nodes.empty()) {
			std::vector<lr::NodeMatrix> Out;
			const lr::Animation* Anim = A.animation >= 0 && size_t(A.animation) < W.track.animations.size() ? &W.track.animations[size_t(A.animation)] : nullptr;
			lr::poseSkeleton(A.nodes, Anim, {Clock.part, Clock.time, Clock.loop}, A.pose, A.scale, Out);
			for (const lr::NodeMatrix& N : Out) {
				Nodes.Add(FrameOf(N.rows, N.position));
			}
		}
		PoseNodes(P, Nodes, FrameOf(Scaled(A.pose.rotation, A.scale), A.pose.position));
	}
	// The plain models' draw distances as the core has them now: a model the core
	// hides (-1) is drawn by no camera (the rocket's two looks, C0R3's laser columns, C2R3's beamdumy); the others are
	// shown or hidden per camera by the core's rule as each view is captured (CaptureViews: World::plainInReach), never by
	// UE's cull distance, which the view distance scale shortens at lower qualities (the rocket from 400 ft at 0.8).
	if (PlainDistances.Num() != int32(W.modelDistances.size())) {
		PlainDistances.Init(-2, int32(W.modelDistances.size())); // (none applied yet)
	}
	for (int32 m = 0; m < PlainDistances.Num() && m < PlainOfEntity.Num(); m++) {
		const std::array<float, 3>& D = W.modelDistances[size_t(m)];
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
	// Material animation: a binding's shown frame swaps its slot's material (by texture, on the entity's own instances);
	// texture scroll: the entity's uv offset.
	if (ShownFrames.Num() != int32(W.track.materialBindings.size())) {
		ShownFrames.Init(0, int32(W.track.materialBindings.size()));
	}
	const auto PlacedOf = [&](bool bAnimated, int32 Entity) -> FPlaced* {
		if (bAnimated) {
			return Animated.IsValidIndex(Entity) ? &Animated[Entity] : nullptr;
		}
		return PlainOfEntity.IsValidIndex(Entity) && Plain.IsValidIndex(PlainOfEntity[Entity]) ? &Plain[PlainOfEntity[Entity]] : nullptr;
	};
	const auto EachSection = [&](FPlaced& P, const std::string* Only, const TFunctionRef<void(UProceduralMeshComponent*, int32, const FLRModelMesh::FSection&)>& Do) {
		TArray<int32> SectionsOf;
		SectionsOf.Init(0, P.Mesh->Parts);
		for (const FLRModelMesh::FSection& S : P.Mesh->Sections) {
			const int32 Index = SectionsOf[S.Part]++;
			if (P.Parts[S.Part].IsValid() && (!Only || S.Material == *Only)) {
				Do(P.Parts[S.Part].Get(), Index, S);
			}
		}
	};
	for (size_t b = 0; b < W.track.materialBindings.size(); b++) {
		const lr::Track::MaterialBinding& Bind = W.track.materialBindings[b];
		// (A binding shows its track's frame, the track's cursor; the code display's are re-rated and
		// stopped by the core)
		const int16 Frame = size_t(Bind.track) < W.materials.size() ? W.materials[size_t(Bind.track)].shown : int16(-1);
		if (ShownFrames[int32(b)] == Frame + 1) {
			continue;
		}
		ShownFrames[int32(b)] = Frame + 1;
		FPlaced* P = PlacedOf(Bind.animated, Bind.entity);
		const lr::Track::MaterialTrack& T = W.track.materialTracks[size_t(Bind.track)];
		if (!P || !P->Mesh || Bind.slot < 0 || size_t(Bind.slot) >= P->Mesh->MaterialNames.size() || Frame < 0 || size_t(Frame) >= T.frames.size()) {
			continue;
		}
		const std::string& Slot = P->Mesh->MaterialNames[size_t(Bind.slot)];
		bool bFlip;
		uint8 D[4];
		UMaterialInstanceDynamic* Shown = MaterialFor(T.frames[size_t(Frame)].first, TrackLibraries, 1, bFlip, D);
		EachSection(*P, &Slot, [&](UProceduralMeshComponent* C, int32 Index, const FLRModelMesh::FSection&) { C->SetMaterial(Index, Shown); });
	}
	for (size_t s = 0; s < W.scrollOffsets.size() && s < W.track.scrolls.size(); s++) {
		const lr::Track::Scroll& Sc = W.track.scrolls[s];
		FPlaced* P = PlacedOf(Sc.animated, Sc.entity);
		if (!P || !P->Mesh) {
			continue;
		}
		const FLinearColor Offset(float(W.scrollOffsets[s][0]) / 65536.0f, float(W.scrollOffsets[s][1]) / 65536.0f, 0, 0);
		EachSection(*P, nullptr, [&](UProceduralMeshComponent* C, int32 Index, const FLRModelMesh::FSection&) {
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

void ULRRaceView::PosePowerups(const lr::World& W)
{
	// Bricks: the four shared entities (the colour brick's cube and streak, the white brick's cube and
	// streaks), each playing its own part 0 from the race's load on the core's clock, posed by the core at each brick's
	// place and size; the colour's materials in slot 0 (P, M, S, T: red, yellow, blue, green).
	for (int32 b = 0; b < Bricks.Num() && size_t(b) < W.bricks.size(); b++) {
		const lr::Brick& B = W.bricks[size_t(b)];
		const float Size = B.size();
		for (int32 m = 0; m < 2; m++) {
			FPlaced* P = m == 0 ? &Bricks[b] : &BrickGlows[b];
			if (!P->Mesh) {
				continue;
			}
			const bool bShown = Size > 0;
			const lr::Track::BrickModel Which = B.colour ? (m == 0 ? lr::Track::BrickCube : lr::Track::BrickStreak)
														 : (m == 0 ? lr::Track::WhiteCube : lr::Track::WhiteStreaks);
			TArray<FTransform> Nodes;
			if (bShown) {
				std::vector<lr::NodeMatrix> Out;
				W.brickNodes(Which, B.position, Size, Out);
				for (const lr::NodeMatrix& N : Out) {
					Nodes.Add(FrameOf(N.rows, N.position));
				}
			}
			for (TWeakObjectPtr<UProceduralMeshComponent>& C : P->Parts) {
				if (C.IsValid()) {
					C->SetVisibility(bShown);
				}
			}
			if (bShown) {
				PoseNodes(*P, Nodes, FrameOf(Scaled(lr::Basis{}, Size * P->Mesh->Scale), B.position));
				TArray<FQuat> Rotations;
				for (const FTransform& N : Nodes) {
					Rotations.Add(N.GetRotation());
				}
				if (Rotations.IsEmpty()) {
					Rotations.Add(FQuat::Identity);
				}
				Relight(*P, Rotations);
			}
			if (B.colour && bShown) {
				// The colour's material: the brick "pbrick" + P, M, S, T (red, yellow, blue, green), its streak "ptrail" + the same,
				// put in slot 0 of the shared models' own tables (both files name ptrailM there: the slot, not the name).
				static const char Letters[5] = {'P', 'P', 'S', 'T', 'M'};
				const std::string Wanted = std::string(m == 0 ? "pbrick" : "ptrail") + Letters[FMath::Min<int32>(B.colour, 4)];
				const std::string Slot0 = P->Mesh->MaterialNames.empty() ? std::string() : P->Mesh->MaterialNames[0];
				TArray<int32> SectionsOf;
				SectionsOf.Init(0, P->Mesh->Parts);
				for (int32 sc = 0; sc < P->Mesh->Sections.Num(); sc++) {
					const int32 Part = P->Mesh->Sections[sc].Part;
					const int32 Index = SectionsOf[Part]++;
					bool bFlip;
					uint8 D[4];
					UProceduralMeshComponent* C = P->Parts[Part].Get();
					if (C && P->Mesh->Sections[sc].Material == Slot0) {
						C->SetMaterial(Index, MaterialFor(Wanted, PowerupLibraries, 1, bFlip, D));
					}
				}
			}
		}
	}
	// Explosions: scaled (1 - 0.05) g + 0.05 (at most 1), alpha (1 - g) 255.
	for (int32 e = 0; e < Explosions.Num() && size_t(e) < W.explosions.size(); e++) {
		const lr::Explosion& X = W.explosions[size_t(e)];
		FPlaced& P = Explosions[e];
		if (!P.Mesh) {
			continue;
		}
		const bool bShown = X.state == lr::Explosion::Exploding;
		const float S = FMath::Min(1.0f, (1 - 0.05f) * X.growth + 0.05f);
		for (TWeakObjectPtr<UProceduralMeshComponent>& C : P.Parts) {
			if (C.IsValid()) {
				C->SetVisibility(bShown);
				C->SetWorldTransform(FrameOf(Scaled(lr::Basis{}, S * P.Mesh->Scale), X.centre));
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

// ---- A view's effects: particles facing its camera, decals (its shadows' alpha) ----

void ULRRaceView::BuildEffects(int32 v, const lr::World& W, const lr::Show& Show)
{
	FView& V = Views[v];
	if (!V.Effects.IsValid()) {
		AActor* A = NewActor(FTransform::Identity);
		UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(A);
		C->SetMobility(EComponentMobility::Movable);
		C->SetCastShadow(false);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->bUseAsyncCooking = false;
		C->SetReceivesDecals(false);
		C->SetupAttachment(A->GetRootComponent());
		C->RegisterComponent();
		A->AddInstanceComponent(C);
		V.Effects = C;
	}
	UProceduralMeshComponent* C = V.Effects.Get();
	C->ClearAllMeshSections();
	struct FBatch
	{
		UMaterialInterface* Material;
		TArray<FVector> P;
		TArray<FVector2D> Uv;
		TArray<FColor> Colour;
		TArray<int32> T;
	};
	TArray<FBatch> Batches;
	const auto BatchOf = [&](UMaterialInterface* M) -> FBatch& {
		for (FBatch& B : Batches) {
			if (B.Material == M) {
				return B;
			}
		}
		FBatch& B = Batches.AddDefaulted_GetRef();
		B.Material = M;
		return B;
	};
	const lr::PlayerCamera& Cam = W.cameras[size_t(v)];
	lr::Vec3 Right, Down, Look;
	Cam.shownRows(Right, Down, Look);
	const lr::Vec3 Eye = Cam.shownEye();
	// Particles: a quad each, centred, along the camera's rows, uvs 0.01..0.98, coloured by the material's diffuse.
	TArray<std::pair<std::string, std::string>> TrackSet;
	for (const std::string& Mdb : EmitterLibraries[1]) {
		TrackSet.Add({Mdb, Mdb.substr(0, Mdb.size() - 3) + "TDB"});
	}
	for (size_t s = 0; s < 2; s++) {
		const lr::EffectSet& Set = Show.sets[s];
		for (const lr::EffectSlot& Slot : Set.slots) {
			if (!Slot.active || Slot.look < 0) {
				continue;
			}
			const float Radius = Set.file.emitters[size_t(Slot.look)].radius;
			if (Radius > 0 && Slot.anchored && lr::lengthSquared(Eye - Slot.anchor) > Radius * Radius) {
				continue;
			}
			for (const lr::ShowParticle& Pt : Slot.particles) {
				float Width, Height;
				std::string Material;
				if (!Show.particleLook(s, Slot, Pt, Width, Height, Material)) {
					continue;
				}
				bool bFlip;
				uint8 D[4];
				UMaterialInstanceDynamic* Mid = MaterialFor(Material, s == 0 ? CommonLibraries : TrackSet, 1, bFlip, D, true);
				FBatch& B = BatchOf(Mid);
				const int32 First = B.P.Num();
				const lr::Vec3 X = Right * (Width * 0.5f), Y = Down * (Height * 0.5f);
				const lr::Vec3 Corners[4] = {Pt.position - X - Y, Pt.position + X - Y, Pt.position + X + Y, Pt.position - X + Y};
				const FVector2D Uvs[4] = {{0.01, 0.01}, {0.98, 0.01}, {0.98, 0.98}, {0.01, 0.98}};
				for (int32 k = 0; k < 4; k++) {
					B.P.Add(Ue(Corners[k]));
					B.Uv.Add(bFlip ? FVector2D(Uvs[k].X, 1 - Uvs[k].Y) : Uvs[k]);
					B.Colour.Add(FColor(D[0], D[1], D[2], D[3]));
				}
				for (const int32 k : {0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2}) {
					B.T.Add(First + k);
				}
			}
		}
	}
	// Billboards: the normal explosions' flashes, the cannonballs.
	const auto Billboard = [&](const lr::Vec3& At, float Width, float Height, const std::string& Material, uint8 Alpha) {
		bool bFlip;
		uint8 D[4];
		TArray<std::pair<std::string, std::string>> Libraries = CommonLibraries;
		Libraries.Append(PowerupLibraries);
		FBatch& B = BatchOf(MaterialFor(Material, Libraries, 1, bFlip, D, true));
		const int32 First = B.P.Num();
		const lr::Vec3 X = Right * (Width * 0.5f), Y = Down * (Height * 0.5f);
		const lr::Vec3 Corners[4] = {At - X - Y, At + X - Y, At + X + Y, At - X + Y};
		const FVector2D Uvs[4] = {{0.01, 0.01}, {0.98, 0.01}, {0.98, 0.98}, {0.01, 0.98}};
		for (int32 k = 0; k < 4; k++) {
			B.P.Add(Ue(Corners[k]));
			B.Uv.Add(bFlip ? FVector2D(Uvs[k].X, 1 - Uvs[k].Y) : Uvs[k]);
			B.Colour.Add(FColor(D[0], D[1], D[2], uint8((int32(D[3]) * Alpha) / 255)));
		}
		for (const int32 k : {0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2}) {
			B.T.Add(First + k);
		}
	};
	for (size_t e = 0; e < 6 && e < W.explosions.size(); e++) {
		const lr::Explosion& X = W.explosions[e];
		if (X.state == lr::Explosion::Exploding) {
			const float G = X.growth;
			Billboard(X.centre, (15 - 0.1f) * G + 0.1f, (15 - 0.2f) * G + 0.2f, "exflash", uint8(FMath::Clamp((2 - 2 * G) * 255, 0.0f, 255.0f)));
		}
	}
	for (const uint8 Index : W.actionList) {
		const lr::Action& A = W.actions[Index];
		if (A.kind == lr::Action::Cannon && A.shot.state == lr::Projectile::Flying) {
			Billboard(A.shot.at, 5, 5, "cannon", 255);
		}
	}

	// The world's sprites: in reach of this eye, turned to this camera, masked and depth-tested like the
	// scenery.
	for (const lr::Sprite& S : W.track.sprites) {
		if (!S.inReach(Eye)) {
			continue;
		}
		const std::string* Name = &S.material;
		if (S.track >= 0) {
			const lr::Track::MaterialTrack& Frames = W.track.materialTracks[size_t(S.track)];
			const int16 Shown = W.materials[size_t(S.track)].shown;
			if (Shown < 0 || size_t(Shown) >= Frames.frames.size()) {
				continue;
			}
			Name = &Frames.frames[size_t(Shown)].first;
		}
		FSpriteLook& Look_ = SpriteLooks.FindOrAdd(UTF8_TO_TCHAR(Name->c_str()));
		if (!Look_.Material) {
			uint8 D[4];
			Look_.Material = MaterialFor(*Name, TrackLibraries, 1, Look_.bFlip, D);
		}
		FBatch& B = BatchOf(Look_.Material);
		AddSprite(S.corners(Right, Down, Look), Look_.bFlip, B.P, B.Uv, B.Colour, B.T);
	}

	// Decals: projected triangles, white times their alpha (a shadow's by this camera's distance).
	for (const lr::Decal& D : Show.decals) {
		UMaterialInterface* M = nullptr;
		uint8 Alpha = D.alpha;
		bool bFlip = false;
		if (D.kind == lr::Decal::Shadow) {
			if (!Racers.IsValidIndex(D.owner) || !Racers[D.owner].Shadow.IsValid()) {
				continue;
			}
			Alpha = Show.shadowAlpha[size_t(D.owner)][size_t(v)];
			const FString Key = FString::Printf(TEXT("shadow%d"), D.owner);
			TObjectPtr<UMaterialInstanceDynamic>* Found = Materials.Find(Key);
			if (!Found) {
				UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(TranslucentBase, this);
				Mid->SetTextureParameterValue(TEXT("Tex"), Racers[D.owner].Shadow.Get());
				Mid->SetScalarParameterValue(TEXT("Alpha"), 180.0f / 255);
				Mid->SetScalarParameterValue(TEXT("Pull"), 0.005f);
				Found = &Materials.Add(Key, Mid);
			}
			M = *Found;
		}
		else {
			uint8 Dif[4];
			TArray<std::pair<std::string, std::string>> Libraries = CommonLibraries;
			Libraries.Append(PowerupLibraries);
			UMaterialInstanceDynamic* Mid = MaterialFor(D.material, Libraries, 1, bFlip, Dif, true);
			const FString Key = FString::Printf(TEXT("decal:%hs"), D.material.c_str());
			TObjectPtr<UMaterialInstanceDynamic>* Found = Materials.Find(Key);
			if (!Found) {
				// (A copy of its instance, pulled toward the eye.)
				UMaterialInstanceDynamic* Pulled = UMaterialInstanceDynamic::Create(Mid->Parent, this);
				UTexture* Tex = nullptr;
				if (Mid->GetTextureParameterValue(TEXT("Tex"), Tex) && Tex) {
					Pulled->SetTextureParameterValue(TEXT("Tex"), Tex);
				}
				float A = 1;
				Mid->GetScalarParameterValue(TEXT("Alpha"), A);
				Pulled->SetScalarParameterValue(TEXT("Alpha"), A);
				Pulled->SetScalarParameterValue(TEXT("Pull"), 0.005f);
				Found = &Materials.Add(Key, Pulled);
			}
			M = *Found;
		}
		if (Alpha == 0 || D.points.empty()) {
			continue;
		}
		FBatch& B = BatchOf(M);
		for (size_t k = 0; k + 2 < D.points.size(); k += 3) {
			const int32 First = B.P.Num();
			for (size_t c = 0; c < 3; c++) {
				B.P.Add(Ue(D.points[k + c]));
				B.Uv.Add(FVector2D(D.uvs[k + c][0], bFlip ? 1 - D.uvs[k + c][1] : D.uvs[k + c][1]));
				B.Colour.Add(FColor(255, 255, 255, Alpha));
			}
			for (const int32 i : {0, 1, 2, 0, 2, 1}) {
				B.T.Add(First + i);
			}
		}
	}
	for (int32 b = 0; b < Batches.Num(); b++) {
		C->CreateMeshSection(b, Batches[b].P, Batches[b].T, {}, Batches[b].Uv, Batches[b].Colour, {}, false);
		C->SetMaterial(b, Batches[b].Material);
	}
}

void ULRRaceView::AddSprite(const std::array<lr::Vec3, 4>& Corners, bool bFlip, TArray<FVector>& P, TArray<FVector2D>& Uv, TArray<FColor>& Colour, TArray<int32>& T)
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

// ---- A tick's views ----

void ULRRaceView::Pose(const lr::World& W, const lr::Show& Show, float WindowScale, float WindowAspect)
{
	if (!bBuilt || !WorldRef.IsValid()) {
		return;
	}
	ShowClockMs = Show.clockMs;
	SmoothBegin(); // (frame smoothing: what this pose moves, from where; nothing when off)
	PoseRacers(W);
	PoseScenery(W);
	PosePowerups(W);
	PoseActions(W);
	const int32 Count = int32(W.cameras.size());
	while (Views.Num() < Count) {
		FView& V = Views.AddDefaulted_GetRef();
		AActor* A = NewActor(FTransform::Identity);
		USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(A);
		Capture->bCaptureEveryFrame = false;
		Capture->bCaptureOnMovement = false;
		Capture->bAlwaysPersistRenderingState = true;
		Capture->CompositeMode = SCCM_Overwrite;
		ConfigureCapture(Capture);
		Capture->bOverride_CustomNearClippingPlane = true;
		Capture->CustomNearClippingPlane = 5 * Cm;
		Capture->SetupAttachment(A->GetRootComponent());
		Capture->RegisterComponent();
		V.Capture = Capture;
	}
	for (int32 v = 0; v < Count; v++) {
		PoseSky(v, W, Show);
		BuildEffects(v, W, Show);
	}
	// (Split screen is split from race frame 1)
	for (int32 v = 0; v < Count; v++) {
		FView& V = Views[v];
		const lr::PlayerCamera& Cam = W.cameras[size_t(v)];
		V.Rect = FIntRect(Cam.viewport[0], Cam.viewport[1], Cam.viewport[2], Cam.viewport[3]);
		// A wide one-player race fills the window's width.
		const bool bWide = Count == 1 && WindowAspect > 4.0f / 3 + 0.01f;
		const float Width = bWide ? V.Rect.Height() * WindowAspect : float(V.Rect.Width());
		const float Scale = WindowScale * Settings.Scale; // (the render scale: the target drawn over the viewport, filtered)
		const FIntPoint Size(FMath::Max(1, FMath::RoundToInt(Width * Scale)), FMath::Max(1, FMath::RoundToInt(V.Rect.Height() * Scale)));
		UTextureRenderTarget2D* Target = V.Target.Get();
		if (!Target || Target->SizeX != Size.X || Target->SizeY != Size.Y) {
			Target = NewObject<UTextureRenderTarget2D>(this);
			Target->RenderTargetFormat = RTF_RGBA8_SRGB;
			Target->ClearColor = FLinearColor::Black;
			Target->InitAutoFormat(Size.X, Size.Y);
			Target->UpdateResourceImmediate(true);
			Targets.Add(Target);
			V.Target = Target;
		}
		USceneCaptureComponent2D* Capture = V.Capture.Get();
		Capture->TextureTarget = Target;
		// What this camera doesn't draw: the other views' skies and effects, the animated scenery it didn't draw.
		Capture->HiddenComponents.Reset();
		for (int32 o = 0; o < Views.Num(); o++) {
			if (o == v) {
				continue;
			}
			if (Views[o].Effects.IsValid()) {
				Capture->HiddenComponents.Add(Views[o].Effects);
			}
			for (const TWeakObjectPtr<UProceduralMeshComponent>& S : Views[o].SkyDome) {
				Capture->HiddenComponents.Add(S);
			}
		}
		for (const FPlaced& P : Tube) {
			for (const TWeakObjectPtr<UProceduralMeshComponent>& Part : P.Parts) {
				if (Part.IsValid() && !Cam.hidden) {
					Capture->HiddenComponents.Add(Part);
				}
			}
		}
		// The plain models out of this camera's reach (the eye to the entity's position, squared, against
		// the core's stored levels; UE's frustum culling does the rest).
		for (int32 m = 0; m < PlainOfEntity.Num() && size_t(m) < W.modelDistances.size(); m++) {
			if (!Plain.IsValidIndex(PlainOfEntity[m]) || W.plainInReach(size_t(m), Cam.shownEye())) {
				continue;
			}
			for (const TWeakObjectPtr<UProceduralMeshComponent>& Part : Plain[PlainOfEntity[m]].Parts) {
				if (Part.IsValid()) {
					Capture->HiddenComponents.Add(Part);
				}
			}
		}
		// The bricks' shared entities' draw distance (400 ft from this camera's eye).
		for (int32 b = 0; b < Bricks.Num() && size_t(b) < W.bricks.size(); b++) {
			const lr::Brick& B = W.bricks[size_t(b)];
			const lr::Track::BrickModel Which = B.colour ? lr::Track::BrickCube : lr::Track::WhiteCube;
			if (lr::World::withinDistance(W.track.brickModels[Which].distances, lr::lengthSquared(B.position - Cam.shownEye()))) {
				continue;
			}
			for (FPlaced* P : {&Bricks[b], &BrickGlows[b]}) {
				for (const TWeakObjectPtr<UProceduralMeshComponent>& Part : P->Parts) {
					if (Part.IsValid()) {
						Capture->HiddenComponents.Add(Part);
					}
				}
			}
		}
		for (int32 k = 0; k < Animated.Num() && size_t(k) < W.drawnBy.size(); k++) {
			if (!(W.drawnBy[size_t(k)] & (1u << v))) {
				for (const TWeakObjectPtr<UProceduralMeshComponent>& Part : Animated[k].Parts) {
					if (Part.IsValid()) {
						Capture->HiddenComponents.Add(Part);
					}
				}
			}
		}
		lr::Vec3 Right, Down, Look;
		Cam.shownRows(Right, Down, Look);
		lr::Vec3 EyeAt = Cam.shownEye();
		float Fov = Cam.fov;
		Capture->ShowOnlyComponents.Reset();
		Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
		if (Cam.hidden) {
			// Warping: only the tube, its parts at 1500 - the ms left, the camera snapped behind a car at the origin facing
			// (0, -1, 0) (the chase view's pose; FOV 45 cos(pi ms left / 1500) + base).
			int32 Left = 0;
			for (const uint8 Index : W.actionList) {
				const lr::Action& A = W.actions[Index];
				if (A.kind == lr::Action::Warp && A.owner == Cam.racer) {
					Left = int32(A.ms);
				}
			}
			for (int32 e = 0; e < 2; e++) {
				FPlaced& P = Tube[e];
				if (!P.Mesh) {
					continue;
				}
				const FFigure* F = Figure(TubeModels[e]);
				TArray<FTransform> Nodes;
				if (F && !F->Nodes.empty()) {
					std::vector<lr::NodeMatrix> Out;
					const lr::Animation* An = F->bAnimated ? &F->Animation : nullptr;
					const float Frames = An ? An->parts[0].rate * float(1500 - Left) : 0;
					lr::poseSkeleton(F->Nodes, An, {An ? 0 : -1, Frames, true}, TubePoses[e], P.Mesh->Scale, Out);
					for (const lr::NodeMatrix& N : Out) {
						Nodes.Add(FrameOf(N.rows, N.position));
					}
				}
				PoseNodes(P, Nodes, FrameOf(Scaled(TubePoses[e].rotation, P.Mesh->Scale), TubePoses[e].position));
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
		Capture->SetWorldLocationAndRotation(Ue(EyeAt), FRotationMatrix::MakeFromXZ(Along(Look), -Along(Down)).Rotator());
		// The horizontal field from the vertical by the viewport's aspect. A wide one-player race's spans
		// the window Hor+: tan(h/2) = tan(v/2) A, A the window's aspect (a 4:3 window gives 4/3).
		const float Aspect = bWide ? WindowAspect : float(V.Rect.Width()) / float(FMath::Max(1, V.Rect.Height()));
		Capture->FOVAngle = FMath::RadiansToDegrees(2.0f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(Fov) * 0.5f) * Aspect));
	}
	if (bSmoothing) {
		SmoothEnd(); // (Smooth captures)
		return;
	}
	CaptureViews();
}

void ULRRaceView::CaptureViews()
{
	for (FView& V : Views) {
		if (V.Capture.IsValid() && V.Capture->TextureTarget) {
			V.Capture->CaptureScene();
		}
	}
}


// ---- The actions' models ----

const ULRRaceView::FFigure* ULRRaceView::Figure(const std::string& Model)
{
	const FString Key = UTF8_TO_TCHAR(Model.c_str());
	if (FFigure** Found = Figures.Find(Key)) {
		return *Found;
	}
	FFigure* F = new FFigure;
	lr::SkeletonFile Nodes;
	lr::AnimationFile Anim;
	if (Cache->get(Common + Upper(Model) + ".SDB", Nodes)) {
		for (const lr::SkeletonFile::Node& N : Nodes.nodes) {
			int16 Parent = -1;
			for (size_t k = 0; k < Nodes.nodes.size(); k++) {
				Parent = !N.parent.empty() && Nodes.nodes[k].name == N.parent ? int16(k) : Parent;
			}
			F->Nodes.push_back({lr::vec(N.position), lr::basisOf({N.rotation[0], N.rotation[1], N.rotation[2], N.rotation[3]}), Parent});
		}
	}
	if (Cache->get(Common + Upper(Model) + ".ADB", Anim)) {
		F->Animation = lr::animationOf(Anim, false);
		F->bAnimated = !F->Animation.parts.empty();
	}
	Figures.Add(Key, F);
	return F;
}

namespace
{
// An action's slot is reused by other kinds and levels: their models differ (a shield's colour is its model's).
FString ActionKey(int32 Action, int32 Role, const std::string& Model)
{
	return FString::Printf(TEXT("%d/%d/%hs"), Action, Role, Model.c_str());
}
} // namespace

ULRRaceView::FPlaced& ULRRaceView::ActionModel(int32 Action, int32 Role, const std::string& Model)
{
	const FString Key = ActionKey(Action, Role, Model);
	if (FPlaced* Found = ActionModels.Find(Key)) {
		return *Found;
	}
	return ActionModels.Add(Key, Place(PowerupActor, UTF8_TO_TCHAR((Common + Upper(Model) + ".GDB").c_str()), PowerupLibraries, true));
}

void ULRRaceView::PoseActions(const lr::World& W)
{
	TSet<FString> Shown;
	// A model at a pose (a skeleton's part at a time, if it has one), with its parts' alpha; its uv offset.
	const auto Show = [&](int32 Action, int32 Role, const std::string& Model, const lr::Pose& At, float Scale, int32 Part, float TimeMs, float Alpha,
						  FVector2D Scroll) {
		FPlaced& P = ActionModel(Action, Role, Model);
		if (!P.Mesh) {
			return;
		}
		Shown.Add(ActionKey(Action, Role, Model));
		const FFigure* F = Figure(Model);
		TArray<FTransform> Nodes;
		if (F && !F->Nodes.empty()) {
			std::vector<lr::NodeMatrix> Out;
			const lr::Animation* A = F->bAnimated ? &F->Animation : nullptr;
			const float Frames = A && Part >= 0 && size_t(Part) < A->parts.size() ? A->parts[size_t(Part)].rate * TimeMs : 0;
			lr::poseSkeleton(F->Nodes, A, {A ? Part : -1, Frames, true}, At, Scale * P.Mesh->Scale, Out);
			for (const lr::NodeMatrix& N : Out) {
				Nodes.Add(FrameOf(N.rows, N.position));
			}
		}
		PoseNodes(P, Nodes, FrameOf(Scaled(At.rotation, Scale * P.Mesh->Scale), At.position));
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
					Mid = UMaterialInstanceDynamic::Create(Alpha < 1 ? TranslucentBase.Get() : Shared->Parent.Get(), C.Get());
					Mid->CopyParameterOverrides(Shared);
					C->SetMaterial(m, Mid);
				}
				if (Mid && Mid->GetOuter() == C) {
					Mid->SetScalarParameterValue(TEXT("Alpha"), Alpha);
					Mid->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(float(Scroll.X), float(Scroll.Y), 0, 0));
				}
			}
		}
	};
	const float Seconds = float(W.session.ms) / 1000;
	for (const uint8 Index : W.actionList) {
		const lr::Action& A = W.actions[Index];
		const int32 i = int32(Index);
		if (A.owner < 0 || size_t(A.owner) >= W.visible.size()) {
			if (A.kind != lr::Action::Dynamite && A.kind != lr::Action::Missile && A.kind != lr::Action::Cannon) {
				continue;
			}
		}
		const lr::Pose* Car = A.owner >= 0 && size_t(A.owner) < W.visible.size() ? &W.visible[size_t(A.owner)] : nullptr;
		switch (A.kind) {
		case lr::Action::Turbo: {
			// The burn on the car (part 0 lighting, 1 boosting, 2 fading) and two flames behind it.
			if (!Car || A.state < 2 || A.state > 4 || (W.racers[size_t(A.owner)].flags & lr::RacerGhost)) {
				break;
			}
			const int32 Level = FMath::Min<int32>(A.level, 2);
			static const char* Burns[3] = {"DT0BURN", "DT1BURN", "DT2CBURN"};
			static const char* Flames[3][2] = {{"DT0FLA1", "DT0FLA2"}, {"DT1FLA1", "DT1FLA2"}, {"DT2CFLA1", "DT2CFLA2"}};
			const int32 Part = A.state - 2;
			const float Alpha = A.state == 4 ? FMath::Max(0.0f, float(int32(A.ms)) - 350) / 350 * 191 / 255 : 1.0f;
			// The burn and both flames share one frame: the chassis car's (not
			// the leaned body's), turned a quarter about its up (rows left, back, up), 2 back and 3 up from it.
			lr::Pose Chassis;
			float ChassisScale = 1;
			const lr::Pose& C = W.member(size_t(A.owner), lr::RacerShape::Member(2), Chassis, ChassisScale) ? Chassis : *Car;
			const lr::Basis& B = C.rotation;
			const lr::Pose At{C.position + B.f * -2 + B.u * 3, {B.s, B.f * -1, B.u}};
			// Burn and flames play one part (lit, boosting, fading) from the state's start.
			static const float Boosts[3] = {1000, 1500, 5000};
			const float Elapsed = (A.state == 2 ? 400 : A.state == 3 ? Boosts[Level] : 700) - float(A.ms);
			Show(i, 0, Burns[Level], At, 1, Part, Elapsed, Alpha, FVector2D::ZeroVector);
			const bool bFlames = A.state != 2 || A.ms < 50;
			if (bFlames) {
				Show(i, 1, Flames[Level][0], At, 1, Part, Elapsed, Alpha, FVector2D(0, 4 * Seconds));
				Show(i, 2, Flames[Level][1], At, 1, Part, Elapsed, Alpha, FVector2D(0, 2 * Seconds));
			}
			break;
		}
		case lr::Action::Shield: {
			if (!Car || A.state < 3 || A.state > 4 || (W.racers[size_t(A.owner)].flags & lr::RacerGhost)) {
				break;
			}
			const int32 Level = FMath::Clamp<int32>(A.level, 0, 3);
			const float Alpha = A.state == 4 ? float(int32(A.ms)) / 1000 * 191 / 255 : 1.0f;
			lr::Pose At{Car->position + lr::Vec3{0, 0, 2}, lr::basisFromForwardUp(Car->rotation.f, {0, 0, 1})};
			Show(i, 0, "SHEOUT" + std::to_string(Level), At, 1, 0, Seconds * 1000, Alpha, FVector2D(0, 4 * Seconds));
			Show(i, 1, "SHEIN" + std::to_string(Level), At, 1, 0, Seconds * 1000, Alpha, FVector2D(0, 4 * Seconds));
			break;
		}
		case lr::Action::Dynamite: {
			if (A.shot.state != lr::Projectile::Flying && A.state != 2) {
				break;
			}
			const float Angle = A.phase;
			const float Cs = FMath::Cos(Angle), Sn = FMath::Sin(Angle);
			lr::Pose At{A.state == 2 && Car ? Car->position + lr::Vec3{0, 0, 5} : A.shot.at, {{Cs, 0, -Sn}, {0, 1, 0}, {Sn, 0, Cs}}};
			Show(i, 0, "BARREL", At, 1, -1, 0, 1, FVector2D::ZeroVector);
			break;
		}
		case lr::Action::Hook:
			if (A.state >= 3 && A.state <= 5) {
				Show(i, 0, "GRAPPLE", W.grapple, 1, -1, 0, 1, FVector2D::ZeroVector);
			}
			break;
		case lr::Action::Missile:
			if (A.state == 2 && Car) {
				Show(i, 0, "DMISSILE", A.ride, 1, 0, A.riseFrames / 0.03f, 1, FVector2D::ZeroVector);
			}
			else if (A.shot.state == lr::Projectile::Flying) {
				const lr::Vec3 F = lr::isZero(A.shot.velocity) ? lr::Vec3{1, 0, 0} : lr::normalize(A.shot.velocity);
				Show(i, 0, "DMISSILE", {A.shot.at, lr::basisFromForwardUp(F, {0, 0, 1})}, 1, -1, 0, 1, FVector2D::ZeroVector);
			}
			break;
		default: break;
		}
	}
	for (auto& [Key, P] : ActionModels) {
		if (!Shown.Contains(Key)) {
			for (TWeakObjectPtr<UProceduralMeshComponent>& C : P.Parts) {
				if (C.IsValid()) {
					C->SetVisibility(false);
				}
			}
		}
	}
}
