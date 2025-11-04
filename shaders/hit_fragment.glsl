#version 120

varying vec3 vNormal;
varying vec3 vPosition;
uniform float uTime;

void main()
{
    // Base color (red-orange for targets)
    vec3 baseColor = vec3(1.0, 0.4, 0.2);
    
    // Calculate glow intensity based on time (fades from 1.0 to 0.0)
    float glow = 1.0 - uTime;
    glow = glow * glow; // Square for faster falloff
    
    // Simple lighting calculation
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    vec3 normal = normalize(vNormal);
    float diffuse = max(dot(normal, lightDir), 0.0);
    
    // Add ambient light so it's never completely dark
    float ambient = 0.4;
    float lighting = ambient + diffuse * 0.6;
    
    // Combine base color with lighting
    vec3 litColor = baseColor * lighting;
    
    // Add bright glow when hit
    vec3 glowColor = vec3(1.0, 0.9, 0.3); // Bright yellow-white
    vec3 finalColor = litColor + glowColor * glow * 1.5;
    
    gl_FragColor = vec4(finalColor, 1.0);
}