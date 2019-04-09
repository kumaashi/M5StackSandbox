//
// Renderer.cpp
//
#include <M5Stack.h>
#include <vector>

#include "vecmat.h"
#include "blit.h"
#include "renderer.h"

#define TILE_MAX (BLIT_QUEUE_MAX * 2 + 1)

namespace renderer
{

struct Point
{
  float x, y, z, w;
};

struct VertexFormatNdc
{
  float x, y, z, w;
  float u, v;
};

struct ScreenPos
{
  uint16_t x, y;
  DepthType z;
  float u, v, w;
};

template <typename T, size_t sz>
struct Container
{
  size_t index = 0;
  T obj[sz];
  inline void push_back(const T &a)
  {
    obj[index++] = a;
  }
  inline void clear()
  {
    index = 0;
  }
  inline size_t size()
  {
    return index;
  }
  inline T *data()
  {
    return &obj[0];
  }
  inline T &operator[](size_t i)
  {
    return obj[i];
  }
};

//Vertex Parameters
static VertexType *vertexDataPointer = nullptr;
static IndexType *indexDataPointer = nullptr;

//Matrix Parameter
static Matrix viewMatrix;
static Matrix projMatrix;
static Matrix worldMatrix;

//Texture Parameter
static uint16_t nullTexture = 0xF0F0;
static uint16_t *texturePointer = &nullTexture;
static uint16_t textureWidth = 1;
static uint16_t textureHeight = 1;

//Vertex Internal
static Container<VertexFormatNdc, MAX_PRIMITIVE_COUNT> vertexListTemp;

//TileBuffers
//setup and create buffers
static ColorType vtilebuffer[TILE_MAX * TILE_X_SIZE * TILE_Y_SIZE];
static DepthType vztilebuffer[TILE_MAX * TILE_X_SIZE * TILE_Y_SIZE];
static ColorType *tilebuffer = nullptr;
static DepthType *ztilebuffer = nullptr;

//Vertex lists
//static std::vector<VertexFormatNdc> vertexListTemp;
//static std::vector<VertexFormatNdc> bucket[2];
static Container<VertexFormatNdc, MAX_CLIP_COUNT> bucket[2];
static std::vector<ScreenPos> screenpos;
static int present_index = 0; //for Debug

inline int edgefunc(uint16_t *a, uint16_t *b, uint16_t *c)
{
  return (c[0] - a[0]) * (b[1] - a[1]) - (c[1] - a[1]) * (b[0] - a[0]);
}

inline float edgefuncf(float *a, float *b, float *c)
{
  return (c[0] - a[0]) * (b[1] - a[1]) - (c[1] - a[1]) * (b[0] - a[0]);
}

static uint16_t MixColor(uint16_t color_a, uint16_t color_b, float x)
{
  const uint16_t RMASK = 0xF100;
  const uint16_t GMASK = 0x07E0;
  const uint16_t BMASK = 0x001F;
  uint16_t r = mix(color_a & RMASK, color_b & RMASK, x) & RMASK;
  uint16_t g = mix(color_a & GMASK, color_b & GMASK, x) & GMASK;
  uint16_t b = mix(color_a & BMASK, color_b & BMASK, x) & BMASK;
  return r | g | b;
}

static VertexFormatNdc MixVertexFormat(VertexFormatNdc *a, VertexFormatNdc *b, float x)
{
  VertexFormatNdc ret;
  ret.x = mix(a->x, b->x, x);
  ret.y = mix(a->y, b->y, x);
  ret.z = mix(a->z, b->z, x);
  ret.w = mix(a->w, b->w, x);
  ret.u = mix(a->u, b->u, x);
  ret.v = mix(a->v, b->v, x);
  return ret;
};

static void PushScreenPos(VertexFormatNdc &v)
{
  uint16_t x = uint16_t((0.5f * (v.x + 1.0) * (WIDTH - 1)) - 0.5f);
  uint16_t y = uint16_t((0.5f * (v.y + 1.0) * (HEIGHT - 1)) + 0.5f);
  DepthType z = DepthType(DEPTH_MAX * (v.z * 0.5f + 0.5f));
  auto tu = v.u;
  auto tv = v.v;
  auto tw = v.w;
  screenpos.push_back({x, y, z, tu, tv, tw});
}

void Reset()
{
  vertexListTemp.clear();
  bucket[0].clear();
  bucket[1].clear();
  screenpos.clear();
}

void SetVertexPointer(VertexType *ptr)
{
  vertexDataPointer = ptr;
}

void SetIndexPointer(IndexType *ptr)
{
  indexDataPointer = ptr;
}

void SetViewMatrix(Matrix &m)
{
  viewMatrix = m;
}

void SetProjMatrix(Matrix &m)
{
  projMatrix = m;
}

void SetWorldMatrix(Matrix &m)
{
  worldMatrix = m;
}

void DrawIndex(int index)
{
  Matrix mvpMatrix;
  MatrixMult(mvpMatrix.data, projMatrix.data, viewMatrix.data);
  MatrixMult(mvpMatrix.data, mvpMatrix.data, worldMatrix.data);
  uint16_t color = 0;

  for (int i = 0; i < index; i++)
  {
    IndexType idx = indexDataPointer[i];
    VertexType *p = &vertexDataPointer[idx];
    if ((i % 3) == 0)
      color = p->color;
    float v[4 + 2];
    v[0] = p->x;
    v[1] = p->y;
    v[2] = p->z;
    v[3] = 1.0f;

    //tentative.
    v[4] = float((p->color >> 0) & 0x00FF) / float(textureWidth);
    v[5] = float((p->color >> 8) & 0x00FF) / float(textureHeight);
    float ndc_v[4];
    VecMulMatrix(ndc_v, v, mvpMatrix.data);

    float rW = 1.0f / ndc_v[3];
    ndc_v[0] *= rW;
    ndc_v[1] *= rW;
    ndc_v[2] *= rW;
    ndc_v[3] = rW;
    vertexListTemp.push_back({ndc_v[0], ndc_v[1], ndc_v[2], ndc_v[3], v[4], v[5]});
  }

  if (vertexListTemp.size() < 3)
    return;

  //https://www.geeksforgeeks.org/polygon-clipping-sutherland-hodgman-algorithm-please-change-bmp-images-jpeg-png/
  for (int i = 0; i < vertexListTemp.size(); i += 3)
  {
    static const Point N[6] = {
        {0, 0, 1, 1},  //0->1
        {0, 0, -1, 1}, //1->0
        {0, 1, 0, 1},  //0->1
        {0, -1, 0, 1}, //1->0
        {1, 0, 0, 1},  //0->1
        {-1, 0, 0, 1}, //1->0
    };
    bucket[0].clear();
    bucket[1].clear();

    bucket[0].push_back(vertexListTemp[i + 0]);
    bucket[0].push_back(vertexListTemp[i + 1]);
    bucket[0].push_back(vertexListTemp[i + 2]);
    bool isDone = false;
    for (int i = 0; i < 6; i++)
    {
      Point n = N[i];
      auto &src = bucket[(i + 0) & 1];
      auto &dest = bucket[(i + 1) & 1];
      dest.clear();
      auto count = src.size();
      if (count <= 0)
      {
        isDone = true;
        break;
      }
      auto vprev = src[count - 1];
      auto tprev = dot((float *)&vprev, (float *)&n);
      for (int i = 0; i < count; i++)
      {
        auto v = src[i];
        auto d = dot((float *)&v, (float *)&n);
        auto dt = (tprev / (tprev - d));
        auto case0 = (d >= 0 && tprev >= 0);
        auto case1 = (d < 0 && tprev >= 0);
        auto case2 = (d >= 0 && tprev < 0);
        if (case0)
          dest.push_back(v);
        if (case1)
          dest.push_back(MixVertexFormat(&vprev, &v, dt));
        if (case2)
        {
          dest.push_back(MixVertexFormat(&vprev, &v, dt));
          dest.push_back(v);
        }
        vprev = v;
        tprev = d;
      } //count
    }   //plane
    if (isDone)
      continue;
    auto &result = bucket[0];

    //to trianglefan.
    if (result.size() >= 3)
    {
      int count = result.size() - 2;
      int baseindex = 1;
      auto vbase = result[0];
      for (int i = 0; i < count; i++)
      {
        // back face cull. todo programmable.
        auto check = edgefuncf((float *)&vbase, (float *)&result[baseindex + 0], (float *)&result[baseindex + 1]);
        if (check <= 0)
        {
          break;
        }

        //prepare done.
        PushScreenPos(vbase);
        PushScreenPos(result[baseindex + 0]);
        PushScreenPos(result[baseindex + 1]);
        baseindex++;
      }
    }
  }
  vertexListTemp.clear();
}

void SetTexturePointer(uint16_t *ptr, int width, int height)
{
  texturePointer = ptr;
  textureWidth = width;
  textureHeight = height;
}

//
// Present(Rasterizer)
//
void Present(uint16_t (*clearcallback)(int x, int y))
{
  //present_index++; //fake interlace, for Debug.
  int discard_count = 0;

#ifdef PROFILE_DRAW_RASTERIZATION
  digitalWrite(22, LOW);
#endif //PROFILE_DRAW_RASTERIZATION

  //rasterization per tiles, and push blit queue.
  int vbuffer_size = screenpos.size();
  int textureWidthMask = textureWidth - 1;
  int textureHeightMask = textureHeight - 1;

  //Create Primitive Lists
  std::vector<uint8_t> chainBuffer;
  const uint8_t mark = 0xFF;
  for (int ty = 0; ty < HEIGHT; ty += TILE_Y_SIZE)
  {
    for (int tx = 0; tx < WIDTH; tx += TILE_X_SIZE)
    {
      for (int i = 0; i < vbuffer_size; i += 3)
      {
        ScreenPos v0 = screenpos[i + 0];
        ScreenPos v1 = screenpos[i + 1];
        ScreenPos v2 = screenpos[i + 2];
        int32_t min_x = _min(_min(v0.x, v1.x), v2.x);
        int32_t max_x = _max(_max(v0.x, v1.x), v2.x);
        int32_t min_y = _min(_min(v0.y, v1.y), v2.y);
        int32_t max_y = _max(_max(v0.y, v1.y), v2.y);
        int32_t min_tx = tx;
        int32_t min_ty = ty;
        int32_t max_tx = tx + TILE_X_SIZE;
        int32_t max_ty = ty + TILE_X_SIZE;
        auto isvalid = !(min_tx > max_x || max_tx < min_x || min_ty > max_y || max_ty < min_y);
        if (isvalid)
        {
          chainBuffer.push_back(i / 3);
        }
      }
      chainBuffer.push_back(mark);
    }
  }

  uint8_t *p = chainBuffer.data();
  for (int ty = 0; ty < HEIGHT; ty += TILE_Y_SIZE)
  {
    for (int tx = 0; tx < WIDTH; tx += TILE_X_SIZE)
    {
      int tileNumber = blit::GetIndex();

      //setup buffer
      ColorType *backbuffer = tilebuffer + (tileNumber % TILE_MAX) * TILE_X_SIZE * TILE_Y_SIZE;
      DepthType *zbuffer = ztilebuffer + (tileNumber % TILE_MAX) * TILE_X_SIZE * TILE_Y_SIZE;

      //clear buffer
      for (int y = 0; y < TILE_Y_SIZE; y++)
      {
        for (int x = 0; x < TILE_X_SIZE; x++)
        {
          int i = x + y * TILE_X_SIZE;
          backbuffer[i] = clearcallback(tx + x, ty + y);
          zbuffer[i] = DEPTH_MAX;
        }
      }

      //rasterization per polygons;
      int listCount = 0;
      while (p != mark)
      {
        listCount++;
        int i = *p++;

        i *= 3;

        ScreenPos v0 = screenpos[i + 0];
        ScreenPos v1 = screenpos[i + 1];
        ScreenPos v2 = screenpos[i + 2];

        //https://www.usfx.bo/nueva/vicerrectorado/citas/TECNOLOGICAS_20/Electronica/62.pdf
        int32_t min_x = _max(tx, _min(_min(v0.x, v1.x), v2.x));
        int32_t max_x = _min(tx + TILE_X_SIZE - 1, _max(_max(v0.x, v1.x), v2.x));
        int32_t min_y = _max(ty, _min(_min(v0.y, v1.y), v2.y));
        int32_t max_y = _min(ty + TILE_Y_SIZE - 1, _max(_max(v0.y, v1.y), v2.y));
        int32_t min_z = _min(_min(v0.z, v1.z), v2.z);
        int32_t max_z = _max(_max(v0.z, v1.z), v2.z);
        int32_t rtx = min_x - tx;
        int32_t rty = min_y - ty;
        int32_t xab = (v1.x - v0.x);
        int32_t xbc = (v2.x - v1.x);
        int32_t xca = (v0.x - v2.x);
        int32_t yab = (v1.y - v0.y);
        int32_t ybc = (v2.y - v1.y);
        int32_t yca = (v0.y - v2.y);

        int32_t eab = (rtx - v0.x) * yab - (rty - v0.y) * xab;
        int32_t ebc = (rtx - v1.x) * ybc - (rty - v1.y) * xbc;
        int32_t eca = (rtx - v2.x) * yca - (rty - v2.y) * xca;
        int32_t r_e = -xca * yab + yca * xab;
        if (r_e == 0)
        {
          r_e = 1;
        }
        int32_t zab = v1.z - v0.z;
        int32_t zca = v0.z - v2.z;
        int32_t zdx = (yca * zab - zca * yab) / r_e;
        int32_t zdy = (zca * xab - xca * zab) / r_e;
        float uab = v1.u - v0.u;
        float uca = v0.u - v2.u;
        float vab = v1.v - v0.v;
        float vca = v0.v - v2.v;
        float wab = v1.w - v0.w;
        float wca = v0.w - v2.w;

        eab -= ty * xab;
        ebc -= ty * xbc;
        eca -= ty * xca;
        eab += tx * yab;
        ebc += tx * ybc;
        eca += tx * yca;

        int32_t ry = min_y - ty;
        for (int32_t iy = min_y; iy <= max_y; iy++)
        {
          int32_t e0 = eab;
          int32_t e1 = ebc;
          int32_t e2 = eca;
          int32_t rx = min_x - tx;
          for (int32_t ix = min_x; ix <= max_x; ix++)
          {
            if (e0 >= 0 && e1 >= 0 && e2 >= 0)
            {
              int idx = rx + ry * TILE_X_SIZE;
              int32_t zz = 0;
              zz += (tx + rx - v0.x) * zdx;
              zz += (ty + ry - v0.y) * zdy;
              zz += v0.z;
              if (zz < zbuffer[idx])
              {
                zbuffer[idx] = zz;
                int u = ((v0.u * e1) + (v1.u * e2) + (v2.u * e0)) * textureWidth;
                int v = ((v0.v * e1) + (v1.v * e2) + (v2.v * e0)) * textureHeight;
                //todo optimization.
                u /= r_e;
                v /= r_e;
                u &= textureWidthMask;
                v &= textureHeightMask;
                backbuffer[idx] = texturePointer[u + v * textureWidth];
              }
            }
            e0 += yab;
            e1 += ybc;
            e2 += yca;
            rx++;
          }
          eab -= xab;
          ebc -= xbc;
          eca -= xca;
          ry++;
        }
      }

#ifdef PROFILE_DRAW_TILE_VISUALIZATION
      for (int y = 0; y < TILE_Y_SIZE; y++)
      {
        for (int x = 0; x < TILE_X_SIZE; x++)
        {
          int i = x + y * TILE_X_SIZE;
          backbuffer[i] += listCount;
        }
      }
#endif //PROFILE_DRAW_TILE_VISUALIZATION

      blit::Request(tx, ty * EXT_NUM + (present_index & 1), TILE_X_SIZE, TILE_Y_SIZE, backbuffer, zbuffer, EXT_NUM);
      p++;
    }
  }
#ifdef PROFILE_DRAW_RASTERIZATION
  digitalWrite(22, HIGH);
#endif //PROFILE_DRAW_RASTERIZATION

  static int count = 0;
  if ((count % 60) == 0)
  {
    printf("vbuffer_size = %d, poly_count=%d discard_count=%d\n", vbuffer_size, vbuffer_size / 3, discard_count);
  }
  count++;
}

void Setup()
{
  //blitter setup.
  blit::Setup();

  tilebuffer = &vtilebuffer[0];
  ztilebuffer = &vztilebuffer[0];
}

} // namespace renderer
