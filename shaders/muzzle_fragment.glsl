#version 120

uniform float uTime;
uniform float uIntensity;
uniform vec3 uFlashColor;

varying vec2 vTexCoord;
varying vec3 vPosition;

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main()
{
    vec2 center = vec2(0.5, 0.5);
    float dist = length(vTexCoord - center);
    
    float radial = 1.0 - smoothstep(0.0, 0.5, dist);
    
    float n = noise(vTexCoord * 20.0 + uTime * 10.0);
    n = mix(0.7, 1.0, n);
    
    float flicker = 0.8 + 0.2 * sin(uTime * 50.0);
    
    float fade = 1.0 - uTime;
    fade = fade * fade;
    
    float alpha = radial * n * flicker * fade * uIntensity;
    
    vec3 hotColor = vec3(1.5, 1.4, 1.2);
    vec3 warmColor = vec3(1.0, 0.5, 0.1);
    vec3 color = mix(hotColor, warmColor, dist * 2.0);
    
    color = color * uFlashColor;
    
    float centerGlow = 1.0 - smoothstep(0.0, 0.2, dist);
    color += centerGlow * vec3(0.5, 0.4, 0.3) * fade;
    
    gl_FragColor = vec4(color, alpha);
}