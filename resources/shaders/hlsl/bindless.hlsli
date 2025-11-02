#define bufferSpace space1
#define texture2DSpace space2
#define samplerSpace space3

ByteAddressBuffer BufferTable[] : register(t0, bufferSpace);
Texture2D TextureTable[] : register(t0, texture2DSpace);
SamplerState SamplerTable[] : register(s0, samplerSpace);

#define BUFLOAD(x, y) Load<x>(sizeof(x) * y)
