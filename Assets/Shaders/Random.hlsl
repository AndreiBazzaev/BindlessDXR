uint GetWangHashSeed(uint seed)
{
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return seed;
}
float rand(uint seed) {
    seed ^= (seed << 13);     
    seed ^= (seed >> 17);     
    seed ^= (seed << 5);     
    return seed / 4294967296.0; // float [0, 1] ( divided on maximum 32-bit unsigned integer) 
}