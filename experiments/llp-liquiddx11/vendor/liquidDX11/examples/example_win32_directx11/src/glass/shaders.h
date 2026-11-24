#pragma once

namespace Glass {

inline constexpr const char* kBlurHLSL = R"HLSL(
cbuffer BlurCB : register(b0)
{
    float2 texel;
    float  spread;
    float  _pad;
};

Texture2D    Src        : register(t0);
SamplerState LinearClamp : register(s0);

struct VSO { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSO VSFull(uint id : SV_VertexID)
{
    VSO o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.uv  = uv;
    o.pos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    return o;
}

float4 PSDown(VSO i) : SV_Target
{
    float2 h = texel * (0.5 * spread);
    float4 s = Src.Sample(LinearClamp, i.uv) * 4.0;
    s += Src.Sample(LinearClamp, i.uv + float2(-h.x, -h.y));
    s += Src.Sample(LinearClamp, i.uv + float2( h.x, -h.y));
    s += Src.Sample(LinearClamp, i.uv + float2(-h.x,  h.y));
    s += Src.Sample(LinearClamp, i.uv + float2( h.x,  h.y));
    return s / 8.0;
}

float4 PSUp(VSO i) : SV_Target
{
    float2 h = texel * (0.5 * spread);
    float4 s = Src.Sample(LinearClamp, i.uv + float2(-h.x * 2.0, 0.0));
    s += Src.Sample(LinearClamp, i.uv + float2(-h.x,  h.y)) * 2.0;
    s += Src.Sample(LinearClamp, i.uv + float2( 0.0,  h.y * 2.0));
    s += Src.Sample(LinearClamp, i.uv + float2( h.x,  h.y)) * 2.0;
    s += Src.Sample(LinearClamp, i.uv + float2( h.x * 2.0, 0.0));
    s += Src.Sample(LinearClamp, i.uv + float2( h.x, -h.y)) * 2.0;
    s += Src.Sample(LinearClamp, i.uv + float2( 0.0, -h.y * 2.0));
    s += Src.Sample(LinearClamp, i.uv + float2(-h.x, -h.y)) * 2.0;
    return s / 12.0;
}
)HLSL";

inline constexpr const char* kGlassHLSL = R"HLSL(
cbuffer GlassCB : register(b0)
{
    float2 window_size;
    float2 desktop_size;
    float2 window_origin;
    float2 light_dir;

    float2 widget_center;
    float2 widget_half_size;

    float  corner_radius;
    float  fade;
    float  blur_mix;
    float  saturation;

    float4 tint_color;

    float  tint_opacity;
    float  brightness;
    float  contrast;
    float  grain;

    float  refr_strength;
    float  refr_band;
    float  highlight;
    float  inner_shadow;

    float  border_intensity;
    float  border_width;
    float  shadow_strength;
    float  shadow_radius;

    float2 shadow_offset;
    float  time;
    float  quad_pad;

    float2 tilt;
    float  sheen;
    float  chroma;

    float2 cursor;
    float2 _pad_cur;

    float2 blob1_center;
    float2 blob1_half;
    float2 blob2_center;
    float2 blob2_half;
    float  blob_k;
    float3 _pad_blob;

    float4 edge_p0;
    float4 edge_p1;
    float4 edge_p2;
    float4 edge_p3;
    float4 edge_p4;
    float4 edge_p5;
    float4 edge_p6;
    float4 edge_p7;
    float4 edge_p8;
    float4 edge_p9;
    float4 edge_p10;
    float4 edge_p11;
};

Texture2D    BlurHeavy  : register(t0);
Texture2D    BlurSoft   : register(t1);
SamplerState LinearClamp : register(s0);

struct VSOut {
    float4 pos    : SV_Position;
    float2 local  : TEXCOORD0;
    float2 client : TEXCOORD1;
};

VSOut VS_Glass(uint vid : SV_VertexID)
{
    static const float2 corners[6] = {
        float2(-1,-1), float2( 1,-1), float2(-1, 1),
        float2(-1, 1), float2( 1,-1), float2( 1, 1)
    };
    float2 sgn   = corners[vid];
    float2 local = sgn * (widget_half_size + quad_pad);
    float2 client = widget_center + local;

    float2 ndc;
    ndc.x = (client.x / window_size.x) * 2.0 - 1.0;
    ndc.y = 1.0 - (client.y / window_size.y) * 2.0;

    VSOut o;
    o.pos    = float4(ndc, 0, 1);
    o.local  = local;
    o.client = client;
    return o;
}

float sdRoundedRect(float2 p, float2 hs, float r)
{
    float2 q  = abs(p) - hs + r;
    float2 qm = max(q, 0.0);

    float  n  = max(edge_p8.x, 2.0);
    float  corner = (n <= 2.001) ? length(qm)
                                 : pow(pow(qm.x, n) + pow(qm.y, n), 1.0 / n);
    return min(max(q.x, q.y), 0.0) + corner - r;
}

float hash21(float2 p)
{
    p = frac(p * float2(123.34, 345.45));
    p += dot(p, p + 34.345);
    return frac(p.x * p.y);
}

float3 sampleBackdrop(float2 uv)
{
    float3 h = BlurHeavy.SampleLevel(LinearClamp, uv, 0).rgb;
    float3 s = BlurSoft .SampleLevel(LinearClamp, uv, 0).rgb;
    return lerp(s, h, blur_mix);
}

float3 sampleBackdropFrost(float2 uv, float frost)
{
    float3 h = BlurHeavy.SampleLevel(LinearClamp, uv, 0).rgb;
    float3 s = BlurSoft .SampleLevel(LinearClamp, uv, 0).rgb;
    return lerp(s, h, saturate(blur_mix + frost));
}

float sdScene(float2 p)
{
    if (blob_k > 0.001)
    {
        float dA = sdRoundedRect(p - (blob1_center - widget_center), blob1_half, corner_radius);
        float dB = sdRoundedRect(p - (blob2_center - widget_center), blob2_half, corner_radius);
        float h  = saturate(0.5 + 0.5 * (dB - dA) / blob_k);
        return lerp(dB, dA, h) - blob_k * h * (1.0 - h);
    }
    return sdRoundedRect(p, widget_half_size, corner_radius);
}

float DirLight(float2 L, float2 nrm, float3 N3, float2 np, float fresR, float band)
{
    float front_amt = edge_p1.y, back_amt = edge_p1.z;
    float fSpread   = edge_p4.x, bSpread  = edge_p4.y;
    float specExp   = edge_p0.w, specAmt  = edge_p1.x;
    float lightH    = edge_p2.y, sheenAmt = edge_p3.y;

    float l  = dot(nrm, L);

    float fr = pow(max(0.0,  l), max(1.2, 2.0 / fSpread));
    float bk = pow(max(0.0, -l), max(1.2, 2.0 / bSpread));
    float c  = fresR * highlight * (fr * front_amt + bk * back_amt);

    float3 Lp = normalize(float3(L * 0.9, lightH));
    float3 Hh = normalize(Lp + float3(0.0, 0.0, 1.0));
    c += pow(saturate(dot(N3, Hh)), specExp) * highlight * specAmt;

    float tp = saturate(0.5 + 0.62 * dot(np, L)); tp *= tp;
    float ur = pow(saturate(dot(nrm, L)), 2.0) * band;
    c += (tp * 0.11 + ur * 0.20) * sheen * sheenAmt;
    return c;
}
)HLSL" R"HLSL(
float4 PS_Glass(VSOut i) : SV_Target
{
    float2 hs = widget_half_size;
    float  d  = sdScene(i.local);
    float  aa = max(fwidth(d), 0.75);

    // Smooth exponential floating drop shadow outside the glass silhouette.
    float dS = sdScene(i.local - shadow_offset);
    float shadow_a = 0.0;
    if (dS > -2.0 && shadow_strength > 0.001)
    {
        float sd = max(dS, 0.0);
        float falloff = exp(-sd / max(shadow_radius * 0.36, 1.0));
        shadow_a = falloff * falloff * shadow_strength;
    }

    if (d > shadow_radius + 4.0 && shadow_a <= 0.002)
        discard;

    float shape = 1.0 - smoothstep(-aa, aa, d);
    float inward = max(0.0, -d);

    // Outward SDF normal for convex meniscus refraction and specular rim lighting.
    const float e = 1.25;
    float dx = sdScene(i.local + float2(e, 0)) - sdScene(i.local - float2(e, 0));
    float dy = sdScene(i.local + float2(0, e)) - sdScene(i.local - float2(0, e));
    float2 nrm = normalize(float2(dx, dy) + 1e-5);

    // Convex liquid-glass meniscus profile: 0 distortion in flat center,
    // smooth inward optical refraction and subtle chromatic dispersion at curved rim.
    float bezelW    = clamp(refr_band, 12.0, max(12.0, min(hs.x, hs.y) * 0.46));
    float t         = saturate(inward / bezelW);
    float edgeCurve = 1.0 - t;
    float meniscus  = edgeCurve * edgeCurve * (3.0 - 2.0 * edgeCurve);
    float steep     = edgeCurve * edgeCurve * edgeCurve;

    float2 refr_px  = -nrm * meniscus * refr_strength;
    float2 base_uv  = clamp((i.client + window_origin + refr_px) / desktop_size, float2(0.001, 0.001), float2(0.999, 0.999));
    float2 cdir     = (-nrm * steep * chroma) / desktop_size;

    // Ultra-clear optical crystal glass: razor-sharp wet rim, virtually zero milky haze.
    float localBlur = saturate(blur_mix * (1.0 - 0.85 * meniscus));
    float3 B;
    B.r = lerp(BlurSoft.SampleLevel(LinearClamp, clamp(base_uv + cdir, 0.001, 0.999), 0).rgb,
               BlurHeavy.SampleLevel(LinearClamp, clamp(base_uv + cdir, 0.001, 0.999), 0).rgb, localBlur).r;
    B.g = lerp(BlurSoft.SampleLevel(LinearClamp, base_uv, 0).rgb,
               BlurHeavy.SampleLevel(LinearClamp, base_uv, 0).rgb, localBlur).g;
    B.b = lerp(BlurSoft.SampleLevel(LinearClamp, clamp(base_uv - cdir, 0.001, 0.999), 0).rgb,
               BlurHeavy.SampleLevel(LinearClamp, clamp(base_uv - cdir, 0.001, 0.999), 0).rgb, localBlur).b;

    // Apple vibrancy: preserve rich luminous color glow through the glass.
    float lum = dot(B, float3(0.2126, 0.7152, 0.0722));
    B = lerp(float3(lum, lum, lum), B, saturation + meniscus * 0.28);
    B = max((B - 0.5) * contrast + 0.5 + brightness, 0.0);

    // Pure crystal-clear Liquid Glass tint: no artificial milky white fog over dark windows.
    float opEff = saturate(tint_opacity);
    float3 glass = lerp(B, tint_color.rgb, opEff);

    // Subtle micro-grain for optical glass texture.
    float g = hash21(i.client + float2(time * 0.5, time * 0.3)) - 0.5;
    glass += g * grain * 0.25;

    // Iconic macOS 26 dual-angle Fresnel specular rim + inner caustic + soft sky sheen.
    float2 np  = i.local / max(hs, float2(1.0, 1.0));
    float2 Ld1 = normalize(light_dir  + tilt * 0.35);
    float2 Ld2 = normalize(edge_p5.xy + tilt * 0.35);

    float outerRim = exp(-inward * 0.82);
    float dirSpec  = pow(saturate(dot(nrm, Ld1)), 2.2) * edge_p5.z
                   + pow(saturate(dot(nrm, Ld2)), 2.8) * edge_p5.w;

    float3 rimColor = float3(0.95, 0.98, 1.00);
    glass += rimColor * outerRim * (border_intensity + dirSpec * highlight);

    // Subtle inner lens caustic band right inside the curved rim.
    float caustic = smoothstep(0.0, 2.5, inward) * exp(-inward * 0.14) * (0.04 + 0.13 * dirSpec);
    glass += rimColor * caustic;

    // Soft top-left diagonal glass sheen across the surface.
    float sky = saturate(0.5 + 0.5 * dot(np, Ld1));
    glass += rimColor * (sky * sky * sheen * (0.18 + 0.65 * edgeCurve));

    // Subtle interactive pointer light kiss along the glass edge.
    float ecCurSize = max(edge_p2.w, 1.0);
    float ecCurGlow = edge_p3.x;
    float cdist = length(i.client - cursor);
    float cspec = exp(-cdist * cdist / (ecCurSize * ecCurSize));
    glass += rimColor * cspec * ecCurGlow * (0.04 + 0.22 * outerRim);

    glass = saturate(glass);

    float a    = shape * fade;
    float shA  = shadow_a * fade * (1.0 - a);
    float outA = a + shA;
    return float4(glass * a, outA);
}
)HLSL";

}
