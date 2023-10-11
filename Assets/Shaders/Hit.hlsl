#include "Common.hlsl"
#define COMMON_RESOURCE_OFFSET 3 // RT output + TLAS + camera
// Shading
struct ShadowHitInfo
{
	bool isHit;
};


[shader("closesthit")] 
void ClosestHit(inout HitInfo payload, Attributes attrib)
{
	float3 barycentrics =
		float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);
	// to get the first vertex index, we multiply the ID on 3
	uint vertId = 3 * PrimitiveIndex();
	// Get colors from vertex data
	StructuredBuffer<uint> primIndexes = ResourceDescriptorHeap[heapIndexes[COMMON_RESOURCE_OFFSET + InstanceID()]];
	uint primHeapIndex = primIndexes[GeometryIndex()];

	float3 hitColor = float3(1.f / 256.f, 1.f / 256.f, 1.f / 256.f) * float(primHeapIndex % 256);

	StructuredBuffer<MaterialStruct> MaterialStructs = ResourceDescriptorHeap[primHeapIndex];
	MaterialStruct material = MaterialStructs[0];// we have only one

	StructuredBuffer<float4x4> transforms = ResourceDescriptorHeap[primHeapIndex + 1]; // + Material
	float4x4 transform = transforms[0];
	StructuredBuffer<float3> triVertex = ResourceDescriptorHeap[primHeapIndex + 2]; // + Material + Transform
	StructuredBuffer<float3> triNormal = ResourceDescriptorHeap[primHeapIndex + 3]; // + Material + Transform + Positions
	StructuredBuffer<float4> triTangent = ResourceDescriptorHeap[primHeapIndex + 3 + material.hasNormals]; // + Material + Transform + Positions + Normals(optional)
	StructuredBuffer<float4> triColor = ResourceDescriptorHeap[primHeapIndex + 3 + material.hasNormals + material.hasTangents]; // + Material + Transform + Positions + Normals(optional) + Tangents(optional)

	StructuredBuffer<int> indices = ResourceDescriptorHeap[primHeapIndex + 3 + material.hasNormals + material.hasTangents + material.hasColors + material.hasTexcoords]; // + Material + Positions + Normals(optional) + Tangents(optional) + Colors(optional) + Texcoords(optional)
	float4 baseColor = float4(0.f, 0.f, 0.f, 0.f);
	if (material.baseTextureIndex >= 0) {
		Texture2D baseColorTexture = ResourceDescriptorHeap[material.baseTextureIndex];
		SamplerState baseColorSampler = SamplerDescriptorHeap[material.baseTextureSamplerIndex];
		StructuredBuffer<float2> triTexcoord = ResourceDescriptorHeap[primHeapIndex + 3 + material.hasNormals +
			material.hasTangents + material.hasColors + material.texCoordIdBase]; // + Material + Transform + Positions + Normals(optional) + Tangents(optional) + Colors(optional) + Texture coords for this prim texture
		float2 uv = triTexcoord[indices[vertId + 0]] * barycentrics.x +
			triTexcoord[indices[vertId + 1]] * barycentrics.y +
			triTexcoord[indices[vertId + 2]] * barycentrics.z;
		baseColor = baseColorTexture.SampleLevel(baseColorSampler, uv, 0) * material.baseColor;
	}
	float2 metallicRoughness = float2(0.f, 0.f);
	if (material.metallicRoughnessTextureIndex >= 0) {
		Texture2D metallicRoughnessTexture = ResourceDescriptorHeap[material.metallicRoughnessTextureIndex];
		SamplerState metallicRoughnessSampler = SamplerDescriptorHeap[material.metallicRoughnessTextureSamplerIndex];
		StructuredBuffer<float2> triTexcoord = ResourceDescriptorHeap[primHeapIndex + 3 + material.hasNormals +
			material.hasTangents + material.hasColors + material.texCoordIdMR]; // + Material + Transform + Positions + Normals(optional) + Tangents(optional) + Colors(optional) + Texture coords for this prim texture
		float2 uv = triTexcoord[indices[vertId + 0]] * barycentrics.x +
			triTexcoord[indices[vertId + 1]] * barycentrics.y +
			triTexcoord[indices[vertId + 2]] * barycentrics.z;
		metallicRoughness = metallicRoughnessTexture.SampleLevel(metallicRoughnessSampler, uv, 0).rg;
		metallicRoughness.r *= material.metallicFactor;
		metallicRoughness.g *= material.roughnessFactor;
	}
	float occlusion = 0.f;
	if (material.occlusionTextureIndex >= 0) {
		Texture2D occlusionTexture = ResourceDescriptorHeap[material.occlusionTextureIndex];
		SamplerState occlusionTextureSampler = SamplerDescriptorHeap[material.occlusionTextureSamplerIndex];
		StructuredBuffer<float2> triTexcoord = ResourceDescriptorHeap[primHeapIndex + 3 + material.hasNormals +
			material.hasTangents + material.hasColors + material.texCoordIdOcclusion]; // + Material + Transform + Positions + Normals(optional) + Tangents(optional) + Colors(optional) + Texture coords for this prim texture
		float2 uv = triTexcoord[indices[vertId + 0]] * barycentrics.x +
			triTexcoord[indices[vertId + 1]] * barycentrics.y +
			triTexcoord[indices[vertId + 2]] * barycentrics.z;
		occlusion = occlusionTexture.SampleLevel(occlusionTextureSampler, uv, 0).b; // if it is a separate texture will b work? it should, as it is usually 3 same values for RGB
		// occludedColor = lerp(color, color * <sampled occlusion
		// texture value>, <occlusion strength>) - from GLTF spec - we will need later for PBR 
		//material.strengthOcclusion
	}
	float3 normal = float3(0.f, 0.f, 0.f);
	if (material.normalTextureIndex >= 0) {
		Texture2D normalTexture = ResourceDescriptorHeap[material.normalTextureIndex];
		SamplerState normalTextureSamplerIndex = SamplerDescriptorHeap[material.normalTextureSamplerIndex];
		StructuredBuffer<float2> triTexcoord = ResourceDescriptorHeap[primHeapIndex + 3 + material.hasNormals +
			material.hasTangents + material.hasColors + material.texCoordIdNorm]; // + Material + Transform + Positions + Normals(optional) + Tangents(optional) + Colors(optional) + Texture coords for this prim texture
		float2 uv = triTexcoord[indices[vertId + 0]] * barycentrics.x +
			triTexcoord[indices[vertId + 1]] * barycentrics.y +
			triTexcoord[indices[vertId + 2]] * barycentrics.z;
		normal = normalTexture.SampleLevel(normalTextureSamplerIndex, uv, 0);
		// scaledNormal = normalize((normal * 2.0f - 1.0f) * float3(material.scaleNormal, material.scaleNormal, 1.0f))
		// scaled - part of GLTF spec
	}
	float3 emissive = float3(0.f, 0.f, 0.f);
	if (material.emissiveTextureIndex >= 0) {
		Texture2D emissiveTexture = ResourceDescriptorHeap[material.emissiveTextureIndex];
		SamplerState emissiveTextureSamplerIndex = SamplerDescriptorHeap[material.emissiveTextureSamplerIndex];
		StructuredBuffer<float2> triTexcoord = ResourceDescriptorHeap[primHeapIndex + 3 + material.hasNormals +
			material.hasTangents + material.hasColors + material.texCoordIdEmiss]; // + Material + Transform + Positions + Normals(optional) + Tangents(optional) + Colors(optional) + Texture coords for this prim texture
		float2 uv = triTexcoord[indices[vertId + 0]] * barycentrics.x +
			triTexcoord[indices[vertId + 1]] * barycentrics.y +
			triTexcoord[indices[vertId + 2]] * barycentrics.z;
		emissive = emissiveTexture.SampleLevel(emissiveTextureSamplerIndex, uv, 0) * material.emisiveFactor;
	}
	
	if (renderMode.mode == 0) {
		// Vertex colors
		hitColor = float3(float4(triColor[indices[vertId + 0]] * barycentrics.x +
			triColor[indices[vertId + 1]] * barycentrics.y +
			triColor[indices[vertId + 2]] * barycentrics.z).xyz);
	}
	if (renderMode.mode == 1) {
		//// Model space normals
		hitColor = (triNormal[indices[vertId + 0]] + 1.f) * 0.5 * barycentrics.x +
			(triNormal[indices[vertId + 1]] + 1.f) * 0.5 * barycentrics.y +
			(triNormal[indices[vertId + 2]] + 1.f) * 0.5 * barycentrics.z;
	}
	if (renderMode.mode == 2) {
		hitColor = float3(baseColor.xyz);
	}
	if (renderMode.mode == 3) {
		hitColor = float3(metallicRoughness.r, metallicRoughness.r, metallicRoughness.r);
	}
	if (renderMode.mode == 4) {
		hitColor = float3(metallicRoughness.g, metallicRoughness.g, metallicRoughness.g);
	}
	if (renderMode.mode == 5) {
		hitColor = float3(metallicRoughness.rg, 0.f);
	}
	if (renderMode.mode == 6) {
		float3(occlusion, occlusion, occlusion);
	}
	if (renderMode.mode == 7) {
		float3(metallicRoughness.r, metallicRoughness.g, occlusion);
	}
	if (renderMode.mode == 8) {
		hitColor = normal;
	}
	if (renderMode.mode == 9) {
		// World space normals
		float3 vertN = triNormal[indices[vertId + 0]] * barycentrics.x +
			triNormal[indices[vertId + 1]] * barycentrics.y +
			triNormal[indices[vertId + 2]] * barycentrics.z;
		float3 modelSpaceN = mul(vertN, (float3x3)transform);
		float3x3 upperLeft3x3ObjectToWorld = (float3x3)ObjectToWorld3x4();
		float3 transformedNormal = normalize(mul(modelSpaceN, (float3x3)ObjectToWorld3x4()));
		hitColor = (transformedNormal + 1.f) * 0.5f;
	}
	if (renderMode.mode == 10) {
		hitColor = emissive;
	}
	payload.colorAndDistance = float4(hitColor, RayTCurrent());
}

[shader("closesthit")]
void ShadedClosestHit(inout HitInfo payload, Attributes attrib)
{
	
	float3 barycentrics =
		float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);
	// to get the first vertex index, we multiply the ID on 3
	uint vertId = 3 * PrimitiveIndex();
	// Get colors from vertex data
	StructuredBuffer<uint> primIndexes = ResourceDescriptorHeap[heapIndexes[COMMON_RESOURCE_OFFSET + InstanceID()]];
	uint primHeapIndex = primIndexes[GeometryIndex()];

	float3 hitColor = float3(1.f / 256.f, 1.f / 256.f, 1.f / 256.f) * float(primHeapIndex % 256);

	StructuredBuffer<MaterialStruct> MaterialStructs = ResourceDescriptorHeap[primHeapIndex];
	MaterialStruct material = MaterialStructs[0];// we have only one

	StructuredBuffer<float4x4> transforms = ResourceDescriptorHeap[primHeapIndex + 1]; // + Material
	float4x4 transform = transforms[0];
	StructuredBuffer<float3> triVertex = ResourceDescriptorHeap[primHeapIndex + 2]; // + Material + Transform
	StructuredBuffer<float3> triNormal = ResourceDescriptorHeap[primHeapIndex + 3]; // + Material + Transform + Positions
	StructuredBuffer<float4> triTangent = ResourceDescriptorHeap[primHeapIndex + 3 + material.hasNormals]; // + Material + Transform + Positions + Normals(optional)
	StructuredBuffer<float4> triColor = ResourceDescriptorHeap[primHeapIndex + 3 + material.hasNormals + material.hasTangents]; // + Material + Transform + Positions + Normals(optional) + Tangents(optional)
	
	StructuredBuffer<int> indices = ResourceDescriptorHeap[primHeapIndex + 3 + material.hasNormals + material.hasTangents + material.hasColors + material.hasTexcoords]; // + Material + Positions + Normals(optional) + Tangents(optional) + Colors(optional) + Texcoords(optional)
	float4 baseColor = float4(0.f, 0.f, 0.f, 0.f);
	if (material.baseTextureIndex >= 0) {
		Texture2D baseColorTexture = ResourceDescriptorHeap[material.baseTextureIndex];
		SamplerState baseColorSampler = SamplerDescriptorHeap[material.baseTextureSamplerIndex];
		StructuredBuffer<float2> triTexcoord = ResourceDescriptorHeap[primHeapIndex + 3 + material.hasNormals +
			material.hasTangents + material.hasColors + material.texCoordIdBase]; // + Material + Transform + Positions + Normals(optional) + Tangents(optional) + Colors(optional) + Texture coords for this prim texture
		float2 uv = triTexcoord[indices[vertId + 0]] * barycentrics.x +
			triTexcoord[indices[vertId + 1]] * barycentrics.y +
			triTexcoord[indices[vertId + 2]] * barycentrics.z;

		uint m, w, h, numLevels;
		baseColorTexture.GetDimensions(m, w, h, numLevels);

		//float2 derivX = ddx(uv); 
		//float2 derivY = ddy(uv);
		////float mipLevel = float(log2(max(length(ddx(uv)), length(ddy(uv)))));
		//float delta_max_sqr = max(dot(derivX, derivX), dot(derivY, derivY));
		//float mip = 0.5 * log2(delta_max_sqr) * float(numLevels);
		baseColor = baseColorTexture.SampleLevel(baseColorSampler, uv, float(renderMode.mode)) * material.baseColor;

		
	}
	float2 metallicRoughness = float2(0.f, 0.f);
	if (material.metallicRoughnessTextureIndex >= 0) {
		Texture2D metallicRoughnessTexture = ResourceDescriptorHeap[material.metallicRoughnessTextureIndex];
		SamplerState metallicRoughnessSampler = SamplerDescriptorHeap[material.metallicRoughnessTextureSamplerIndex];
		StructuredBuffer<float2> triTexcoord = ResourceDescriptorHeap[primHeapIndex + 3 + material.hasNormals +
			material.hasTangents + material.hasColors + material.texCoordIdMR]; // + Material + Transform + Positions + Normals(optional) + Tangents(optional) + Colors(optional) + Texture coords for this prim texture
		float2 uv = triTexcoord[indices[vertId + 0]] * barycentrics.x +
			triTexcoord[indices[vertId + 1]] * barycentrics.y +
			triTexcoord[indices[vertId + 2]] * barycentrics.z;
		metallicRoughness = metallicRoughnessTexture.SampleLevel(metallicRoughnessSampler, uv, 0).rg;
		metallicRoughness.r *= material.metallicFactor;
		metallicRoughness.g *= material.roughnessFactor;
	}
	float occlusion = 0.f;
	if (material.occlusionTextureIndex >= 0) {
		Texture2D occlusionTexture = ResourceDescriptorHeap[material.occlusionTextureIndex];
		SamplerState occlusionTextureSampler = SamplerDescriptorHeap[material.occlusionTextureSamplerIndex];
		StructuredBuffer<float2> triTexcoord = ResourceDescriptorHeap[primHeapIndex + 3 + material.hasNormals +
			material.hasTangents + material.hasColors + material.texCoordIdOcclusion]; // + Material + Transform + Positions + Normals(optional) + Tangents(optional) + Colors(optional) + Texture coords for this prim texture
		float2 uv = triTexcoord[indices[vertId + 0]] * barycentrics.x +
			triTexcoord[indices[vertId + 1]] * barycentrics.y +
			triTexcoord[indices[vertId + 2]] * barycentrics.z;
		occlusion = occlusionTexture.SampleLevel(occlusionTextureSampler, uv, 0).b; // if it is a separate texture will b work? it should, as it is usually 3 same values for RGB
		// occludedColor = lerp(color, color * <sampled occlusion
		// texture value>, <occlusion strength>) - from GLTF spec - we will need later for PBR 
		//material.strengthOcclusion
	}
	float3 normal = float3(0.f, 0.f, 0.f);
	if (material.normalTextureIndex >= 0) {
		Texture2D normalTexture = ResourceDescriptorHeap[material.normalTextureIndex];
		SamplerState normalTextureSamplerIndex = SamplerDescriptorHeap[material.normalTextureSamplerIndex];
		StructuredBuffer<float2> triTexcoord = ResourceDescriptorHeap[primHeapIndex + 3 + material.hasNormals +
			material.hasTangents + material.hasColors + material.texCoordIdNorm]; // + Material + Transform + Positions + Normals(optional) + Tangents(optional) + Colors(optional) + Texture coords for this prim texture
		float2 uv = triTexcoord[indices[vertId + 0]] * barycentrics.x +
			triTexcoord[indices[vertId + 1]] * barycentrics.y +
			triTexcoord[indices[vertId + 2]] * barycentrics.z;
		normal = normalTexture.SampleLevel(normalTextureSamplerIndex, uv, 0);
		// scaledNormal = normalize((normal * 2.0f - 1.0f) * float3(material.scaleNormal, material.scaleNormal, 1.0f))
		// scaled - part of GLTF spec
	}
	float3 emissive = float3(0.f, 0.f, 0.f);
	if (material.emissiveTextureIndex >= 0) {
		Texture2D emissiveTexture = ResourceDescriptorHeap[material.emissiveTextureIndex];
		SamplerState emissiveTextureSamplerIndex = SamplerDescriptorHeap[material.emissiveTextureSamplerIndex];
		StructuredBuffer<float2> triTexcoord = ResourceDescriptorHeap[primHeapIndex + 3 + material.hasNormals +
			material.hasTangents + material.hasColors + material.texCoordIdEmiss]; // + Material + Transform + Positions + Normals(optional) + Tangents(optional) + Colors(optional) + Texture coords for this prim texture
		float2 uv = triTexcoord[indices[vertId + 0]] * barycentrics.x +
			triTexcoord[indices[vertId + 1]] * barycentrics.y +
			triTexcoord[indices[vertId + 2]] * barycentrics.z;
		emissive = emissiveTexture.SampleLevel(emissiveTextureSamplerIndex, uv, 0) * material.emisiveFactor;
	}

	if (renderMode.mode == 0) {
		// Vertex colors
		hitColor = float3(float4(triColor[indices[vertId + 0]] * barycentrics.x +
			triColor[indices[vertId + 1]] * barycentrics.y +
			triColor[indices[vertId + 2]] * barycentrics.z).xyz);
	}
	if (renderMode.mode == 1) {
		//// Model space normals
		hitColor = (triNormal[indices[vertId + 0]] + 1.f) * 0.5 * barycentrics.x +
			(triNormal[indices[vertId + 1]] + 1.f) * 0.5 * barycentrics.y +
			(triNormal[indices[vertId + 2]] + 1.f) * 0.5 * barycentrics.z;
	}
	if (renderMode.mode == 2) {
		hitColor = float3(baseColor.xyz);
	}
	if (renderMode.mode == 3) {
		hitColor = float3(metallicRoughness.r, metallicRoughness.r, metallicRoughness.r);
	}
	if (renderMode.mode == 4) {
		hitColor = float3(metallicRoughness.g, metallicRoughness.g, metallicRoughness.g);
	}
	if (renderMode.mode == 5) {
		hitColor = float3(metallicRoughness.rg, 0.f);
	}
	if (renderMode.mode == 6) {
		float3(occlusion, occlusion, occlusion);
	}
	if (renderMode.mode == 7) {
		float3(metallicRoughness.r, metallicRoughness.g, occlusion);
	}
	if (renderMode.mode == 8) {
		hitColor = normal;
	}
	if (renderMode.mode == 9) {
		// World space normals
		float3 vertN = triNormal[indices[vertId + 0]] * barycentrics.x +
			triNormal[indices[vertId + 1]] * barycentrics.y +
			triNormal[indices[vertId + 2]] * barycentrics.z;
		float3 modelSpaceN = mul(vertN, (float3x3)transform);
		float3x3 upperLeft3x3ObjectToWorld = (float3x3)ObjectToWorld3x4();
		float3 transformedNormal = normalize(mul(modelSpaceN, (float3x3)ObjectToWorld3x4()));
		hitColor = (transformedNormal + 1.f) * 0.5f;
	}
	if (renderMode.mode == 10) {
		hitColor = emissive;
	}
	if (renderMode.mode == 11) {
		float3 lightPos = float3(0, 2.5, 0);
		// Find the world - space hit position 
		float3 worldOriginLight = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
		float3 lightDir = normalize(lightPos - worldOriginLight);
		// Fire a shadow ray. The direction is hard-coded here, but can be fetched 
		// from a constant-buffer 
		RayDesc ray;
		ray.Origin = worldOriginLight;
		ray.Direction = lightDir;
		ray.TMin = 0.01;
		ray.TMax = 100000;
		bool hit = true;
		// Initialize the ray payload 
		ShadowHitInfo shadowPayload;
		shadowPayload.isHit = false; // Trace the ray 
		RaytracingAccelerationStructure sceneBVH = ResourceDescriptorHeap[heapIndexes[1]];
		TraceRay(sceneBVH, RAY_FLAG_NONE, 0xFF, 1, 0, 1, ray, shadowPayload);
		float factor = shadowPayload.isHit ? 0.3 : 1.0;
		hitColor = baseColor * factor;
	}
	hitColor = float3(baseColor.xyz);
	payload.colorAndDistance = float4(hitColor, RayTCurrent());
}