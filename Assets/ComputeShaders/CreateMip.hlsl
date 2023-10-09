
Texture2D<float4> inputTexture : register(t0);
RWTexture2D<float4> outputTexture : register(u0);
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float4 neighbours[4];
    int2 coords[4];
    int2 i_coords = DTid.xy * 2;
    coords[0] = i_coords;
    coords[1] = i_coords + int2(1, 0);
    coords[2] = i_coords + int2(0, 1);
    coords[3] = i_coords + int2(1, 1);
    neighbours[0] = inputTexture[coords[0]];
    neighbours[1] = inputTexture[coords[1]];
    neighbours[2] = inputTexture[coords[2]];
    neighbours[3] = inputTexture[coords[3]];
    float4 sum = neighbours[0] + neighbours[1] + neighbours[2] + neighbours[3];
    float4 average = sum * 0.25;
    outputTexture[DTid.xy] = average;
}