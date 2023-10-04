// Hit information, aka ray payload
// This sample only carries a shading color and hit distance.
// Note that the payload should be kept as small as possible,
// and that its size must be declared in the corresponding
// D3D12_RAYTRACING_SHADER_CONFIG pipeline subobjet.
struct HitInfo
{
  float4 colorAndDistance;
};

// Attributes output by the raytracing when hitting a surface,
// here the barycentric coordinates
struct Attributes
{
  float2 bary;
};
struct MaterialStruct {
	uint hasNormals;
	uint hasTangents;
	uint hasColors;
	uint hasTexcoords;

	uint alphaMode; // 0 - "OPAQUE", 1 - "MASK", 2 - "BLEND"
	float alphaCutoff; // default 0.5
	uint doubleSided; // 0 - false, 1 - true

	int baseTextureIndex; // Index in heap
	int baseTextureSamplerIndex; // Index in heap
	uint texCoordIdBase;
	float4 baseColor;

	int metallicRoughnessTextureIndex;
	int metallicRoughnessTextureSamplerIndex;
	uint texCoordIdMR;
	float metallicFactor;
	float roughnessFactor;


	int occlusionTextureIndex;
	int occlusionTextureSamplerIndex;
	uint texCoordIdOcclusion;
	float strengthOcclusion; // look into tiny_gltf.h for formula

	int normalTextureIndex;
	int normalTextureSamplerIndex;
	uint texCoordIdNorm;
	float scaleNormal; // look into tiny_gltf.h for formula

	int emissiveTextureIndex;
	int emissiveTextureSamplerIndex;
	uint texCoordIdEmiss;
	float3 emisiveFactor;

};
struct RenderModeStruct {
	uint mode;
};
//BINDLESS
StructuredBuffer<uint> heapIndexes : register(t0, space1);
ConstantBuffer<RenderModeStruct> renderMode : register(b0, space1);
// For now render modes
	// 0 - vertex colors
	// 1 - vertex normals
	// 2 - base color
	// 3 - metallic 
	// 4 - roughness
	// 5 - metallic roughness
	// 6 - occlusion
	// 7 - orm
	// 8 - normal map 
	// 9 - world space normals 
	// 10 - emissive
	// 11 - basecolor + shadows