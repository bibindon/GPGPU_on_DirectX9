float4x4 g_matWorldViewProj;
float4 g_lightNormal = { 0.3f, 1.0f, 0.5f, 0.0f };

texture texture1;
sampler textureSampler = sampler_state {
    Texture = (texture1);
    MipFilter = LINEAR;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
};

texture g_gpuCurrentPositionTexture;
sampler g_gpuCurrentPositionSampler = sampler_state {
    Texture = (g_gpuCurrentPositionTexture);
    AddressU = CLAMP;
    AddressV = CLAMP;
    MipFilter = POINT;
    MinFilter = POINT;
    MagFilter = POINT;
};

texture g_gpuPreviousPositionTexture;
sampler g_gpuPreviousPositionSampler = sampler_state {
    Texture = (g_gpuPreviousPositionTexture);
    AddressU = CLAMP;
    AddressV = CLAMP;
    MipFilter = POINT;
    MinFilter = POINT;
    MagFilter = POINT;
};

float g_gpuDeltaTime = 0.0166667f;
float g_gpuDamping = 0.995f;
float g_gpuCollisionRadius = 1.1f;

void VertexShader1(in  float4 inPosition  : POSITION,
                   in  float4 inNormal    : NORMAL0,
                   in  float4 inTexCood   : TEXCOORD0,

                   out float4 outPosition : POSITION,
                   out float4 outDiffuse  : COLOR0,
                   out float4 outTexCood  : TEXCOORD0)
{
    outPosition = mul(inPosition, g_matWorldViewProj);

    float lightIntensity = dot(inNormal, g_lightNormal);
    outDiffuse.rgb = max(0, lightIntensity);
    outDiffuse.a = 1.0f;

    outTexCood = inTexCood;
}

void PixelShader1(in float4 inScreenColor : COLOR0,
                  in float2 inTexCood     : TEXCOORD0,

                  out float4 outColor     : COLOR)
{
    float4 workColor = (float4)0;
    workColor = tex2D(textureSampler, inTexCood);
    outColor = inScreenColor * workColor + 0.3f;
}

void GpuClothUpdatePixelShader(in float2 inTexCood : TEXCOORD0,
                               out float4 outColor : COLOR)
{
    float3 currentPosition = tex2D(g_gpuCurrentPositionSampler, inTexCood).xyz;
    float3 previousPosition = tex2D(g_gpuPreviousPositionSampler, inTexCood).xyz;
    float3 gravity = float3(0.0f, -9.8f, 0.0f);
    float3 velocity = (currentPosition - previousPosition) * g_gpuDamping;
    float3 nextPosition = currentPosition + velocity + gravity * g_gpuDeltaTime * g_gpuDeltaTime;
    float distanceFromSphereCenter = length(nextPosition);

    if (distanceFromSphereCenter < g_gpuCollisionRadius)
    {
        if (distanceFromSphereCenter <= 0.0001f)
        {
            nextPosition = float3(0.0f, g_gpuCollisionRadius, 0.0f);
        }
        else
        {
            nextPosition = nextPosition / distanceFromSphereCenter * g_gpuCollisionRadius;
        }
    }

    outColor = float4(nextPosition, 1.0f);
}

technique Technique1
{
   pass Pass1
   {
      VertexShader = compile vs_2_0 VertexShader1();
      PixelShader = compile ps_2_0 PixelShader1();
   }
}

technique GpuClothUpdateTechnique
{
   pass Pass1
   {
      VertexShader = NULL;
      PixelShader = compile ps_2_0 GpuClothUpdatePixelShader();
   }
}
