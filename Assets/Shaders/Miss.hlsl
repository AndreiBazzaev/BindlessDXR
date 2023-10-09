#include "Common.hlsl"

[shader("miss")]
void Miss(inout HitInfo payload : SV_RayPayload)
{
    
    uint2 launchIndex = DispatchRaysIndex().xy;
    float2 dims = float2(DispatchRaysDimensions().xy);
    float rampy = launchIndex.y / dims.y;
    float rampx = launchIndex.x / dims.x;
    payload.colorAndDistance = float4(rampx, rampx * rampy, rampy, -1.0f);
}