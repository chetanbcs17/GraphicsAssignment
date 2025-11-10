#version 120

varying vec2 vTexCoord;

uniform sampler2D uSmokeTex;
uniform vec3 uSmokeColor;
uniform float uTime;

void main() {
    // Calculate distance from center for radial fade
    vec2 center = vec2(0.5, 0.5);
    float dist = length(vTexCoord - center);
    
    // Create soft circular gradient
    float radialFade = 1.0 - smoothstep(0.2, 0.5, dist);
    
    // Add some noise-like variation using texture coordinates
    float noise = sin(vTexCoord.x * 10.0) * cos(vTexCoord.y * 10.0) * 0.1 + 0.9;
    
    vec4 texColor = texture2D(uSmokeTex, vTexCoord);
    
    // Fade out over time
    float timeFade = 1.0 - (uTime / 10.0);
    timeFade = clamp(timeFade, 0.0, 1.0);
    
    // Combine all fades for soft, billowy appearance
    float alpha = radialFade * timeFade * noise * 0.7;
    
    // Slightly vary color for depth
    vec3 finalColor = uSmokeColor * (0.9 + noise * 0.1);
    
    gl_FragColor = vec4(finalColor, alpha);
}