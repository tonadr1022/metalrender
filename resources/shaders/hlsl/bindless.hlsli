#define bufferSpace space1
// #define texture2DSpace space2

ByteAddressBuffer BufferTable[] : register(t0, bufferSpace);
// Texture2D TextureTable[] : register(t0, texture2DSpace);
#define BUFLOAD(x, y) Load<x>(sizeof(x) * y)
