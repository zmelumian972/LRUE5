// The race view's asset store (LRCoreRaceAssets.h): materials and textures from the cache, models as UE meshes, figures,
// animations, the race's libraries.

#include "LRCoreRaceAssets.h"

#include "Engine/Texture2D.h"
#include "LRCore/Race/Track.h"
#include "LRCore/Show/View.h"
#include "LRData/Archive.h"
#include "LRData/Cache.h"
#include "LRData/Cars.h"
#include "LRData/Image.h"
#include "LRData/Look.h"
#include "LRData/Model.h"
#include "LRData/Race.h"
#include "LRData/Scene.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/UObjectGlobals.h"

#include "LRCoreModelMesh.h"
#include "LRCoreRaceParts.h"

using namespace LRUe;

UTexture2D* FLRRaceAssets::Texture(const std::string& List, const std::string& Name, bool bPoint, bool bClamp)
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
bool FLRRaceAssets::LoadBases()
{
	if (!MaskedBase) {
		MaskedBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LR/Materials/M_ClassicMasked.M_ClassicMasked"));
		TranslucentBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LR/Materials/M_ClassicTranslucent.M_ClassicTranslucent"));
		AdditiveBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LR/Materials/M_ClassicAdditive.M_ClassicAdditive"));
	}
	return MaskedBase && TranslucentBase && AdditiveBase;
}

namespace
{
// A file of the cache decoded once into Decoded (none if it doesn't decode).
template <typename T>
const T* DecodedOnce(const lr::Cache& Cache, std::unordered_map<std::string, std::optional<T>>& Decoded, const std::string& Path)
{
	auto [It, bNew] = Decoded.try_emplace(Path);
	if (bNew) {
		T File;
		if (Cache.get(Path, File)) {
			It->second = std::move(File);
		}
	}
	return It->second ? &*It->second : nullptr;
}
} // namespace

const lr::MaterialLibrary* FLRRaceAssets::LibraryAt(const std::string& Path) { return DecodedOnce(*Cache, LibraryCache, Path); }

const lr::TextureList* FLRRaceAssets::TexturesAt(const std::string& Path) { return DecodedOnce(*Cache, TextureCache, Path); }

UMaterialInstanceDynamic* FLRRaceAssets::MaterialFor(const std::string& Name, const TArray<std::pair<std::string, std::string>>& Libraries,
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
		// The library's own list, else the shared effects' (a track's EMITCxRx.MDB names COMMON/EMITTER.TDB's smoke),
		// else the track world's (C2R0's bat frame "bat" is only in its COMBINED.TDB)
		TArray<std::string> Candidates = {List, Common + "EMITTER.TDB"};
		for (const auto& Library : TrackLibraries) {
			Candidates.Add(Library.second);
		}
		for (const std::string& Candidate : Candidates) {
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
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Parent, Outer);
	if (bTextured) {
		if (UTexture2D* T = Texture(List, M->texture, M->point, M->clamp)) {
			Mid->SetTextureParameterValue(TEXT("Tex"), T);
		}
	}
	Mid->SetScalarParameterValue(TEXT("Alpha"), Constant);
	Materials.Add(Key, Mid);
	return Mid;
}

const FLRModelMesh* FLRRaceAssets::Model(const FString& Path, bool bMirror)
{
	const FString Key = Path + (bMirror ? TEXT("#m") : TEXT(""));
	if (TUniquePtr<FLRModelMesh>* Found = Meshes.Find(Key)) {
		return Found->Get();
	}
	lr::Model M;
	const bool bHas = Cache->get(TCHAR_TO_UTF8(*Path), M);
	return ModelOf(Key, bHas ? &M : nullptr, bMirror);
}

const FLRModelMesh* FLRRaceAssets::ModelOf(const FString& Key, const lr::Model* Source, bool bMirror)
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
					const lr::Vec3 N = M.normals.empty() ? lr::Vec3{0, 0, 1}
														 : lr::Vec3{M.normals[V * 3], M.normals[V * 3 + 1] * Flip, M.normals[V * 3 + 2]};
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

UTexture2D* FLRRaceAssets::ImageTexture(const std::string& Path, bool bFlip)
{
	const FString Key = FString::Printf(TEXT("image#%hs#%d"), Path.c_str(), bFlip);
	if (TObjectPtr<UTexture2D>* Found = Textures.Find(Key)) {
		return *Found;
	}
	lr::Image Loaded;
	UTexture2D* Made = nullptr;
	if (Cache->get(Path, Loaded) && Loaded.width && Loaded.height) {
		Made = NewTexture(int32(Loaded.width), int32(Loaded.height), TF_Bilinear, TA_Wrap, [&](uint8* Mip) {
			for (uint32 y = 0; y < Loaded.height; y++) {
				const uint32 Row = bFlip ? Loaded.height - 1 - y : y;
				for (uint32 x = 0; x < Loaded.width; x++) {
					const uint8* FromPixel = &Loaded.rgba[(size_t(Row) * Loaded.width + x) * 4];
					uint8* ToPixel = &Mip[(size_t(y) * Loaded.width + x) * 4];
					ToPixel[0] = FromPixel[2], ToPixel[1] = FromPixel[1], ToPixel[2] = FromPixel[0], ToPixel[3] = 255;
				}
			}
		});
	}
	Textures.Add(Key, Made);
	return Made;
}

UMaterialInterface* FLRRaceAssets::Picture(const std::string& File, bool bFlip, float Alpha)
{
	const FString Key = FString::Printf(TEXT("picture#%hs#%d#%g"), File.c_str(), bFlip, Alpha);
	if (TObjectPtr<UMaterialInstanceDynamic>* Found = Materials.Find(Key)) {
		return Found->Get();
	}
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Alpha < 1 ? TranslucentBase.Get() : MaskedBase.Get(), Outer);
	if (UTexture2D* Tex = ImageTexture(AssetPath("MENUDATA/PARTDB/", File, ".BMP"), bFlip)) {
		Mid->SetTextureParameterValue(TEXT("Tex"), Tex);
	}
	Mid->SetScalarParameterValue(TEXT("Alpha"), Alpha);
	Materials.Add(Key, Mid);
	return Mid;
}

const FLRRaceAssets::FFigure* FLRRaceAssets::Figure(const std::string& Model)
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

const lr::Animation* FLRRaceAssets::MenuAnimation(const std::string& Path)
{
	const FString Key = UTF8_TO_TCHAR(Path.c_str());
	if (TUniquePtr<lr::Animation>* Found = MenuAnimations.Find(Key)) {
		return Found->Get();
	}
	lr::AnimationFile AnimFile;
	TUniquePtr<lr::Animation> Anim;
	if (!Path.empty() && Cache->get(Path, AnimFile)) {
		Anim = MakeUnique<lr::Animation>(lr::animationOf(AnimFile, false));
	}
	return MenuAnimations.Add(Key, MoveTemp(Anim)).Get();
}

// The effects', the power-ups' and the local world's libraries, joined once for the race's frames.
void FLRRaceAssets::LoadRaceLibraries(const lr::RaceFile& Race, const std::string& Dir)
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
		EmitterLibraries.Reset();
		for (const auto& L : LocalLibraries) {
			EmitterLibraries.Add({L.first, L.first.substr(0, L.first.size() - 3) + "TDB"});
		}
	}
	SharedLibraries = CommonLibraries;
	SharedLibraries.Append(PowerupLibraries);
}

void FLRRaceAssets::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(MaskedBase);
	Collector.AddReferencedObject(TranslucentBase);
	Collector.AddReferencedObject(AdditiveBase);
	Collector.AddReferencedObjects(Textures);
	Collector.AddReferencedObjects(Materials);
}

void FLRRaceAssets::Reset()
{
	Meshes.Reset();
	Figures.Reset();
	MenuAnimations.Reset();
}
