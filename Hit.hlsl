#include "Common.hlsl"
// Shading
struct ShadowHitInfo
{
	bool isHit;
};
// Model Data-------------
struct ColorStruct {
	float4 col1;
	float4 col2;
	float4 col3; 
	float4 padding;

	float4x4 padding1;
	float4x4 padding2;
	float4x4 padding3;
};
// Vertex, Index data------
struct STriVertex
{
	float3 vertex;
};
StructuredBuffer<STriVertex> BTriVertex : register(t0);
StructuredBuffer<int> indices: register(t1);
//------------------------



[shader("closesthit")] 
void ClosestHit(inout HitInfo payload, Attributes attrib) 
{
	float3 barycentrics =
		float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);
	// to get the first vertex index, we multiply the ID on 3
	uint vertId = 3 * PrimitiveIndex();
	// Get colors from vertex data

	ConstantBuffer<ColorStruct> colBuffer = ResourceDescriptorHeap[renderResource.instanceDataIndex + InstanceID()];

	float3 hitColor = float3(0.f, 1.f, 0.f);
	hitColor = colBuffer.col1 * float(GeometryIndex() % 4) / 4.f;
	//;
	// REMINDER!!!: Please read up on Default Heap usage. An upload heap is used here for code simplicity and because there are very few verts
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
	RaytracingAccelerationStructure sceneBVH = ResourceDescriptorHeap[renderResource.TlasIndex];
	TraceRay(sceneBVH, RAY_FLAG_NONE, 0xFF, 1, 0, 1, ray, shadowPayload);
	float factor = shadowPayload.isHit ? 0.3 : 1.0; 
	float3 barycentrics = float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y); 
	float4 hitColor = float4(float3(0.7, 0.7, 0.3) * factor, RayTCurrent()); 
	payload.colorAndDistance = float4(hitColor);
}