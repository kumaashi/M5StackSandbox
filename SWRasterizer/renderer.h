//
// Renderer.h
//
#pragma once

#include <stdint.h>

//spec configuration
#define EXT_NUM 2
#define WIDTH (320)
#define HEIGHT (240 / EXT_NUM)
#define TILE_X_DIV 1
#define TILE_Y_DIV 6
#define TILE_X_SIZE (WIDTH / TILE_X_DIV)
#define TILE_Y_SIZE (HEIGHT / TILE_Y_DIV)
#define MAX_CLIP_COUNT 8 //Don't change.
#define MAX_PRIMITIVE_COUNT 64
#define DEPTH_MAX 0xFFFF

//#define PROFILE_CACHE_UPDATE
//#define PROFILE_DRAW_RASTERIZATION
//#define PROFILE_DRAW_TILE_VISUALIZATION

namespace renderer
{
typedef uint16_t ColorType;
typedef uint16_t DepthType;
typedef uint16_t IndexType;

typedef struct VertexType_t
{
  float x, y, z;
  uint16_t color;
} VertexType;

struct Matrix
{
  float data[16] = {
      1, 0, 0, 0, //v0
      0, 1, 0, 0, //v1
      0, 0, 1, 0, //v2
      0, 0, 0, 1, //v3
  };
};

void Reset();
void SetViewMatrix(Matrix &m);
void SetProjMatrix(Matrix &m);
void SetWorldMatrix(Matrix &m);
void SetVertexPointer(VertexType *ptr);
void SetIndexPointer(IndexType *ptr);
void SetTexturePointer(uint16_t *ptr, int width, int height);
void DrawIndex(int index);
void Present(uint16_t (*clearcallback)(int x, int y) = nullptr);
void Setup();
} // namespace renderer
