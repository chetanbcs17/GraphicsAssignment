// fpv_game.c
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

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define DEG2RAD 0.0174533
#define MAX_BULLETS 200
#define MAX_OBJECTS 10

// Camera rotation (orbit)
int th = 45, ph = 20;
float camX = 0, camY = 2, camZ = 15;

int mode_view = 2; // 0 = orbit view, 1 = Perspective orbit, 2 = FPV

// FPV camera
float fpvX = 0.0f, fpvY = 1.5f, fpvZ = 0.0f;
float fpvYaw = 180.0f;
float fpvPitch = 0.0f;
float fpvSpeed = 0.35f;
float fpvSens = 0.15f; // used for mouse delta scaling

float lightAngle = 90.0;
float lightY = 8.0;
float lightDist = 20.0;

int mode_proj = 0;
float asp = 1;
float dim = 40; // bigger default dimension
int fov = 70;

int windowWidth = 800, windowHeight = 600;

// Texture IDs
GLuint texWall, texFloor, texCeil, texWood, texMetal, texShade, texPrism;

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
    int type; // 0 = prism, 1 = hexagon, 2 = octahedron
    float explodeTime;
} Target;

Bullet bullets[MAX_BULLETS];
Target targets[MAX_OBJECTS];
int bulletCount = 0;

// mouse capture
bool mouseCaptured = false;

// --- Crosshair ---
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

    glColor3f(1.0f, 0.0f, 0.0f);
    glLineWidth(2.0f);

    glBegin(GL_LINES);
    glVertex2f(-0.05f, 0.0f);
    glVertex2f(0.05f, 0.0f);
    glVertex2f(0.0f, -0.05f);
    glVertex2f(0.0f, 0.05f);
    glEnd();

    glLineWidth(1.0f);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);

    glEnable(GL_DEPTH_TEST);
}

// --- Gun ---
void drawGun()
{
    glDisable(GL_LIGHTING);
    glPushMatrix();
    glTranslatef(0.35f, -0.25f, -0.6f);
    glRotatef(-8.0f, 0, 1, 0);

    // Barrel
    glColor3f(0.12f, 0.12f, 0.12f);
    glPushMatrix();
    glTranslatef(0.0f, 0.0f, -0.05f);
    glRotatef(90, 0, 1, 0);
    GLUquadric *quad = gluNewQuadric();
    gluCylinder(quad, 0.02, 0.015, 0.35, 16, 1);
    gluDeleteQuadric(quad);
    glPopMatrix();

    // Body
    glColor3f(0.18f, 0.18f, 0.18f);
    glPushMatrix();
    glScalef(0.26f, 0.08f, 0.12f);
    glutSolidCube(1.0);
    glPopMatrix();

    // Handle
    glColor3f(0.2f, 0.1f, 0.05f);
    glPushMatrix();
    glTranslatef(-0.05f, -0.08f, -0.05f);
    glRotatef(-15, 1, 0, 0);
    glScalef(0.06f, 0.12f, 0.03f);
    glutSolidCube(1.0);
    glPopMatrix();

    // Iron sight
    glColor3f(0, 0, 0);
    glPushMatrix();
    glTranslatef(0.05f, 0.03f, -0.25f);
    glScalef(0.02f, 0.02f, 0.08f);
    glutSolidCube(1.0);
    glPopMatrix();

    glPopMatrix();
    glEnable(GL_LIGHTING);
}

// --- Textures ---
GLuint loadTexture(const char *file)
{
    int w, h, n;
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

// --- Target initialization ---
void initTargets()
{
    targets[0] = (Target){6.0f, 0.75f, 6.0f, 0, true, 0, -1.0};
    targets[1] = (Target){3.0f, 1.0f, 12.0f, 0, true, 1, -1.0};
    targets[2] = (Target){-4.0f, 1.2f, 10.0f, 0, true, 2, -1.0};
    targets[3] = (Target){-10.0f, 1.5f, -6.0f, 0, true, 0, -1.0};
    targets[4] = (Target){10.0f, 1.5f, -2.0f, 0, true, 1, -1.0};
    for (int i = 5; i < MAX_OBJECTS; i++)
    {
        targets[i] = (Target){(float)(rand() % 30 - 15), 0.8f + (rand() % 20) / 20.0f, (float)(rand() % 50 - 10), 0, true, rand() % 3, -1.0};
    }
}

// --- Bullet handling ---
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
    float speed = 1.0f;

    bullets[idx].x = fpvX + dirX * 0.6f;
    bullets[idx].y = fpvY + dirY * 0.6f;
    bullets[idx].z = fpvZ + dirZ * 0.6f;
    bullets[idx].dx = dirX * speed;
    bullets[idx].dy = dirY * speed;
    bullets[idx].dz = dirZ * speed;
    bullets[idx].active = true;
    bullets[idx].life = 6.0f;

    bulletCount++;
    printf("Bullet fired! Total fired: %d\n", bulletCount);
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

        if (bullets[i].life <= 0 || fabsf(bullets[i].x) > 80 || fabsf(bullets[i].z) > 80 || bullets[i].y < -5 || bullets[i].y > 50)
            bullets[i].active = false;
    }
}

// Collision
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
            if (dist < 0.9f)
            {
                bullets[i].active = false;
                targets[j].hits++;
                printf("Hit! Target %d - Hits: %d/5\n", j, targets[j].hits);
                if (targets[j].hits >= 5)
                {
                    targets[j].alive = false;
                    targets[j].explodeTime = 0.0f;
                    printf("Target %d destroyed!\n", j);
                }
                break;
            }
        }
    }
}

// Draw bullets
void drawBullets()
{
    glDisable(GL_LIGHTING);
    glColor3f(1, 1, 0.2);
    for (int i = 0; i < MAX_BULLETS; i++)
    {
        if (!bullets[i].active)
            continue;
        glPushMatrix();
        glTranslatef(bullets[i].x, bullets[i].y, bullets[i].z);
        glutSolidSphere(0.05, 8, 8);
        glPopMatrix();
    }
    glEnable(GL_LIGHTING);
}

// Draw explosion
void drawExplosion(float x, float y, float z, float time)
{
    glDisable(GL_LIGHTING);
    int particles = 28;
    for (int i = 0; i < particles; i++)
    {
        float angle = (2.0f * M_PI * i) / particles;
        float radius = time * 2.0f;
        float px = x + cosf(angle) * radius;
        float py = y + sinf(angle * 0.7f) * radius;
        float pz = z + sinf(angle) * radius;
        float fade = 1.0f - (time / 1.5f);
        glColor4f(1.0f, 0.45f, 0.0f, fade);
        glPushMatrix();
        glTranslatef(px, py, pz);
        glutSolidSphere(0.12, 6, 6);
        glPopMatrix();
    }
    glEnable(GL_LIGHTING);
}

// --- Custom lookAt ---
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
    double s[3] = {f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0]};
    double lens = sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    if (lens < 1e-9)
        lens = 1.0;
    for (int i = 0; i < 3; i++)
        s[i] /= lens;

    double u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};
    double M[16] = {s[0], u[0], -f[0], 0, s[1], u[1], -f[1], 0, s[2], u[2], -f[2], 0, 0, 0, 0, 1};
    glMultMatrixd(M);
    glTranslated(-ex, -ey, -ez);
}

// --- FPV / Orbit camera ---
void addView()
{
    if (mode_view == 0 || mode_view == 1)
    {
        float radTh = th * DEG2RAD;
        float radPh = ph * DEG2RAD;
        float radius = 28.0f;
        float ex = radius * cosf(radPh) * sinf(radTh);
        float ey = radius * sinf(radPh) + 1.5f;
        float ez = radius * cosf(radPh) * cosf(radTh);
        lookAt(ex, ey, ez, 0.0, 1.5, 0.0, 0.0, 1.0, 0.0);
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

// --- Idle ---
void idle()
{
    lightAngle += 0.2;
    if (lightAngle > 360)
        lightAngle -= 360;

    updateBullets();
    checkCollisions();

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