#pragma once

#include "CoreMinimal.h"
#include "LRCore/Common/Math.h"

#include <string>
#include <vector>

// A model's sections in UE's space: positions in the model's units (game y flipped) times 30.48, a section per part and
// material, with its material's name and the game's normals (for lighting). A model joining nodes (lr::joinsNodes) has
// one part: each vertex rides its own node, placed in the world as posed.
struct FLRModelMesh
{
	struct FSection
	{
		int32 Part = 0;
		std::string Material;
		TArray<FVector> Positions, Normals;
		TArray<FVector2D> Uvs;
		TArray<FColor> Colours;
		TArray<int32> Triangles;
		TArray<lr::Vec3> GameNormals;
		TArray<int32> VertexNodes; // joined models': each vertex's node
	};
	TArray<FSection> Sections;
	int32 Parts = 1;
	float Scale = 1;
	bool bLit = false;
	bool bJoined = false;
	lr::Sphere Bounds;
	std::vector<std::string> MaterialNames; // the model's material table (what material animation swaps by slot)
};
