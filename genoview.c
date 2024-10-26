#include "raylib.h"
#include "raymath.h"
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#include "rlgl.h"

#include <assert.h>

//----------------------------------------------------------------------------------
// Camera
//----------------------------------------------------------------------------------

// Basic Orbit Camera with simple controls
typedef struct {

    Camera3D cam3d;
    float azimuth;
    float altitude;
    float distance;
    Vector3 offset;

} OrbitCamera;

static inline void OrbitCameraInit(OrbitCamera* camera)
{
    memset(&camera->cam3d, 0, sizeof(Camera3D));
    camera->cam3d.position = (Vector3){ 2.0f, 3.0f, 5.0f };
    camera->cam3d.target = (Vector3){ -0.5f, 1.0f, 0.0f };
    camera->cam3d.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera->cam3d.fovy = 45.0f;
    camera->cam3d.projection = CAMERA_PERSPECTIVE;

    camera->azimuth = 0.0f;
    camera->altitude = 0.4f;
    camera->distance = 4.0f;
    camera->offset = Vector3Zero();
}

static inline void OrbitCameraUpdate(
    OrbitCamera* camera,
    Vector3 target,
    float azimuthDelta,
    float altitudeDelta,
    float offsetDeltaX,
    float offsetDeltaY,
    float mouseWheel,
    float dt)
{
    camera->azimuth = camera->azimuth + 1.0f * dt * -azimuthDelta;
    camera->altitude = Clamp(camera->altitude + 1.0f * dt * altitudeDelta, 0.0, 0.4f * PI);
    camera->distance = Clamp(camera->distance +  20.0f * dt * -mouseWheel, 0.1f, 100.0f);
    
    Quaternion rotationAzimuth = QuaternionFromAxisAngle((Vector3){0, 1, 0}, camera->azimuth);
    Vector3 position = Vector3RotateByQuaternion((Vector3){0, 0, camera->distance}, rotationAzimuth);
    Vector3 axis = Vector3Normalize(Vector3CrossProduct(position, (Vector3){0, 1, 0}));

    Quaternion rotationAltitude = QuaternionFromAxisAngle(axis, camera->altitude);

    Vector3 localOffset = (Vector3){ dt * offsetDeltaX, dt * -offsetDeltaY, 0.0f };
    localOffset = Vector3RotateByQuaternion(localOffset, rotationAzimuth);

    camera->offset = Vector3Add(camera->offset, Vector3RotateByQuaternion(localOffset, rotationAltitude));

    Vector3 cameraTarget = Vector3Add(camera->offset, target);
    Vector3 eye = Vector3Add(cameraTarget, Vector3RotateByQuaternion(position, rotationAltitude));

    camera->cam3d.target = cameraTarget;
    camera->cam3d.position = eye;
}

//----------------------------------------------------------------------------------
// Shadow Maps
//----------------------------------------------------------------------------------

typedef struct
{
    Vector3 target;
    Vector3 position;
    Vector3 up;
    double width;
    double height;
    double near;
    double far;
    
} ShadowLight;

RenderTexture2D LoadShadowMap(int width, int height)
{
    RenderTexture2D target = { 0 };
    target.id = rlLoadFramebuffer();
    target.texture.width = width;
    target.texture.height = height;
    assert(target.id);
    
    rlEnableFramebuffer(target.id);

    target.depth.id = rlLoadTextureDepth(width, height, false);
    target.depth.width = width;
    target.depth.height = height;
    target.depth.format = 19;       //DEPTH_COMPONENT_24BIT?
    target.depth.mipmaps = 1;
    rlFramebufferAttach(target.id, target.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    assert(rlFramebufferComplete(target.id));

    rlDisableFramebuffer();

    return target;
}

void UnloadShadowMap(RenderTexture2D target)
{
    if (target.id > 0)
    {
        rlUnloadFramebuffer(target.id);
    }
}

void BeginShadowMap(RenderTexture2D target, ShadowLight shadowLight)
{
    BeginTextureMode(target);
    ClearBackground(WHITE);
    
    rlDrawRenderBatchActive();      // Update and draw internal render batch

    rlMatrixMode(RL_PROJECTION);    // Switch to projection matrix
    rlPushMatrix();                 // Save previous matrix, which contains the settings for the 2d ortho projection
    rlLoadIdentity();               // Reset current matrix (projection)

    rlOrtho(
        -shadowLight.width/2, shadowLight.width/2, 
        -shadowLight.height/2, shadowLight.height/2, 
        shadowLight.near, shadowLight.far);

    rlMatrixMode(RL_MODELVIEW);     // Switch back to modelview matrix
    rlLoadIdentity();               // Reset current matrix (modelview)

    // Setup Camera view
    Matrix matView = MatrixLookAt(shadowLight.position, shadowLight.target, shadowLight.up);
    rlMultMatrixf(MatrixToFloat(matView));      // Multiply modelview matrix by view matrix (camera)

    rlEnableDepthTest();            // Enable DEPTH_TEST for 3D    
}

void EndShadowMap()
{
    rlDrawRenderBatchActive();      // Update and draw internal render batch

    rlMatrixMode(RL_PROJECTION);    // Switch to projection matrix
    rlPopMatrix();                  // Restore previous matrix (projection) from matrix stack

    rlMatrixMode(RL_MODELVIEW);     // Switch back to modelview matrix
    rlLoadIdentity();               // Reset current matrix (modelview)

    rlDisableDepthTest();           // Disable DEPTH_TEST for 2D

    EndTextureMode();
}

void SetShaderValueShadowMap(Shader shader, int locIndex, RenderTexture2D target)
{
    if (locIndex > -1)
    {
        rlEnableShader(shader.id);
        int slot = 10; // Can be anything 0 to 15, but 0 will probably be taken up
        rlActiveTextureSlot(slot);
        rlEnableTexture(target.depth.id);
        rlSetUniform(locIndex, &slot, SHADER_UNIFORM_INT, 1);
    }
}

//----------------------------------------------------------------------------------
// GBuffer
//----------------------------------------------------------------------------------

typedef struct
{
    unsigned int id;        // OpenGL framebuffer object id
    Texture color;          // Color buffer attachment texture
    Texture normal;         // Normal buffer attachment texture
    Texture depth;          // Depth buffer attachment texture
    
} GBuffer;

GBuffer LoadGBuffer(int width, int height)
{
    GBuffer target = { 0 };
    target.id = rlLoadFramebuffer();
    assert(target.id);
    
    rlEnableFramebuffer(target.id);

    target.color.id = rlLoadTexture(NULL, width, height, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
    target.color.width = width;
    target.color.height = height;
    target.color.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    target.color.mipmaps = 1;
    rlFramebufferAttach(target.id, target.color.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    
    target.normal.id = rlLoadTexture(NULL, width, height, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 1);
    target.normal.width = width;
    target.normal.height = height;
    target.normal.format = PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
    target.normal.mipmaps = 1;
    rlFramebufferAttach(target.id, target.normal.id, RL_ATTACHMENT_COLOR_CHANNEL1, RL_ATTACHMENT_TEXTURE2D, 0);
    
    target.depth.id = rlLoadTextureDepth(width, height, false);
    target.depth.width = width;
    target.depth.height = height;
    target.depth.format = 19;       //DEPTH_COMPONENT_24BIT?
    target.depth.mipmaps = 1;
    rlFramebufferAttach(target.id, target.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);

    assert(rlFramebufferComplete(target.id));

    rlDisableFramebuffer();

    return target;
}

void UnloadGBuffer(GBuffer target)
{
    if (target.id > 0)
    {
        rlUnloadFramebuffer(target.id);
    }
}

void BeginGBuffer(GBuffer target, Camera3D camera)
{
    rlDrawRenderBatchActive();      // Update and draw internal render batch

    rlEnableFramebuffer(target.id); // Enable render target
    rlActiveDrawBuffers(2);

    // Set viewport and RLGL internal framebuffer size
    rlViewport(0, 0, target.color.width, target.color.height);
    rlSetFramebufferWidth(target.color.width);
    rlSetFramebufferHeight(target.color.height);

    ClearBackground(BLACK);

    rlMatrixMode(RL_PROJECTION);    // Switch to projection matrix
    rlPushMatrix();                 // Save previous matrix, which contains the settings for the 2d ortho projection
    rlLoadIdentity();               // Reset current matrix (projection)

    float aspect = (float)target.color.width/(float)target.color.height;

    // NOTE: zNear and zFar values are important when computing depth buffer values
    if (camera.projection == CAMERA_PERSPECTIVE)
    {
        // Setup perspective projection
        double top = rlGetCullDistanceNear()*tan(camera.fovy*0.5*DEG2RAD);
        double right = top*aspect;

        rlFrustum(-right, right, -top, top, rlGetCullDistanceNear(), rlGetCullDistanceFar());
    }
    else if (camera.projection == CAMERA_ORTHOGRAPHIC)
    {
        // Setup orthographic projection
        double top = camera.fovy/2.0;
        double right = top*aspect;

        rlOrtho(-right, right, -top,top, rlGetCullDistanceNear(), rlGetCullDistanceFar());
    }

    rlMatrixMode(RL_MODELVIEW);     // Switch back to modelview matrix
    rlLoadIdentity();               // Reset current matrix (modelview)

    // Setup Camera view
    Matrix matView = MatrixLookAt(camera.position, camera.target, camera.up);
    rlMultMatrixf(MatrixToFloat(matView));      // Multiply modelview matrix by view matrix (camera)

    rlEnableDepthTest();            // Enable DEPTH_TEST for 3D
}

void EndGBuffer(int windowWidth, int windowHeight)
{
    rlDrawRenderBatchActive();      // Update and draw internal render batch
    
    rlDisableDepthTest();           // Disable DEPTH_TEST for 2D
    rlActiveDrawBuffers(1);
    rlDisableFramebuffer();         // Disable render target (fbo)

    rlMatrixMode(RL_PROJECTION);        // Switch to projection matrix
    rlPopMatrix();                  // Restore previous matrix (projection) from matrix stack
    rlLoadIdentity();                   // Reset current matrix (projection)
    rlOrtho(0, windowWidth, windowHeight, 0, 0.0f, 1.0f);

    rlMatrixMode(RL_MODELVIEW);         // Switch back to modelview matrix
    rlLoadIdentity();                   // Reset current matrix (modelview)
}

//----------------------------------------------------------------------------------
// Geno Character and Animation
//----------------------------------------------------------------------------------

Model LoadGenoModel(const char* fileName)
{
    Model model = { 0 };
    model.transform = MatrixIdentity();
  
    FILE* f = fopen(fileName, "rb");
    if (f == NULL)
    {
        TRACELOG(LOG_ERROR, "MODEL Unable to read skinned model file %s", fileName);
        return model;
    }
    
    model.materialCount = 1;
    model.materials = RL_CALLOC(model.materialCount, sizeof(Mesh));
    model.materials[0] = LoadMaterialDefault();

    model.meshCount = 1;
    model.meshes = RL_CALLOC(model.meshCount, sizeof(Mesh));
    model.meshMaterial = RL_CALLOC(model.meshCount, sizeof(Mesh));
    model.meshMaterial[0] = 0;

    fread(&model.meshes[0].vertexCount, sizeof(int), 1, f);
    fread(&model.meshes[0].triangleCount, sizeof(int), 1, f);
    fread(&model.boneCount, sizeof(int), 1, f);

    model.meshes[0].boneCount = model.boneCount;
    model.meshes[0].vertices = RL_CALLOC(model.meshes[0].vertexCount * 3, sizeof(float));
    model.meshes[0].texcoords = RL_CALLOC(model.meshes[0].vertexCount * 2, sizeof(float));
    model.meshes[0].normals = RL_CALLOC(model.meshes[0].vertexCount * 3, sizeof(float));
    model.meshes[0].boneIds = RL_CALLOC(model.meshes[0].vertexCount * 4, sizeof(unsigned char));
    model.meshes[0].boneWeights = RL_CALLOC(model.meshes[0].vertexCount * 4, sizeof(float));
    model.meshes[0].indices = RL_CALLOC(model.meshes[0].triangleCount * 3, sizeof(unsigned short));
    model.meshes[0].animVertices = RL_CALLOC(model.meshes[0].vertexCount * 3, sizeof(float));
    model.meshes[0].animNormals = RL_CALLOC(model.meshes[0].vertexCount * 3, sizeof(float));
    model.bones =  RL_CALLOC(model.boneCount, sizeof(BoneInfo));
    model.bindPose =  RL_CALLOC(model.boneCount, sizeof(Transform));
    
    fread(model.meshes[0].vertices, sizeof(float), model.meshes[0].vertexCount * 3, f);
    fread(model.meshes[0].texcoords, sizeof(float), model.meshes[0].vertexCount * 2, f);
    fread(model.meshes[0].normals, sizeof(float), model.meshes[0].vertexCount * 3, f);
    fread(model.meshes[0].boneIds, sizeof(unsigned char), model.meshes[0].vertexCount * 4, f);
    fread(model.meshes[0].boneWeights, sizeof(float), model.meshes[0].vertexCount * 4, f);
    fread(model.meshes[0].indices, sizeof(unsigned short), model.meshes[0].triangleCount * 3, f);
    memcpy(model.meshes[0].animVertices, model.meshes[0].vertices, sizeof(float) * model.meshes[0].vertexCount * 3);
    memcpy(model.meshes[0].animNormals, model.meshes[0].normals, sizeof(float) * model.meshes[0].vertexCount * 3);
    fread(model.bones, sizeof(BoneInfo), model.boneCount, f);
    fread(model.bindPose, sizeof(Transform), model.boneCount, f);
    fclose(f);
    
    model.meshes[0].boneMatrices = RL_CALLOC(model.boneCount, sizeof(Matrix));
    for (int i = 0; i < model.boneCount; i++)
    {
        model.meshes[0].boneMatrices[i] = MatrixIdentity();
    }
    
    UploadMesh(&model.meshes[0], true);
    
    return model;
}

ModelAnimation LoadGenoModelAnimation(const char* fileName)
{
    ModelAnimation animation = { 0 };
    
    FILE* f = fopen(fileName, "rb");
    if (f == NULL)
    {
        TRACELOG(LOG_ERROR, "MODEL ANIMATION Unable to read animation file %s", fileName);
        return animation;
    }
    
    fread(&animation.frameCount, sizeof(int), 1, f);
    fread(&animation.boneCount, sizeof(int), 1, f);
    
    animation.bones = RL_CALLOC(animation.boneCount, sizeof(BoneInfo));
    fread(animation.bones, sizeof(BoneInfo), animation.boneCount, f);        
    
    animation.framePoses = RL_CALLOC(animation.frameCount, sizeof(Transform*));
    for (int i = 0; i < animation.frameCount; i++)
    {
        animation.framePoses[i] = RL_CALLOC(animation.boneCount, sizeof(Transform));
        fread(animation.framePoses[i], sizeof(Transform), animation.boneCount, f);        
    }

    fclose(f);
    
    return animation;
}

ModelAnimation LoadEmptyModelAnimation(Model model)
{
    ModelAnimation animation = { 0 };
    animation.frameCount = 1;
    animation.boneCount = model.boneCount;
    
    animation.bones = RL_CALLOC(animation.boneCount, sizeof(BoneInfo));
    memcpy(animation.bones, model.bones, animation.boneCount * sizeof(BoneInfo));
    
    animation.framePoses = RL_CALLOC(animation.frameCount, sizeof(Transform*));
    for (int i = 0; i < animation.frameCount; i++)
    {
        animation.framePoses[i] = RL_CALLOC(animation.boneCount, sizeof(Transform));
        memcpy(animation.framePoses[i], model.bindPose, animation.boneCount * sizeof(Transform));
    }

    return animation;
}

//----------------------------------------------------------------------------------
// Debug Draw
//----------------------------------------------------------------------------------

static inline void DrawTransform(Transform t, float scale)
{
    Matrix rotMatrix = QuaternionToMatrix(t.rotation);
  
    DrawLine3D(
        t.translation,
        Vector3Add(t.translation, (Vector3){ scale * rotMatrix.m0, scale * rotMatrix.m1, scale * rotMatrix.m2 }),
        RED);
        
    DrawLine3D(
        t.translation,
        Vector3Add(t.translation, (Vector3){ scale * rotMatrix.m4, scale * rotMatrix.m5, scale * rotMatrix.m6 }),
        GREEN);
        
    DrawLine3D(
        t.translation,
        Vector3Add(t.translation, (Vector3){ scale * rotMatrix.m8, scale * rotMatrix.m9, scale * rotMatrix.m10 }),
        BLUE);
}

static inline void DrawModelBindPose(Model model, Color color)
{
    for (int i = 0; i < model.boneCount; i++)
    {
        DrawSphereWires(
            model.bindPose[i].translation,
            0.01f,
            4,
            6,
            color);
            
        DrawTransform(model.bindPose[i], 0.1f);

        if (model.bones[i].parent != -1)
        {
            DrawLine3D(
                model.bindPose[i].translation,
                model.bindPose[model.bones[i].parent].translation,
                color);
        }
    }
}

static inline void DrawModelAnimationFrameSkeleton(ModelAnimation animation, int frame, Color color)
{
    for (int i = 0; i < animation.boneCount; i++)
    {
        DrawSphereWires(
            animation.framePoses[frame][i].translation,
            0.01f,
            4,
            6,
            color);

        DrawTransform(animation.framePoses[frame][i], 0.1f);

        if (animation.bones[i].parent != -1)
        {
            DrawLine3D(
                animation.framePoses[frame][i].translation,
                animation.framePoses[frame][animation.bones[i].parent].translation,
                color);
        }
    }
}

//----------------------------------------------------------------------------------
// App
//----------------------------------------------------------------------------------

int main(int argc, char **argv)
{
    // Init Window
    
    const int screenWidth = 1280;
    const int screenHeight = 720;
    
    SetConfigFlags(FLAG_VSYNC_HINT);
    InitWindow(screenWidth, screenHeight, "GenoView");
    SetTargetFPS(60);

    // Shaders
    
    Shader shadowShader = LoadShader("./resources/shadow.vs", "./resources/shadow.fs");
    int shadowShaderLightClipNear = GetShaderLocation(shadowShader, "lightClipNear");
    int shadowShaderLightClipFar = GetShaderLocation(shadowShader, "lightClipFar");
    
    Shader skinnedShadowShader = LoadShader("./resources/skinnedShadow.vs", "./resources/shadow.fs");
    int skinnedShadowShaderLightClipNear = GetShaderLocation(skinnedShadowShader, "lightClipNear");
    int skinnedShadowShaderLightClipFar = GetShaderLocation(skinnedShadowShader, "lightClipFar");
    
    Shader skinnedBasicShader = LoadShader("./resources/skinnedBasic.vs", "./resources/basic.fs");
    int skinnedBasicShaderSpecularity = GetShaderLocation(skinnedBasicShader, "specularity");
    int skinnedBasicShaderGlossiness = GetShaderLocation(skinnedBasicShader, "glossiness");
    int skinnedBasicShaderCamClipNear = GetShaderLocation(skinnedBasicShader, "camClipNear");
    int skinnedBasicShaderCamClipFar = GetShaderLocation(skinnedBasicShader, "camClipFar");

    Shader basicShader = LoadShader("./resources/basic.vs", "./resources/basic.fs");
    int basicShaderSpecularity = GetShaderLocation(basicShader, "specularity");
    int basicShaderGlossiness = GetShaderLocation(basicShader, "glossiness");
    int basicShaderCamClipNear = GetShaderLocation(basicShader, "camClipNear");
    int basicShaderCamClipFar = GetShaderLocation(basicShader, "camClipFar");
    
    Shader lightingShader = LoadShader("./resources/post.vs", "./resources/lighting.fs");
    int lightingShaderGBufferColor = GetShaderLocation(lightingShader, "gbufferColor");
    int lightingShaderGBufferNormal = GetShaderLocation(lightingShader, "gbufferNormal");
    int lightingShaderGBufferDepth = GetShaderLocation(lightingShader, "gbufferDepth");
    int lightingShaderSSAO = GetShaderLocation(lightingShader, "ssao");
    int lightingShaderCamPos = GetShaderLocation(lightingShader, "camPos");
    int lightingShaderCamInvViewProj = GetShaderLocation(lightingShader, "camInvViewProj");
    int lightingShaderLightDir = GetShaderLocation(lightingShader, "lightDir");
    int lightingShaderSunColor = GetShaderLocation(lightingShader, "sunColor");
    int lightingShaderSunStrength = GetShaderLocation(lightingShader, "sunStrength");
    int lightingShaderSkyColor = GetShaderLocation(lightingShader, "skyColor");
    int lightingShaderSkyStrength = GetShaderLocation(lightingShader, "skyStrength");
    int lightingShaderGroundStrength = GetShaderLocation(lightingShader, "groundStrength");
    int lightingShaderAmbientStrength = GetShaderLocation(lightingShader, "ambientStrength");
    int lightingShaderExposure = GetShaderLocation(lightingShader, "exposure");
    int lightingShaderCamClipNear = GetShaderLocation(lightingShader, "camClipNear");
    int lightingShaderCamClipFar = GetShaderLocation(lightingShader, "camClipFar");
    
    Shader ssaoShader = LoadShader("./resources/post.vs", "./resources/ssao.fs");
    int ssaoShaderGBufferNormal = GetShaderLocation(ssaoShader, "gbufferNormal");
    int ssaoShaderGBufferDepth = GetShaderLocation(ssaoShader, "gbufferDepth");
    int ssaoShaderCamView = GetShaderLocation(ssaoShader, "camView");
    int ssaoShaderCamProj = GetShaderLocation(ssaoShader, "camProj");
    int ssaoShaderCamInvProj = GetShaderLocation(ssaoShader, "camInvProj");
    int ssaoShaderCamInvViewProj = GetShaderLocation(ssaoShader, "camInvViewProj");
    int ssaoShaderLightViewProj = GetShaderLocation(ssaoShader, "lightViewProj");
    int ssaoShaderShadowMap = GetShaderLocation(ssaoShader, "shadowMap");
    int ssaoShaderShadowInvResolution = GetShaderLocation(ssaoShader, "shadowInvResolution");
    int ssaoShaderCamClipNear = GetShaderLocation(ssaoShader, "camClipNear");
    int ssaoShaderCamClipFar = GetShaderLocation(ssaoShader, "camClipFar");
    int ssaoShaderLightClipNear = GetShaderLocation(ssaoShader, "lightClipNear");
    int ssaoShaderLightClipFar = GetShaderLocation(ssaoShader, "lightClipFar");
    int ssaoShaderLightDir = GetShaderLocation(ssaoShader, "lightDir");
    
    Shader blurShader = LoadShader("./resources/post.vs", "./resources/blur.fs");
    int blurShaderGBufferNormal = GetShaderLocation(blurShader, "gbufferNormal");
    int blurShaderGBufferDepth = GetShaderLocation(blurShader, "gbufferDepth");
    int blurShaderInputTexture = GetShaderLocation(blurShader, "inputTexture");
    int blurShaderCamInvProj = GetShaderLocation(blurShader, "camInvProj");
    int blurShaderCamClipNear = GetShaderLocation(blurShader, "camClipNear");
    int blurShaderCamClipFar = GetShaderLocation(blurShader, "camClipFar");
    int blurShaderInvTextureResolution = GetShaderLocation(blurShader, "invTextureResolution");
    int blurShaderBlurDirection = GetShaderLocation(blurShader, "blurDirection");

    Shader fxaaShader = LoadShader("./resources/post.vs", "./resources/fxaa.fs");
    int fxaaShaderInputTexture = GetShaderLocation(fxaaShader, "inputTexture");
    int fxaaShaderInvTextureResolution = GetShaderLocation(fxaaShader, "invTextureResolution");
    
    // Objects
    
    Mesh groundMesh = GenMeshPlane(20.0f, 20.0f, 10, 10);
    Model groundModel = LoadModelFromMesh(groundMesh);
    Vector3 groundPosition = (Vector3){ 0.0f, -0.01f, 0.0f };
    
    Model genoModel = LoadGenoModel("./resources/Geno.bin");
    Vector3 genoPosition = (Vector3){ 0.0f, 0.0f, 0.0f };
    
    // Animation
    
    // ModelAnimation testAnimation = LoadGenoModelAnimation("./resources/ground1_subject1.bin");
    //ModelAnimation testAnimation = LoadGenoModelAnimation("./resources/kthstreet_gPO_sFM_cAll_d02_mPO_ch01_atombounce_001.bin");
    ModelAnimation testAnimation = LoadEmptyModelAnimation(genoModel);
    int animationFrame = 0;
    
    assert(testAnimation.boneCount == genoModel.boneCount);
    
    // Camera
    
    OrbitCamera camera;
    OrbitCameraInit(&camera);
    
    // Shadows
    
    Vector3 lightDir = Vector3Normalize((Vector3){ 0.35f, -1.0f, -0.35f });
    
    ShadowLight shadowLight = (ShadowLight){ 0 };
    shadowLight.target = Vector3Zero();
    shadowLight.position = Vector3Scale(lightDir, -5.0f);
    shadowLight.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    shadowLight.width = 5.0f;
    shadowLight.height = 5.0f;
    shadowLight.near = 0.01f;
    shadowLight.far = 10.0f;
    
    int shadowWidth = 1024;
    int shadowHeight = 1024;
    Vector2 shadowInvResolution = (Vector2){ 1.0f / shadowWidth, 1.0f / shadowHeight };
    RenderTexture2D shadowMap = LoadShadowMap(shadowWidth, shadowHeight);    
    
    // GBuffer and Render Textures
    
    GBuffer gbuffer = LoadGBuffer(screenWidth, screenHeight);
    RenderTexture2D lighted = LoadRenderTexture(screenWidth, screenHeight);
    RenderTexture2D ssaoFront = LoadRenderTexture(screenWidth, screenHeight);
    RenderTexture2D ssaoBack = LoadRenderTexture(screenWidth, screenHeight);
    
    // UI
    
    bool drawBoneTransforms = false;
    
    // Go
    
    while (!WindowShouldClose())
    {
        // Animation
        
        animationFrame = (animationFrame + 1) % testAnimation.frameCount;
        UpdateModelAnimationBoneMatrices(genoModel, testAnimation, animationFrame);

        // Shadow Light Tracks Character
        
        Vector3 hipPosition = testAnimation.framePoses[animationFrame][0].translation;
        
        shadowLight.target = (Vector3){ hipPosition.x, 0.0f, hipPosition.z };
        shadowLight.position = Vector3Add(shadowLight.target, Vector3Scale(lightDir, -5.0f));

        // Update Camera
        
        OrbitCameraUpdate(
            &camera,
            (Vector3){ hipPosition.x, 0.75f, hipPosition.z },
            (IsKeyDown(KEY_LEFT_CONTROL) && IsMouseButtonDown(0)) ? GetMouseDelta().x : 0.0f,
            (IsKeyDown(KEY_LEFT_CONTROL) && IsMouseButtonDown(0)) ? GetMouseDelta().y : 0.0f,
            (IsKeyDown(KEY_LEFT_CONTROL) && IsMouseButtonDown(1)) ? GetMouseDelta().x : 0.0f,
            (IsKeyDown(KEY_LEFT_CONTROL) && IsMouseButtonDown(1)) ? GetMouseDelta().y : 0.0f,
            GetMouseWheelMove(),
            GetFrameTime());
        
        // Render
        
        rlDisableColorBlend();
        
        BeginDrawing();
        
        // Render Shadow Maps
        
        BeginShadowMap(shadowMap, shadowLight);  
        
        Matrix lightViewProj = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
        float lightClipNear = rlGetCullDistanceNear();
        float lightClipFar = rlGetCullDistanceFar();
        
        SetShaderValue(shadowShader, shadowShaderLightClipNear, &lightClipNear, SHADER_UNIFORM_FLOAT);
        SetShaderValue(shadowShader, shadowShaderLightClipFar, &lightClipFar, SHADER_UNIFORM_FLOAT);
        SetShaderValue(skinnedShadowShader, skinnedShadowShaderLightClipNear, &lightClipNear, SHADER_UNIFORM_FLOAT);
        SetShaderValue(skinnedShadowShader, skinnedShadowShaderLightClipFar, &lightClipFar, SHADER_UNIFORM_FLOAT);
        
        groundModel.materials[0].shader = shadowShader;
        DrawModel(groundModel, groundPosition, 1.0f, WHITE);
        
        genoModel.materials[0].shader = skinnedShadowShader;
        DrawModel(genoModel, genoPosition, 1.0f, WHITE);
        
        EndShadowMap();
        
        // Render GBuffer
        
        BeginGBuffer(gbuffer, camera.cam3d);
        
        Matrix camView = rlGetMatrixModelview();
        Matrix camProj = rlGetMatrixProjection();
        Matrix camInvProj = MatrixInvert(camProj);
        Matrix camInvViewProj = MatrixInvert(MatrixMultiply(camView, camProj));
        float camClipNear = rlGetCullDistanceNear();
        float camClipFar = rlGetCullDistanceFar();

        float specularity = 0.5f;
        float glossiness = 10.0f;        
        
        SetShaderValue(basicShader, basicShaderSpecularity, &specularity, SHADER_UNIFORM_FLOAT);
        SetShaderValue(basicShader, basicShaderGlossiness, &glossiness, SHADER_UNIFORM_FLOAT);
        SetShaderValue(basicShader, basicShaderCamClipNear, &camClipNear, SHADER_UNIFORM_FLOAT);
        SetShaderValue(basicShader, basicShaderCamClipFar, &camClipFar, SHADER_UNIFORM_FLOAT);
        
        SetShaderValue(skinnedBasicShader, skinnedBasicShaderSpecularity, &specularity, SHADER_UNIFORM_FLOAT);
        SetShaderValue(skinnedBasicShader, skinnedBasicShaderGlossiness, &glossiness, SHADER_UNIFORM_FLOAT);
        SetShaderValue(skinnedBasicShader, skinnedBasicShaderCamClipNear, &camClipNear, SHADER_UNIFORM_FLOAT);
        SetShaderValue(skinnedBasicShader, skinnedBasicShaderCamClipFar, &camClipFar, SHADER_UNIFORM_FLOAT);        
        
        groundModel.materials[0].shader = basicShader;
        DrawModel(groundModel, groundPosition, 1.0f, WHITE);
        
        genoModel.materials[0].shader = skinnedBasicShader;
        DrawModel(genoModel, genoPosition, 1.0f, ORANGE);       
        
        EndGBuffer(screenWidth, screenHeight);
        
        // Render SSAO and Shadows
        
        BeginTextureMode(ssaoFront);
        
        BeginShaderMode(ssaoShader);
        
        SetShaderValueTexture(ssaoShader, ssaoShaderGBufferNormal, gbuffer.normal);
        SetShaderValueTexture(ssaoShader, ssaoShaderGBufferDepth, gbuffer.depth);
        SetShaderValueMatrix(ssaoShader, ssaoShaderCamView, camView);
        SetShaderValueMatrix(ssaoShader, ssaoShaderCamProj, camProj);
        SetShaderValueMatrix(ssaoShader, ssaoShaderCamInvProj, camInvProj);
        SetShaderValueMatrix(ssaoShader, ssaoShaderCamInvViewProj, camInvViewProj);
        SetShaderValueMatrix(ssaoShader, ssaoShaderLightViewProj, lightViewProj);
        SetShaderValueShadowMap(ssaoShader, ssaoShaderShadowMap, shadowMap);
        SetShaderValue(ssaoShader, ssaoShaderShadowInvResolution, &shadowInvResolution, SHADER_UNIFORM_VEC2);
        SetShaderValue(ssaoShader, ssaoShaderCamClipNear, &camClipNear, SHADER_UNIFORM_FLOAT);
        SetShaderValue(ssaoShader, ssaoShaderCamClipFar, &camClipFar, SHADER_UNIFORM_FLOAT);
        SetShaderValue(ssaoShader, ssaoShaderLightClipNear, &lightClipNear, SHADER_UNIFORM_FLOAT);
        SetShaderValue(ssaoShader, ssaoShaderLightClipFar, &lightClipFar, SHADER_UNIFORM_FLOAT);
        SetShaderValue(ssaoShader, ssaoShaderLightDir, &lightDir, SHADER_UNIFORM_VEC3);
        
        ClearBackground(WHITE);
        
        DrawTextureRec(
            ssaoFront.texture,
            (Rectangle){ 0, 0, ssaoFront.texture.width, -ssaoFront.texture.height },
            (Vector2){ 0, 0 },
            WHITE);

        EndShaderMode();

        EndTextureMode();
        
        // Blur Horizontal
        
        BeginTextureMode(ssaoBack);
        
        BeginShaderMode(blurShader);
        
        Vector2 blurDirection = (Vector2){ 1.0f, 0.0f };
        Vector2 blurInvTextureResolution = (Vector2){ 1.0f / ssaoFront.texture.width, 1.0f / ssaoFront.texture.height };
        
        SetShaderValueTexture(blurShader, blurShaderGBufferNormal, gbuffer.normal);
        SetShaderValueTexture(blurShader, blurShaderGBufferDepth, gbuffer.depth);
        SetShaderValueTexture(blurShader, blurShaderInputTexture, ssaoFront.texture);
        SetShaderValueMatrix(blurShader, blurShaderCamInvProj, camInvProj);
        SetShaderValue(blurShader, blurShaderCamClipNear, &camClipNear, SHADER_UNIFORM_FLOAT);
        SetShaderValue(blurShader, blurShaderCamClipFar, &camClipFar, SHADER_UNIFORM_FLOAT);
        SetShaderValue(blurShader, blurShaderInvTextureResolution, &blurInvTextureResolution, SHADER_UNIFORM_VEC2);
        SetShaderValue(blurShader, blurShaderBlurDirection, &blurDirection, SHADER_UNIFORM_VEC2);

        DrawTextureRec(
            ssaoBack.texture,
            (Rectangle){ 0, 0, ssaoBack.texture.width, -ssaoBack.texture.height },
            (Vector2){ 0, 0 },
            WHITE);

        EndShaderMode();

        EndTextureMode();
      
        // Blur Vertical
        
        BeginTextureMode(ssaoFront);
        
        BeginShaderMode(blurShader);
        
        blurDirection = (Vector2){ 0.0f, 1.0f };
        
        SetShaderValueTexture(blurShader, blurShaderInputTexture, ssaoBack.texture);
        SetShaderValue(blurShader, blurShaderBlurDirection, &blurDirection, SHADER_UNIFORM_VEC2);

        DrawTextureRec(
            ssaoFront.texture,
            (Rectangle){ 0, 0, ssaoFront.texture.width, -ssaoFront.texture.height },
            (Vector2){ 0, 0 },
            WHITE);

        EndShaderMode();

        EndTextureMode();
      
        // Light GBuffer
        
        BeginTextureMode(lighted);
        
        BeginShaderMode(lightingShader);
        
        Vector3 sunColor = (Vector3){ 253.0f / 255.0f, 255.0f / 255.0f, 232.0f / 255.0f };
        float sunStrength = 0.25f;
        Vector3 skyColor = (Vector3){ 174.0f / 255.0f, 183.0f / 255.0f, 190.0f / 255.0f };
        float skyStrength = 0.2f;
        float groundStrength = 0.1f;
        float ambientStrength = 1.0f;
        float exposure = 0.9f;
        
        SetShaderValueTexture(lightingShader, lightingShaderGBufferColor, gbuffer.color);
        SetShaderValueTexture(lightingShader, lightingShaderGBufferNormal, gbuffer.normal);
        SetShaderValueTexture(lightingShader, lightingShaderGBufferDepth, gbuffer.depth);
        SetShaderValueTexture(lightingShader, lightingShaderSSAO, ssaoFront.texture);
        SetShaderValue(lightingShader, lightingShaderCamPos, &camera.cam3d.position, SHADER_UNIFORM_VEC3);
        SetShaderValueMatrix(lightingShader, lightingShaderCamInvViewProj, camInvViewProj);
        SetShaderValue(lightingShader, lightingShaderLightDir, &lightDir, SHADER_UNIFORM_VEC3);
        SetShaderValue(lightingShader, lightingShaderSunColor, &sunColor, SHADER_UNIFORM_VEC3);
        SetShaderValue(lightingShader, lightingShaderSunStrength, &sunStrength, SHADER_UNIFORM_FLOAT);
        SetShaderValue(lightingShader, lightingShaderSkyColor, &skyColor, SHADER_UNIFORM_VEC3);
        SetShaderValue(lightingShader, lightingShaderSkyStrength, &skyStrength, SHADER_UNIFORM_FLOAT);
        SetShaderValue(lightingShader, lightingShaderGroundStrength, &groundStrength, SHADER_UNIFORM_FLOAT);
        SetShaderValue(lightingShader, lightingShaderAmbientStrength, &ambientStrength, SHADER_UNIFORM_FLOAT);
        SetShaderValue(lightingShader, lightingShaderExposure, &exposure, SHADER_UNIFORM_FLOAT);
        SetShaderValue(lightingShader, lightingShaderCamClipNear, &camClipNear, SHADER_UNIFORM_FLOAT);
        SetShaderValue(lightingShader, lightingShaderCamClipFar, &camClipFar, SHADER_UNIFORM_FLOAT);
        
        ClearBackground(RAYWHITE);
        
        DrawTextureRec(
            gbuffer.color,
            (Rectangle){ 0, 0, gbuffer.color.width, -gbuffer.color.height },
            (Vector2){ 0, 0 },
            WHITE);
        
        EndShaderMode();        
        
        // Debug Draw
        
        BeginMode3D(camera.cam3d);
        
        if (drawBoneTransforms)
        {
            DrawModelAnimationFrameSkeleton(testAnimation, animationFrame, GRAY);
        }
  
        EndMode3D();

        EndTextureMode();
        
        // Render Final with FXAA
        
        BeginShaderMode(fxaaShader);

        Vector2 fxaaInvTextureResolution = (Vector2){ 1.0f / lighted.texture.width, 1.0f / lighted.texture.height };
        
        SetShaderValueTexture(fxaaShader, fxaaShaderInputTexture, lighted.texture);
        SetShaderValue(fxaaShader, fxaaShaderInvTextureResolution, &fxaaInvTextureResolution, SHADER_UNIFORM_VEC2);
        
        DrawTextureRec(
            lighted.texture,
            (Rectangle){ 0, 0, lighted.texture.width, -lighted.texture.height },
            (Vector2){ 0, 0 },
            WHITE);
        
        EndShaderMode();
  
        // UI
  
        rlEnableColorBlend();
  
        GuiGroupBox((Rectangle){ 20, 10, 190, 180 }, "Camera");

        GuiLabel((Rectangle){ 30, 20, 150, 20 }, "Ctrl + Left Click - Rotate");
        GuiLabel((Rectangle){ 30, 40, 150, 20 }, "Ctrl + Right Click - Pan");
        GuiLabel((Rectangle){ 30, 60, 150, 20 }, "Mouse Scroll - Zoom");
        GuiLabel((Rectangle){ 30, 80, 150, 20 }, TextFormat("Target: [% 5.3f % 5.3f % 5.3f]", camera.cam3d.target.x, camera.cam3d.target.y, camera.cam3d.target.z));
        GuiLabel((Rectangle){ 30, 100, 150, 20 }, TextFormat("Offset: [% 5.3f % 5.3f % 5.3f]", camera.offset.x, camera.offset.y, camera.offset.z));
        GuiLabel((Rectangle){ 30, 120, 150, 20 }, TextFormat("Azimuth: %5.3f", camera.azimuth));
        GuiLabel((Rectangle){ 30, 140, 150, 20 }, TextFormat("Altitude: %5.3f", camera.altitude));
        GuiLabel((Rectangle){ 30, 160, 150, 20 }, TextFormat("Distance: %5.3f", camera.distance));
  
        GuiGroupBox((Rectangle){ screenWidth - 260, 10, 240, 40 }, "Rendering");

        GuiCheckBox((Rectangle){ screenWidth - 250, 20, 20, 20 }, "Draw Transfoms", &drawBoneTransforms);

  
        EndDrawing();
    }

    UnloadRenderTexture(lighted);
    UnloadRenderTexture(ssaoBack);
    UnloadRenderTexture(ssaoFront);
    UnloadRenderTexture(lighted);
    UnloadGBuffer(gbuffer);

    UnloadShadowMap(shadowMap);
    
    UnloadModelAnimation(testAnimation);
    
    UnloadModel(genoModel);
    UnloadModel(groundModel);
    
    UnloadShader(fxaaShader);    
    UnloadShader(blurShader);    
    UnloadShader(ssaoShader);    
    UnloadShader(lightingShader);    
    UnloadShader(basicShader);    
    UnloadShader(skinnedBasicShader);
    UnloadShader(skinnedShadowShader);
    UnloadShader(shadowShader);
        
    CloseWindow();

    return 0;
}