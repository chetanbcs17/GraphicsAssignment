#version 120

uniform sampler2D uTexture;
uniform float uTime;

varying vec2 vTexCoord;
varying float vAlpha;

void main()
{
=
    vec4 texColor = texture2D(uTexture, vTexCoord);
    
    vec4 glassColor = vec4(0.7, 0.85, 0.95, 0.6);
    vec4 color = mix(texColor, glassColor, 0.3);
    
    float edge = abs(0.5 - vTexCoord.x) + abs(0.5 - vTexCoord.y);
    edge = 1.0 - smoothstep(0.3, 0.8, edge);
    color.rgb += edge * 0.3;

    color.a *= vAlpha;
    
    float sparkle = fract(sin(dot(vTexCoord, vec2(12.9898, 78.233))) * 43758.5453);
    if (sparkle > 0.95)
        color.rgb += vec3(0.5) * vAlpha;
    
    gl_FragColor = color;
}