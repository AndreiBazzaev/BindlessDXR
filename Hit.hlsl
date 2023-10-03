#include "Common.hlsl"
#define COMMON_RESOURCE_OFFSET 3
#define NUM_PER_PRIMITIVE_BUFFERS 3
// Shading
struct ShadowHitInfo
{
	bool isHit;
};
struct MaterialStruct {
	uint hasNormals;
	uint hasTangents;
	uint hasColors;
	uint hasTexcoords;

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

	StructuredBuffer<float3> triVertex = ResourceDescriptorHeap[primHeapIndex + 1]; // + Material
	StructuredBuffer<float3> triNormal = ResourceDescriptorHeap[primHeapIndex + 2]; // + Material + Positions
	StructuredBuffer<float4> triTangent = ResourceDescriptorHeap[primHeapIndex + 2 + material.hasNormals]; // + Material + Positions + Normals(optional)
	StructuredBuffer<float4> triColor = ResourceDescriptorHeap[primHeapIndex + 2 + material.hasNormals + material.hasTangents]; // + Material + Positions + Normals(optional) + Tangents(optional)
	StructuredBuffer<float2> triTexcoord = ResourceDescriptorHeap[primHeapIndex + 2 + material.hasNormals + material.hasTangents + material.hasColors]; // + Material + Positions + Normals(optional) + Tangents(optional) + Colors(optional)

	StructuredBuffer<int> indices = ResourceDescriptorHeap[primHeapIndex + 2 + material.hasNormals + material.hasTangents + material.hasColors + material.hasTexcoords]; // + Material + Positions + Normals(optional) + Tangents(optional) + Colors(optional) + Texcoords(optional)



	//float3 hitColor = float3(0.f, 1.f, 0.f);

	//// Model space normals
	hitColor = (triNormal[indices[vertId + 0]] + 1.f) * 0.5 * barycentrics.x +
		(triNormal[indices[vertId + 1]] + 1.f) * 0.5 * barycentrics.y +
		(triNormal[indices[vertId + 2]] + 1.f) * 0.5 * barycentrics.z;
	
	hitColor = float3(1.f / 4.f, 1.f / 4.f, 1.f / 4.f) * float((material.hasNormals + material.hasTangents + material.hasColors + material.hasTexcoords) % 5);
	payload.colorAndDistance = float4(hitColor, RayTCurrent());
}
// SECOND HIT SHADER for plane
[shader("closesthit")]
void PlaneClosestHit(inout HitInfo payload, Attributes attrib)
{
	float3 lightPos = float3(2, 2, -2); 
	// Find the world - space hit position 
	float3 worldOrigin = WorldRayOrigin() + RayTCurrent() * WorldRayDirection(); 
	float3 lightDir = normalize(lightPos - worldOrigin); 
	// Fire a shadow ray. The direction is hard-coded here, but can be fetched 
	// from a constant-buffer 
	RayDesc ray; 
	ray.Origin = worldOrigin; 
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
	float3 barycentrics = float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y); 


	uint vertId = 3 * PrimitiveIndex();
	StructuredBuffer<float3> triNormal = ResourceDescriptorHeap[heapIndexes[COMMON_RESOURCE_OFFSET + InstanceID()] + GeometryIndex() * NUM_PER_PRIMITIVE_BUFFERS + 1];
	StructuredBuffer<int> indices = ResourceDescriptorHeap[heapIndexes[COMMON_RESOURCE_OFFSET + InstanceID()] + GeometryIndex() * NUM_PER_PRIMITIVE_BUFFERS + 2];


	// Model space normals
	float3 hitColor = (triNormal[indices[vertId + 0]] + 1.f) * 0.5 * barycentrics.x +
		(triNormal[indices[vertId + 1]] + 1.f) * 0.5 * barycentrics.y +
		(triNormal[indices[vertId + 2]] + 1.f) * 0.5 * barycentrics.z;

	hitColor = hitColor * factor;
	payload.colorAndDistance = float4(hitColor, RayTCurrent());
}