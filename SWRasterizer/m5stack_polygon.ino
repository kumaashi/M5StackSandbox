//
// m5stack_polygon.ino
//
#include <M5Stack.h>
#include <vector>

#include "vecmat.h"
#include "renderer.h"

#include "m5stack_tex.h"

#define MAX_SIN_TABLE_SIZE 256
float sintable[MAX_SIN_TABLE_SIZE];

void gentable()
{
  float phase = 0;
  for (int i = 0; i < MAX_SIN_TABLE_SIZE; i++)
  {
    sintable[i] = sin(float(i * 2 * 3.141592653f) / float(MAX_SIN_TABLE_SIZE));
  }
}

float usin(int i)
{
  return sintable[i % MAX_SIN_TABLE_SIZE];
}

float ucos(int i)
{
  return sintable[(i + (MAX_SIN_TABLE_SIZE / 4)) % MAX_SIN_TABLE_SIZE];
}

// CUBE DATA
// ref : http://www.opengl-tutorial.org//beginners-tutorials/tutorial-4-a-colored-cube/
static const renderer::VertexType cube_vertex[] = {
    // front
    {-1, -1, 1, 0x003F}, //0
    {1, -1, 1, 0x0000},  //1
    {1, 1, 1, 0x3F00},   //2
    {-1, 1, 1, 0x3F3F},  //3
    // back
    {-1, -1, -1, 0x0000}, //4
    {1, -1, -1, 0x003F},  //5
    {1, 1, -1, 0x3F3F},   //6
    {-1, 1, -1, 0x3F00},  //7
};

static const renderer::IndexType cube_indexs[] = {
    // front
    0, 1, 2, 2, 3, 0, //0
    // top
    3, 2, 6, 6, 7, 3, //1
    // back
    7, 6, 5, 5, 4, 7, //2
    // bottom
    4, 5, 1, 1, 0, 4, //3
    // left
    4, 0, 3, 3, 7, 4, //4
    // right
    1, 5, 6, 6, 2, 1, //5
};

float frand()
{
  return (float(rand() & 0xFFFF) / float(0xFFFF)) * 2.0f - 1.0f;
}

//use sintable
inline void MatrixRotation(float *dest, int x, int y, int z)
{
  MatrixIdent(dest);
  float sin_a = usin(x);
  float cos_a = ucos(x);
  float sin_b = usin(y);
  float cos_b = ucos(y);
  float sin_c = usin(z);
  float cos_c = ucos(z);
  dest[0 + 0] = cos_b * cos_c;
  dest[0 + 1] = -cos_b * sin_c;
  dest[0 + 2] = sin_b;
  dest[4 + 0] = sin_a * sin_b * cos_c + cos_a * sin_c;
  dest[4 + 1] = -sin_a * sin_b * sin_c + cos_a * cos_c;
  dest[4 + 2] = -sin_a * cos_b;
  dest[8 + 0] = -cos_a * sin_b * cos_c + sin_a * sin_c;
  dest[8 + 1] = cos_a * sin_b * sin_c + sin_a * cos_c;
  dest[8 + 2] = cos_a * cos_b;
}

static int update_num = 0;
static uint16_t *tex = (uint16_t *)M5Stack_raw;
void CacheUpdate()
{
  using namespace renderer;
#ifdef PROFILE_CACHE_UPDATE
  digitalWrite(22, LOW);
#endif //PROFILE_CACHE_UPDATE

  //For Debug
  static int isInc = 1;
  if (M5.BtnA.wasReleased())
  {
    isInc = !isInc;
  }

  static float motion_sign = 1;
  if (M5.BtnB.wasReleased())
  {
    motion_sign *= -1;
  }

  static int timer_count = 0;
  if (timer_count > 60)
  {
    motion_sign *= -1;
    timer_count = 0;
  }
  timer_count++;

  static float posdt = 0.0f;
  static float angle = 5.0;
  static float radius = 8.2;
  float delta = 0.03;
  posdt = _clamp(posdt + 0.02 * motion_sign, 0, 1);
  float sposdt = smoothstep(0.2, 0.8, posdt);
  float pos[3] = {
      cos(angle * 0.9f) * radius,
      -sin(angle * 0.8f) * radius,
      sin(angle * 0.85) * radius,
  };

  srand(12345);
  angle += delta * isInc;
  update_num = update_num + 1 * isInc;

  //prepare rendering.
  Matrix view;
  Matrix proj;
  Matrix world;
  MatrixLookAt(view.data, pos[0], pos[1], pos[2], 0, 0, 0, 0, 1, 0);
  MatrixProjection(proj.data, 45.0, 1.25, 1.0, 256.0);
  renderer::Reset();
  renderer::SetViewMatrix(view);
  renderer::SetProjMatrix(proj);
  renderer::SetVertexPointer((VertexType *)cube_vertex);
  renderer::SetIndexPointer((IndexType *)cube_indexs);
  renderer::SetTexturePointer(tex, 64, 64);
  for (int i = 0; i < 16; i++) //max 50
  {
    float x = mix(2.5f * float(i % 4) - 4.0f, frand() * 4.0f, sposdt);
    float y = mix(2.5f * float(i / 4) - 4.0f, frand() * 4.0f, sposdt);
    float z = mix(0.0f * float(i / 4) - 0.0f, frand() * 4.0f, sposdt);
    Matrix scale;
    scale.data[10] = 0.25f;
    Matrix trans;
    VecSet(&trans.data[12], x, y, z, 1);
    Matrix rotate;
    Matrix world;
    MatrixRotation(rotate.data,
                   mix(0, (rand() & 0x7F), smoothstep(0.1f, 0.9f, sposdt)),
                   mix(0, (rand() & 0x7F), smoothstep(0.1f, 0.9f, sposdt)),
                   0);
    MatrixMult(world.data, rotate.data, scale.data);
    MatrixMult(world.data, trans.data, world.data);
    SetWorldMatrix(world);
    DrawIndex(36); //_countof(cube_vertex)
  }
#ifdef PROFILE_CACHE_UPDATE
  digitalWrite(22, HIGH);
#endif //PROFILE_CACHE_UPDATE
}

void loop()
{
  M5.update();
  CacheUpdate();

  renderer::Present([](int x, int y) -> uint16_t {
    int dist = int(usin(3 * y + update_num * 2) * 20);
    int u = ((x + dist) / 5) % 64;
    int v = (y / 2) % 64;
    int idx = u + v * 64;
    return tex[idx];
  });
}

void setup(void)
{
  M5.begin();
  pinMode(22, OUTPUT); //For Debug
  gentable();
  renderer::Setup();
}
