#include "Common.hlsl"
#define COMMON_RESOURCE_OFFSET 4 // RT output + TLAS + camera
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
	

	
	//Bindless
	ConstantBuffer<CamStruct> camBuffer = ResourceDescriptorHeap[heapIndexes[2]];
	RaytracingAccelerationStructure sceneBVH = ResourceDescriptorHeap[heapIndexes[1]];
	RWTexture2D<float4>  gOutput = ResourceDescriptorHeap[heapIndexes[0]];

	uint2 launchIndex = DTid.xy;
	float2 dims = float2(camBuffer.width, camBuffer.height);
	float2 d = (((launchIndex.xy + 0.5f) / dims.xy) * 2.f - 1.f);
	float4 returnColorDistance = float4(0.f, 0.f, 0.f, 0.f);
	RayDesc ray;

	ray.Origin = mul(camBuffer.viewInv, float4(0, 0, 0, 1));
	float4 target = mul(camBuffer.projectionInv, float4(d.x, -d.y, 1, 1));
	ray.Direction = mul(camBuffer.viewInv, float4(target.xyz, 0));
	ray.TMin = 0;
	ray.TMax = 100000;
	
	// ----------------- INLINE
	RayQuery<RAY_FLAG_CULL_NON_OPAQUE |
		RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
	// Set up a trace.  No work is done yet.
	query.TraceRayInline(
		sceneBVH,
		RAY_FLAG_NONE, // OR'd with flags above
		0xFF,
		ray);
	// Proceed() below is where behind-the-scenes traversal happens,
	// including the heaviest of any driver inlined code.
	// In this simplest of scenarios, Proceed() only needs
	// to be called once rather than a loop:
	// Based on the template specialization above,
	// traversal completion is guaranteed.
	query.Proceed();
	if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
	{
		float colorFromID = float(query.CommittedInstanceIndex()) / 13.f;
		/*ShadeMyTriangleHit(
			q.CommittedInstanceIndex(),
			q.CommittedPrimitiveIndex(),
			q.CommittedGeometryIndex(),
			q.CommittedRayT(),
			q.CommittedTriangleBarycentrics(),
			q.CommittedTriangleFrontFace());*/
		float2 attribBary = query.CommittedTriangleBarycentrics();
		float3 barycentrics =
			float3(1.f - attribBary.x - attribBary.y, attribBary.x, attribBary.y);
		// to get the first vertex index, we multiply the ID on 3
		uint vertId = 3 * query.CommittedPrimitiveIndex();
		// Get colors from vertex data
		StructuredBuffer<uint> primIndexes = ResourceDescriptorHeap[heapIndexes[COMMON_RESOURCE_OFFSET + query.CommittedInstanceIndex()]];
		uint primHeapIndex = primIndexes[query.CommittedGeometryIndex()];

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
		float mip = query.CommittedRayT() / 5.f; // NEEDS TO BE REPLACED BY SOME FANCY SMART METHOD
		float4 baseColor = float4(0.f, 0.f, 0.f, 0.f);
		if (material.baseTextureIndex >= 0) {
			Texture2D baseColorTexture = ResourceDescriptorHeap[material.baseTextureIndex];
			SamplerState baseColorSampler = SamplerDescriptorHeap[material.baseTextureSamplerIndex];
			StructuredBuffer<float2> triTexcoord = ResourceDescriptorHeap[primHeapIndex + 3 + material.hasNormals +
				material.hasTangents + material.hasColors + material.texCoordIdBase]; // + Material + Transform + Positions + Normals(optional) + Tangents(optional) + Colors(optional) + Texture coords for this prim texture
			float2 uv = triTexcoord[indices[vertId + 0]] * barycentrics.x +
				triTexcoord[indices[vertId + 1]] * barycentrics.y +
				triTexcoord[indices[vertId + 2]] * barycentrics.z;

			//uint m, w, h, numLevels;
			//baseColorTexture.GetDimensions(m, w, h, numLevels);

			//float2 derivX = ddx(uv); 
			//float2 derivY = ddy(uv);
			////float mipLevel = float(log2(max(length(ddx(uv)), length(ddy(uv)))));
			//float delta_max_sqr = max(dot(derivX, derivX), dot(derivY, derivY));
			//float mip = 0.5 * log2(delta_max_sqr) * float(numLevels);
			baseColor = baseColorTexture.SampleLevel(baseColorSampler, uv, mip) * material.baseColor;


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
			metallicRoughness = metallicRoughnessTexture.SampleLevel(metallicRoughnessSampler, uv, mip).rg;
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
			occlusion = occlusionTexture.SampleLevel(occlusionTextureSampler, uv, mip).b; // if it is a separate texture will b work? it should, as it is usually 3 same values for RGB
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
			normal = normalTexture.SampleLevel(normalTextureSamplerIndex, uv, mip);
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
			emissive = emissiveTexture.SampleLevel(emissiveTextureSamplerIndex, uv, mip) * material.emisiveFactor;
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
			float3x3 upperLeft3x3ObjectToWorld = (float3x3)query.CommittedWorldToObject3x4();
			float3 transformedNormal = normalize(mul(modelSpaceN, (float3x3)query.CommittedWorldToObject3x4()));
			hitColor = (transformedNormal + 1.f) * 0.5f;
		}
		if (renderMode.mode == 10) {
			hitColor = emissive;
		}


		returnColorDistance = float4(hitColor, query.CommittedRayT());
	}
	else // COMMITTED_NOTHING - MISS
	{
		float rampy = launchIndex.y / dims.y;
		float rampx = launchIndex.x / dims.x;
		returnColorDistance = float4(rampx, rampx * rampy, rampy, -1.0f);
	}
	//--------------------
	/*
	// SEEDING
	uint pixelID = dims.x * DispatchRaysIndex().y + DispatchRaysIndex().x;
	uint seed = GetWangHashSeed(pixelID * 3 + 1);*/

	// We output the data from the ray's payload
	gOutput[launchIndex] = returnColorDistance;
}
