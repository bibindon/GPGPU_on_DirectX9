#define D3D_DEBUG_INFO

#pragma comment( lib, "d3d9.lib" )
#if defined(DEBUG) || defined(_DEBUG)
#pragma comment( lib, "d3dx9d.lib" )
#else
#pragma comment( lib, "d3dx9.lib" )
#endif

#include <d3d9.h>
#include <d3dx9.h>
#include <windowsx.h>
#include <string>
#include <tchar.h>
#include <cassert>
#include <crtdbg.h>
#include <vector>
#include <cmath>
#include <float.h>

#define SAFE_RELEASE(p) { if (p) { (p)->Release(); (p) = NULL; } }

const int WINDOW_SIZE_W = 1600;
const int WINDOW_SIZE_H = 900;
int g_clothVertexCountX = 16;
int g_clothVertexCountZ = 16;
int g_clothParticleCount = 256;
const float CLOTH_SIZE = 4.0f;
const float CLOTH_START_Y = 1.1f;
const float SPHERE_DISPLAY_RADIUS = 1.0f;
const float CLOTH_COLLISION_RADIUS = 1.1f;
const float CAMERA_MIN_PITCH = -1.3f;
const float CAMERA_MAX_PITCH = 1.3f;
const float CAMERA_MIN_DISTANCE = 2.0f;
const float CAMERA_MAX_DISTANCE = 20.0f;
const int SETTINGS_COMBO_ID = 1001;
const int SETTINGS_RESET_BUTTON_ID = 1002;
const int SETTINGS_CLOTH_COMBO_ID = 1003;

enum SimulationMode
{
    SIMULATION_MODE_CPU = 0,
    SIMULATION_MODE_CPU_OPENMP,
    SIMULATION_MODE_GPU
};

LPDIRECT3D9 g_pD3D = NULL;
LPDIRECT3DDEVICE9 g_pd3dDevice = NULL;
LPD3DXFONT g_pFont = NULL;
LPD3DXEFFECT g_pEffect = NULL;
HWND g_hSettingsWnd = NULL;
HWND g_hSimulationCombo = NULL;
HWND g_hClothCombo = NULL;
LPDIRECT3DTEXTURE9 g_pGpuCurrentPositionTexture = NULL;
LPDIRECT3DTEXTURE9 g_pGpuPreviousPositionTexture = NULL;
LPDIRECT3DTEXTURE9 g_pGpuNextPositionTexture = NULL;
LPDIRECT3DSURFACE9 g_pGpuReadbackSurface = NULL;
LPDIRECT3DTEXTURE9 g_pGpuUploadStagingTexture = NULL;
bool g_bClose = false;
bool g_bCameraDragging = false;
POINT g_lastMousePosition { };
float g_cameraYaw = 0.0f;
float g_cameraPitch = 0.35f;
float g_cameraDistance = 8.5f;
SimulationMode g_simulationMode = SIMULATION_MODE_CPU_OPENMP;
SimulationMode g_previousSimulationMode = SIMULATION_MODE_CPU_OPENMP;
LARGE_INTEGER g_performanceFrequency { };
LARGE_INTEGER g_previousPerformanceCounter { };
float g_currentFps = 0.0f;

struct MeshModel
{
    LPD3DXMESH pMesh;
    std::vector<D3DMATERIAL9> materials;
    std::vector<LPDIRECT3DTEXTURE9> textures;
    DWORD dwNumMaterials;
};

MeshModel g_sphereModel { };
MeshModel g_clothModel { };

struct ClothParticle
{
    D3DXVECTOR3 position;
    D3DXVECTOR3 previousPosition;
    D3DXVECTOR3 normal;
    bool bFixed;
};

std::vector<ClothParticle> g_clothParticles;
DWORD g_clothPositionOffset = 0;
DWORD g_clothNormalOffset = 0;
DWORD g_clothVertexStride = 0;
bool g_bClothReady = false;

struct GpuClothTexel
{
    float x;
    float y;
    float z;
    float w;
};

struct ScreenVertex
{
    float x;
    float y;
    float z;
    float rhw;
    float u;
    float v;
};

static void TextDraw(LPD3DXFONT pFont, TCHAR* text, int X, int Y);
static void LoadMeshModel(LPCTSTR pFileName, MeshModel* pModel);
static void CleanupMeshModel(MeshModel* pModel);
static void DrawMeshModel(MeshModel* pModel, const D3DXMATRIX& world, const D3DXMATRIX& viewProj);
static void InitializeClothSimulation();
static void UpdateClothSimulation();
static void UpdateClothSimulationGpu();
static void UpdateClothSimulationCpuBody(bool bUseOpenMP);
static void ApplyClothConstraints();
static void SatisfyClothConstraint(int indexA, int indexB, float restLength);
static void ApplySphereCollision(D3DXVECTOR3* pPosition);
static void UpdateClothNormals();
static void WriteClothMeshVertices();
static int GetClothIndex(int x, int z);
static void BuildCameraViewMatrix(D3DXMATRIX* pView);
static void CreateSettingsWindow(HINSTANCE hInstance);
static void UpdateFps();
static void CreateGpuClothResources();
static void CleanupGpuClothResources();
static void LoadClothMeshForResolution(int vertexCountX, int vertexCountZ, LPCTSTR filename);
static void UploadGpuClothTexture(LPDIRECT3DTEXTURE9 pTexture, bool bUsePreviousPosition);
static void UploadGpuClothTextures();
static void UploadGpuClothCurrentTexture();
static bool DownloadGpuClothPositions();
static void RenderGpuClothPass();
static bool IsValidGpuClothPosition(const D3DXVECTOR3& position);
static bool IsOpenMPSimulationEnabled();
static bool IsGpuSimulationEnabled();
static LPCTSTR GetSimulationModeText();
static void UpdateSimulationModeFromCombo();
static void InitD3D(HWND hWnd);
static void Cleanup();
static void Render();
LRESULT WINAPI MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI SettingsMsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

extern int WINAPI _tWinMain(_In_ HINSTANCE hInstance,
                            _In_opt_ HINSTANCE hPrevInstance,
                            _In_ LPTSTR lpCmdLine,
                            _In_ int nCmdShow);

int WINAPI _tWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPTSTR lpCmdLine,
                     _In_ int nCmdShow)
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    QueryPerformanceFrequency(&g_performanceFrequency);
    QueryPerformanceCounter(&g_previousPerformanceCounter);

    WNDCLASSEX wc { };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = MsgProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hIcon = NULL;
    wc.hCursor = NULL;
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = _T("Window1");
    wc.hIconSm = NULL;

    ATOM atom = RegisterClassEx(&wc);
    assert(atom != 0);

    RECT rect;
    SetRect(&rect, 0, 0, WINDOW_SIZE_W, WINDOW_SIZE_H);
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    rect.right = rect.right - rect.left;
    rect.bottom = rect.bottom - rect.top;
    rect.top = 0;
    rect.left = 0;

    HWND hWnd = CreateWindow(_T("Window1"),
                             _T("Hello DirectX9 World !!"),
                             WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT,
                             CW_USEDEFAULT,
                             rect.right,
                             rect.bottom,
                             NULL,
                             NULL,
                             wc.hInstance,
                             NULL);

    InitD3D(hWnd);
    CreateSettingsWindow(wc.hInstance);
    ShowWindow(hWnd, SW_SHOWDEFAULT);
    UpdateWindow(hWnd);

    MSG msg;

    while (true)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            DispatchMessage(&msg);
        }
        else
        {
            //Sleep(16);
            Render();
        }

        if (g_bClose)
        {
            break;
        }
    }

    Cleanup();

    UnregisterClass(_T("Window1"), wc.hInstance);
    return 0;
}

void TextDraw(LPD3DXFONT pFont, TCHAR* text, int X, int Y)
{
    RECT rect = { X, Y, 0, 0 };

    // DrawTextの戻り値は文字数である。
    // そのため、hResultの中身が整数でもエラーが起きているわけではない。
    HRESULT hResult = pFont->DrawText(NULL,
                                      text,
                                      -1,
                                      &rect,
                                      DT_LEFT | DT_NOCLIP,
                                      D3DCOLOR_ARGB(255, 0, 0, 0));

    assert((int)hResult >= 0);
}

void InitD3D(HWND hWnd)
{
    HRESULT hResult = E_FAIL;

    g_pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    assert(g_pD3D != NULL);

    D3DPRESENT_PARAMETERS d3dpp;
    ZeroMemory(&d3dpp, sizeof(d3dpp));
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
    d3dpp.BackBufferCount = 1;
    d3dpp.MultiSampleType = D3DMULTISAMPLE_NONE;
    d3dpp.MultiSampleQuality = 0;
    d3dpp.EnableAutoDepthStencil = TRUE;
    d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    d3dpp.hDeviceWindow = hWnd;
    d3dpp.Flags = 0;
    d3dpp.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
    d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;

    if (true)
    {
        hResult = g_pD3D->CreateDevice(D3DADAPTER_DEFAULT,
                                       D3DDEVTYPE_HAL,
                                       hWnd,
                                       D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                       &d3dpp,
                                       &g_pd3dDevice);

        if (FAILED(hResult))
        {
            hResult = g_pD3D->CreateDevice(D3DADAPTER_DEFAULT,
                                           D3DDEVTYPE_HAL,
                                           hWnd,
                                           D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                                           &d3dpp,
                                           &g_pd3dDevice);

            assert(hResult == S_OK);
        }
    }
    else
    {
        hResult = g_pD3D->CreateDevice(D3DADAPTER_DEFAULT,
                                       D3DDEVTYPE_REF,
                                       hWnd,
                                       D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                                       &d3dpp,
                                       &g_pd3dDevice);

        assert(hResult == S_OK);
    }

    hResult = D3DXCreateFont(g_pd3dDevice,
                             20,
                             0,
                             FW_HEAVY,
                             1,
                             FALSE,
                             SHIFTJIS_CHARSET,
                             OUT_TT_ONLY_PRECIS,
                             CLEARTYPE_NATURAL_QUALITY,
                             FF_DONTCARE,
                             _T("ＭＳ ゴシック"),
                             &g_pFont);

    assert(hResult == S_OK);

    hResult = g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    assert(hResult == S_OK);

    LoadMeshModel(_T("sphere.x"), &g_sphereModel);
    LoadClothMeshForResolution(16, 16, _T("cloth16x16.x"));

    hResult = D3DXCreateEffectFromFile(g_pd3dDevice,
                                       _T("simple.fx"),
                                       NULL,
                                       NULL,
                                       D3DXSHADER_DEBUG | D3DXSHADER_SKIPOPTIMIZATION,
                                       NULL,
                                       &g_pEffect,
                                       NULL);

    assert(hResult == S_OK);
}

void LoadMeshModel(LPCTSTR pFileName, MeshModel* pModel)
{
    HRESULT hResult = E_FAIL;
    LPD3DXBUFFER pD3DXMtrlBuffer = NULL;

    hResult = D3DXLoadMeshFromX(pFileName,
                                D3DXMESH_SYSTEMMEM,
                                g_pd3dDevice,
                                NULL,
                                &pD3DXMtrlBuffer,
                                NULL,
                                &pModel->dwNumMaterials,
                                &pModel->pMesh);

    assert(hResult == S_OK);

    D3DXMATERIAL* d3dxMaterials = (D3DXMATERIAL*)pD3DXMtrlBuffer->GetBufferPointer();
    pModel->materials.resize(pModel->dwNumMaterials);
    pModel->textures.resize(pModel->dwNumMaterials);

    for (DWORD i = 0; i < pModel->dwNumMaterials; i++)
    {
        pModel->materials[i] = d3dxMaterials[i].MatD3D;
        pModel->materials[i].Ambient = pModel->materials[i].Diffuse;
        pModel->textures[i] = NULL;
        
        //--------------------------------------------------------------
        // Unicode文字セットでもマルチバイト文字セットでも
        // "d3dxMaterials[i].pTextureFilename"はマルチバイト文字セットになる。
        // 
        // 一方で、D3DXCreateTextureFromFileはプロジェクト設定で
        // Unicode文字セットかマルチバイト文字セットか変わる。
        //--------------------------------------------------------------

        std::string pTexPath(d3dxMaterials[i].pTextureFilename);

        if (!pTexPath.empty())
        {
            bool bUnicode = false;

#ifdef UNICODE
            bUnicode = true;
#endif

            if (!bUnicode)
            {
                hResult = D3DXCreateTextureFromFileA(g_pd3dDevice, pTexPath.c_str(), &pModel->textures[i]);
                assert(hResult == S_OK);
            }
            else
            {
                int len = MultiByteToWideChar(CP_ACP, 0, pTexPath.c_str(), -1, nullptr, 0);
                std::wstring pTexPathW(len, 0);
                MultiByteToWideChar(CP_ACP, 0, pTexPath.c_str(), -1, &pTexPathW[0], len);

                hResult = D3DXCreateTextureFromFileW(g_pd3dDevice, pTexPathW.c_str(), &pModel->textures[i]);
                assert(hResult == S_OK);
            }
        }
    }

    hResult = pD3DXMtrlBuffer->Release();
    assert(hResult == S_OK);
}

void CleanupMeshModel(MeshModel* pModel)
{
    for (auto& texture : pModel->textures)
    {
        SAFE_RELEASE(texture);
    }

    SAFE_RELEASE(pModel->pMesh);
    pModel->materials.clear();
    pModel->textures.clear();
    pModel->dwNumMaterials = 0;
}

void Cleanup()
{
    if (g_hSettingsWnd != NULL)
    {
        DestroyWindow(g_hSettingsWnd);
        g_hSettingsWnd = NULL;
        g_hSimulationCombo = NULL;
        g_hClothCombo = NULL;
    }

    CleanupGpuClothResources();
    CleanupMeshModel(&g_clothModel);
    CleanupMeshModel(&g_sphereModel);
    g_clothParticles.clear();
    g_bClothReady = false;
    SAFE_RELEASE(g_pEffect);
    SAFE_RELEASE(g_pFont);
    SAFE_RELEASE(g_pd3dDevice);
    SAFE_RELEASE(g_pD3D);
}

void Render()
{
    HRESULT hResult = E_FAIL;

    D3DXMATRIX matViewProj;
    D3DXMATRIX View, Proj;
    D3DXMATRIX matSphereWorld;
    D3DXMATRIX matClothWorld;

    UpdateFps();

    D3DXMatrixPerspectiveFovLH(&Proj,
                               D3DXToRadian(45),
                               (float)WINDOW_SIZE_W / WINDOW_SIZE_H,
                               1.0f,
                               10000.0f);

    BuildCameraViewMatrix(&View);
    matViewProj = View * Proj;
    UpdateClothSimulation();

    D3DXMatrixScaling(&matSphereWorld, 2.0f, 2.0f, 2.0f);
    D3DXMatrixIdentity(&matClothWorld);

    hResult = g_pd3dDevice->Clear(0,
                                  NULL,
                                  D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                                  D3DCOLOR_XRGB(100, 100, 100),
                                  1.0f,
                                  0);

    assert(hResult == S_OK);

    hResult = g_pd3dDevice->BeginScene();
    assert(hResult == S_OK);

    TCHAR msg[100];
    _tcscpy_s(msg, 100, _T("Xファイルの読み込みと表示"));
    TextDraw(g_pFont, msg, 0, 0);
    _tcscpy_s(msg, 100, _T("Simulation: "));
    _tcscat_s(msg, 100, GetSimulationModeText());
    TextDraw(g_pFont, msg, 0, 24);
    _stprintf_s(msg, 100, _T("FPS: %.1f"), g_currentFps);
    TextDraw(g_pFont, msg, 0, 48);

    hResult = g_pEffect->SetTechnique("Technique1");
    assert(hResult == S_OK);

    UINT numPass;
    hResult = g_pEffect->Begin(&numPass, 0);
    assert(hResult == S_OK);

    hResult = g_pEffect->BeginPass(0);
    assert(hResult == S_OK);

    DrawMeshModel(&g_sphereModel, matSphereWorld, matViewProj);
    DrawMeshModel(&g_clothModel, matClothWorld, matViewProj);

    hResult = g_pEffect->EndPass();
    assert(hResult == S_OK);

    hResult = g_pEffect->End();
    assert(hResult == S_OK);

    hResult = g_pd3dDevice->EndScene();
    assert(hResult == S_OK);

    hResult = g_pd3dDevice->Present(NULL, NULL, NULL, NULL);
    assert(hResult == S_OK);
}

void DrawMeshModel(MeshModel* pModel, const D3DXMATRIX& world, const D3DXMATRIX& viewProj)
{
    HRESULT hResult = E_FAIL;
    D3DXMATRIX matWorldViewProj = world * viewProj;

    hResult = g_pEffect->SetMatrix("g_matWorldViewProj", &matWorldViewProj);
    assert(hResult == S_OK);

    for (DWORD i = 0; i < pModel->dwNumMaterials; i++)
    {
        hResult = g_pEffect->SetTexture("texture1", pModel->textures[i]);
        assert(hResult == S_OK);

        hResult = g_pEffect->CommitChanges();
        assert(hResult == S_OK);

        hResult = pModel->pMesh->DrawSubset(i);
        assert(hResult == S_OK);
    }
}

void InitializeClothSimulation()
{
    HRESULT hResult = E_FAIL;
    D3DVERTEXELEMENT9 declaration[MAX_FVF_DECL_SIZE];

    hResult = g_clothModel.pMesh->GetDeclaration(declaration);
    assert(hResult == S_OK);

    g_clothPositionOffset = 0;
    g_clothNormalOffset = 0;

    for (int i = 0; declaration[i].Stream != 0xFF; i++)
    {
        if (declaration[i].Usage == D3DDECLUSAGE_POSITION)
        {
            g_clothPositionOffset = declaration[i].Offset;
        }

        if (declaration[i].Usage == D3DDECLUSAGE_NORMAL)
        {
            g_clothNormalOffset = declaration[i].Offset;
        }
    }

    g_clothVertexStride = g_clothModel.pMesh->GetNumBytesPerVertex();
    g_clothParticles.resize(g_clothVertexCountX * g_clothVertexCountZ);

    float spacing = CLOTH_SIZE / (float)(g_clothVertexCountX - 1);
    float halfSize = CLOTH_SIZE * 0.5f;

    for (int z = 0; z < g_clothVertexCountZ; z++)
    {
        for (int x = 0; x < g_clothVertexCountX; x++)
        {
            int index = GetClothIndex(x, z);
            float positionX = -halfSize + spacing * (float)x;
            float positionZ = -halfSize + spacing * (float)z;

            g_clothParticles[index].position = D3DXVECTOR3(positionX, CLOTH_START_Y, positionZ);
            g_clothParticles[index].previousPosition = g_clothParticles[index].position;
            g_clothParticles[index].normal = D3DXVECTOR3(0.0f, 1.0f, 0.0f);
            g_clothParticles[index].bFixed = false;
        }
    }

    g_bClothReady = true;
    UpdateClothNormals();
    WriteClothMeshVertices();

    if (g_pGpuCurrentPositionTexture != NULL)
    {
        UploadGpuClothTextures();
    }
}

void UpdateClothSimulation()
{
    if (!g_bClothReady)
    {
        return;
    }

    if (IsGpuSimulationEnabled())
    {
        UpdateClothSimulationGpu();
        return;
    }

    UpdateClothSimulationCpuBody(IsOpenMPSimulationEnabled());
}

void UpdateClothSimulationCpuBody(bool bUseOpenMP)
{
    const D3DXVECTOR3 gravity(0.0f, -9.8f, 0.0f);
    const float deltaTime = 1.0f / 60.0f;
    const float damping = 0.995f;

    if (bUseOpenMP)
    {
        int particleCount = (int)g_clothParticles.size();

#pragma omp parallel for
        for (int i = 0; i < particleCount; i++)
        {
            ClothParticle& particle = g_clothParticles[i];

            if (!particle.bFixed)
            {
                D3DXVECTOR3 velocity = (particle.position - particle.previousPosition) * damping;
                D3DXVECTOR3 nextPosition = particle.position + velocity + gravity * deltaTime * deltaTime;
                particle.previousPosition = particle.position;
                particle.position = nextPosition;
                ApplySphereCollision(&particle.position);
            }
        }
    }
    else
    {
        for (auto& particle : g_clothParticles)
        {
            if (particle.bFixed)
            {
                continue;
            }

            D3DXVECTOR3 velocity = (particle.position - particle.previousPosition) * damping;
            D3DXVECTOR3 nextPosition = particle.position + velocity + gravity * deltaTime * deltaTime;
            particle.previousPosition = particle.position;
            particle.position = nextPosition;
            ApplySphereCollision(&particle.position);
        }
    }

    ApplyClothConstraints();
    UpdateClothNormals();
    WriteClothMeshVertices();
}

void ApplyClothConstraints()
{
    const int constraintIterations = 8;
    const float structuralRestLength = CLOTH_SIZE / (float)(g_clothVertexCountX - 1);
    const float shearRestLength = structuralRestLength * sqrtf(2.0f);

    for (int iteration = 0; iteration < constraintIterations; iteration++)
    {
        for (int z = 0; z < g_clothVertexCountZ; z++)
        {
            for (int x = 0; x < g_clothVertexCountX; x++)
            {
                if (x + 1 < g_clothVertexCountX)
                {
                    SatisfyClothConstraint(GetClothIndex(x, z), GetClothIndex(x + 1, z), structuralRestLength);
                }

                if (z + 1 < g_clothVertexCountZ)
                {
                    SatisfyClothConstraint(GetClothIndex(x, z), GetClothIndex(x, z + 1), structuralRestLength);
                }

                if (x + 1 < g_clothVertexCountX && z + 1 < g_clothVertexCountZ)
                {
                    SatisfyClothConstraint(GetClothIndex(x, z), GetClothIndex(x + 1, z + 1), shearRestLength);
                    SatisfyClothConstraint(GetClothIndex(x + 1, z), GetClothIndex(x, z + 1), shearRestLength);
                }
            }
        }

        if (IsOpenMPSimulationEnabled())
        {
            int particleCount = (int)g_clothParticles.size();

#pragma omp parallel for
            for (int i = 0; i < particleCount; i++)
            {
                ApplySphereCollision(&g_clothParticles[i].position);
            }
        }
        else
        {
            for (auto& particle : g_clothParticles)
            {
                ApplySphereCollision(&particle.position);
            }
        }
    }
}

void UpdateClothSimulationGpu()
{
    RenderGpuClothPass();

    LPDIRECT3DTEXTURE9 pOldPreviousTexture = g_pGpuPreviousPositionTexture;
    g_pGpuPreviousPositionTexture = g_pGpuCurrentPositionTexture;
    g_pGpuCurrentPositionTexture = g_pGpuNextPositionTexture;
    g_pGpuNextPositionTexture = pOldPreviousTexture;

    if (!DownloadGpuClothPositions())
    {
        UpdateClothSimulationCpuBody(false);
        UploadGpuClothTextures();
        return;
    }

    ApplyClothConstraints();
    UpdateClothNormals();
    WriteClothMeshVertices();
    UploadGpuClothCurrentTexture();
}

void CreateGpuClothResources()
{
    HRESULT hResult = E_FAIL;

    hResult = g_pd3dDevice->CreateTexture(g_clothVertexCountX,
                                          g_clothVertexCountZ,
                                          1,
                                          D3DUSAGE_RENDERTARGET,
                                          D3DFMT_A32B32G32R32F,
                                          D3DPOOL_DEFAULT,
                                          &g_pGpuCurrentPositionTexture,
                                          NULL);
    assert(hResult == S_OK);

    hResult = g_pd3dDevice->CreateTexture(g_clothVertexCountX,
                                          g_clothVertexCountZ,
                                          1,
                                          D3DUSAGE_RENDERTARGET,
                                          D3DFMT_A32B32G32R32F,
                                          D3DPOOL_DEFAULT,
                                          &g_pGpuPreviousPositionTexture,
                                          NULL);
    assert(hResult == S_OK);

    hResult = g_pd3dDevice->CreateTexture(g_clothVertexCountX,
                                          g_clothVertexCountZ,
                                          1,
                                          D3DUSAGE_RENDERTARGET,
                                          D3DFMT_A32B32G32R32F,
                                          D3DPOOL_DEFAULT,
                                          &g_pGpuNextPositionTexture,
                                          NULL);
    assert(hResult == S_OK);

    hResult = g_pd3dDevice->CreateOffscreenPlainSurface(g_clothVertexCountX,
                                                        g_clothVertexCountZ,
                                                        D3DFMT_A32B32G32R32F,
                                                        D3DPOOL_SYSTEMMEM,
                                                        &g_pGpuReadbackSurface,
                                                        NULL);
    assert(hResult == S_OK);

    hResult = g_pd3dDevice->CreateTexture(g_clothVertexCountX,
                                          g_clothVertexCountZ,
                                          1,
                                          0,
                                          D3DFMT_A32B32G32R32F,
                                          D3DPOOL_SYSTEMMEM,
                                          &g_pGpuUploadStagingTexture,
                                          NULL);
    assert(hResult == S_OK);

    UploadGpuClothTextures();
}

void CleanupGpuClothResources()
{
    SAFE_RELEASE(g_pGpuUploadStagingTexture);
    SAFE_RELEASE(g_pGpuReadbackSurface);
    SAFE_RELEASE(g_pGpuNextPositionTexture);
    SAFE_RELEASE(g_pGpuPreviousPositionTexture);
    SAFE_RELEASE(g_pGpuCurrentPositionTexture);
}

void LoadClothMeshForResolution(int vertexCountX, int vertexCountZ, LPCTSTR filename)
{
    g_clothVertexCountX = vertexCountX;
    g_clothVertexCountZ = vertexCountZ;
    g_clothParticleCount = vertexCountX * vertexCountZ;

    CleanupMeshModel(&g_clothModel);
    CleanupGpuClothResources();
    g_clothParticles.clear();
    g_bClothReady = false;

    LoadMeshModel(filename, &g_clothModel);
    InitializeClothSimulation();
    CreateGpuClothResources();
}

void UploadGpuClothTexture(LPDIRECT3DTEXTURE9 pTexture, bool bUsePreviousPosition)
{
    HRESULT hResult = E_FAIL;
    std::vector<GpuClothTexel> texels;
    LPDIRECT3DSURFACE9 pSurface = NULL;

    texels.resize(g_clothParticleCount);

    for (int i = 0; i < g_clothParticleCount; i++)
    {
        D3DXVECTOR3 position = g_clothParticles[i].position;

        if (bUsePreviousPosition)
        {
            position = g_clothParticles[i].previousPosition;
        }

        texels[i].x = position.x;
        texels[i].y = position.y;
        texels[i].z = position.z;
        texels[i].w = 1.0f;
    }

    hResult = pTexture->GetSurfaceLevel(0, &pSurface);
    assert(hResult == S_OK);

    {
        LPDIRECT3DSURFACE9 pStagingSurface = NULL;
        D3DLOCKED_RECT lockedRect { };

        hResult = g_pGpuUploadStagingTexture->GetSurfaceLevel(0, &pStagingSurface);
        assert(hResult == S_OK);

        hResult = pStagingSurface->LockRect(&lockedRect, NULL, 0);
        assert(hResult == S_OK);

        for (int z = 0; z < g_clothVertexCountZ; z++)
        {
            BYTE* pDestRow = (BYTE*)lockedRect.pBits + lockedRect.Pitch * z;
            BYTE* pSrcRow = (BYTE*)texels.data() + sizeof(GpuClothTexel) * g_clothVertexCountX * z;
            memcpy(pDestRow, pSrcRow, sizeof(GpuClothTexel) * g_clothVertexCountX);
        }

        hResult = pStagingSurface->UnlockRect();
        assert(hResult == S_OK);

        SAFE_RELEASE(pStagingSurface);
    }

    hResult = g_pd3dDevice->UpdateTexture(g_pGpuUploadStagingTexture,
                                          pTexture);
    assert(hResult == S_OK);

    SAFE_RELEASE(pSurface);
}

void UploadGpuClothTextures()
{
    if (g_pGpuCurrentPositionTexture == NULL ||
        g_pGpuPreviousPositionTexture == NULL ||
        g_pGpuNextPositionTexture == NULL)
    {
        return;
    }

    UploadGpuClothTexture(g_pGpuCurrentPositionTexture, false);
    UploadGpuClothTexture(g_pGpuPreviousPositionTexture, true);
    UploadGpuClothTexture(g_pGpuNextPositionTexture, false);
}

void UploadGpuClothCurrentTexture()
{
    UploadGpuClothTexture(g_pGpuCurrentPositionTexture, false);
}

bool DownloadGpuClothPositions()
{
    HRESULT hResult = E_FAIL;
    LPDIRECT3DSURFACE9 pSurface = NULL;
    D3DLOCKED_RECT lockedRect { };
    std::vector<D3DXVECTOR3> downloadedPositions;
    D3DXVECTOR3 minPosition(FLT_MAX, FLT_MAX, FLT_MAX);
    D3DXVECTOR3 maxPosition(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    bool bValid = true;

    downloadedPositions.resize(g_clothParticleCount);

    hResult = g_pGpuCurrentPositionTexture->GetSurfaceLevel(0, &pSurface);
    if (FAILED(hResult))
    {
        return false;
    }

    hResult = g_pd3dDevice->GetRenderTargetData(pSurface, g_pGpuReadbackSurface);
    if (FAILED(hResult))
    {
        SAFE_RELEASE(pSurface);
        return false;
    }

    hResult = g_pGpuReadbackSurface->LockRect(&lockedRect, NULL, D3DLOCK_READONLY);
    if (FAILED(hResult))
    {
        SAFE_RELEASE(pSurface);
        return false;
    }

    for (int z = 0; z < g_clothVertexCountZ; z++)
    {
        BYTE* pRow = (BYTE*)lockedRect.pBits + lockedRect.Pitch * z;
        GpuClothTexel* pTexels = (GpuClothTexel*)pRow;

        for (int x = 0; x < g_clothVertexCountX; x++)
        {
            int index = GetClothIndex(x, z);
            D3DXVECTOR3 position(pTexels[x].x, pTexels[x].y, pTexels[x].z);

            if (!IsValidGpuClothPosition(position))
            {
                bValid = false;
            }

            if (position.x < minPosition.x)
            {
                minPosition.x = position.x;
            }

            if (position.y < minPosition.y)
            {
                minPosition.y = position.y;
            }

            if (position.z < minPosition.z)
            {
                minPosition.z = position.z;
            }

            if (position.x > maxPosition.x)
            {
                maxPosition.x = position.x;
            }

            if (position.y > maxPosition.y)
            {
                maxPosition.y = position.y;
            }

            if (position.z > maxPosition.z)
            {
                maxPosition.z = position.z;
            }

            downloadedPositions[index] = position;
        }
    }

    hResult = g_pGpuReadbackSurface->UnlockRect();
    assert(hResult == S_OK);

    SAFE_RELEASE(pSurface);

    if (fabsf(maxPosition.x - minPosition.x) < 0.0001f &&
        fabsf(maxPosition.z - minPosition.z) < 0.0001f)
    {
        bValid = false;
    }

    if (!bValid)
    {
        return false;
    }

    for (int i = 0; i < g_clothParticleCount; i++)
    {
        D3DXVECTOR3 oldPosition = g_clothParticles[i].position;
        g_clothParticles[i].previousPosition = oldPosition;
        g_clothParticles[i].position = downloadedPositions[i];
    }

    return true;
}

bool IsValidGpuClothPosition(const D3DXVECTOR3& position)
{
    if (!_finite(position.x))
    {
        return false;
    }

    if (!_finite(position.y))
    {
        return false;
    }

    if (!_finite(position.z))
    {
        return false;
    }

    if (fabsf(position.x) > 100.0f)
    {
        return false;
    }

    if (fabsf(position.y) > 100.0f)
    {
        return false;
    }

    if (fabsf(position.z) > 100.0f)
    {
        return false;
    }

    return true;
}

void RenderGpuClothPass()
{
    HRESULT hResult = E_FAIL;
    LPDIRECT3DSURFACE9 pOldRenderTarget = NULL;
    LPDIRECT3DSURFACE9 pOldDepthStencil = NULL;
    LPDIRECT3DSURFACE9 pNextSurface = NULL;
    D3DVIEWPORT9 oldViewport { };
    D3DVIEWPORT9 gpuViewport { };

    hResult = g_pd3dDevice->GetRenderTarget(0, &pOldRenderTarget);
    assert(hResult == S_OK);

    hResult = g_pd3dDevice->GetDepthStencilSurface(&pOldDepthStencil);
    assert(hResult == S_OK);

    hResult = g_pd3dDevice->GetViewport(&oldViewport);
    assert(hResult == S_OK);

    hResult = g_pd3dDevice->SetTexture(0, NULL);
    assert(hResult == S_OK);

    hResult = g_pd3dDevice->SetTexture(1, NULL);
    assert(hResult == S_OK);

    hResult = g_pGpuNextPositionTexture->GetSurfaceLevel(0, &pNextSurface);
    assert(hResult == S_OK);

    hResult = g_pd3dDevice->SetRenderTarget(0, pNextSurface);
    assert(hResult == S_OK);

    hResult = g_pd3dDevice->SetDepthStencilSurface(NULL);
    assert(hResult == S_OK);

    gpuViewport.X = 0;
    gpuViewport.Y = 0;
    gpuViewport.Width = g_clothVertexCountX;
    gpuViewport.Height = g_clothVertexCountZ;
    gpuViewport.MinZ = 0.0f;
    gpuViewport.MaxZ = 1.0f;
    hResult = g_pd3dDevice->SetViewport(&gpuViewport);
    assert(hResult == S_OK);

    ScreenVertex vertices[4] =
    {
        { -0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f },
        { (float)g_clothVertexCountX - 0.5f, -0.5f, 0.0f, 1.0f, 1.0f, 0.0f },
        { -0.5f, (float)g_clothVertexCountZ - 0.5f, 0.0f, 1.0f, 0.0f, 1.0f },
        { (float)g_clothVertexCountX - 0.5f, (float)g_clothVertexCountZ - 0.5f, 0.0f, 1.0f, 1.0f, 1.0f },
    };

    hResult = g_pEffect->SetTechnique("GpuClothUpdateTechnique");
    assert(hResult == S_OK);

    hResult = g_pEffect->SetTexture("g_gpuCurrentPositionTexture", g_pGpuCurrentPositionTexture);
    assert(hResult == S_OK);

    hResult = g_pEffect->SetTexture("g_gpuPreviousPositionTexture", g_pGpuPreviousPositionTexture);
    assert(hResult == S_OK);

    hResult = g_pEffect->SetFloat("g_gpuDeltaTime", 1.0f / 60.0f);
    assert(hResult == S_OK);

    hResult = g_pEffect->SetFloat("g_gpuDamping", 0.995f);
    assert(hResult == S_OK);

    hResult = g_pEffect->SetFloat("g_gpuCollisionRadius", CLOTH_COLLISION_RADIUS);
    assert(hResult == S_OK);

    hResult = g_pd3dDevice->BeginScene();
    assert(hResult == S_OK);

    UINT numPass = 0;
    hResult = g_pEffect->Begin(&numPass, 0);
    assert(hResult == S_OK);

    hResult = g_pEffect->BeginPass(0);
    assert(hResult == S_OK);

    hResult = g_pd3dDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    assert(hResult == S_OK);

    hResult = g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,
                                            2,
                                            vertices,
                                            sizeof(ScreenVertex));
    assert(hResult == S_OK);

    hResult = g_pEffect->EndPass();
    assert(hResult == S_OK);

    hResult = g_pEffect->End();
    assert(hResult == S_OK);

    hResult = g_pd3dDevice->EndScene();
    assert(hResult == S_OK);

    hResult = g_pd3dDevice->SetTexture(0, NULL);
    assert(hResult == S_OK);

    hResult = g_pd3dDevice->SetTexture(1, NULL);
    assert(hResult == S_OK);

    hResult = g_pd3dDevice->SetViewport(&oldViewport);
    assert(hResult == S_OK);

    hResult = g_pd3dDevice->SetDepthStencilSurface(pOldDepthStencil);
    assert(hResult == S_OK);

    hResult = g_pd3dDevice->SetRenderTarget(0, pOldRenderTarget);
    assert(hResult == S_OK);

    SAFE_RELEASE(pNextSurface);
    SAFE_RELEASE(pOldDepthStencil);
    SAFE_RELEASE(pOldRenderTarget);
}

void SatisfyClothConstraint(int indexA, int indexB, float restLength)
{
    ClothParticle& particleA = g_clothParticles[indexA];
    ClothParticle& particleB = g_clothParticles[indexB];
    D3DXVECTOR3 delta = particleB.position - particleA.position;
    float length = D3DXVec3Length(&delta);

    if (length <= 0.0001f)
    {
        return;
    }

    D3DXVECTOR3 correction = delta * ((length - restLength) / length);

    if (!particleA.bFixed && !particleB.bFixed)
    {
        particleA.position += correction * 0.5f;
        particleB.position -= correction * 0.5f;
    }
    else if (!particleA.bFixed)
    {
        particleA.position += correction;
    }
    else if (!particleB.bFixed)
    {
        particleB.position -= correction;
    }
}

void ApplySphereCollision(D3DXVECTOR3* pPosition)
{
    float distance = D3DXVec3Length(pPosition);

    if (distance >= CLOTH_COLLISION_RADIUS)
    {
        return;
    }

    if (distance <= 0.0001f)
    {
        pPosition->x = 0.0f;
        pPosition->y = CLOTH_COLLISION_RADIUS;
        pPosition->z = 0.0f;
        return;
    }

    D3DXVECTOR3 normal = *pPosition / distance;
    *pPosition = normal * CLOTH_COLLISION_RADIUS;
}

void UpdateClothNormals()
{
    if (IsOpenMPSimulationEnabled())
    {
        int particleCount = (int)g_clothParticles.size();

#pragma omp parallel for
        for (int i = 0; i < particleCount; i++)
        {
            g_clothParticles[i].normal = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
        }
    }
    else
    {
        for (auto& particle : g_clothParticles)
        {
            particle.normal = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
        }
    }

    for (int z = 0; z < g_clothVertexCountZ - 1; z++)
    {
        for (int x = 0; x < g_clothVertexCountX - 1; x++)
        {
            int indexA = GetClothIndex(x, z);
            int indexB = GetClothIndex(x + 1, z);
            int indexC = GetClothIndex(x, z + 1);
            int indexD = GetClothIndex(x + 1, z + 1);
            D3DXVECTOR3 normal;
            D3DXVECTOR3 edgeA;
            D3DXVECTOR3 edgeB;

            edgeA = g_clothParticles[indexC].position - g_clothParticles[indexA].position;
            edgeB = g_clothParticles[indexB].position - g_clothParticles[indexA].position;
            D3DXVec3Cross(&normal, &edgeA, &edgeB);
            g_clothParticles[indexA].normal += normal;
            g_clothParticles[indexC].normal += normal;
            g_clothParticles[indexB].normal += normal;

            edgeA = g_clothParticles[indexC].position - g_clothParticles[indexB].position;
            edgeB = g_clothParticles[indexD].position - g_clothParticles[indexB].position;
            D3DXVec3Cross(&normal, &edgeA, &edgeB);
            g_clothParticles[indexB].normal += normal;
            g_clothParticles[indexC].normal += normal;
            g_clothParticles[indexD].normal += normal;
        }
    }

    if (IsOpenMPSimulationEnabled())
    {
        int particleCount = (int)g_clothParticles.size();

#pragma omp parallel for
        for (int i = 0; i < particleCount; i++)
        {
            float length = D3DXVec3Length(&g_clothParticles[i].normal);

            if (length <= 0.0001f)
            {
                g_clothParticles[i].normal = D3DXVECTOR3(0.0f, 1.0f, 0.0f);
            }
            else
            {
                D3DXVec3Normalize(&g_clothParticles[i].normal, &g_clothParticles[i].normal);
            }
        }
    }
    else
    {
        for (auto& particle : g_clothParticles)
        {
            float length = D3DXVec3Length(&particle.normal);

            if (length <= 0.0001f)
            {
                particle.normal = D3DXVECTOR3(0.0f, 1.0f, 0.0f);
            }
            else
            {
                D3DXVec3Normalize(&particle.normal, &particle.normal);
            }
        }
    }
}

void WriteClothMeshVertices()
{
    HRESULT hResult = E_FAIL;
    BYTE* pVertices = NULL;

    hResult = g_clothModel.pMesh->LockVertexBuffer(0, (void**)&pVertices);
    assert(hResult == S_OK);

    int vertexCount = (int)g_clothModel.pMesh->GetNumVertices();

    if (IsOpenMPSimulationEnabled())
    {
#pragma omp parallel for
        for (int i = 0; i < vertexCount; i++)
        {
            BYTE* pVertex = pVertices + g_clothVertexStride * i;
            D3DXVECTOR3* pPosition = (D3DXVECTOR3*)(pVertex + g_clothPositionOffset);
            D3DXVECTOR3* pNormal = (D3DXVECTOR3*)(pVertex + g_clothNormalOffset);

            *pPosition = g_clothParticles[i].position;
            *pNormal = g_clothParticles[i].normal;
        }
    }
    else
    {
        for (int i = 0; i < vertexCount; i++)
        {
            BYTE* pVertex = pVertices + g_clothVertexStride * i;
            D3DXVECTOR3* pPosition = (D3DXVECTOR3*)(pVertex + g_clothPositionOffset);
            D3DXVECTOR3* pNormal = (D3DXVECTOR3*)(pVertex + g_clothNormalOffset);

            *pPosition = g_clothParticles[i].position;
            *pNormal = g_clothParticles[i].normal;
        }
    }

    hResult = g_clothModel.pMesh->UnlockVertexBuffer();
    assert(hResult == S_OK);
}

int GetClothIndex(int x, int z)
{
    return z * g_clothVertexCountX + x;
}

bool IsOpenMPSimulationEnabled()
{
    return g_simulationMode == SIMULATION_MODE_CPU_OPENMP;
}

bool IsGpuSimulationEnabled()
{
    return g_simulationMode == SIMULATION_MODE_GPU;
}

LPCTSTR GetSimulationModeText()
{
    if (g_simulationMode == SIMULATION_MODE_GPU)
    {
        return _T("GPU");
    }

    if (g_simulationMode == SIMULATION_MODE_CPU_OPENMP)
    {
        return _T("CPU_OPENMP");
    }

    return _T("CPU");
}

void UpdateSimulationModeFromCombo()
{
    if (g_hSimulationCombo == NULL)
    {
        return;
    }

    LRESULT selectedIndex = SendMessage(g_hSimulationCombo, CB_GETCURSEL, 0, 0);

    if (selectedIndex == SIMULATION_MODE_CPU_OPENMP)
    {
        g_simulationMode = SIMULATION_MODE_CPU_OPENMP;
    }
    else if (selectedIndex == SIMULATION_MODE_GPU)
    {
        g_simulationMode = SIMULATION_MODE_GPU;
    }
    else
    {
        g_simulationMode = SIMULATION_MODE_CPU;
    }

    if (g_simulationMode != g_previousSimulationMode)
    {
        InitializeClothSimulation();
    }

    g_previousSimulationMode = g_simulationMode;
}

void BuildCameraViewMatrix(D3DXMATRIX* pView)
{
    D3DXVECTOR3 target(0.0f, 0.0f, 0.0f);
    D3DXVECTOR3 up(0.0f, 1.0f, 0.0f);
    float cosPitch = cosf(g_cameraPitch);
    D3DXVECTOR3 eye;

    eye.x = g_cameraDistance * sinf(g_cameraYaw) * cosPitch;
    eye.y = g_cameraDistance * sinf(g_cameraPitch);
    eye.z = -g_cameraDistance * cosf(g_cameraYaw) * cosPitch;

    D3DXMatrixLookAtLH(pView, &eye, &target, &up);
}

void UpdateFps()
{
    LARGE_INTEGER currentCounter { };
    QueryPerformanceCounter(&currentCounter);

    double elapsedSeconds = (double)(currentCounter.QuadPart - g_previousPerformanceCounter.QuadPart) /
                            (double)g_performanceFrequency.QuadPart;
    g_previousPerformanceCounter = currentCounter;

    if (elapsedSeconds <= 0.0)
    {
        return;
    }

    float instantFps = (float)(1.0 / elapsedSeconds);

    if (g_currentFps <= 0.0f)
    {
        g_currentFps = instantFps;
    }
    else
    {
        g_currentFps = g_currentFps * 0.9f + instantFps * 0.1f;
    }
}

void CreateSettingsWindow(HINSTANCE hInstance)
{
    WNDCLASSEX wc { };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = SettingsMsgProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = _T("SettingsWindow");
    wc.hIconSm = NULL;

    RegisterClassEx(&wc);

    g_hSettingsWnd = CreateWindow(_T("SettingsWindow"),
                                  _T("設定"),
                                  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                  CW_USEDEFAULT,
                                  CW_USEDEFAULT,
                                  260,
                                  180,
                                  NULL,
                                  NULL,
                                  hInstance,
                                  NULL);

    assert(g_hSettingsWnd != NULL);

    HWND hLabel = CreateWindow(_T("STATIC"),
                               _T("Simulation"),
                               WS_CHILD | WS_VISIBLE,
                               16,
                               18,
                               100,
                               22,
                               g_hSettingsWnd,
                               NULL,
                               hInstance,
                               NULL);
    assert(hLabel != NULL);

    g_hSimulationCombo = CreateWindow(_T("COMBOBOX"),
                                      NULL,
                                      WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                      112,
                                      14,
                                      120,
                                      120,
                                      g_hSettingsWnd,
                                      (HMENU)(INT_PTR)SETTINGS_COMBO_ID,
                                      hInstance,
                                      NULL);
    assert(g_hSimulationCombo != NULL);

    SendMessage(g_hSimulationCombo, CB_ADDSTRING, 0, (LPARAM)_T("CPU"));
    SendMessage(g_hSimulationCombo, CB_ADDSTRING, 0, (LPARAM)_T("CPU_OPENMP"));
    SendMessage(g_hSimulationCombo, CB_ADDSTRING, 0, (LPARAM)_T("GPU"));
    SendMessage(g_hSimulationCombo, CB_SETCURSEL, SIMULATION_MODE_CPU_OPENMP, 0);
    UpdateSimulationModeFromCombo();

    HWND hClothLabel = CreateWindow(_T("STATIC"),
                                    _T("Cloth"),
                                    WS_CHILD | WS_VISIBLE,
                                    16,
                                    54,
                                    100,
                                    22,
                                    g_hSettingsWnd,
                                    NULL,
                                    hInstance,
                                    NULL);
    assert(hClothLabel != NULL);

    g_hClothCombo = CreateWindow(_T("COMBOBOX"),
                                 NULL,
                                 WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                 112,
                                 50,
                                 120,
                                 120,
                                 g_hSettingsWnd,
                                 (HMENU)(INT_PTR)SETTINGS_CLOTH_COMBO_ID,
                                 hInstance,
                                 NULL);
    assert(g_hClothCombo != NULL);

    SendMessage(g_hClothCombo, CB_ADDSTRING, 0, (LPARAM)_T("16x16"));
    SendMessage(g_hClothCombo, CB_ADDSTRING, 0, (LPARAM)_T("128x128"));
    SendMessage(g_hClothCombo, CB_SETCURSEL, 0, 0);

    HWND hResetButton = CreateWindow(_T("BUTTON"),
                                     _T("リセット"),
                                     WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                     112,
                                     86,
                                     120,
                                     28,
                                     g_hSettingsWnd,
                                     (HMENU)(INT_PTR)SETTINGS_RESET_BUTTON_ID,
                                     hInstance,
                                     NULL);
    assert(hResetButton != NULL);

    ShowWindow(g_hSettingsWnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hSettingsWnd);
}

LRESULT WINAPI MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_LBUTTONDOWN:
    {
        g_bCameraDragging = true;
        g_lastMousePosition.x = GET_X_LPARAM(lParam);
        g_lastMousePosition.y = GET_Y_LPARAM(lParam);
        SetCapture(hWnd);
        return 0;
    }
    case WM_LBUTTONUP:
    {
        g_bCameraDragging = false;
        ReleaseCapture();
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        if (g_bCameraDragging)
        {
            POINT currentPosition;
            currentPosition.x = GET_X_LPARAM(lParam);
            currentPosition.y = GET_Y_LPARAM(lParam);

            int deltaX = currentPosition.x - g_lastMousePosition.x;
            int deltaY = currentPosition.y - g_lastMousePosition.y;
            g_cameraYaw += (float)deltaX * 0.01f;
            g_cameraPitch += (float)deltaY * 0.01f;

            if (g_cameraPitch < CAMERA_MIN_PITCH)
            {
                g_cameraPitch = CAMERA_MIN_PITCH;
            }

            if (g_cameraPitch > CAMERA_MAX_PITCH)
            {
                g_cameraPitch = CAMERA_MAX_PITCH;
            }

            g_lastMousePosition = currentPosition;
        }

        return 0;
    }
    case WM_MOUSEWHEEL:
    {
        int wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        g_cameraDistance -= (float)wheelDelta * 0.01f;

        if (g_cameraDistance < CAMERA_MIN_DISTANCE)
        {
            g_cameraDistance = CAMERA_MIN_DISTANCE;
        }

        if (g_cameraDistance > CAMERA_MAX_DISTANCE)
        {
            g_cameraDistance = CAMERA_MAX_DISTANCE;
        }

        return 0;
    }
    case WM_DESTROY:
    {
        PostQuitMessage(0);
        g_bClose = true;
        return 0;
    }
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

LRESULT WINAPI SettingsMsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
    {
        int controlId = LOWORD(wParam);
        int notification = HIWORD(wParam);

        if (controlId == SETTINGS_COMBO_ID && notification == CBN_SELCHANGE)
        {
            UpdateSimulationModeFromCombo();
            return 0;
        }

        if (controlId == SETTINGS_CLOTH_COMBO_ID && notification == CBN_SELCHANGE)
        {
            LRESULT selectedIndex = SendMessage(g_hClothCombo, CB_GETCURSEL, 0, 0);

            if (selectedIndex == 0)
            {
                LoadClothMeshForResolution(16, 16, _T("cloth16x16.x"));
            }
            else
            {
                LoadClothMeshForResolution(128, 128, _T("cloth128x128.x"));
            }

            return 0;
        }

        if (controlId == SETTINGS_RESET_BUTTON_ID && notification == BN_CLICKED)
        {
            InitializeClothSimulation();
            return 0;
        }

        break;
    }
    case WM_CLOSE:
    {
        ShowWindow(hWnd, SW_HIDE);
        return 0;
    }
    case WM_DESTROY:
    {
        if (hWnd == g_hSettingsWnd)
        {
            g_hSettingsWnd = NULL;
            g_hSimulationCombo = NULL;
            g_hClothCombo = NULL;
        }

        return 0;
    }
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

