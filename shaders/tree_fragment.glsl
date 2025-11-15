// GLSL 1.20 (OpenGL 2.1)
uniform sampler2D uLeafTexture;
uniform vec3 uLightDir;
uniform float uTime;

varying vec3 Normal;
varying vec3 Position;
varying vec2 TexCoord;

void main()
{
    // Texture lookup
    vec4 texColor = texture2D(uLeafTexture, TexCoord);

    // Discard transparent parts of the leaf texture (important if using a leaf sprite sheet)
    if (texColor.a < 0.1)
        discard;

    // Simple fixed-function style lighting calculation
    vec3 N = normalize(Normal);
    vec3 L = normalize(uLightDir);

    // Light components
    vec3 ambient = vec3(0.2); // Base ambient light
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * vec3(0.8); // Diffuse light

    // Optional: add a subtle color shift based on the wind (for extra life)
    vec3 flutterColorOffset = vec3(0.05) * sin(uTime * 3.0);

    vec3 finalLight = ambient + diffuse + flutterColorOffset;

    // Apply lighting to the texture color
    gl_FragColor = texColor * vec4(finalLight, 1.0);
}