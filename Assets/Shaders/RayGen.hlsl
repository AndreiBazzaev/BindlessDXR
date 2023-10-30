#include "Common.hlsl"

struct CamStruct {
	float4x4 view;
	float4x4 projection;
	float4x4 viewInv;
	float4x4 projectionInv;
};

[shader("raygeneration")] 
void RayGen() {
	// Initialize the ray payload (data that is retrieved ffrom a single ray)
	HitInfo payload;
	// Setup default "output" value for the ray
	payload.colorAndDistance = float4(0, 0, 0, 0);
	// Get the location within the dispatched 2D grid of work items
	uint2 launchIndex = DispatchRaysIndex().xy;
	float2 dims = float2(DispatchRaysDimensions().xy);
	float2 d = (((launchIndex.xy + 0.5f) / dims.xy) * 2.f - 1.f);

	// Define a ray, consisting of origin, direction, and the min-max distance values
	float aspectRatio = dims.x / dims.y;
	// Perspective
	RayDesc ray;

	//Bindless
	ConstantBuffer<CamStruct> camBuffer = ResourceDescriptorHeap[heapIndexes[2]];
	RaytracingAccelerationStructure sceneBVH = ResourceDescriptorHeap[heapIndexes[1]];
	RWTexture2D<float4>  gOutput = ResourceDescriptorHeap[heapIndexes[0]];

	ray.Origin = mul(camBuffer.viewInv, float4(0, 0, 0, 1));
	float4 target = mul(camBuffer.projectionInv, float4(d.x, -d.y, 1, 1));
	ray.Direction = mul(camBuffer.viewInv, float4(target.xyz, 0));
	ray.TMin = 0;
	ray.TMax = 100000;

	// Seeding
	StructuredBuffer<uint> frameIndexBuffer = ResourceDescriptorHeap[heapIndexes[3]];
	uint frameIndex = frameIndexBuffer[0]; // can be used for more random pos
	uint2 dimentions = DispatchRaysDimensions().xy;
	uint pixelID = dimentions.x * DispatchRaysIndex().y + DispatchRaysIndex().x;
	uint seed = GetWangHashSeed(pixelID * 3 + 1);
	payload.seed = seed;
	// Trace the ray description
	TraceRay(sceneBVH,RAY_FLAG_NONE,0xFF, 0, 0, 0, ray, payload);
	// We output the data from the ray's payload
	gOutput[launchIndex] = float4(payload.colorAndDistance.rgb, 1.f);
}
