// The menus' 3D in the Classic look: each view's world and
// things built from the data cache with the race view's materials and posed only from the core's MenuScene (its
// camera, lights, things' world matrices, a minifig's skeleton, part and frame through the shared skeleton sampler,
// its body skinned per vertex; a cinematic's entities on their nodes).

#include "LRCoreRace.h"

#include "LRCoreModelMesh.h"

#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "LRCore/Kart/Car.h"
#include "LRCore/Common/Game.h"
#include "LRCore/Menus/Menus.h"
#include "LRCore/Race/Track.h"
#include "LRData/Cache.h"
#include "LRData/Image.h"
#include "LRData/Look.h"
#include "LRData/Model.h"
#include "LRData/Scene.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

namespace
{
constexpr float Cm = 30.48f;
FVector Ue(const lr::Vec3& V) { return FVector(V.x, -V.y, V.z) * Cm; }
FVector Along(const lr::Vec3& V) { return FVector(V.x, -V.y, V.z); }
FTransform FrameOf(const lr::Basis& B, const lr::Vec3& At) { return FTransform(FMatrix(Along(B.f), -Along(B.s), Along(B.u), Ue(At))); }
std::string Upper(std::string S)
{
	for (char& C : S) {
		C = char(std::toupper(uint8_t(C)));
	}
	return S;
}
const char* const Expressions[6] = {"DFLT", "ANGRY", "BLINK", "HAPPY", "SAD", "SUPRZ"};
const char* const PartFiles[5] = {"", "MENUDATA/LEGOMAN.ADB", "MENUDATA/RSANIM.ADB", "MENUDATA/CBANIM.ADB", "MENUDATA/CMAMAN.ADB"};
TArray<std::pair<std::string, std::string>> Library(const std::string& Base)
{
	TArray<std::pair<std::string, std::string>> L;
	L.Add({Base + ".MDB", Base + ".TDB"});
	return L;
}
// A colour effect's transform of a prelit colour: red, green, blue (byte >> shift) + offset,
// capped at 255; alpha kept.
FColor Transformed(const FColor& C, const int32 Shift[3], const int32 Offset[3])
{
	const auto Channel = [&](uint8 V, int32 i) { return uint8(FMath::Min(255, (int32(V) >> Shift[i]) + Offset[i])); };
	return FColor(Channel(C.R, 0), Channel(C.G, 1), Channel(C.B, 2), C.A);
}
} // namespace

bool ULRRaceView::InitBase(UWorld* World, const lr::Cache& InCache)
{
	WorldRef = World;
	Cache = &InCache;
	if (!MaskedBase) {
		MaskedBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LR/Materials/M_ClassicMasked.M_ClassicMasked"));
		TranslucentBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LR/Materials/M_ClassicTranslucent.M_ClassicTranslucent"));
		AdditiveBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LR/Materials/M_ClassicAdditive.M_ClassicAdditive"));
	}
	return MaskedBase && TranslucentBase && AdditiveBase;
}

UTexture2D* ULRRaceView::ImageTexture(const std::string& Path, bool bFlip)
{
	const FString Key = FString::Printf(TEXT("image#%hs#%d"), Path.c_str(), bFlip);
	if (TObjectPtr<UTexture2D>* Found = Textures.Find(Key)) {
		return *Found;
	}
	lr::Image I;
	UTexture2D* Made = nullptr;
	if (Cache->get(Path, I) && I.width && I.height) {
		Made = UTexture2D::CreateTransient(int32(I.width), int32(I.height), PF_B8G8R8A8);
		Made->SRGB = true;
		Made->Filter = TF_Bilinear;
		Made->NeverStream = true;
		uint8* Mip = static_cast<uint8*>(Made->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
		for (uint32 y = 0; y < I.height; y++) {
			const uint32 Row = bFlip ? I.height - 1 - y : y;
			for (uint32 x = 0; x < I.width; x++) {
				const uint8* S = &I.rgba[(size_t(Row) * I.width + x) * 4];
				uint8* D = &Mip[(size_t(y) * I.width + x) * 4];
				D[0] = S[2], D[1] = S[1], D[2] = S[0], D[3] = 255;
			}
		}
		Made->GetPlatformData()->Mips[0].BulkData.Unlock();
		Made->UpdateResource();
	}
	Textures.Add(Key, Made);
	return Made;
}

const lr::Animation* ULRRaceView::MenuAnimation(const std::string& Path)
{
	const FString Key = UTF8_TO_TCHAR(Path.c_str());
	if (lr::Animation** Found = MenuAnimations.Find(Key)) {
		return *Found;
	}
	lr::AnimationFile F;
	lr::Animation* A = nullptr;
	if (!Path.empty() && Cache->get(Path, F)) {
		A = new lr::Animation(lr::animationOf(F, false));
	}
	MenuAnimations.Add(Key, A);
	return A;
}

// A built mesh (a car, the held piece, a carousel item, the marks) as one component with a section per material: the
// library's materials, vertex colours lit by the view's lights, at the alpha given (translucent below 1).
UProceduralMeshComponent* ULRRaceView::BuiltMesh(AActor* Actor, const lr::CarMesh& Mesh, const TArray<std::pair<std::string, std::string>>& Libraries,
	const std::vector<std::string>& Names, float Alpha, bool bBright)
{
	UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(Actor);
	C->SetMobility(EComponentMobility::Movable);
	C->SetCastShadow(false);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->bUseAsyncCooking = false;
	C->SetupAttachment(Actor->GetRootComponent());
	C->RegisterComponent();
	Actor->AddInstanceComponent(C);
	lr::MaterialLibrary Lib;
	Cache->get(Libraries.Num() ? Libraries[0].first : std::string(), Lib);
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
		int32* S = SectionOf.Find(T.material);
		if (!S) {
			S = &SectionOf.Add(T.material, Sections.Num());
			Sections.AddDefaulted_GetRef().Material = T.material;
		}
		const std::string Name = T.material >= 0 && size_t(T.material) < Names.size() ? Names[size_t(T.material)] : std::string();
		const int32 m = Lib.find(Name);
		const std::array<uint8_t, 4> D = m >= 0 ? Lib.materials[size_t(m)].diffuse : std::array<uint8_t, 4>{200, 200, 200, 255};
		const std::array<uint8_t, 4> Amb = m >= 0 ? Lib.materials[size_t(m)].ambient : std::array<uint8_t, 4>{200, 200, 200, 255};
		FSection& Sec = Sections[*S];
		for (int32 c = 0; c < 3; c++) {
			const lr::CarMesh::Vertex& V = Mesh.vertices[T.vertices[size_t(c)]];
			const lr::Vec3 N = lr::normalize({V.normal[0] / 127.0f, V.normal[1] / 127.0f, V.normal[2] / 127.0f});
			Sec.T.Add(Sec.P.Num());
			Sec.P.Add(FVector(V.position[0], -V.position[1], V.position[2]) * Cm);
			Sec.N.Add(Along(N));
			Sec.Uv.Add(FVector2D(V.uv[0], V.uv[1]));
			Sec.Colour.Add(LitVertex(N, D.data(), Amb.data(), Boost));
		}
	}
	for (int32 s = 0; s < Sections.Num(); s++) {
		C->CreateMeshSection(s, Sections[s].P, Sections[s].T, Sections[s].N, Sections[s].Uv, Sections[s].Colour, {}, false);
		bool bFlip;
		uint8 D[4];
		const int32 Mi = Sections[s].Material;
		const std::string Name = Mi >= 0 && size_t(Mi) < Names.size() ? Names[size_t(Mi)] : std::string();
		C->SetMaterial(s, MaterialFor(Name, Libraries, Alpha, bFlip, D, Alpha < 1));
	}
	return C;
}

// The menus' lights: an ambient (none: the device's 255) and the directional lights (none: no
// light terms).
void ULRRaceView::LightMenu(const lr::MenuLighting& L)
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

ULRRaceView::FSkin ULRRaceView::BuildSkin(AActor* Actor, const lr::Model& M, const TArray<std::pair<std::string, std::string>>& Libraries,
	const TMap<FString, UMaterialInterface*>* Overrides, bool bFaceless, float Alpha)
{
	FSkin S;
	S.Model = M;
	UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(Actor);
	C->SetMobility(EComponentMobility::Movable);
	C->SetCastShadow(false);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->bUseAsyncCooking = false;
	C->SetupAttachment(Actor->GetRootComponent());
	C->RegisterComponent();
	Actor->AddInstanceComponent(C);
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
		FSkin::FSection& Into = S.Sections[*At];
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
		FSkin::FSection& Sec = S.Sections[s];
		bool bFlip = false;
		uint8 Diffuse[4], Ambient[4];
		UMaterialInterface* Mid = MaterialFor(SectionMaterials[s], Libraries, Alpha, bFlip, Diffuse, Alpha < 1, Ambient);
		if (UMaterialInterface* const* Over = Overrides ? Overrides->Find(UTF8_TO_TCHAR(SectionMaterials[s].c_str())) : nullptr) {
			// (A minifig's face, torso and legs: their own pictures, ambient and diffuse 200)
			Mid = *Over;
			bFlip = false;
			for (int32 c = 0; c < 3; c++) {
				Diffuse[c] = Ambient[c] = 200;
			}
		}
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

void ULRRaceView::PoseSkin(FSkin& S, const std::vector<lr::NodeMatrix>& Nodes, const FVector& At)
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
		const FSkin::FSection& Sec = S.Sections[s];
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
				Normal = lr::normalize(lr::toWorld(R, {S.Model.normals[size_t(V) * 3], S.Model.normals[size_t(V) * 3 + 1], S.Model.normals[size_t(V) * 3 + 2]}));
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

// A cinematic entity's model table as the material tracks left it: each slot's sections drawn
// with the material written there (found across the cinematic's libraries), the model's own where nothing is written.
void ULRRaceView::ShowTable(FMenuThing& T, const lr::MenuScene::Thing& Thing)
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
		for (FPlaced& P : T.Placed) {
			if (!P.Mesh || Slot < 0 || size_t(Slot) >= P.Mesh->MaterialNames.size()) {
				continue;
			}
			const std::string& Own = P.Mesh->MaterialNames[size_t(Slot)];
			bool bFlip;
			uint8 D[4];
			UMaterialInstanceDynamic* Shown = MaterialFor(Now ? *Now : Own, T.Libraries, 1, bFlip, D);
			TArray<int32> SectionsOf;
			SectionsOf.Init(0, P.Mesh->Parts);
			for (const FLRModelMesh::FSection& S : P.Mesh->Sections) {
				const int32 Index = SectionsOf[S.Part]++;
				if (S.Material == Own && P.Parts[S.Part].IsValid()) {
					P.Parts[S.Part]->SetMaterial(Index, Shown);
				}
			}
		}
		for (FSkin& S : T.Skins) {
			if (!S.Component.IsValid() || Slot < 0 || size_t(Slot) >= S.Model.materials.size()) {
				continue;
			}
			const std::string& Own = S.Model.materials[size_t(Slot)];
			bool bFlip;
			uint8 D[4];
			UMaterialInstanceDynamic* Shown = MaterialFor(Now ? *Now : Own, T.Libraries, 1, bFlip, D);
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
void ULRRaceView::Recolour(FPlaced& P, const lr::MenuScene::Thing& Thing)
{
	if (!Thing.coloured || !P.Mesh || P.bLit) {
		return;
	}
	const int32 Shift[3] = {Thing.colourShift[0], Thing.colourShift[1], Thing.colourShift[2]};
	const int32 Offset[3] = {Thing.colourOffset[0], Thing.colourOffset[1], Thing.colourOffset[2]};
	TArray<int32> SectionsOf;
	SectionsOf.Init(0, P.Mesh->Parts);
	for (const FLRModelMesh::FSection& S : P.Mesh->Sections) {
		const int32 Index = SectionsOf[S.Part]++;
		if (!P.Parts[S.Part].IsValid()) {
			continue;
		}
		TArray<FColor> Colours;
		for (const FColor& C : S.Colours) {
			Colours.Add(Transformed(C, Shift, Offset));
		}
		P.Parts[S.Part]->UpdateMeshSection(Index, S.Positions, {}, {}, Colours, {});
	}
}

void ULRRaceView::ClearThing(FMenuThing& T)
{
	for (FPlaced& P : T.Placed) {
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
	for (FSkin& S : T.Skins) {
		if (S.Component.IsValid()) {
			S.Component->DestroyComponent();
		}
	}
	T = {};
}

namespace
{
// A skeleton file's nodes for the sampler.
std::vector<lr::SkeletonNode> NodesOf(const lr::SkeletonFile& S)
{
	std::vector<lr::SkeletonNode> Out;
	for (const lr::SkeletonFile::Node& N : S.nodes) {
		int16 Parent = -1;
		for (size_t k = 0; k < S.nodes.size(); k++) {
			Parent = !N.parent.empty() && S.nodes[k].name == N.parent ? int16(k) : Parent;
		}
		Out.push_back({lr::vec(N.position), lr::basisOf({N.rotation[0], N.rotation[1], N.rotation[2], N.rotation[3]}), Parent});
	}
	return Out;
}
} // namespace

UMaterialInterface* ULRRaceView::Picture(const std::string& File, bool bFlip, float Alpha)
{
	const FString Key = FString::Printf(TEXT("picture#%hs#%d#%g"), File.c_str(), bFlip, Alpha);
	if (TObjectPtr<UMaterialInstanceDynamic>* Found = Materials.Find(Key)) {
		return Found->Get();
	}
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Alpha < 1 ? TranslucentBase.Get() : MaskedBase.Get(), this);
	if (UTexture2D* Tex = ImageTexture("MENUDATA/PARTDB/" + Upper(File) + ".BMP", bFlip)) {
		Mid->SetTextureParameterValue(TEXT("Tex"), Tex);
	}
	Mid->SetScalarParameterValue(TEXT("Alpha"), Alpha);
	Materials.Add(Key, Mid);
	return Mid;
}

void ULRRaceView::BuildThing(FMenuThing& T, AActor* Actor, const lr::MenuScene& Scene, const lr::MenuScene::Thing& Thing, const lr::Menus& Menus,
	const lr::Game& Game, const lr::MenuData& Data)
{
	using Kind = lr::MenuScene::Thing;
	const lr::PartCatalog& Cat = Data.parts;
	const TArray<std::pair<std::string, std::string>> BodyParts = Library("MENUDATA/PARTDB/BODYPART");
	const TArray<std::pair<std::string, std::string>> Pieces = Library("MENUDATA/PIECEDB/LPIECEHI");
	// A minifig's (or a ring item's) pictures, flipped as the face's: the face's for an expression, the torso's, the legs' (the torso flipped too: decided from the menus shots, its hands and print).
	TMap<FString, UMaterialInterface*> Overrides;
	const auto Dress = [&](const TCHAR* Material, const std::string& File, bool bFlip) { Overrides.Add(Material, this->Picture(File, bFlip)); };
	const auto Name = [](const std::vector<lr::PartCatalog::Part>& Ring, int32 i) { return i >= 0 && size_t(i) < Ring.size() ? Ring[size_t(i)].name : std::string(); };
	switch (Thing.kind) {
	case Kind::Minifig: {
		// The menus' body (the seated leg box in a car) skinned per vertex on the skeleton
		// the core names, the hat's head from CBBODIES.GCB on the body's face node, the pictures.
		const uint8 Face = Thing.looks[0], Hat = Thing.looks[1], Legs = Thing.looks[2], Torso = Thing.looks[3];
		const int32 Body = Thing.seated ? 4 : FMath::Clamp(Thing.body, 0, 4);
		const std::string Folder = "MENUDATA/PARTDB/MENUPART/";
		Dress(TEXT("face"), Name(Cat.rings[1], Face) + Expressions[FMath::Min<int32>(Thing.looks[4], 5)], true);
		Dress(TEXT("torso"), Name(Cat.rings[2], Torso), true);
		Dress(TEXT("legs"), Name(Cat.rings[3], Legs), true);
		lr::Model M;
		if (size_t(Body) < Cat.bodies.size() && Cache->get(Folder + Upper(Cat.bodies[size_t(Body)]) + ".GDB", M)) {
			for (const lr::Model::Section& Sec : M.sections) {
				if (Sec.material < M.materials.size() && M.materials[Sec.material] == "face") {
					T.FacePart = Sec.part;
				}
			}
			T.Skins.Add(BuildSkin(Actor, M, BodyParts, &Overrides, true));
		}
		const std::string HatModel = size_t(Hat) < Cat.hatModels.size() ? Cat.hatModels[Hat] : std::string("head");
		if (const lr::HeadLibrary::Head* H = Data.heads[0].find(HatModel)) {
			T.Placed.Add(PlaceMesh(Actor, ModelOf(FString::Printf(TEXT("head#0#%hs"), HatModel.c_str()), &H->model, false), BodyParts, true, 1, &Overrides));
		}
		lr::SkeletonFile S;
		if (Cache->get(Thing.skeleton.empty() && size_t(Body) < Cat.bodies.size() ? Folder + Upper(Cat.bodies[size_t(Body)]) + ".SDB" : Thing.skeleton, S)) {
			T.Nodes = NodesOf(S);
		}
		T.Animation = MenuAnimation(!Thing.partFile.empty() ? Thing.partFile : std::string(PartFiles[Thing.parts]));
		break;
	}
	case Kind::Entity: {
		// A cinematic's entity: its model through the cinematic's folder, then MENUDATA/, its
		// materials found by name across every library the cinematic's worlds loaded, newest first (the
		// blended world names none); an animated one skinned on its nodes; its model's colour transform while a colour
		// effect runs on it.
		if (Scene.cinemaScreen >= Data.screens.size()) {
			break;
		}
		const lr::MenuData::Screen& Sc = Data.screens[Scene.cinemaScreen];
		if (Thing.entity[0] < 0 || size_t(Thing.entity[0]) >= Sc.cinemaWorlds.size()) {
			break;
		}
		const auto Path = [&](const std::string& Base, const char* Ext) {
			const std::string Own = Sc.cinemaFolder + Upper(Base) + Ext;
			return Cache->has(Own) ? Own : "MENUDATA/" + Upper(Base) + Ext;
		};
		T.Libraries.Reset();
		for (size_t i = Sc.cinemaLibraries.size(); i-- > 0;) {
			T.Libraries.Add({Sc.cinemaLibraries[i].materials, Sc.cinemaLibraries[i].textures});
		}
		lr::Model M;
		if (!Cache->get(Path(Thing.model, ".GDB"), M)) {
			break;
		}
		if (Thing.entity[1]) {
			FSkin& S = T.Skins.Add_GetRef(BuildSkin(Actor, M, T.Libraries, nullptr, false));
			S.bColoured = Thing.coloured && M.kind == lr::Model::Vertices::Colored;
			for (int32 c = 0; c < 3; c++) {
				S.Shift[c] = Thing.colourShift[size_t(c)], S.Offset[c] = Thing.colourOffset[size_t(c)];
			}
		}
		else {
			FPlaced& P = T.Placed.Add_GetRef(PlaceMesh(Actor, ModelOf(UTF8_TO_TCHAR((Path(Thing.model, ".GDB")).c_str()), &M, false), T.Libraries, true, 1, nullptr));
			if (M.kind == lr::Model::Vertices::Colored) {
				Recolour(P, Thing);
			}
		}
		break;
	}
	case Kind::Part: {
		// A driver ring's item: a hat's own entry, or the ring's body part model with its picture.
		const int32 Ring = Thing.colour;
		if (Ring == 0) {
			if (const lr::HeadLibrary::Head* H = Data.heads[0].find(Thing.model)) {
				T.Placed.Add(PlaceMesh(Actor, ModelOf(FString::Printf(TEXT("head#0#%hs"), Thing.model.c_str()), &H->model, false), BodyParts, true, 1, nullptr));
			}
			break;
		}
		if (Ring == 1) {
			Dress(TEXT("face"), Name(Cat.rings[1], Thing.type) + "DFLT", true);
		}
		else if (Ring == 2) {
			Dress(TEXT("torso"), Name(Cat.rings[2], Thing.type), true);
		}
		else {
			Dress(TEXT("legs"), Name(Cat.rings[3], Thing.type), true);
		}
		const FLRModelMesh* M = Model(UTF8_TO_TCHAR(("MENUDATA/PARTDB/MENUPART/" + Upper(Thing.model) + ".GDB").c_str()), false);
		T.Placed.Add(PlaceMesh(Actor, M, BodyParts, true, 1, &Overrides));
		break;
	}
	case Kind::Car: {
		// The builders' and racer select's car is the menus' mesh; a cinematic's its swapped one; the garage's is its
		// racer's, built here.
		lr::CarMesh Built;
		const lr::CarMesh* Mesh = Thing.model == "cinema" ? &Menus.cinemaCar : &Menus.carMesh;
		if (Menus.screen == lr::ScreenGarage) {
			const int32 n = int32(Menus.state.racers.size());
			if (const lr::RacerRecord* R = n && Menus.state.shown >= 0 ? Game.racer(Menus.state.racers[size_t(Menus.state.shown % n)]) : nullptr) {
				lr::Car Car;
				Car.load(Data.pieces, R->pieces);
				Car.chooseOffset(Data.pieces);
				Built = Car.menuMesh(Data.pieces, Data.sets.materials);
				Mesh = &Built;
			}
		}
		T.Built.Add(BuiltMesh(Actor, *Mesh, Pieces, Data.pieces.materials, 1, Thing.lit));
		break;
	}
	case Kind::Chassis: {
		const lr::ChassisTable::Chassis* C = nullptr;
		for (const lr::ChassisTable::Chassis& Each : Data.menuChassis.chassis) {
			C = Each.model == Thing.model ? &Each : C;
		}
		if (!C) {
			break;
		}
		const std::string Base = "MENUDATA/PIECEDB/";
		TArray<std::pair<std::string, std::string>> Libs;
		Libs.Add({Base + Upper(C->materials.empty() ? C->model : C->materials) + ".MDB", Base + Upper(C->textures.empty() ? C->model : C->textures) + ".TDB"});
		T.Placed.Add(Place(Actor, UTF8_TO_TCHAR((Base + Upper(C->model) + ".GDB").c_str()), Libs, true));
		lr::SkeletonFile S;
		if (Cache->get(Base + Upper(C->skeleton.empty() ? C->model : C->skeleton) + ".SDB", S)) {
			T.Nodes = NodesOf(S);
		}
		T.Animation = MenuAnimation(Base + Upper(C->animation.empty() ? C->model : C->animation) + ".ADB");
		break;
	}
	case Kind::Piece:
		// A piece carousel's item, or the builder's held piece.
		if (Scene.world.empty()) {
			T.Built.Add(BuiltMesh(Actor, Menus.carouselItem(Thing.type, Thing.colour), Pieces, Data.pieces.materials, 1, false));
			break;
		}
		T.Built.Add(BuiltMesh(Actor, Menus.heldMesh, Pieces, Data.pieces.materials, float(Thing.alpha) / 255.0f, Thing.lit));
		break;
	case Kind::Mark: {
		// One 1x1 quad in the car's frame with the overlay's material (BLDRGRID's second), uvs (0.5, 0)-(0.99, 0.49).
		lr::MaterialLibrary Grid;
		Cache->get("MENUDATA/PIECEDB/BLDRGRID.MDB", Grid);
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
		T.Built.Add(BuiltMesh(Actor, Quad, Library("MENUDATA/PIECEDB/BLDRGRID"), Names, 1, false));
		break;
	}
	default: break;
	}
}

void ULRRaceView::PoseThing(FMenuThing& T, const lr::MenuScene& Scene, const lr::MenuScene::Thing& Thing, const lr::Menus& Menus, const FVector& At)
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
		Menus.entityNodes(Scene, Thing, Out);
		ShowTable(T, Thing);
	}
	else if (!T.Nodes.empty()) {
		const lr::Basis Unit = {W.rows.f * (1 / Scale), W.rows.s * (1 / Scale), W.rows.u * (1 / Scale)};
		const bool bChassis = Thing.kind == lr::MenuScene::Thing::Chassis;
		const int32 Part = bChassis ? 0 : Thing.animation;
		const lr::Animation* A = T.Animation && Part >= 0 && size_t(Part) < T.Animation->parts.size() ? T.Animation : nullptr;
		lr::poseSkeleton(T.Nodes, A, {A ? Part : -1, Thing.frame, true}, lr::Pose{W.position, Unit}, Scale, Out);
	}
	for (FSkin& S : T.Skins) {
		PoseSkin(S, Out, At);
	}
	TArray<FTransform> Nodes;
	TArray<FQuat> Rotations;
	for (const lr::NodeMatrix& N : Out) {
		Nodes.Add(FrameOf(N.rows, N.position) * Offset);
		Rotations.Add(Nodes.Last().GetRotation());
	}
	for (FPlaced& P : T.Placed) {
		if (Thing.kind == lr::MenuScene::Thing::Minifig) {
			// The head: its whole mesh rides the face node.
			const FTransform Head = Nodes.IsValidIndex(T.FacePart) ? Nodes[T.FacePart] : Whole;
			TArray<FTransform> HeadNodes;
			HeadNodes.Init(Head, P.Parts.Num());
			PoseNodes(P, HeadNodes, Head);
			TArray<FQuat> HeadRotations;
			HeadRotations.Init(Head.GetRotation(), FMath::Max(1, P.Parts.Num()));
			Relight(P, HeadRotations);
			continue;
		}
		const bool bWhole = Thing.kind != lr::MenuScene::Thing::Chassis;
		PoseNodes(P, bWhole ? TArray<FTransform>() : Nodes, Whole);
		TArray<FQuat> R = bWhole ? TArray<FQuat>() : Rotations;
		if (R.IsEmpty()) {
			R.Add(Whole.GetRotation());
		}
		Relight(P, R);
	}
}

// A cinematic's worlds' sprites, turned to its camera each frame.
void ULRRaceView::PoseMenuSprites(FMenuView& V, const lr::MenuScene& Scene, const lr::MenuData& Data)
{
	if (Scene.cinemaScreen >= Data.screens.size()) {
		return;
	}
	const lr::MenuData::Screen& Sc = Data.screens[Scene.cinemaScreen];
	TArray<std::pair<std::string, std::string>> Libraries;
	for (size_t i = Sc.cinemaLibraries.size(); i-- > 0;) {
		Libraries.Add({Sc.cinemaLibraries[i].materials, Sc.cinemaLibraries[i].textures});
	}
	const lr::Vec3 Eye = lr::vec(Scene.eye), Look = lr::normalize(lr::vec(Scene.target) - Eye);
	const lr::Vec3 Right = lr::normalize(lr::cross(Look, lr::vec(Scene.up)));
	const lr::Vec3 Down = lr::cross(Look, Right);
	int32 Section = 0;
	for (const lr::MenuData::CinemaWorld& World : Sc.cinemaWorlds) {
		for (const lr::SceneFile::Sprite& File : World.scene.sprites) {
			if (!V.Sprites.IsValid()) {
				UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(V.Actor.Get());
				C->SetMobility(EComponentMobility::Movable);
				C->SetCastShadow(false);
				C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				C->bUseAsyncCooking = false;
				C->SetupAttachment(V.Actor->GetRootComponent());
				C->RegisterComponent();
				V.Sprites = C;
			}
			const lr::Sprite S = lr::Sprite::of(File, false);
			if (!S.inReach(Eye)) {
				continue;
			}
			bool bFlip;
			uint8 D[4];
			UMaterialInstanceDynamic* Mid = MaterialFor(S.material, Libraries, 1, bFlip, D);
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

ULRRaceView::FMenuCamera ULRRaceView::PoseMenuView(UWorld* World, const lr::Cache& InCache, const lr::MenuData& Data, int32 v, const FVector& At,
	const lr::MenuScene& Scene, const lr::Menus& Menus, const lr::Game& Game, AActor*& OutActor)
{
	FMenuCamera Out;
	OutActor = nullptr;
	if (!InitBase(World, InCache)) {
		return Out;
	}
	while (MenuViews.Num() <= v) {
		MenuViews.AddDefaulted();
	}
	FMenuView& V = MenuViews[v];
	if (!V.Actor.IsValid()) {
		AActor* A = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(At));
		USceneComponent* Root = NewObject<USceneComponent>(A, TEXT("Root"));
		Root->SetMobility(EComponentMobility::Movable);
		A->SetRootComponent(Root);
		Root->RegisterComponent();
		A->SetActorLocation(At);
		V = {};
		V.Actor = A;
	}
	AActor* A = V.Actor.Get();
	OutActor = A;
	// The view's lights (from the core).
	LightMenu(Scene.lighting);
	// Its world (a menu .WDB of that name: its entities where they stand, at rest), rebuilt when it changes; a
	// cinematic's or preview's draws only what its cues show.
	const bool bCinema = Scene.cinemaScreen != 0;
	if (V.World != Scene.world || V.bCinema != bCinema) {
		for (FMenuThing& T : V.Things) {
			ClearThing(T);
		}
		V.Things.Reset();
		TArray<UActorComponent*> Old;
		A->GetComponents(Old);
		for (UActorComponent* C : Old) {
			if (C != A->GetRootComponent()) {
				C->DestroyComponent();
			}
		}
		V.World = Scene.world;
		V.bCinema = bCinema;
		V.Last = {};
		V.Blended.Reset();
		const std::string Wanted = "/" + Upper(Scene.world) + ".WDB";
		for (const std::string& F : bCinema || !Scene.drawWorld ? std::vector<std::string>() : Cache->paths()) {
			if (Scene.world.empty() || !F.starts_with("MENUDATA/") || !F.ends_with(Wanted)) {
				continue;
			}
			lr::SceneFile S;
			if (!Cache->get(F, S)) {
				break;
			}
			const std::string Folder = F.substr(0, F.rfind('/') + 1);
			TArray<std::pair<std::string, std::string>> Libraries;
			for (size_t i = 0; i < S.materialLibraries.size(); i++) {
				Libraries.Add({Folder + Upper(S.materialLibraries[i]) + ".MDB",
					Folder + Upper(i < S.textureLists.size() ? S.textureLists[i] : S.materialLibraries[i]) + ".TDB"});
			}
			for (const std::vector<lr::SceneFile::Entity>* List : {&S.entities, &S.animated}) {
				for (const lr::SceneFile::Entity& E : *List) {
					if (E.model < 0 || size_t(E.model) >= S.models.size()) {
						continue;
					}
					FPlaced P = Place(A, UTF8_TO_TCHAR((Folder + Upper(S.models[size_t(E.model)]) + ".GDB").c_str()), Libraries, true);
					if (!P.Mesh) {
						continue;
					}
					const lr::Basis B = lr::basisFromForwardUp(lr::normalize(lr::vec(E.forward)), lr::normalize(lr::vec(E.up)));
					const float K = E.scale * P.Mesh->Scale;
					const FTransform T = FrameOf({B.f * K, B.s * K, B.u * K}, lr::vec(E.position)) * FTransform(At);
					TArray<FQuat> R;
					for (TWeakObjectPtr<UProceduralMeshComponent>& C : P.Parts) {
						if (C.IsValid()) {
							C->SetWorldTransform(T);
						}
					}
					R.Add(T.GetRotation());
					Relight(P, R);
				}
			}
			// The view's blended world (the driver builder's spotlight cone and the stand's shadow,
			// racer select's turntable shadows): the folder's BLENDED.WDB drawn after the world and the view's things
			// (its translucent sort later), its materials found by name in the libraries the world loaded, newest first
			// (it names none); its animated entities play their part at the scene's frame.
			lr::SceneFile B;
			if (Scene.blendedWorld && Cache->get(Folder + "BLENDED.WDB", B)) {
				TArray<std::pair<std::string, std::string>> Newest;
				for (int32 i = Libraries.Num(); i-- > 0;) {
					Newest.Add(Libraries[i]);
				}
				for (const std::vector<lr::SceneFile::Entity>* List : {&B.entities, &B.animated}) {
					for (const lr::SceneFile::Entity& E : *List) {
						if (E.model < 0 || size_t(E.model) >= B.models.size()) {
							continue;
						}
						FBlended& Bl = V.Blended.AddDefaulted_GetRef();
						Bl.Placed = Place(A, UTF8_TO_TCHAR((Folder + Upper(B.models[size_t(E.model)]) + ".GDB").c_str()), Newest, true);
						if (!Bl.Placed.Mesh) {
							V.Blended.Pop();
							continue;
						}
						for (TWeakObjectPtr<UProceduralMeshComponent>& C : Bl.Placed.Parts) {
							if (C.IsValid()) {
								C->SetTranslucentSortPriority(1);
							}
						}
						Bl.Where = {lr::vec(E.position), lr::basisFromForwardUp(lr::normalize(lr::vec(E.forward)), lr::normalize(lr::vec(E.up)))};
						Bl.Scale = E.scale * Bl.Placed.Mesh->Scale;
						lr::SkeletonFile Skeleton;
						if (List == &B.animated && E.skeleton >= 0 && size_t(E.skeleton) < B.skeletons.size() &&
							Cache->get(Folder + Upper(B.skeletons[size_t(E.skeleton)]) + ".SDB", Skeleton)) {
							Bl.Nodes = NodesOf(Skeleton);
							Bl.Animation = E.animation >= 0 && size_t(E.animation) < B.animations.size()
								? MenuAnimation(Folder + Upper(B.animations[size_t(E.animation)]) + ".ADB")
								: nullptr;
							Bl.Part = E.part;
						}
					}
				}
			}
			break;
		}
	}
	// The blended world's entities posed: plain ones where they stand, animated ones on their nodes at the scene's frame.
	for (FBlended& Bl : V.Blended) {
		const lr::Basis& R = Bl.Where.rotation;
		const FTransform Whole = FrameOf({R.f * Bl.Scale, R.s * Bl.Scale, R.u * Bl.Scale}, Bl.Where.position) * FTransform(At);
		TArray<FTransform> Nodes;
		TArray<FQuat> Rotations;
		if (!Bl.Nodes.empty()) {
			const lr::Animation* Anim = Bl.Animation && Bl.Part >= 0 && size_t(Bl.Part) < Bl.Animation->parts.size() ? Bl.Animation : nullptr;
			std::vector<lr::NodeMatrix> Posed;
			lr::poseSkeleton(Bl.Nodes, Anim, {Anim ? Bl.Part : -1, Scene.blendedFrame, true}, Bl.Where, Bl.Scale, Posed);
			for (const lr::NodeMatrix& N : Posed) {
				Nodes.Add(FrameOf(N.rows, N.position) * FTransform(At));
				Rotations.Add(Nodes.Last().GetRotation());
			}
		}
		PoseNodes(Bl.Placed, Nodes, Whole);
		if (Rotations.IsEmpty()) {
			Rotations.Add(Whole.GetRotation());
		}
		Relight(Bl.Placed, Rotations);
	}
	// Its things, rebuilt when what they are changes, posed every frame; a ring's items lit each by its colours
	// (ambient the primary, the light the secondary along the scene's light).
	const bool bRing = Scene.world.empty() && !bCinema;
	V.Things.SetNum(FMath::Max(V.Things.Num(), int32(Scene.things.size())));
	for (int32 i = 0; i < V.Things.Num(); i++) {
		FMenuThing& T = V.Things[i];
		if (size_t(i) >= Scene.things.size()) {
			if (!T.Key.IsEmpty()) {
				ClearThing(T);
			}
			continue;
		}
		const lr::MenuScene::Thing& Th = Scene.things[size_t(i)];
		if (bRing) {
			lr::MenuLighting Item = {Th.primary | 0xff000000u, {{Scene.lighting.lights.empty() ? lr::normalize(lr::Vec3{-1, -1, -1}) : Scene.lighting.lights[0].first,
				Th.secondary | 0xff000000u}}};
			LightMenu(Item);
		}
		FString Key;
		switch (Th.kind) {
		case lr::MenuScene::Thing::Minifig:
			Key = FString::Printf(TEXT("M%d %d %d %d %d %d %d %d %hs %hs"), Th.seated, Th.body, Th.looks[0], Th.looks[1], Th.looks[2], Th.looks[3], Th.looks[4],
				int32(Th.parts), Th.skeleton.c_str(), Th.partFile.c_str());
			break;
		case lr::MenuScene::Thing::Entity:
			// (Built again when a colour effect starts, moves or stops on its model.)
			Key = FString::Printf(TEXT("E%d %d %d %hs %d %d %d %d %d %d %d"), Th.entity[0], Th.entity[1], Th.entity[2], Th.model.c_str(), Th.coloured,
				Th.colourShift[0], Th.colourShift[1], Th.colourShift[2], Th.colourOffset[0], Th.colourOffset[1], Th.colourOffset[2]);
			break;
		case lr::MenuScene::Thing::Part: Key = FString::Printf(TEXT("P%d %d %hs"), Th.colour, Th.type, Th.model.c_str()); break;
		case lr::MenuScene::Thing::Car: {
			const lr::CarMesh& Mesh = Th.model == "cinema" ? Menus.cinemaCar : Menus.carMesh;
			Key = FString::Printf(TEXT("C%hs %d %d %u %d"), Th.model.c_str(), int32(Mesh.vertices.size()), Menus.state.shown,
				FCrc::MemCrc32(Mesh.vertices.data(), int32(Mesh.vertices.size() * sizeof(lr::CarMesh::Vertex))), Th.lit);
			break;
		}
		case lr::MenuScene::Thing::Chassis: Key = FString::Printf(TEXT("H%hs"), Th.model.c_str()); break;
		case lr::MenuScene::Thing::Piece:
			Key = bRing ? FString::Printf(TEXT("R%d %d %08x %08x"), Th.type, Th.colour, Th.primary, Th.secondary)
						: FString::Printf(TEXT("K%d %d %d %d %u %d"), Th.type, Th.colour, Th.alpha, Th.lit,
							  FCrc::MemCrc32(Menus.heldMesh.vertices.data(), int32(Menus.heldMesh.vertices.size() * sizeof(lr::CarMesh::Vertex))),
							  int32(Menus.heldMesh.vertices.size()));
			break;
		case lr::MenuScene::Thing::Mark: Key = FString::Printf(TEXT("X%g %g %g"), Th.at[0], Th.at[1], Th.at[2]); break;
		default: break;
		}
		if (Key != T.Key) {
			ClearThing(T);
			T.Key = Key;
			BuildThing(T, A, Scene, Th, Menus, Game, Data);
		}
		PoseThing(T, Scene, Th, Menus, At);
	}
	if (bCinema && Scene.camera) {
		PoseMenuSprites(V, Scene, Data);
	}
	// The camera: the core's (the world's when it has one; a cinematic's stack), else the last.
	const FVector Ahead = Along({Scene.target[0] - Scene.eye[0], Scene.target[1] - Scene.eye[1], Scene.target[2] - Scene.eye[2]});
	if (!Scene.camera || Ahead.IsNearlyZero()) {
		return V.Last;
	}
	Out.Eye = Ue(lr::vec(Scene.eye)) + At;
	Out.Rotation = FRotationMatrix::MakeFromXZ(Ahead, Along(lr::vec(Scene.up))).Rotator();
	Out.Fov = Scene.fov;
	Out.bValid = true;
	V.Last = Out;
	return Out;
}
