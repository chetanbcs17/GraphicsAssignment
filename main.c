// fpv_arena_game.c
#define GL_GLEXT_PROTOTYPES
#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define DEG2RAD 0.0174533
#define MAX_BULLETS 200
#define MAX_OBJECTS 25
#define MAX_BUILDINGS 1

// Camera rotation (orbit)
int th = 45, ph = 20;
float camX = 0, camY = 2, camZ = 15;

int mode_view = 2; // 0 = orbit view, 1 = Perspective orbit, 2 = FPV
int w, h, n;
// FPV camera
float fpvX = 0.0f, fpvY = 2.5f, fpvZ = 5.0f;
float fpvYaw = 180.0f;
float fpvPitch = 0.0f;
float fpvSpeed = 0.3f;
float fpvSens = 0.15f;

float lightAngle = 90.0;
float lightY = 15.0;
float lightDist = 30.0;

int mode_proj = 0;
float asp = 1;
float dim = 60;
int fov = 70;

int windowWidth = 1200, windowHeight = 800;
int lastMouseX, lastMouseY;
bool firstMouse = true;
#define MAX_STAIRS 10

// Texture IDs
GLuint texGround, texBuilding, texRoof, texConcrete, texMetal, texWood, texTarget;
GLuint texGunDiffuse, texGunSpecular, texGunNormal;

// Building structure
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

// Gun model structure
typedef struct
{
    float *vertices;
    float *normals;
    float *texCoords;
    int vertexCount;
} GunModel;

GunModel gunModel;

// Bullet structure
typedef struct
{
    float x, y, z;
    float dx, dy, dz;
    bool active;
    float life;
} Bullet;

// Target object structure
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

// Gun animation variables
float gunRecoil = 0.0f;
float muzzleFlashTime = 0.0f;

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

// Check if player is near a door
int getNearestDoor()
{
    float doorRange = 4.0f;
    for (int i = 0; i < MAX_BUILDINGS; i++)
    {
        Building *b = &buildings[i];

        // Door is on the front face (positive z side)
        float doorX = b->x;
        float doorZ = b->z + b->d;
        float doorY = 0.0f;

        float dx = fpvX - doorX;
        float dy = fpvY - doorY - 1.5f; // Door center height
        float dz = fpvZ - doorZ;
        float dist = sqrtf(dx * dx + dz * dz);

        if (dist < doorRange && fabsf(dy) < 2.0f)
            return i;
    }
    return -1;
}

// Crosshair display
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

    // Outer circle
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

void drawGun()
{
    glDisable(GL_BLEND);
    glPushMatrix();

    glTranslatef(0.35f, -0.35f, -2.0f);
    glTranslatef(0, gunRecoil * 0.05f, gunRecoil * 0.1f);

    int tid = getNearestTargetIndex();
    if (tid >= 0)
    {
        float dx = targets[tid].x - fpvX;
        float dy = targets[tid].y - fpvY;
        float dz = targets[tid].z - fpvZ;

        float targetYaw = atan2f(dx, dz) * (180.0f / M_PI);
        float horiz = sqrtf(dx * dx + dz * dz);
        float targetPitch = atan2f(dy, horiz) * (180.0f / M_PI);

        float yawOffset = (targetYaw - fpvYaw) * 0.3f;
        float pitchOffset = (targetPitch - fpvPitch) * 0.3f;

        if (yawOffset > 15.0f)
            yawOffset = 15.0f;
        if (yawOffset < -15.0f)
            yawOffset = -15.0f;
        if (pitchOffset > 15.0f)
            pitchOffset = 15.0f;
        if (pitchOffset < -15.0f)
            pitchOffset = -15.0f;

        glRotatef(yawOffset, 0, 1, 0);
        glRotatef(-pitchOffset, 1, 0, 0);
    }

    glPushMatrix();
    glRotatef(-90, 0, 1, 0);
    glScalef(0.28f, 0.28f, 0.28f);

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_LIGHTING);
    glEnable(GL_NORMALIZE);

    float gunAmbient[] = {0.35f, 0.35f, 0.37f, 1.0f};
    float gunDiffuse[] = {0.45f, 0.45f, 0.5f, 1.0f};
    float gunSpecular[] = {0.95f, 0.95f, 1.0f, 1.0f};
    float gunShininess[] = {90.0f};
    glMaterialfv(GL_FRONT, GL_AMBIENT, gunAmbient);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, gunDiffuse);
    glMaterialfv(GL_FRONT, GL_SPECULAR, gunSpecular);
    glMaterialfv(GL_FRONT, GL_SHININESS, gunShininess);

    if (gunModel.vertexCount > 0)
    {
        glBindTexture(GL_TEXTURE_2D, texGunDiffuse);
        glBegin(GL_TRIANGLES);
        for (int i = 0; i < gunModel.vertexCount; i++)
        {
            glNormal3f(gunModel.normals[i * 3],
                       gunModel.normals[i * 3 + 1],
                       gunModel.normals[i * 3 + 2]);
            glTexCoord2f(gunModel.texCoords[i * 2],
                         gunModel.texCoords[i * 2 + 1]);
            glVertex3f(gunModel.vertices[i * 3],
                       gunModel.vertices[i * 3 + 1],
                       gunModel.vertices[i * 3 + 2]);
        }
        glEnd();
    }
    else
    {
        // Procedural gun fallback
        glBindTexture(GL_TEXTURE_2D, texMetal);

        glPushMatrix();
        glColor3f(0.22f, 0.22f, 0.24f);
        glTranslatef(0, 0, -0.5f);
        glScalef(0.5f, 0.2f, 1.2f);
        glutSolidCube(1.0);
        glPopMatrix();

        glColor3f(0.17f, 0.17f, 0.19f);
        glPushMatrix();
        glTranslatef(0.0f, 0.08f, -1.5f);
        glRotatef(90, 0, 1, 0);
        GLUquadric *barrel = gluNewQuadric();
        gluQuadricTexture(barrel, GL_TRUE);
        gluCylinder(barrel, 0.06, 0.05, 1.2, 24, 1);
        gluDeleteQuadric(barrel);
        glPopMatrix();

        glColor3f(0.1f, 0.1f, 0.1f);
        glPushMatrix();
        glTranslatef(0.0f, 0.08f, -2.7f);
        glRotatef(90, 0, 1, 0);
        GLUquadric *muzzle = gluNewQuadric();
        gluQuadricTexture(muzzle, GL_TRUE);
        gluCylinder(muzzle, 0.07, 0.06, 0.15, 24, 1);
        gluDeleteQuadric(muzzle);
        glPopMatrix();

        glColor3f(0.2f, 0.2f, 0.22f);
        glPushMatrix();
        glTranslatef(0, 0.15f, -0.8f + gunRecoil * 0.15f);
        glScalef(0.45f, 0.12f, 1.1f);
        glutSolidCube(1.0);
        glPopMatrix();

        glColor3f(0.14f, 0.08f, 0.05f);
        glPushMatrix();
        glTranslatef(0.0f, -0.2f, 0.1f);
        glRotatef(-25, 1, 0, 0);
        glScalef(0.15f, 0.35f, 0.1f);
        glutSolidCube(1.0);
        glPopMatrix();
    }

    glDisable(GL_TEXTURE_2D);
    glPopMatrix();
    glPopMatrix();

    glEnable(GL_BLEND);
    drawMuzzleFlash();
    glDisable(GL_BLEND);

    float defAmbient[] = {0.2f, 0.2f, 0.2f, 1.0f};
    float defDiffuse[] = {0.8f, 0.8f, 0.8f, 1.0f};
    float defSpecular[] = {0.0f, 0.0f, 0.0f, 1.0f};
    float defShininess[] = {0.0f};
    glMaterialfv(GL_FRONT, GL_AMBIENT, defAmbient);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, defDiffuse);
    glMaterialfv(GL_FRONT, GL_SPECULAR, defSpecular);
    glMaterialfv(GL_FRONT, GL_SHININESS, defShininess);
}

bool loadOBJ(const char *path, GunModel *model)
{
    FILE *file = fopen(path, "r");
    if (!file)
    {
        printf("Failed to open OBJ file: %s\n", path);
        return false;
    }

    float *tempVertices = (float *)malloc(10000 * 3 * sizeof(float));
    float *tempNormals = (float *)malloc(10000 * 3 * sizeof(float));
    float *tempTexCoords = (float *)malloc(10000 * 2 * sizeof(float));
    int vCount = 0, nCount = 0, tCount = 0;

    float *vertices = (float *)malloc(30000 * sizeof(float));
    float *normals = (float *)malloc(30000 * sizeof(float));
    float *texCoords = (float *)malloc(20000 * sizeof(float));
    int faceCount = 0;

    char line[256];
    while (fgets(line, sizeof(line), file))
    {
        if (strncmp(line, "v ", 2) == 0)
        {
            sscanf(line, "v %f %f %f",
                   &tempVertices[vCount * 3],
                   &tempVertices[vCount * 3 + 1],
                   &tempVertices[vCount * 3 + 2]);
            vCount++;
        }
        else if (strncmp(line, "vn ", 3) == 0)
        {
            sscanf(line, "vn %f %f %f",
                   &tempNormals[nCount * 3],
                   &tempNormals[nCount * 3 + 1],
                   &tempNormals[nCount * 3 + 2]);
            nCount++;
        }
        else if (strncmp(line, "vt ", 3) == 0)
        {
            sscanf(line, "vt %f %f",
                   &tempTexCoords[tCount * 2],
                   &tempTexCoords[tCount * 2 + 1]);
            tCount++;
        }
        else if (strncmp(line, "f ", 2) == 0)
        {
            int v1, v2, v3, t1, t2, t3, n1, n2, n3;
            int matches = sscanf(line, "f %d/%d/%d %d/%d/%d %d/%d/%d",
                                 &v1, &t1, &n1, &v2, &t2, &n2, &v3, &t3, &n3);

            if (matches == 9)
            {
                vertices[faceCount * 9] = tempVertices[(v1 - 1) * 3];
                vertices[faceCount * 9 + 1] = tempVertices[(v1 - 1) * 3 + 1];
                vertices[faceCount * 9 + 2] = tempVertices[(v1 - 1) * 3 + 2];
                normals[faceCount * 9] = tempNormals[(n1 - 1) * 3];
                normals[faceCount * 9 + 1] = tempNormals[(n1 - 1) * 3 + 1];
                normals[faceCount * 9 + 2] = tempNormals[(n1 - 1) * 3 + 2];
                texCoords[faceCount * 6] = tempTexCoords[(t1 - 1) * 2];
                texCoords[faceCount * 6 + 1] = tempTexCoords[(t1 - 1) * 2 + 1];

                vertices[faceCount * 9 + 3] = tempVertices[(v2 - 1) * 3];
                vertices[faceCount * 9 + 4] = tempVertices[(v2 - 1) * 3 + 1];
                vertices[faceCount * 9 + 5] = tempVertices[(v2 - 1) * 3 + 2];
                normals[faceCount * 9 + 3] = tempNormals[(n2 - 1) * 3];
                normals[faceCount * 9 + 4] = tempNormals[(n2 - 1) * 3 + 1];
                normals[faceCount * 9 + 5] = tempNormals[(n2 - 1) * 3 + 2];
                texCoords[faceCount * 6 + 2] = tempTexCoords[(t2 - 1) * 2];
                texCoords[faceCount * 6 + 3] = tempTexCoords[(t2 - 1) * 2 + 1];

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
        }
    }

    fclose(file);
    free(tempVertices);
    free(tempNormals);
    free(tempTexCoords);

    model->vertices = vertices;
    model->normals = normals;
    model->texCoords = texCoords;
    model->vertexCount = faceCount * 3;

    printf("Loaded gun model: %d vertices\n", model->vertexCount);
    return true;
}

float getStairHeightAt(float x, float z)
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

        float stairX = b->x + b->w - 1.0f;
        float stairZStart = b->z - b->d + 0.8f;
        float stairDepth = 0.35f;
        float stairHeight = 0.25f;
        int stepsPerFloor = (int)(b->storyHeight / stairHeight) + 2;

        if (fabsf(x - stairX) > 1.0f)
            continue;

        float dz = z - stairZStart;
        if (dz < 0 || dz > stepsPerFloor * stairDepth * (b->stories - 1))
            continue;

        int floor = (int)(dz / (stepsPerFloor * stairDepth));
        float localZ = fmodf(dz, stepsPerFloor * stairDepth);

        float stepIndex = localZ / stairDepth;
        float y = floor * b->storyHeight + stepIndex * stairHeight;

        return y + 1.5f;
    }

    return 2.5f;
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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    stbi_image_free(data);
    return tex;
}

float getTerrainHeight(float x, float z)
{
    return 0.0f; // Flat terrain for easier building navigation
}

void initBuildings()
{
    buildings[0] = (Building){20.0f, 20.0f, 6.0f, 6.0f, 3, 4.0f, 0, 0, false, 0.0f};
    buildings[1] = (Building){-25.0f, 15.0f, 5.0f, 7.0f, 2, 4.0f, 0, 0, false, 0.0f};
    buildings[2] = (Building){30.0f, -10.0f, 7.0f, 5.0f, 4, 4.0f, 0, 0, false, 0.0f};
    buildings[3] = (Building){-20.0f, -20.0f, 6.0f, 6.0f, 2, 4.0f, 0, 0, false, 0.0f};
    buildings[4] = (Building){15.0f, -30.0f, 5.0f, 5.0f, 3, 4.0f, 0, 0, false, 0.0f};
    buildings[5] = (Building){-30.0f, -5.0f, 8.0f, 4.0f, 5, 4.0f, 0, 0, false, 0.0f};
}

void initTargets()
{
    int targetIdx = 0;

    // Place targets inside buildings on different floors
    for (int b = 0; b < MAX_BUILDINGS && targetIdx < MAX_OBJECTS - 5; b++)
    {
        Building *building = &buildings[b];
        for (int floor = 0; floor < building->stories && targetIdx < MAX_OBJECTS - 5; floor++)
        {
            float tx = building->x + (rand() % 3 - 1) * 2.0f;
            float ty = floor * building->storyHeight + 2.0f;
            float tz = building->z + (rand() % 3 - 1) * 2.0f;

            targets[targetIdx] = (Target){
                tx, ty, tz, 0, true, rand() % 3, -1.0,
                0.8f + (rand() % 4) / 10.0f, b, floor};
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
            tx, ty, tz, 0, true, rand() % 3, -1.0,
            0.7f + (rand() % 5) / 10.0f, -1, 0};
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
                printf("Hit! Target %d - Hits: %d/3 (Floor %d)\n", j, targets[j].hits, targets[j].floor);
                if (targets[j].hits >= 3)
                {
                    targets[j].alive = false;
                    targets[j].explodeTime = 0.0f;
                    targetsDestroyed++;
                    printf("Target %d destroyed! Total: %d/%d\n", j, targetsDestroyed, MAX_OBJECTS);
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

    gunRecoil = 1.0f;
    muzzleFlashTime = 0.1f;

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

void quad(GLuint tex, float x1, float y1, float z1, float x2, float y2, float z2,
          float x3, float y3, float z3, float x4, float y4, float z4,
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
    float step = 5.0f;
    float start = -gridSize;

    glColor3f(0.4f, 0.6f, 0.3f);
    glBegin(GL_QUADS);
    glNormal3f(0, 1, 0);
    glTexCoord2f(0, 0);
    glVertex3f(start, 0, start);
    glTexCoord2f(20, 0);
    glVertex3f(gridSize, 0, start);
    glTexCoord2f(20, 20);
    glVertex3f(gridSize, 0, gridSize);
    glTexCoord2f(0, 20);
    glVertex3f(start, 0, gridSize);
    glEnd();

    glColor3f(1.0f, 1.0f, 1.0f);
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
    else if (orient == 2) // Left (-x)
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
    else if (orient == 3) // Right (+x)
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

    // Hinge on the right side, door opens to the right
    glTranslatef(0.9f, 0, 0);  // Move to hinge point
    glRotatef(angle, 0, 1, 0); // Rotate around hinge
    glTranslatef(-0.9f, 0, 0); // Move back

    // Door panel (dark brown wood)
    glColor3f(0.25f, 0.15f, 0.1f);
    glPushMatrix();
    glScalef(1.8f, 2.8f, 0.15f);
    glutSolidCube(1.0);
    glPopMatrix();

    // Door handle (gold)
    glColor3f(0.8f, 0.7f, 0.3f);
    glPushMatrix();
    glTranslatef(-0.6f, 0, 0.15f);
    glScalef(0.2f, 0.08f, 0.08f);
    glutSolidCube(1.0);
    glPopMatrix();

    // Door panel details
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

void drawStairs(Building *b)
{
    // Stair geometry (local units)
    float stepHeight = 0.25f;
    float stepDepth = 0.35f;
    float stepWidth = 1.5f;

    // Local start position: near the back-right corner INSIDE the building
    float stairX = b->w - 1.0f;   // right interior side
    float stairZ0 = -b->d + 1.0f; // slightly inside back wall

    // Draw stairs for each story
    for (int floor = 0; floor < b->stories; floor++)
    {
        float baseY = floor * b->storyHeight;
        int numSteps = (int)(b->storyHeight / stepHeight);

        for (int s = 0; s < numSteps; s++)
        {
            float y = baseY + s * stepHeight;
            float z = stairZ0 + s * stepDepth;

            // keep inside the building volume
            if (z > b->d - 1.0f)
                break;

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
                st->minY = y;
                st->maxY = y + stepHeight;
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
    float baseY = 0.0f;
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
        quad(wallTex, -b->w, floorY, -b->d, b->w, floorY, -b->d,
             b->w, nextFloorY, -b->d, -b->w, nextFloorY, -b->d, 0, 0, 1, 2);

        // Windows on back wall
        glDisable(GL_TEXTURE_2D);
        drawWindow(-b->w / 2, floorY + b->storyHeight / 2, -b->d - 0.05f, 1.5f, 1.8f, 1);
        drawWindow(b->w / 2, floorY + b->storyHeight / 2, -b->d - 0.05f, 1.5f, 1.8f, 1);
        glEnable(GL_TEXTURE_2D);

        // Left wall (-x)
        glBindTexture(GL_TEXTURE_2D, wallTex);
        quad(wallTex, -b->w, floorY, -b->d, -b->w, floorY, b->d,
             -b->w, nextFloorY, b->d, -b->w, nextFloorY, -b->d, 1, 0, 0, 2);

        glDisable(GL_TEXTURE_2D);
        drawWindow(-b->w - 0.05f, floorY + b->storyHeight / 2, 0, 1.5f, 1.8f, 2);
        glEnable(GL_TEXTURE_2D);

        // Right wall (+x)
        glBindTexture(GL_TEXTURE_2D, wallTex);
        quad(wallTex, b->w, floorY, -b->d, b->w, floorY, b->d,
             b->w, nextFloorY, b->d, b->w, nextFloorY, -b->d, -1, 0, 0, 2);

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
                 -doorWidth / 2, nextFloorY, b->d, -b->w, nextFloorY, b->d, 0, 0, -1, 2);

            // Right part of front wall
            quad(wallTex, doorWidth / 2, floorY, b->d, b->w, floorY, b->d,
                 b->w, nextFloorY, b->d, doorWidth / 2, nextFloorY, b->d, 0, 0, -1, 2);

            // Top part above door
            float doorHeight = 2.8f;
            quad(wallTex, -doorWidth / 2, doorHeight, b->d, doorWidth / 2, doorHeight, b->d,
                 doorWidth / 2, nextFloorY, b->d, -doorWidth / 2, nextFloorY, b->d, 0, 0, -1, 1);

            // Draw the door
            glDisable(GL_TEXTURE_2D);
            drawDoor(0, doorHeight / 2, b->d + 0.08f, b->doorAngle);
            glEnable(GL_TEXTURE_2D);
        }
        else
        {
            // Upper floors - full wall with windows
            quad(wallTex, -b->w, floorY, b->d, b->w, floorY, b->d,
                 b->w, nextFloorY, b->d, -b->w, nextFloorY, b->d, 0, 0, -1, 2);

            glDisable(GL_TEXTURE_2D);
            drawWindow(-b->w / 2, floorY + b->storyHeight / 2, b->d + 0.05f, 1.5f, 1.8f, 0);
            drawWindow(b->w / 2, floorY + b->storyHeight / 2, b->d + 0.05f, 1.5f, 1.8f, 0);
            glEnable(GL_TEXTURE_2D);
        }

        // Floor ceiling
        glBindTexture(GL_TEXTURE_2D, texConcrete);
        quad(texConcrete, -b->w, nextFloorY, -b->d, b->w, nextFloorY, -b->d,
             b->w, nextFloorY, b->d, -b->w, nextFloorY, b->d, 0, 1, 0, 2);

        // Floor
        if (floor == 0 || floor < b->stories)
        {
            quad(texConcrete, -b->w, floorY + 0.05f, -b->d, b->w, floorY + 0.05f, -b->d,
                 b->w, floorY + 0.05f, b->d, -b->w, floorY + 0.05f, b->d, 0, 1, 0, 2);
        }
    }

    // Roof
    glBindTexture(GL_TEXTURE_2D, texRoof);
    quad(texRoof, -b->w, totalHeight, -b->d, b->w, totalHeight, -b->d,
         b->w, totalHeight, b->d, -b->w, totalHeight, b->d, 0, 1, 0, 2);

    // Draw stairs inside the building
    drawStairs(b);

    glPopMatrix();
}

void drawTarget(Target *t)
{
    glEnable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glPushMatrix();
    glTranslatef(t->x, t->y, t->z);
    glScalef(t->scale, t->scale, t->scale);

    if (t->hits == 0)
        glColor3f(1.0f, 0.3f, 0.3f);
    else if (t->hits == 1)
        glColor3f(1.0f, 0.6f, 0.1f);
    else
        glColor3f(1.0f, 0.9f, 0.2f);

    if (t->type == 0)
    {
        glutSolidCube(1.5);
    }
    else if (t->type == 1)
    {
        glutSolidSphere(0.8, 16, 16);
    }
    else
    {
        glutSolidTorus(0.3, 0.6, 12, 16);
    }

    glPopMatrix();
    glEnable(GL_TEXTURE_2D);
    glColor3f(1.0f, 1.0f, 1.0f);
}

void lookAt(double ex, double ey, double ez,
            double cx, double cy, double cz,
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
        lookAt(fpvX, fpvY, fpvZ, fpvX + dirX, fpvY + dirY, fpvZ + dirZ, 0.0, 1.0, 0.0);
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
            drawExplosion(targets[i].x, targets[i].y, targets[i].z, targets[i].explodeTime);
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
        drawGun();
        drawCrosshair();
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
            const char *msg = buildings[nearDoor].doorOpen ? "Press E to close door" : "Press E to open door";
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
        const char *modeName = (mode_view == 0) ? "Orthographic" : (mode_view == 1) ? "Perspective Orbit"
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
            printf("Door %d %s\n", nearDoor, buildings[nearDoor].doorOpen ? "opening..." : "closing...");
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
            fpvX += forwardX * fpvSpeed;
            fpvZ += forwardZ * fpvSpeed;

            float targetY = getStairHeightAt(fpvX, fpvZ);
            fpvY += (targetY - fpvY) * 0.3f;
        }
        else if (ch == 's')
        {
            fpvX -= forwardX * fpvSpeed;
            fpvZ -= forwardZ * fpvSpeed;

            float targetY = getStairHeightAt(fpvX, fpvZ);
            fpvY += (targetY - fpvY) * 0.3f;
        }
        else if (ch == 'a')
        {
            fpvX -= rightX * fpvSpeed;
            fpvZ -= rightZ * fpvSpeed;
        }
        else if (ch == 'd')
        {
            fpvX += rightX * fpvSpeed;
            fpvZ += rightZ * fpvSpeed;
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

    texGround = loadTexture("floor.png");
    texBuilding = loadTexture("wall.png");
    texRoof = loadTexture("ceiling.png");
    texConcrete = loadTexture("concrete.png");
    texMetal = loadTexture("metal.png");
    texWood = loadTexture("wood.png");
    texTarget = loadTexture("metal.png");

    texGunDiffuse = loadTexture("gun_diffuse.png");
    if (!texGunDiffuse)
        texGunDiffuse = texMetal;

    texGunSpecular = loadTexture("gun_specular.png");
    if (!texGunSpecular)
        texGunSpecular = texMetal;

    gunModel.vertexCount = 0;
    if (!loadOBJ("Glock18.obj", &gunModel))
    {
        printf("Using procedural gun model (gun.obj not found)\n");
    }

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
    printf("\n🎮 FPV Mode Controls:\n");
    printf("W/A/S/D - Move horizontally\n");
    printf("Q/Z - Move down/up (use to go between floors)\n");
    printf("Mouse - Look around (when captured)\n");
    printf("Left Click / SPACE - Shoot\n");
    printf("Right Click - Toggle mouse capture\n");
    printf("E - Open/Close door (when near entrance)\n");
    printf("C - Center view\n");
    printf("R - Reset targets and close all doors\n");
    printf("\n💡 Light Controls:\n");
    printf("I/K - Move light up/down\n");
    printf("J/U - Rotate light\n");
    printf("\n🎯 Objective: Destroy all %d targets!\n", MAX_OBJECTS);
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