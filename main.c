#define GL_GLEXT_PROTOTYPES
#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define DEG2RAD 0.0174533
#define MAX_BULLETS 200
#define MAX_OBJECTS 25
#define MAX_BUILDINGS 4

#define RAD2DEG 57.29577951308232f

#define MAX_GLASS_SHARDS 30
#define MAX_SHATTERED_WINDOWS 20

float gunAimYaw = 0.0f;
float gunAimPitch = 0.0f;
float gunAimTargetYaw = 0.0f;
float gunAimTargetPitch = 0.0f;
float gunAimBlend = 0.0f;

int th = 45, ph = 20;
float camX = 0, camY = 2, camZ = 15;

int mode_view = 2;
int w, h, n;

float fpvX = 0.0f, fpvY = 2.5f, fpvZ = 5.0f;
float fpvYaw = 180.0f;
float fpvPitch = 0.0f;
float fpvSpeed = 0.3f;
float fpvSens = 0.15f;
float hitEffectTime = 0.0f;

float lightAngle = 90.0;
float lightY = 15.0;
float lightDist = 30.0;

int mode_proj = 0;
float asp = 1;
float dim = 60;
int fov = 70;

GLuint hitShaderProgram = 0;
GLuint muzzleFlashProgram = 0;
GLuint muzzleFlashVBO = 0;

int windowWidth = 1200, windowHeight = 800;
int lastMouseX, lastMouseY;
bool firstMouse = true;
#define MAX_STAIRS 10

GLuint texGround, texBuilding, texRoof, texConcrete, texMetal, texWood,
    texTarget;
GLuint texGunDiffuse, texGunSpecular, texGunNormal;

typedef struct
{
  float x, z;
  float w, d;
  int stories;
  float storyHeight;
  GLuint wallTex;
  GLuint roofTex;
  bool doorOpen;
  float doorAngle;
} Building;

Building buildings[MAX_BUILDINGS];

typedef struct
{
  float *vertices;
  float *normals;
  float *texCoords;
  int vertexCount;
} GunModel;

GunModel gunModel;

typedef struct
{
  float x, y, z;
  float dx, dy, dz;
  bool active;
  float life;
} Bullet;

typedef struct
{
  float x, y, z;
  int hits;
  bool alive;
  int type;
  float explodeTime;
  float scale;
  int buildingId;
  int floor;
} Target;

Bullet bullets[MAX_BULLETS];
Target targets[MAX_OBJECTS];
int bulletCount = 0;
int targetsDestroyed = 0;

typedef struct
{
  float minX, maxX;
  float minY, maxY;
  float minZ, maxZ;
} Stair;

Stair stairs[MAX_STAIRS];
int stairCount = 0;

bool mouseCaptured = false;

float gunRecoil = 0.0f;
float muzzleFlashTime = 0.0f;

GLuint loadShader(GLenum type, const char *src)
{
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &src, NULL);
  glCompileShader(shader);

  GLint success;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success)
  {
    char info[512];
    glGetShaderInfoLog(shader, 512, NULL, info);
    printf("Shader compile error: %s\n", info);
    return 0;
  }
  return shader;
}
char *loadFileText(const char *path)
{
  FILE *file = fopen(path, "r");
  if (!file)
    return NULL;

  fseek(file, 0, SEEK_END);
  long length = ftell(file);
  fseek(file, 0, SEEK_SET);

  char *buffer = malloc(length + 1);
  if (!buffer)
  {
    fclose(file);
    return NULL;
  }

  fread(buffer, 1, length, file);
  buffer[length] = '\0';
  fclose(file);
  return buffer;
}

GLuint createShaderProgramFromFiles(const char *vertPath, const char *fragPath)
{
  char *vertSrc = loadFileText(vertPath);
  char *fragSrc = loadFileText(fragPath);
  if (!vertSrc || !fragSrc)
  {
    printf("Error reading shader files\n");
    return 0;
  }

  GLuint vert = loadShader(GL_VERTEX_SHADER, vertSrc);
  GLuint frag = loadShader(GL_FRAGMENT_SHADER, fragSrc);

  GLuint program = glCreateProgram();
  glAttachShader(program, vert);
  glAttachShader(program, frag);
  glLinkProgram(program);

  GLint success;
  glGetProgramiv(program, GL_LINK_STATUS, &success);
  if (!success)
  {
    char info[512];
    glGetProgramInfoLog(program, 512, NULL, info);
    printf("Shader link error: %s\n", info);
  }

  free(vertSrc);
  free(fragSrc);
  return program;
}

void updateHitEffect()
{
  if (hitEffectTime < 1.0f)
    hitEffectTime += 0.016f;
}
void initMuzzleFlashGeometry()
{
  // Create a simple quad for the muzzle flash billboard
  float quad[] = {
      // Positions (x, y, z) and TexCoords (u, v)
      -0.15f, -0.15f, 0.0f, 0.0f, 0.0f,
      0.15f, -0.15f, 0.0f, 1.0f, 0.0f,
      0.15f, 0.15f, 0.0f, 1.0f, 1.0f,
      -0.15f, 0.15f, 0.0f, 0.0f, 1.0f};

  glGenBuffers(1, &muzzleFlashVBO);
  glBindBuffer(GL_ARRAY_BUFFER, muzzleFlashVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  printf("Muzzle flash geometry initialized\n");
}

int getNearestTargetIndex()
{
  int best = -1;
  float bestDist = 1e9f;
  for (int i = 0; i < MAX_OBJECTS; i++)
  {
    if (!targets[i].alive)
      continue;
    float dx = targets[i].x - fpvX;
    float dy = targets[i].y - fpvY;
    float dz = targets[i].z - fpvZ;
    float d = sqrtf(dx * dx + dy * dy + dz * dz);
    if (d < bestDist)
    {
      bestDist = d;
      best = i;
    }
  }
  return best;
}

float getTerrainHeight(float x, float z)
{
  for (int i = 0; i < MAX_BUILDINGS; i++)
  {
    Building *b = &buildings[i];

    float padding = 2.0f; // Extra flat area around building
    float minX = b->x - b->w - padding;
    float maxX = b->x + b->w + padding;
    float minZ = b->z - b->d - padding;
    float maxZ = b->z + b->d + padding;

    if (x >= minX && x <= maxX && z >= minZ && z <= maxZ)
    {
      // Inside building area - return flat height at building's base
      // Calculate what the base height would be at building center
      const float frequency = 0.1f;
      const float amplitude = 2.5f;
      return sinf(b->x * frequency) * cosf(b->z * frequency) * amplitude;
    }
  }

  // Outside building areas - use wavy terrain
  const float frequency = 0.1f;
  const float amplitude = 2.5f;
  return sinf(x * frequency) * cosf(z * frequency) * amplitude;
}

int getCurrentFloorInBuilding(float x, float z, float y)
{
  for (int i = 0; i < MAX_BUILDINGS; i++)
  {
    Building *b = &buildings[i];

    float minX = b->x - b->w;
    float maxX = b->x + b->w;
    float minZ = b->z - b->d;
    float maxZ = b->z + b->d;
    if (x < minX || x > maxX || z < minZ || z > maxZ)
      continue;

    float baseY = getTerrainHeight(b->x, b->z);
    float floorHeight = b->storyHeight;

    for (int floor = b->stories - 1; floor >= 0; floor--)
    {
      float floorY = baseY + 0.05f + (floor * floorHeight);
      float nextFloorY = baseY + 0.05f + ((floor + 1) * floorHeight);

      if (y >= floorY - 0.5f && y < nextFloorY + 2.0f)
      {
        return floor;
      }
    }

    return 0;
  }

  return -1;
}

bool isInsideBuilding(float x, float z)
{
  for (int i = 0; i < MAX_BUILDINGS; i++)
  {
    Building *b = &buildings[i];
    float minX = b->x - b->w;
    float maxX = b->x + b->w;
    float minZ = b->z - b->d;
    float maxZ = b->z + b->d;
    if (x >= minX && x <= maxX && z >= minZ && z <= maxZ)
    {
      return true;
    }
  }
  return false;
}

int getNearestDoor()
{
  float doorRange = 4.0f;
  for (int i = 0; i < MAX_BUILDINGS; i++)
  {
    Building *b = &buildings[i];
    float doorX = b->x;
    float doorZ = b->z + b->d;
    float doorY = getTerrainHeight(b->x, b->z);

    float dx = fpvX - doorX;
    float dy = fpvY - doorY - 1.5f;
    float dz = fpvZ - doorZ;
    float dist = sqrtf(dx * dx + dz * dz);

    if (dist < doorRange && fabsf(dy) < 2.0f)
      return i;
  }
  return -1;
}

void drawCrosshair()
{
  glDisable(GL_LIGHTING);
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_DEPTH_TEST);

  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  glOrtho(-1, 1, -1, 1, -1, 1);

  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  glColor3f(0.0f, 1.0f, 0.0f);
  glLineWidth(2.0f);

  glBegin(GL_LINES);
  glVertex2f(-0.03f, 0.0f);
  glVertex2f(0.03f, 0.0f);
  glVertex2f(0.0f, -0.03f);
  glVertex2f(0.0f, 0.03f);
  glEnd();

  glBegin(GL_LINE_LOOP);
  for (int i = 0; i < 20; i++)
  {
    float angle = i * 2.0f * M_PI / 20.0f;
    glVertex2f(0.02f * cosf(angle), 0.02f * sinf(angle));
  }
  glEnd();

  glLineWidth(1.0f);
  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
  glEnable(GL_DEPTH_TEST);
}

// Draw muzzle flash
void drawMuzzleFlash()
{
  if (muzzleFlashTime <= 0.0f)
    return;

  if (muzzleFlashProgram != 0 && muzzleFlashVBO != 0)
  {
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    glUseProgram(muzzleFlashProgram);

    GLint timeLoc = glGetUniformLocation(muzzleFlashProgram, "uTime");
    GLint intensityLoc = glGetUniformLocation(muzzleFlashProgram, "uIntensity");
    GLint colorLoc = glGetUniformLocation(muzzleFlashProgram, "uFlashColor");

    float normalizedTime = 1.0f - (muzzleFlashTime / 0.1f);
    float intensity = muzzleFlashTime / 0.1f;

    if (timeLoc != -1)
      glUniform1f(timeLoc, normalizedTime);
    if (intensityLoc != -1)
      glUniform1f(intensityLoc, intensity);
    if (colorLoc != -1)
      glUniform3f(colorLoc, 1.0f, 0.9f, 0.6f);

    glPushMatrix();

    glTranslatef(0.5f, -0.35f, -1.8f);

    float scale = 1.0f + (1.0f - normalizedTime) * 0.5f;
    glScalef(scale, scale, scale);

    glBindBuffer(GL_ARRAY_BUFFER, muzzleFlashVBO);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    glVertexPointer(3, GL_FLOAT, 5 * sizeof(float), (void *)0);
    glTexCoordPointer(2, GL_FLOAT, 5 * sizeof(float), (void *)(3 * sizeof(float)));

    glDrawArrays(GL_QUADS, 0, 4);

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glPopMatrix();

    // Draw additional flash layers for more intensity
    for (int i = 0; i < 3; i++)
    {
      glPushMatrix();
      glTranslatef(0.5f, -0.35f, -1.8f);

      // Rotate each layer
      glRotatef(i * 45.0f + normalizedTime * 180.0f, 0, 0, 1);

      float layerScale = scale * (1.0f - i * 0.15f);
      glScalef(layerScale, layerScale, layerScale);

      glBindBuffer(GL_ARRAY_BUFFER, muzzleFlashVBO);
      glEnableClientState(GL_VERTEX_ARRAY);
      glEnableClientState(GL_TEXTURE_COORD_ARRAY);

      glVertexPointer(3, GL_FLOAT, 5 * sizeof(float), (void *)0);
      glTexCoordPointer(2, GL_FLOAT, 5 * sizeof(float), (void *)(3 * sizeof(float)));

      glDrawArrays(GL_QUADS, 0, 4);

      glDisableClientState(GL_VERTEX_ARRAY);
      glDisableClientState(GL_TEXTURE_COORD_ARRAY);
      glBindBuffer(GL_ARRAY_BUFFER, 0);

      glPopMatrix();
    }

    glUseProgram(0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
  }
  else
  {
    // Fallback to old sphere-based method
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    float intensity = muzzleFlashTime / 0.1f;

    glPushMatrix();
    glTranslatef(0.5f, -0.35f, -1.8f);

    glColor4f(1.0f, 0.9f, 0.3f, intensity * 0.9f);
    glutSolidSphere(0.15f * intensity, 12, 12);

    glColor4f(1.0f, 0.5f, 0.0f, intensity * 0.5f);
    glutSolidSphere(0.25f * intensity, 12, 12);

    for (int i = 0; i < 8; i++)
    {
      float angle = i * M_PI / 4.0f;
      float dist = 0.3f * intensity;
      glPushMatrix();
      glTranslatef(cosf(angle) * dist, sinf(angle) * dist, 0);
      glColor4f(1.0f, 0.8f, 0.2f, intensity * 0.7f);
      glutSolidSphere(0.03f, 6, 6);
      glPopMatrix();
    }

    glPopMatrix();

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
  }
}

bool loadOBJ(const char *path, GunModel *model)
{
  FILE *file = fopen(path, "r");
  if (!file)
  {
    printf("Failed to open OBJ file: %s\n", path);
    return false;
  }

  // Allocate larger buffers for big models (50k vertices/faces)
  float *tempVertices = (float *)malloc(50000 * 3 * sizeof(float));
  float *tempNormals = (float *)malloc(50000 * 3 * sizeof(float));
  float *tempTexCoords = (float *)malloc(50000 * 2 * sizeof(float));
  int vCount = 0, nCount = 0, tCount = 0;

  float *vertices = (float *)malloc(150000 * sizeof(float));
  float *normals = (float *)malloc(150000 * sizeof(float));
  float *texCoords = (float *)malloc(100000 * sizeof(float));
  int faceCount = 0;
  int maxFaces = 50000; // Maximum number of faces to load

  char line[256];
  int lineNum = 0;

  while (fgets(line, sizeof(line), file))
  {
    lineNum++;

    // Skip comments and empty lines
    if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
      continue;

    if (strncmp(line, "v ", 2) == 0)
    {
      float x, y, z;
      if (sscanf(line, "v %f %f %f", &x, &y, &z) == 3)
      {
        tempVertices[vCount * 3] = x;
        tempVertices[vCount * 3 + 1] = y;
        tempVertices[vCount * 3 + 2] = z;
        vCount++;
      }
    }
    else if (strncmp(line, "vn ", 3) == 0)
    {
      float x, y, z;
      if (sscanf(line, "vn %f %f %f", &x, &y, &z) == 3)
      {
        tempNormals[nCount * 3] = x;
        tempNormals[nCount * 3 + 1] = y;
        tempNormals[nCount * 3 + 2] = z;
        nCount++;
      }
    }
    else if (strncmp(line, "vt ", 3) == 0)
    {
      float u, v;
      if (sscanf(line, "vt %f %f", &u, &v) == 2)
      {
        tempTexCoords[tCount * 2] = u;
        tempTexCoords[tCount * 2 + 1] = v;
        tCount++;
      }
    }
    else if (strncmp(line, "f ", 2) == 0)
    {
      // Check if we've reached max faces
      if (faceCount >= maxFaces)
      {
        printf("Warning: Reached maximum face count (%d), stopping load\n",
               maxFaces);
        break;
      }

      int v1, v2, v3, t1, t2, t3, n1, n2, n3;
      int matches;

      // Try format: f v/vt/vn v/vt/vn v/vt/vn
      matches = sscanf(line, "f %d/%d/%d %d/%d/%d %d/%d/%d", &v1, &t1, &n1, &v2,
                       &t2, &n2, &v3, &t3, &n3);

      if (matches == 9)
      {
        // Bounds checking
        if (v1 < 1 || v1 > vCount || v2 < 1 || v2 > vCount || v3 < 1 ||
            v3 > vCount || t1 < 1 || t1 > tCount || t2 < 1 || t2 > tCount ||
            t3 < 1 || t3 > tCount || n1 < 1 || n1 > nCount || n2 < 1 ||
            n2 > nCount || n3 < 1 || n3 > nCount)
        {
          printf("Line %d: Index out of bounds, skipping\n", lineNum);
          continue;
        }

        // All three components present

        // Vertex 1
        vertices[faceCount * 9] = tempVertices[(v1 - 1) * 3];
        vertices[faceCount * 9 + 1] = tempVertices[(v1 - 1) * 3 + 1];
        vertices[faceCount * 9 + 2] = tempVertices[(v1 - 1) * 3 + 2];
        normals[faceCount * 9] = tempNormals[(n1 - 1) * 3];
        normals[faceCount * 9 + 1] = tempNormals[(n1 - 1) * 3 + 1];
        normals[faceCount * 9 + 2] = tempNormals[(n1 - 1) * 3 + 2];
        texCoords[faceCount * 6] = tempTexCoords[(t1 - 1) * 2];
        texCoords[faceCount * 6 + 1] = tempTexCoords[(t1 - 1) * 2 + 1];

        // Vertex 2
        vertices[faceCount * 9 + 3] = tempVertices[(v2 - 1) * 3];
        vertices[faceCount * 9 + 4] = tempVertices[(v2 - 1) * 3 + 1];
        vertices[faceCount * 9 + 5] = tempVertices[(v2 - 1) * 3 + 2];
        normals[faceCount * 9 + 3] = tempNormals[(n2 - 1) * 3];
        normals[faceCount * 9 + 4] = tempNormals[(n2 - 1) * 3 + 1];
        normals[faceCount * 9 + 5] = tempNormals[(n2 - 1) * 3 + 2];
        texCoords[faceCount * 6 + 2] = tempTexCoords[(t2 - 1) * 2];
        texCoords[faceCount * 6 + 3] = tempTexCoords[(t2 - 1) * 2 + 1];

        // Vertex 3
        vertices[faceCount * 9 + 6] = tempVertices[(v3 - 1) * 3];
        vertices[faceCount * 9 + 7] = tempVertices[(v3 - 1) * 3 + 1];
        vertices[faceCount * 9 + 8] = tempVertices[(v3 - 1) * 3 + 2];
        normals[faceCount * 9 + 6] = tempNormals[(n3 - 1) * 3];
        normals[faceCount * 9 + 7] = tempNormals[(n3 - 1) * 3 + 1];
        normals[faceCount * 9 + 8] = tempNormals[(n3 - 1) * 3 + 2];
        texCoords[faceCount * 6 + 4] = tempTexCoords[(t3 - 1) * 2];
        texCoords[faceCount * 6 + 5] = tempTexCoords[(t3 - 1) * 2 + 1];

        faceCount++;
      }
      else
      {
        // Try format: f v//vn v//vn v//vn
        matches = sscanf(line, "f %d//%d %d//%d %d//%d", &v1, &n1, &v2, &n2,
                         &v3, &n3);

        if (matches == 6)
        {
          // Bounds checking
          if (v1 < 1 || v1 > vCount || v2 < 1 || v2 > vCount || v3 < 1 ||
              v3 > vCount || n1 < 1 || n1 > nCount || n2 < 1 || n2 > nCount ||
              n3 < 1 || n3 > nCount)
          {
            printf(
                "Line %d: Index out of bounds "
                "(v1=%d,v2=%d,v3=%d,n1=%d,n2=%d,n3=%d), vCount=%d, nCount=%d\n",
                lineNum, v1, v2, v3, n1, n2, n3, vCount, nCount);
            continue;
          }

          if (lineNum % 10000 == 0) // Print progress every 10k lines
            printf("Line %d: Processing face %d with v//vn format\n", lineNum,
                   faceCount);

          // Vertex 1
          vertices[faceCount * 9] = tempVertices[(v1 - 1) * 3];
          vertices[faceCount * 9 + 1] = tempVertices[(v1 - 1) * 3 + 1];
          vertices[faceCount * 9 + 2] = tempVertices[(v1 - 1) * 3 + 2];
          normals[faceCount * 9] = tempNormals[(n1 - 1) * 3];
          normals[faceCount * 9 + 1] = tempNormals[(n1 - 1) * 3 + 1];
          normals[faceCount * 9 + 2] = tempNormals[(n1 - 1) * 3 + 2];
          texCoords[faceCount * 6] = 0.0f;
          texCoords[faceCount * 6 + 1] = 0.0f;

          // Vertex 2
          vertices[faceCount * 9 + 3] = tempVertices[(v2 - 1) * 3];
          vertices[faceCount * 9 + 4] = tempVertices[(v2 - 1) * 3 + 1];
          vertices[faceCount * 9 + 5] = tempVertices[(v2 - 1) * 3 + 2];
          normals[faceCount * 9 + 3] = tempNormals[(n2 - 1) * 3];
          normals[faceCount * 9 + 4] = tempNormals[(n2 - 1) * 3 + 1];
          normals[faceCount * 9 + 5] = tempNormals[(n2 - 1) * 3 + 2];
          texCoords[faceCount * 6 + 2] = 1.0f;
          texCoords[faceCount * 6 + 3] = 0.0f;

          // Vertex 3
          vertices[faceCount * 9 + 6] = tempVertices[(v3 - 1) * 3];
          vertices[faceCount * 9 + 7] = tempVertices[(v3 - 1) * 3 + 1];
          vertices[faceCount * 9 + 8] = tempVertices[(v3 - 1) * 3 + 2];
          normals[faceCount * 9 + 6] = tempNormals[(n3 - 1) * 3];
          normals[faceCount * 9 + 7] = tempNormals[(n3 - 1) * 3 + 1];
          normals[faceCount * 9 + 8] = tempNormals[(n3 - 1) * 3 + 2];
          texCoords[faceCount * 6 + 4] = 0.5f;
          texCoords[faceCount * 6 + 5] = 1.0f;

          faceCount++;
        }
        else
        {
          matches =
              sscanf(line, "f %d/%d %d/%d %d/%d", &v1, &t1, &v2, &t2, &v3, &t3);

          if (matches == 6)
          {
            if (v1 < 1 || v1 > vCount || v2 < 1 || v2 > vCount || v3 < 1 ||
                v3 > vCount || t1 < 1 || t1 > tCount || t2 < 1 || t2 > tCount ||
                t3 < 1 || t3 > tCount)
            {
              printf("Line %d: Index out of bounds, skipping\n", lineNum);
              continue;
            }

            if (lineNum % 10000 == 0)
              printf("Line %d: Found face with v/vt format (no normals)\n",
                     lineNum);
            float *p1 = &tempVertices[(v1 - 1) * 3];
            float *p2 = &tempVertices[(v2 - 1) * 3];
            float *p3 = &tempVertices[(v3 - 1) * 3];

            float u[3] = {p2[0] - p1[0], p2[1] - p1[1], p2[2] - p1[2]};
            float v[3] = {p3[0] - p1[0], p3[1] - p1[1], p3[2] - p1[2]};

            float nx = u[1] * v[2] - u[2] * v[1];
            float ny = u[2] * v[0] - u[0] * v[2];
            float nz = u[0] * v[1] - u[1] * v[0];

            float len = sqrtf(nx * nx + ny * ny + nz * nz);
            if (len > 0.0001f)
            {
              nx /= len;
              ny /= len;
              nz /= len;
            }

            vertices[faceCount * 9] = p1[0];
            vertices[faceCount * 9 + 1] = p1[1];
            vertices[faceCount * 9 + 2] = p1[2];
            normals[faceCount * 9] = nx;
            normals[faceCount * 9 + 1] = ny;
            normals[faceCount * 9 + 2] = nz;
            texCoords[faceCount * 6] = tempTexCoords[(t1 - 1) * 2];
            texCoords[faceCount * 6 + 1] = tempTexCoords[(t1 - 1) * 2 + 1];

            vertices[faceCount * 9 + 3] = p2[0];
            vertices[faceCount * 9 + 4] = p2[1];
            vertices[faceCount * 9 + 5] = p2[2];
            normals[faceCount * 9 + 3] = nx;
            normals[faceCount * 9 + 4] = ny;
            normals[faceCount * 9 + 5] = nz;
            texCoords[faceCount * 6 + 2] = tempTexCoords[(t2 - 1) * 2];
            texCoords[faceCount * 6 + 3] = tempTexCoords[(t2 - 1) * 2 + 1];

            vertices[faceCount * 9 + 6] = p3[0];
            vertices[faceCount * 9 + 7] = p3[1];
            vertices[faceCount * 9 + 8] = p3[2];
            normals[faceCount * 9 + 6] = nx;
            normals[faceCount * 9 + 7] = ny;
            normals[faceCount * 9 + 8] = nz;
            texCoords[faceCount * 6 + 4] = tempTexCoords[(t3 - 1) * 2];
            texCoords[faceCount * 6 + 5] = tempTexCoords[(t3 - 1) * 2 + 1];

            faceCount++;
          }
          else
          {
            matches = sscanf(line, "f %d %d %d", &v1, &v2, &v3);

            if (matches == 3)
            {
              if (v1 < 1 || v1 > vCount || v2 < 1 || v2 > vCount || v3 < 1 ||
                  v3 > vCount)
              {
                printf("Line %d: Index out of bounds, skipping\n", lineNum);
                continue;
              }

              if (lineNum % 10000 == 0)
                printf("Line %d: Found face with v format (vertices only)\n",
                       lineNum);

              float *p1 = &tempVertices[(v1 - 1) * 3];
              float *p2 = &tempVertices[(v2 - 1) * 3];
              float *p3 = &tempVertices[(v3 - 1) * 3];

              float u[3] = {p2[0] - p1[0], p2[1] - p1[1], p2[2] - p1[2]};
              float v[3] = {p3[0] - p1[0], p3[1] - p1[1], p3[2] - p1[2]};

              float nx = u[1] * v[2] - u[2] * v[1];
              float ny = u[2] * v[0] - u[0] * v[2];
              float nz = u[0] * v[1] - u[1] * v[0];

              float len = sqrtf(nx * nx + ny * ny + nz * nz);
              if (len > 0.0001f)
              {
                nx /= len;
                ny /= len;
                nz /= len;
              }

              // Vertex 1
              vertices[faceCount * 9] = p1[0];
              vertices[faceCount * 9 + 1] = p1[1];
              vertices[faceCount * 9 + 2] = p1[2];
              normals[faceCount * 9] = nx;
              normals[faceCount * 9 + 1] = ny;
              normals[faceCount * 9 + 2] = nz;
              texCoords[faceCount * 6] = 0.0f;
              texCoords[faceCount * 6 + 1] = 0.0f;

              // Vertex 2
              vertices[faceCount * 9 + 3] = p2[0];
              vertices[faceCount * 9 + 4] = p2[1];
              vertices[faceCount * 9 + 5] = p2[2];
              normals[faceCount * 9 + 3] = nx;
              normals[faceCount * 9 + 4] = ny;
              normals[faceCount * 9 + 5] = nz;
              texCoords[faceCount * 6 + 2] = 1.0f;
              texCoords[faceCount * 6 + 3] = 0.0f;

              // Vertex 3
              vertices[faceCount * 9 + 6] = p3[0];
              vertices[faceCount * 9 + 7] = p3[1];
              vertices[faceCount * 9 + 8] = p3[2];
              normals[faceCount * 9 + 6] = nx;
              normals[faceCount * 9 + 7] = ny;
              normals[faceCount * 9 + 8] = nz;
              texCoords[faceCount * 6 + 4] = 0.5f;
              texCoords[faceCount * 6 + 5] = 1.0f;

              faceCount++;
            }
            else
            {
              printf("Line %d: Unrecognized face format: %s", lineNum, line);
            }
          }
        }
      }
    }
  }

  fclose(file);
  free(tempVertices);
  free(tempNormals);
  free(tempTexCoords);

  printf("Parsed: %d vertices, %d normals, %d texture coords\n", vCount, nCount,
         tCount);
  printf("Created: %d faces\n", faceCount);

  model->vertices = vertices;
  model->normals = normals;
  model->texCoords = texCoords;
  model->vertexCount = faceCount * 3;

  printf("Loaded gun model: %d vertices\n", model->vertexCount);
  return true;
}

float getStairHeightAt(float x, float z)
{
  // Default desired eye height is terrain height + eye offset
  const float eyeOffset = 1.5f;

  // Check if we're inside any building first
  for (int i = 0; i < MAX_BUILDINGS; i++)
  {
    Building *b = &buildings[i];

    float minX = b->x - b->w;
    float maxX = b->x + b->w;
    float minZ = b->z - b->d;
    float maxZ = b->z + b->d;
    if (x < minX || x > maxX || z < minZ || z > maxZ)
      continue;

    // Inside a building - check if we're on stairs
    float stairX = b->x + b->w - 1.0f;
    float stairZStart = b->z - b->d + 0.8f;
    float stairDepth = 0.35f;
    float stairHeight = 0.25f;
    int stepsPerFloor = (int)(b->storyHeight / stairHeight) + 2;

    bool onStairs = (fabsf(x - stairX) <= 1.0f);
    if (onStairs)
    {
      float dz = z - stairZStart;
      if (dz >= 0 && dz <= stepsPerFloor * stairDepth * (b->stories - 1))
      {
        // On stairs - use stair height
        int floor = (int)(dz / (stepsPerFloor * stairDepth));
        float localZ = fmodf(dz, stepsPerFloor * stairDepth);
        float stepIndex = localZ / stairDepth;
        float localY = floor * b->storyHeight + stepIndex * stairHeight;
        float baseY = getTerrainHeight(b->x, b->z);
        return baseY + localY + eyeOffset;
      }
    }

    int currentFloor = getCurrentFloorInBuilding(x, z, fpvY);

    float baseY =
        getTerrainHeight(b->x, b->z); // Building's base terrain height
    float floorHeight = b->storyHeight;

    // Calculate the floor Y position for the current floor
    float currentFloorY = baseY + 0.05f + (currentFloor * floorHeight);

    // Calculate height offset relative to the current floor
    float heightOffset;
    if (fpvY > currentFloorY + 0.3f && fpvY < currentFloorY + 10.0f)
    {
      // Calculate offset from current floor
      heightOffset = fpvY - currentFloorY;
      // Clamp to reasonable range (1.0 to 3.0 above floor)
      if (heightOffset < 1.0f)
        heightOffset = eyeOffset; // Ensure at least normal eye height
      if (heightOffset > 3.0f)
        heightOffset = eyeOffset;
    }
    else
    {
      heightOffset = eyeOffset; // Default to normal eye height
    }

    // Maintain proper height above the current floor level
    return currentFloorY + heightOffset;
  }

  // Outside all buildings - use terrain height
  return getTerrainHeight(x, z) + eyeOffset;
}

GLuint loadTexture(const char *file)
{
  unsigned char *data = stbi_load(file, &w, &h, &n, 3);
  if (!data)
  {
    printf("Failed to load %s\n", file);
    return 0;
  }
  GLuint tex;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE,
               data);
  glGenerateMipmap(GL_TEXTURE_2D);
  stbi_image_free(data);
  return tex;
}

void initBuildings()
{
  buildings[0] =
      (Building){20.0f, 20.0f, 6.0f, 6.0f, 3, 4.0f, 0, 0, false, 0.0f};
  buildings[1] =
      (Building){-25.0f, 15.0f, 5.0f, 7.0f, 2, 4.0f, 0, 0, false, 0.0f};
  buildings[2] =
      (Building){30.0f, -10.0f, 7.0f, 5.0f, 4, 4.0f, 0, 0, false, 0.0f};
  buildings[3] =
      (Building){-20.0f, -20.0f, 6.0f, 6.0f, 2, 4.0f, 0, 0, false, 0.0f};
  buildings[4] =
      (Building){15.0f, -30.0f, 5.0f, 5.0f, 3, 4.0f, 0, 0, false, 0.0f};
  buildings[5] =
      (Building){-30.0f, -5.0f, 8.0f, 4.0f, 5, 4.0f, 0, 0, false, 0.0f};
}

void initTargets()
{
  int targetIdx = 0;

  // Place targets inside buildings on different floors
  for (int b = 0; b < MAX_BUILDINGS && targetIdx < MAX_OBJECTS - 5; b++)
  {
    Building *building = &buildings[b];
    for (int floor = 0;
         floor < building->stories && targetIdx < MAX_OBJECTS - 5; floor++)
    {
      float tx = building->x + (rand() % 3 - 1) * 2.0f;
      float ty = floor * building->storyHeight + 2.0f;
      float tz = building->z + (rand() % 3 - 1) * 2.0f;

      targets[targetIdx] = (Target){
          tx, ty, tz, 0, true, rand() % 3, -1.0, 0.8f + (rand() % 4) / 10.0f,
          b, floor};
      targetIdx++;
    }
  }

  // Remaining ground targets
  for (; targetIdx < MAX_OBJECTS; targetIdx++)
  {
    float tx = (rand() % 80 - 40);
    float tz = (rand() % 80 - 40);
    float ty = getTerrainHeight(tx, tz) + 1.5f;
    targets[targetIdx] = (Target){
        tx, ty, tz, 0, true, rand() % 3, -1.0, 0.7f + (rand() % 5) / 10.0f,
        -1, 0};
  }

  targetsDestroyed = 0;
}

void checkCollisions()
{
  for (int i = 0; i < MAX_BULLETS; i++)
  {
    if (!bullets[i].active)
      continue;
    for (int j = 0; j < MAX_OBJECTS; j++)
    {
      if (!targets[j].alive)
        continue;
      float dx = bullets[i].x - targets[j].x;
      float dy = bullets[i].y - targets[j].y;
      float dz = bullets[i].z - targets[j].z;
      float dist = sqrtf(dx * dx + dy * dy + dz * dz);
      if (dist < (0.8f * targets[j].scale))
      {
        bullets[i].active = false;
        targets[j].hits++;
        hitEffectTime = 0.0f;
        printf("Hit! Target %d - Hits: %d/3 (Floor %d)\n", j, targets[j].hits,
               targets[j].floor);
        if (targets[j].hits >= 3)
        {
          targets[j].alive = false;
          targets[j].explodeTime = 0.0f;
          targetsDestroyed++;
          printf("Target %d destroyed! Total: %d/%d\n", j, targetsDestroyed,
                 MAX_OBJECTS);
        }
        break;
      }
    }
  }
}

void updateBullets()
{
  for (int i = 0; i < MAX_BULLETS; i++)
  {
    if (!bullets[i].active)
      continue;

    bullets[i].x += bullets[i].dx;
    bullets[i].y += bullets[i].dy;
    bullets[i].z += bullets[i].dz;
    bullets[i].life -= 0.01f;

    if (bullets[i].life <= 0 || fabsf(bullets[i].x) > 100 ||
        fabsf(bullets[i].z) > 100 || bullets[i].y < -5 || bullets[i].y > 50)
      bullets[i].active = false;
  }
}

void drawBullets()
{
  glDisable(GL_LIGHTING);
  glColor3f(1, 0.9, 0.3);
  for (int i = 0; i < MAX_BULLETS; i++)
  {
    if (!bullets[i].active)
      continue;
    glPushMatrix();
    glTranslatef(bullets[i].x, bullets[i].y, bullets[i].z);
    glutSolidSphere(0.08, 8, 8);
    glPopMatrix();
  }
  glEnable(GL_LIGHTING);
}

void shootBullet()
{
  if (mode_view != 2)
    return;

  int idx = bulletCount % MAX_BULLETS;
  float radYaw = fpvYaw * DEG2RAD;
  float radPitch = fpvPitch * DEG2RAD;

  float dirX = cosf(radPitch) * sinf(radYaw);
  float dirY = sinf(radPitch);
  float dirZ = cosf(radPitch) * cosf(radYaw);

  float speed = 1.2f;

  bullets[idx].x = fpvX + dirX * 0.7f;
  bullets[idx].y = fpvY + dirY * 0.7f;
  bullets[idx].z = fpvZ + dirZ * 0.7f;
  bullets[idx].dx = dirX * speed;
  bullets[idx].dy = dirY * speed;
  bullets[idx].dz = dirZ * speed;
  bullets[idx].active = true;
  bullets[idx].life = 8.0f;

  // recoil + muzzle flash
  gunRecoil = 1.0f;
  muzzleFlashTime = 0.1f;

  // --- Aim gun to nearest target while shooting ---
  int nearest = getNearestTargetIndex();
  if (nearest >= 0)
  {
    float tx = targets[nearest].x - fpvX;
    float ty = targets[nearest].y - fpvY;
    float tz = targets[nearest].z - fpvZ;
    float horiz = sqrtf(tx * tx + tz * tz);
    // desired absolute angles (world space)
    float desiredYaw = atan2f(tx, tz) * RAD2DEG; // degrees
    float desiredPitch = atan2f(ty, horiz) * RAD2DEG;

    // compute local offset between camera and desired angles
    // gun will rotate by (desired - camera)
    gunAimTargetYaw = desiredYaw - fpvYaw;
    // wrap into [-180,180]
    while (gunAimTargetYaw > 180.0f)
      gunAimTargetYaw -= 360.0f;
    while (gunAimTargetYaw < -180.0f)
      gunAimTargetYaw += 360.0f;

    gunAimTargetPitch = desiredPitch - fpvPitch;
    // clamp modestly so gun doesn't contort
    if (gunAimTargetPitch > 35.0f)
      gunAimTargetPitch = 35.0f;
    if (gunAimTargetPitch < -35.0f)
      gunAimTargetPitch = -35.0f;

    gunAimBlend = 1.0f;
    // Snap barrel to target immediately on shot, choosing nearest yaw to avoid
    // flips
    float snapYaw = gunAimTargetYaw;
    while (snapYaw - gunAimYaw > 180.0f)
      snapYaw -= 360.0f;
    while (snapYaw - gunAimYaw < -180.0f)
      snapYaw += 360.0f;
    gunAimYaw = snapYaw;
    gunAimPitch = gunAimTargetPitch;
  }

  bulletCount++;
}

void drawExplosion(float x, float y, float z, float time)
{
  glDisable(GL_LIGHTING);
  glEnable(GL_BLEND);
  int particles = 30;
  for (int i = 0; i < particles; i++)
  {
    float angle = (2.0f * M_PI * i) / particles;
    float radius = time * 3.0f;
    float px = x + cosf(angle) * radius;
    float py = y + sinf(angle * 0.7f) * radius;
    float pz = z + sinf(angle) * radius;
    float fade = 1.0f - (time / 1.5f);
    glColor4f(1.0f, 0.5f + fade * 0.3f, 0.0f, fade);
    glPushMatrix();
    glTranslatef(px, py, pz);
    glutSolidSphere(0.15, 6, 6);
    glPopMatrix();
  }
  glDisable(GL_BLEND);
  glEnable(GL_LIGHTING);
}

void quad(GLuint tex, float x1, float y1, float z1, float x2, float y2,
          float z2, float x3, float y3, float z3, float x4, float y4, float z4,
          float nx, float ny, float nz, float texScale)
{
  if (tex)
    glBindTexture(GL_TEXTURE_2D, tex);
  glNormal3f(nx, ny, nz);
  glBegin(GL_QUADS);
  glTexCoord2f(0, 0);
  glVertex3f(x1, y1, z1);
  glTexCoord2f(texScale, 0);
  glVertex3f(x2, y2, z2);
  glTexCoord2f(texScale, texScale);
  glVertex3f(x3, y3, z3);
  glTexCoord2f(0, texScale);
  glVertex3f(x4, y4, z4);
  glEnd();
}

void drawTerrain()
{
  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, texGround);

  int gridSize = 100;
  float step = 2.0f;
  float start = -gridSize;

  glColor3f(1.0f, 1.0f, 1.0f);

  for (float z = start; z < gridSize; z += step)
  {
    glBegin(GL_TRIANGLE_STRIP);
    for (float x = start; x <= gridSize; x += step)
    {
      // Use the updated getTerrainHeight which handles flat areas
      float y1 = getTerrainHeight(x, z);
      float y2 = getTerrainHeight(x, z + step);

      // Calculate normals for proper lighting
      float dx = 0.1f;
      float dz = 0.1f;
      float hL = getTerrainHeight(x - dx, z);
      float hR = getTerrainHeight(x + dx, z);
      float hD = getTerrainHeight(x, z - dz);
      float hU = getTerrainHeight(x, z + dz);

      float nx = (hL - hR) / (2.0f * dx);
      float nz = (hD - hU) / (2.0f * dz);
      float ny = 1.0f;

      // Normalize
      float len = sqrtf(nx * nx + ny * ny + nz * nz);
      if (len > 0.001f)
      {
        nx /= len;
        ny /= len;
        nz /= len;
      }

      glNormal3f(nx, ny, nz);
      glTexCoord2f((x - start) / 10.0f, (z - start) / 10.0f);
      glVertex3f(x, y1, z);

      glTexCoord2f((x - start) / 10.0f, ((z + step) - start) / 10.0f);
      glVertex3f(x, y2, z + step);
    }
    glEnd();
  }

  glColor3f(1.0f, 1.0f, 1.0f);
}

void drawGun()
{
  // Fallback if no model loaded
  if (gunModel.vertexCount <= 0 || !gunModel.vertices)
  {
    printf("Gun model not loaded, drawing fallback box.\n");
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texGunDiffuse);
    glDisable(GL_LIGHTING);
    glPushMatrix();
    float recoilZ = -0.02f * gunRecoil;
    glTranslatef(0.55f, -0.35f, -1.2f + recoilZ);
    glRotatef(gunAimPitch, 1, 0, 0);
    glRotatef(gunAimYaw, 0, 1, 0);
    glScalef(0.6f, 0.3f, 1.1f);
    glutSolidCube(1.0);
    glPopMatrix();
    glEnable(GL_LIGHTING);
    return;
  }

  // Clear depth buffer so gun renders on top
  glClear(GL_DEPTH_BUFFER_BIT);

  // Save all OpenGL state
  glPushAttrib(GL_ALL_ATTRIB_BITS);

  // Set up rendering state for solid gun
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glDepthMask(GL_TRUE);

  glEnable(GL_LIGHTING); // Enable lighting for realistic look
  glEnable(GL_LIGHT0);

  glEnable(GL_TEXTURE_2D);
  glEnable(GL_COLOR_MATERIAL);
  glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); // ensure filled
  glDisable(GL_CULL_FACE);                   // show both sides
  glDisable(GL_BLEND);

  // Set up lighting for the gun
  float gunLightPos[] = {1.0f, 1.0f, 2.0f, 0.0f};
  float gunLightAmbient[] = {0.5f, 0.5f, 0.5f, 1.0f};
  float gunLightDiffuse[] = {0.8f, 0.8f, 0.8f, 1.0f};
  float gunLightSpecular[] = {1.0f, 1.0f, 1.0f, 1.0f};

  glLightfv(GL_LIGHT0, GL_POSITION, gunLightPos);
  glLightfv(GL_LIGHT0, GL_AMBIENT, gunLightAmbient);
  glLightfv(GL_LIGHT0, GL_DIFFUSE, gunLightDiffuse);
  glLightfv(GL_LIGHT0, GL_SPECULAR, gunLightSpecular);

  // Set material properties for metallic gun
  float matAmbient[] = {0.3f, 0.3f, 0.3f, 1.0f};
  float matDiffuse[] = {0.4f, 0.4f, 0.4f, 1.0f};
  float matSpecular[] = {0.8f, 0.8f, 0.8f, 1.0f};
  float matShininess = 64.0f;

  glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, matAmbient);
  glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, matDiffuse);
  glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, matSpecular);
  glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, matShininess);

  // Set up separate projection for gun
  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  gluPerspective(fov, asp, 0.01, 10.0);

  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  // Position gun in view space
  float recoilZ = -0.02f * gunRecoil;
  glTranslatef(0.5f, -0.4f, -1.0f + recoilZ);

  glRotatef(90.0f, 0, 1, 0);
  glRotatef(-10.0f, 1, 0, 0);
  glRotatef(5.0f, 0, 0, 1);

  // Scale to appropriate size - INCREASE THIS VALUE TO MAKE GUN BIGGER
  glScalef(0.15f, 0.15f, 0.15f);

  // Bind texture
  if (texGunDiffuse)
  {
    glBindTexture(GL_TEXTURE_2D, texGunDiffuse);
  }

  // Set base color (gray/black for gun metal)
  glColor4f(0.5f, 0.5f, 0.5f, 1.0f);

  // Enable client arrays
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_NORMAL_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);

  // Set array pointers
  glVertexPointer(3, GL_FLOAT, 0, gunModel.vertices);
  glNormalPointer(GL_FLOAT, 0, gunModel.normals);
  glTexCoordPointer(2, GL_FLOAT, 0, gunModel.texCoords);

  glDisable(GL_LIGHTING);
  glDisable(GL_CULL_FACE);
  glColor3f(0.7f, 0.7f, 0.7f);

  // Draw the gun model
  glDrawArrays(GL_TRIANGLES, 0, gunModel.vertexCount);

  // Disable client arrays
  glDisableClientState(GL_VERTEX_ARRAY);
  glDisableClientState(GL_NORMAL_ARRAY);
  glDisableClientState(GL_TEXTURE_COORD_ARRAY);

  // Restore matrices
  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);

  // Restore all OpenGL state
  glPopAttrib();
}

void drawWindow(float x, float y, float z, float w, float h, int orient)
{
  // orient: 0=front(+z), 1=back(-z), 2=left(-x), 3=right(+x)
  glDisable(GL_TEXTURE_2D);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // Window frame (brown wood)
  glColor3f(0.3f, 0.25f, 0.2f);
  float frameThick = 0.08f;

  if (orient == 0) // Front (+z)
  {
    glBegin(GL_QUADS);
    glVertex3f(x - w / 2, y - h / 2, z);
    glVertex3f(x + w / 2, y - h / 2, z);
    glVertex3f(x + w / 2, y + h / 2, z);
    glVertex3f(x - w / 2, y + h / 2, z);
    glEnd();

    // Glass
    glColor4f(0.6f, 0.8f, 0.9f, 0.4f);
    glBegin(GL_QUADS);
    glVertex3f(x - w / 2 + frameThick, y - h / 2 + frameThick, z + 0.01f);
    glVertex3f(x + w / 2 - frameThick, y - h / 2 + frameThick, z + 0.01f);
    glVertex3f(x + w / 2 - frameThick, y + h / 2 - frameThick, z + 0.01f);
    glVertex3f(x - w / 2 + frameThick, y + h / 2 - frameThick, z + 0.01f);
    glEnd();
  }
  else if (orient == 1) // Back (-z)
  {
    glBegin(GL_QUADS);
    glVertex3f(x - w / 2, y - h / 2, z);
    glVertex3f(x + w / 2, y - h / 2, z);
    glVertex3f(x + w / 2, y + h / 2, z);
    glVertex3f(x - w / 2, y + h / 2, z);
    glEnd();

    glColor4f(0.6f, 0.8f, 0.9f, 0.4f);
    glBegin(GL_QUADS);
    glVertex3f(x - w / 2 + frameThick, y - h / 2 + frameThick, z - 0.01f);
    glVertex3f(x + w / 2 - frameThick, y - h / 2 + frameThick, z - 0.01f);
    glVertex3f(x + w / 2 - frameThick, y + h / 2 - frameThick, z - 0.01f);
    glVertex3f(x - w / 2 + frameThick, y + h / 2 - frameThick, z - 0.01f);
    glEnd();
  }
  else if (orient == 2)
  {
    glBegin(GL_QUADS);
    glVertex3f(x, y - h / 2, z - w / 2);
    glVertex3f(x, y - h / 2, z + w / 2);
    glVertex3f(x, y + h / 2, z + w / 2);
    glVertex3f(x, y + h / 2, z - w / 2);
    glEnd();

    glColor4f(0.6f, 0.8f, 0.9f, 0.4f);
    glBegin(GL_QUADS);
    glVertex3f(x - 0.01f, y - h / 2 + frameThick, z - w / 2 + frameThick);
    glVertex3f(x - 0.01f, y - h / 2 + frameThick, z + w / 2 - frameThick);
    glVertex3f(x - 0.01f, y + h / 2 - frameThick, z + w / 2 - frameThick);
    glVertex3f(x - 0.01f, y + h / 2 - frameThick, z - w / 2 + frameThick);
    glEnd();
  }
  else if (orient == 3)
  {
    glBegin(GL_QUADS);
    glVertex3f(x, y - h / 2, z - w / 2);
    glVertex3f(x, y - h / 2, z + w / 2);
    glVertex3f(x, y + h / 2, z + w / 2);
    glVertex3f(x, y + h / 2, z - w / 2);
    glEnd();

    glColor4f(0.6f, 0.8f, 0.9f, 0.4f);
    glBegin(GL_QUADS);
    glVertex3f(x + 0.01f, y - h / 2 + frameThick, z - w / 2 + frameThick);
    glVertex3f(x + 0.01f, y - h / 2 + frameThick, z + w / 2 - frameThick);
    glVertex3f(x + 0.01f, y + h / 2 - frameThick, z + w / 2 - frameThick);
    glVertex3f(x + 0.01f, y + h / 2 - frameThick, z - w / 2 + frameThick);
    glEnd();
  }

  glDisable(GL_BLEND);
  glEnable(GL_TEXTURE_2D);
  glColor3f(1.0f, 1.0f, 1.0f);
}

void drawDoor(float x, float y, float z, float angle)
{
  glDisable(GL_TEXTURE_2D);

  glPushMatrix();
  glTranslatef(x, y, z);

  glTranslatef(0.9f, 0, 0);
  glRotatef(angle, 0, 1, 0);
  glTranslatef(-0.9f, 0, 0);

  glColor3f(0.25f, 0.15f, 0.1f);
  glPushMatrix();
  glScalef(1.8f, 2.8f, 0.15f);
  glutSolidCube(1.0);
  glPopMatrix();

  glColor3f(0.8f, 0.7f, 0.3f);
  glPushMatrix();
  glTranslatef(-0.6f, 0, 0.15f);
  glScalef(0.2f, 0.08f, 0.08f);
  glutSolidCube(1.0);
  glPopMatrix();

  glColor3f(0.2f, 0.12f, 0.08f);
  glPushMatrix();
  glTranslatef(0, 0.6f, 0.08f);
  glScalef(1.4f, 1.0f, 0.02f);
  glutSolidCube(1.0);
  glPopMatrix();

  glPushMatrix();
  glTranslatef(0, -0.6f, 0.08f);
  glScalef(1.4f, 1.0f, 0.02f);
  glutSolidCube(1.0);
  glPopMatrix();

  glPopMatrix();
  glEnable(GL_TEXTURE_2D);
  glColor3f(1.0f, 1.0f, 1.0f);
}

bool checkCollisionWithStairs(float nextX, float nextY, float nextZ)
{
  float padding = 0.25f;
  for (int i = 0; i < stairCount; i++)
  {
    Stair *st = &stairs[i];

    if (nextX >= st->minX - padding && nextX <= st->maxX + padding &&
        nextZ >= st->minZ - padding && nextZ <= st->maxZ + padding &&
        nextY >= st->minY - 0.5f && nextY <= st->maxY + 0.5f)
    {
      return true;
    }
  }
  return false;
}

void drawStairs(Building *b, float baseYWorld)
{
  float stepHeight = 0.25f;
  float stepDepth = 0.35f;
  float stepWidth = 1.5f;

  float stairX = b->w - 1.0f;
  float stairZ0 = -b->d + 1.0f;

  for (int floor = 0; floor < b->stories; floor++)
  {
    float baseY = floor * b->storyHeight;
    int numSteps = (int)(b->storyHeight / stepHeight);

    for (int s = 0; s < numSteps; s++)
    {
      float y = baseY + s * stepHeight;
      float z = stairZ0 + s * stepDepth;

      if (z > b->d - 1.0f)
      {
        float openWidth = 1.8f;
        float openHeight = 2.5f;
        float passageX = b->x + stairX;
        float passageZ = b->z + z + 0.3f;

        // optional: visualize the opening for debugging
        glDisable(GL_TEXTURE_2D);
        glColor3f(0.7f, 0.9f, 0.7f);
        glPushMatrix();
        glTranslatef(passageX, baseYWorld + y + 1.2f, passageZ);
        glScalef(openWidth, openHeight, 0.1f);
        glutWireCube(1.0);
        glPopMatrix();
        glEnable(GL_TEXTURE_2D);
        break;
      }

      glPushMatrix();
      glTranslatef(stairX, y, z);
      glColor3f(0.6f, 0.6f, 0.6f);
      glScalef(stepWidth, stepHeight, stepDepth);
      glutSolidCube(1.0f);
      glPopMatrix();

      // record collision box in WORLD coordinates
      if (stairCount < MAX_STAIRS)
      {
        Stair *st = &stairs[stairCount++];
        float halfW = stepWidth / 2.0f;
        float halfD = stepDepth / 2.0f;

        // convert local (relative to building) to world space
        st->minX = (b->x + stairX) - halfW;
        st->maxX = (b->x + stairX) + halfW;
        st->minZ = (b->z + z) - halfD;
        st->maxZ = (b->z + z) + halfD;
        st->minY = baseYWorld + y;
        st->maxY = baseYWorld + y + stepHeight;
      }
    }

    // flip side for the next floor
    stairX = -stairX;
    stairZ0 = -b->d + 1.0f;
  }
}

void drawBuilding(Building *b)
{
  glEnable(GL_TEXTURE_2D);
  float baseY = getTerrainHeight(b->x, b->z);
  float totalHeight = b->stories * b->storyHeight;

  glPushMatrix();
  glTranslatef(b->x, baseY, b->z);

  GLuint wallTex = (b->wallTex == 0) ? texBuilding : texConcrete;

  // Draw each floor
  for (int floor = 0; floor < b->stories; floor++)
  {
    float floorY = floor * b->storyHeight;
    float nextFloorY = (floor + 1) * b->storyHeight;

    // Back wall (-z)
    glBindTexture(GL_TEXTURE_2D, wallTex);
    quad(wallTex, -b->w, floorY, -b->d, b->w, floorY, -b->d, b->w, nextFloorY,
         -b->d, -b->w, nextFloorY, -b->d, 0, 0, 1, 2);

    // Windows on back wall
    glDisable(GL_TEXTURE_2D);
    drawWindow(-b->w / 2, floorY + b->storyHeight / 2, -b->d - 0.05f, 1.5f,
               1.8f, 1);
    drawWindow(b->w / 2, floorY + b->storyHeight / 2, -b->d - 0.05f, 1.5f, 1.8f,
               1);
    glEnable(GL_TEXTURE_2D);

    // Left wall (-x)
    glBindTexture(GL_TEXTURE_2D, wallTex);
    quad(wallTex, -b->w, floorY, -b->d, -b->w, floorY, b->d, -b->w, nextFloorY,
         b->d, -b->w, nextFloorY, -b->d, 1, 0, 0, 2);

    glDisable(GL_TEXTURE_2D);
    drawWindow(-b->w - 0.05f, floorY + b->storyHeight / 2, 0, 1.5f, 1.8f, 2);
    glEnable(GL_TEXTURE_2D);

    // Right wall (+x)
    glBindTexture(GL_TEXTURE_2D, wallTex);
    quad(wallTex, b->w, floorY, -b->d, b->w, floorY, b->d, b->w, nextFloorY,
         b->d, b->w, nextFloorY, -b->d, -1, 0, 0, 2);

    glDisable(GL_TEXTURE_2D);
    drawWindow(b->w + 0.05f, floorY + b->storyHeight / 2, 0, 1.5f, 1.8f, 3);
    glEnable(GL_TEXTURE_2D);

    // Front wall (+z) - ground floor has door
    glBindTexture(GL_TEXTURE_2D, wallTex);
    if (floor == 0)
    {
      float doorWidth = 1.8f;

      // Left part of front wall
      quad(wallTex, -b->w, floorY, b->d, -doorWidth / 2, floorY, b->d,
           -doorWidth / 2, nextFloorY, b->d, -b->w, nextFloorY, b->d, 0, 0, -1,
           2);

      quad(wallTex, doorWidth / 2, floorY, b->d, b->w, floorY, b->d, b->w,
           nextFloorY, b->d, doorWidth / 2, nextFloorY, b->d, 0, 0, -1, 2);

      float doorHeight = 2.8f;
      quad(wallTex, -doorWidth / 2, doorHeight, b->d, doorWidth / 2, doorHeight,
           b->d, doorWidth / 2, nextFloorY, b->d, -doorWidth / 2, nextFloorY,
           b->d, 0, 0, -1, 1);

      glDisable(GL_TEXTURE_2D);
      drawDoor(0, doorHeight / 2, b->d + 0.08f, b->doorAngle);
      glEnable(GL_TEXTURE_2D);
    }
    else
    {
      quad(wallTex, -b->w, floorY, b->d, b->w, floorY, b->d, b->w, nextFloorY,
           b->d, -b->w, nextFloorY, b->d, 0, 0, -1, 2);

      glDisable(GL_TEXTURE_2D);
      drawWindow(-b->w / 2, floorY + b->storyHeight / 2, b->d + 0.05f, 1.5f,
                 1.8f, 0);
      drawWindow(b->w / 2, floorY + b->storyHeight / 2, b->d + 0.05f, 1.5f,
                 1.8f, 0);
      glEnable(GL_TEXTURE_2D);
    }

    // Floor ceiling
    glBindTexture(GL_TEXTURE_2D, texConcrete);
    quad(texConcrete, -b->w, nextFloorY, -b->d, b->w, nextFloorY, -b->d, b->w,
         nextFloorY, b->d, -b->w, nextFloorY, b->d, 0, 1, 0, 2);

    // Floor
    if (floor == 0 || floor < b->stories)
    {
      quad(texConcrete, -b->w, floorY + 0.05f, -b->d, b->w, floorY + 0.05f,
           -b->d, b->w, floorY + 0.05f, b->d, -b->w, floorY + 0.05f, b->d, 0, 1,
           0, 2);
    }
  }

  // Roof
  glBindTexture(GL_TEXTURE_2D, texRoof);
  quad(texRoof, -b->w, totalHeight, -b->d, b->w, totalHeight, -b->d, b->w,
       totalHeight, b->d, -b->w, totalHeight, b->d, 0, 1, 0, 2);

  // Draw stairs inside the building (and record world-space collisions)
  drawStairs(b, baseY);

  glPopMatrix();
}

void drawTarget(Target *t)
{
  glPushMatrix();
  glTranslatef(t->x, t->y, t->z);
  glScalef(t->scale, t->scale, t->scale);

  bool isHit = (t->hits > 0 && hitEffectTime < 1.0f);

  if (isHit && hitShaderProgram != 0)
  {
    glUseProgram(hitShaderProgram);
    GLint timeLoc = glGetUniformLocation(hitShaderProgram, "uTime");
    if (timeLoc != -1)
      glUniform1f(timeLoc, hitEffectTime);
  }
  else
  {
    glUseProgram(0);

    float mat_color[4];
    if (t->hits == 0)
    {
      mat_color[0] = 1.0f;
      mat_color[1] = 0.3f;
      mat_color[2] = 0.3f;
      mat_color[3] = 1.0f;
    }
    else if (t->hits == 1)
    {
      mat_color[0] = 1.0f;
      mat_color[1] = 0.6f;
      mat_color[2] = 0.1f;
      mat_color[3] = 1.0f;
    }
    else
    {
      mat_color[0] = 1.0f;
      mat_color[1] = 0.9f;
      mat_color[2] = 0.2f;
      mat_color[3] = 1.0f;
    }

    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, mat_color);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, (float[]){0.3f, 0.3f, 0.3f, 1.0f});
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 32.0f);
  }

  switch (t->type)
  {
  case 0:
    glutSolidCube(1.5);
    break;
  case 1:
    glutSolidSphere(0.8, 16, 16);
    break;
  case 2:
  default:
    glutSolidTorus(0.3, 0.6, 12, 16);
    break;
  }

  glUseProgram(0);
  glColor3f(1.0f, 1.0f, 1.0f);
  glPopMatrix();
}

bool checkCollision(float nextX, float nextZ)
{
  float padding = 0.3f;

  for (int i = 0; i < MAX_BUILDINGS; i++)
  {
    Building *b = &buildings[i];
    float minX = b->x - b->w - padding;
    float maxX = b->x + b->w + padding;
    float minZ = b->z - b->d - padding;
    float maxZ = b->z + b->d + padding;

    if (b->doorOpen)
    {
      float doorWidth = 1.8f;
      float doorDepth = 2.0f;
      float doorMinX = b->x - doorWidth / 2.0f;
      float doorMaxX = b->x + doorWidth / 2.0f;
      float doorMinZ = b->z + b->d - 0.5f;
      float doorMaxZ = b->z + b->d + doorDepth;

      if (nextX >= doorMinX && nextX <= doorMaxX &&
          nextZ >= doorMinZ && nextZ <= doorMaxZ)
      {
        return false;
      }
    }

    if (nextX >= minX && nextX <= maxX &&
        nextZ >= minZ && nextZ <= maxZ)
    {

      if (b->doorOpen && isInsideBuilding(nextX, nextZ))
      {
        continue;
      }
      return true;
    }
  }

  float nextY = getStairHeightAt(nextX, nextZ);
  if (checkCollisionWithStairs(nextX, nextY, nextZ))
    return true;

  return false;
}

void lookAt(double ex, double ey, double ez, double cx, double cy, double cz,
            double ux, double uy, double uz)
{
  double f[3] = {cx - ex, cy - ey, cz - ez};
  double lenf = sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
  if (lenf < 1e-9)
    lenf = 1.0;
  for (int i = 0; i < 3; i++)
    f[i] /= lenf;

  double up[3] = {ux, uy, uz};
  double s[3] = {f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2],
                 f[0] * up[1] - f[1] * up[0]};
  double lens = sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
  if (lens < 1e-9)
    lens = 1.0;
  for (int i = 0; i < 3; i++)
    s[i] /= lens;

  double u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2],
                 s[0] * f[1] - s[1] * f[0]};
  double M[16] = {s[0], u[0], -f[0], 0, s[1], u[1], -f[1], 0,
                  s[2], u[2], -f[2], 0, 0, 0, 0, 1};
  glMultMatrixd(M);
  glTranslated(-ex, -ey, -ez);
}

void addView()
{
  if (mode_view == 0 || mode_view == 1)
  {
    float radTh = th * DEG2RAD;
    float radPh = ph * DEG2RAD;
    float radius = 50.0f;
    float ex = radius * cosf(radPh) * sinf(radTh);
    float ey = radius * sinf(radPh) + 10.0f;
    float ez = radius * cosf(radPh) * cosf(radTh);
    lookAt(ex, ey, ez, 0.0, 5.0, 0.0, 0.0, 1.0, 0.0);
  }
  else if (mode_view == 2)
  {
    float radYaw = fpvYaw * DEG2RAD;
    float radPitch = fpvPitch * DEG2RAD;
    float dirX = cosf(radPitch) * sinf(radYaw);
    float dirY = sinf(radPitch);
    float dirZ = cosf(radPitch) * cosf(radYaw);
    lookAt(fpvX, fpvY, fpvZ, fpvX + dirX, fpvY + dirY, fpvZ + dirZ, 0.0, 1.0,
           0.0);
  }
}

void idle()
{
  lightAngle += 0.15;
  if (lightAngle > 360)
    lightAngle -= 360;

  updateBullets();
  checkCollisions();

  if (gunRecoil > 0)
  {
    gunRecoil -= 0.08f;
    if (gunRecoil < 0)
      gunRecoil = 0;
  }

  if (muzzleFlashTime > 0)
  {
    muzzleFlashTime -= 0.015f;
    if (muzzleFlashTime < 0)
      muzzleFlashTime = 0;
  }

  if (gunAimBlend > 0.001f)
  {

    float s = 0.22f;
    gunAimYaw += (gunAimTargetYaw - gunAimYaw) * s;
    gunAimPitch += (gunAimTargetPitch - gunAimPitch) * s;

    gunAimBlend -= 0.03f;
    if (gunAimBlend < 0.0f)
      gunAimBlend = 0.0f;

    if (gunAimBlend <= 0.0f)
    {

      gunAimYaw *= 0.85f;
      gunAimPitch *= 0.85f;
    }
  }

  // Update door animations
  for (int i = 0; i < MAX_BUILDINGS; i++)
  {
    if (buildings[i].doorOpen && buildings[i].doorAngle < 90.0f)
    {
      buildings[i].doorAngle += 2.5f;
      if (buildings[i].doorAngle > 90.0f)
        buildings[i].doorAngle = 90.0f;
    }
    else if (!buildings[i].doorOpen && buildings[i].doorAngle > 0.0f)
    {
      buildings[i].doorAngle -= 2.5f;
      if (buildings[i].doorAngle < 0.0f)
        buildings[i].doorAngle = 0.0f;
    }
  }

  for (int i = 0; i < MAX_OBJECTS; i++)
  {
    if (!targets[i].alive && targets[i].explodeTime >= 0)
    {
      targets[i].explodeTime += 0.02f;
      if (targets[i].explodeTime > 1.5f)
        targets[i].explodeTime = -1.0f;
    }
  }
  glutPostRedisplay();
}

void display()
{
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  updateHitEffect();

  glClearColor(0.5f, 0.7f, 0.9f, 1.0f);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  if (mode_view == 0)
  {
    double size = dim;
    glOrtho(-asp * size, asp * size, -size, size, -100, 100);
  }
  else
  {
    gluPerspective(fov, asp, 0.1, 500.0);
  }
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  addView();

  float lx = lightDist * cosf(DEG2RAD * lightAngle);
  float lz = lightDist * sinf(DEG2RAD * lightAngle);
  float pos[] = {lx, lightY, lz, 1.0f};

  glEnable(GL_LIGHTING);
  glEnable(GL_LIGHT0);
  float amb[] = {0.5f, 0.5f, 0.55f, 1};
  float diff[] = {1, 0.95, 0.9, 1};
  glLightfv(GL_LIGHT0, GL_AMBIENT, amb);
  glLightfv(GL_LIGHT0, GL_DIFFUSE, diff);
  glLightfv(GL_LIGHT0, GL_POSITION, pos);

  drawTerrain();

  // Draw all buildings
  for (int i = 0; i < MAX_BUILDINGS; i++)
  {
    drawBuilding(&buildings[i]);
  }

  // Draw targets
  for (int i = 0; i < MAX_OBJECTS; i++)
  {
    if (targets[i].alive)
    {
      drawTarget(&targets[i]);
    }
    else if (targets[i].explodeTime >= 0 && targets[i].explodeTime < 1.5f)
    {
      drawExplosion(targets[i].x, targets[i].y, targets[i].z,
                    targets[i].explodeTime);
    }
  }

  drawBullets();

  // Draw light indicator
  glDisable(GL_LIGHTING);
  glDisable(GL_TEXTURE_2D);
  glColor3f(1, 1, 0.8f);
  glPushMatrix();
  glTranslatef(lx, lightY, lz);
  glutSolidSphere(0.5, 16, 16);
  glPopMatrix();

  // FPV mode: draw gun and crosshair
  if (mode_view == 2)
  {
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    drawCrosshair();
    drawMuzzleFlash();
    drawGun();
    glPopMatrix();

    // Draw door prompt
    int nearDoor = getNearestDoor();
    if (nearDoor >= 0)
    {
      glDisable(GL_DEPTH_TEST);
      glDisable(GL_LIGHTING);
      glMatrixMode(GL_PROJECTION);
      glPushMatrix();
      glLoadIdentity();
      glOrtho(-1, 1, -1, 1, -1, 1);
      glMatrixMode(GL_MODELVIEW);
      glPushMatrix();
      glLoadIdentity();

      glColor3f(1.0f, 1.0f, 0.0f);
      glRasterPos2f(-0.18f, -0.7f);
      const char *msg = buildings[nearDoor].doorOpen ? "Press E to close door"
                                                     : "Press E to open door";
      for (const char *c = msg; *c != '\0'; c++)
      {
        glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, *c);
      }

      glPopMatrix();
      glMatrixMode(GL_PROJECTION);
      glPopMatrix();
      glMatrixMode(GL_MODELVIEW);
      glEnable(GL_DEPTH_TEST);
    }
  }

  glutSwapBuffers();
}

void special(int key, int x, int y)
{
  if (mode_view == 0 || mode_view == 1)
  {
    if (key == GLUT_KEY_LEFT)
      th -= 5;
    else if (key == GLUT_KEY_RIGHT)
      th += 5;
    else if (key == GLUT_KEY_UP)
      ph += 5;
    else if (key == GLUT_KEY_DOWN)
      ph -= 5;
  }
  else if (mode_view == 2)
  {
    if (key == GLUT_KEY_LEFT)
      fpvYaw += 2.0f;
    else if (key == GLUT_KEY_RIGHT)
      fpvYaw -= 2.0f;
    else if (key == GLUT_KEY_UP)
      fpvPitch += 2.0f;
    else if (key == GLUT_KEY_DOWN)
      fpvPitch -= 2.0f;

    if (fpvPitch > 89.0f)
      fpvPitch = 89.0f;
    if (fpvPitch < -89.0f)
      fpvPitch = -89.0f;
  }
  glutPostRedisplay();
}

void toggleMouseCapture(bool capture)
{
  mouseCaptured = capture;
  if (mouseCaptured)
  {
    glutSetCursor(GLUT_CURSOR_NONE);
    glutWarpPointer(windowWidth / 2, windowHeight / 2);
  }
  else
  {
    glutSetCursor(GLUT_CURSOR_INHERIT);
  }
}

void mouseMove(int x, int y)
{
  if (!mouseCaptured)
    return;

  int dx = x - windowWidth / 2;
  int dy = y - windowHeight / 2;

  fpvYaw -= dx * fpvSens;
  fpvPitch += dy * fpvSens;

  if (fpvPitch > 89.0f)
    fpvPitch = 89.0f;
  if (fpvPitch < -89.0f)
    fpvPitch = -89.0f;

  glutWarpPointer(windowWidth / 2, windowHeight / 2);
  glutPostRedisplay();
}

void mouseClick(int button, int state, int x, int y)
{
  if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN)
  {
    shootBullet();
  }
  else if (button == GLUT_RIGHT_BUTTON && state == GLUT_DOWN)
  {
    toggleMouseCapture(!mouseCaptured);
  }
}

void keyboard(unsigned char ch, int x, int y)
{
  if (ch == 27)
    exit(0);
  else if (ch == 'v')
  {
    mode_view = (mode_view + 1) % 3;
    const char *modeName = (mode_view == 0)   ? "Orthographic"
                           : (mode_view == 1) ? "Perspective Orbit"
                                              : "First-Person";
    printf("Switched to %s View\n", modeName);
    if (mode_view == 2)
    {
      toggleMouseCapture(true);
    }
    else
    {
      toggleMouseCapture(false);
    }
  }
  else if (ch == ' ' && mode_view == 2)
  {
    shootBullet();
  }
  else if (ch == 'e' && mode_view == 2)
  {
    int nearDoor = getNearestDoor();
    if (nearDoor >= 0)
    {
      buildings[nearDoor].doorOpen = !buildings[nearDoor].doorOpen;
      printf("Door %d %s\n", nearDoor,
             buildings[nearDoor].doorOpen ? "opening..." : "closing...");
    }
  }
  else if (ch == 'r')
  {
    initTargets();
    bulletCount = 0;
    for (int i = 0; i < MAX_BULLETS; i++)
      bullets[i].active = false;
    for (int i = 0; i < MAX_BUILDINGS; i++)
    {
      buildings[i].doorOpen = false;
      buildings[i].doorAngle = 0.0f;
    }
    printf("Game reset!\n");
  }
  else if (ch == 'i')
    lightY += 1.0f;
  else if (ch == 'k')
    lightY -= 1.0f;
  else if (ch == 'j')
    lightAngle -= 5;
  else if (ch == 'u')
    lightAngle += 5;
  else if (mode_view == 2)
  {
    float radYaw = fpvYaw * DEG2RAD;
    float forwardX = sinf(radYaw);
    float forwardZ = cosf(radYaw);
    float rightX = cosf(radYaw);
    float rightZ = -sinf(radYaw);

    if (ch == 'w')
    {
      float nextX = fpvX + forwardX * fpvSpeed;
      float nextZ = fpvZ + forwardZ * fpvSpeed;
      if (!checkCollision(nextX, nextZ))
      {
        fpvX = nextX;
        fpvZ = nextZ;
      }
      float targetY = getStairHeightAt(fpvX, fpvZ);
      fpvY += (targetY - fpvY) * 0.3f;
    }
    else if (ch == 's')
    {
      float nextX = fpvX - forwardX * fpvSpeed;
      float nextZ = fpvZ - forwardZ * fpvSpeed;
      if (!checkCollision(nextX, nextZ))
      {
        fpvX = nextX;
        fpvZ = nextZ;
      }
      float targetY = getStairHeightAt(fpvX, fpvZ);
      fpvY += (targetY - fpvY) * 0.3f;
    }
    else if (ch == 'a')
    {
      float nextX = fpvX - rightX * fpvSpeed;
      float nextZ = fpvZ - rightZ * fpvSpeed;
      if (!checkCollision(nextX, nextZ))
      {
        fpvX = nextX;
        fpvZ = nextZ;
      }
      float targetY = getStairHeightAt(fpvX, fpvZ);
      fpvY += (targetY - fpvY) * 0.3f;
    }
    else if (ch == 'd')
    {
      float nextX = fpvX + rightX * fpvSpeed;
      float nextZ = fpvZ + rightZ * fpvSpeed;
      if (!checkCollision(nextX, nextZ))
      {
        fpvX = nextX;
        fpvZ = nextZ;
      }
      float targetY = getStairHeightAt(fpvX, fpvZ);
      fpvY += (targetY - fpvY) * 0.3f;
    }
    else if (ch == 'q')
    {
      fpvY -= 0.3f; // Move down
      if (fpvY < 1.0f)
        fpvY = 1.0f;
    }
    else if (ch == 'z')
    {
      fpvY += 0.3f; // Move up
      if (fpvY > 30.0f)
        fpvY = 30.0f;
    }
    else if (ch == 'c')
    {
      fpvYaw = 180.0f;
      fpvPitch = 0.0f;
    }
  }
  glutPostRedisplay();
}

void init()
{
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_COLOR_MATERIAL);
  glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

  texGround = loadTexture("gravelly_sand.png");
  texBuilding = loadTexture("wall.png");
  texRoof = loadTexture("ceiling.png");
  texConcrete = loadTexture("concrete.png");
  texMetal = loadTexture("metal.png");
  texWood = loadTexture("wood.png");
  texTarget = loadTexture("metal.png");

  texGunDiffuse = loadTexture("gun_specular.png");
  if (!texGunDiffuse)
    texGunDiffuse = loadTexture("gun.png");
  if (!texGunDiffuse)
    texGunDiffuse = loadTexture("Gun _obj/Gun.png");
  if (!texGunDiffuse)
    texGunDiffuse = texMetal;

  texGunSpecular = loadTexture("gun_specular.png");
  if (!texGunSpecular)
    texGunSpecular = texMetal;

  hitShaderProgram = createShaderProgramFromFiles(
      "shaders/hit_vertex.glsl",
      "shaders/hit_fragment.glsl");
  if (hitShaderProgram == 0)
  {
    printf("WARNING: Hit shader failed to load, using fixed pipeline\n");
  }

  muzzleFlashProgram = createShaderProgramFromFiles(
      "shaders/muzzle_vertex.glsl",
      "shaders/muzzle_fragment.glsl");
  if (muzzleFlashProgram == 0)
  {
    printf("WARNING: Muzzle flash shader failed to load, using fallback\n");
  }
  else
  {
    printf("Muzzle flash shader loaded successfully\n");
    initMuzzleFlashGeometry();
  }

  printf("texGunDiffuse=%u texGunSpecular=%u\n", texGunDiffuse, texGunSpecular);

  gunModel.vertexCount = 0;
  if (!loadOBJ("Glock18.obj", &gunModel))
  {
    printf("Using procedural gun model (gun.obj not found)\n");
  }
  printf("verts=%d texCoords=%p\n", gunModel.vertexCount, gunModel.texCoords);

  initBuildings();
  initTargets();

  for (int i = 0; i < MAX_BULLETS; i++)
    bullets[i].active = false;

  mouseCaptured = false;
}

void reshape(int w, int h)
{
  if (h == 0)
    h = 1;
  asp = (double)w / (double)h;
  glViewport(0, 0, w, h);
  windowWidth = w;
  windowHeight = h;
}

int main(int argc, char **argv)
{
  srand((unsigned)time(NULL));
  glutInit(&argc, argv);
  glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_DEPTH);
  glutInitWindowSize(windowWidth, windowHeight);
  glutCreateWindow("FPV Arena - Multi-Story Buildings");
  init();
  glutDisplayFunc(display);
  glutSpecialFunc(special);
  glutKeyboardFunc(keyboard);
  glutReshapeFunc(reshape);
  glutIdleFunc(idle);
  glutPassiveMotionFunc(mouseMove);
  glutMotionFunc(mouseMove);
  glutMouseFunc(mouseClick);

  printf("=== FPV ARENA SHOOTING GAME ===\n");
  printf("Buildings have multiple floors to explore!\n\n");
  printf("V - Switch view modes (Orthographic/Perspective/FPV)\n");
  printf("\n FPV Mode Controls:\n");
  printf("W/A/S/D - Move horizontally\n");
  printf("Q/Z - Move down/up (use to go between floors)\n");
  printf("Mouse - Look around (when captured)\n");
  printf("Left Click / SPACE - Shoot\n");
  printf("Right Click - Toggle mouse capture\n");
  printf("E - Open/Close door (when near entrance)\n");
  printf("C - Center view\n");
  printf("R - Reset targets and close all doors\n");
  printf("\n Light Controls:\n");
  printf("I/K - Move light up/down\n");
  printf("J/U - Rotate light\n");
  printf("\n Objective: Destroy all %d targets!\n", MAX_OBJECTS);
  printf("- Buildings have %d floors each (2-5 stories)\n", MAX_BUILDINGS);
  printf("- Targets are hidden on different floors\n");
  printf("- Use Q/Z to navigate between floors\n");
  printf("- Look for stairs inside buildings\n");
  printf("- Windows show targets from outside\n");
  printf("- Each target requires 3 hits to destroy\n");
  printf("\nESC - Exit\n");
  printf("================================\n");

  glutMainLoop();
  return 0;
}