// GLSL 1.20 (OpenGL 2.1)
varying vec3 Normal;
varying vec3 Position;
varying vec2 TexCoord;

uniform float uTime;
uniform vec3 uLightDir; // Simple directional light

void main()
{
    Normal = gl_NormalMatrix * gl_Normal;
    TexCoord = gl_MultiTexCoord0.xy;

    vec4 vertex = gl_Vertex;

    // --- Fluttering Logic ---
    // This is applied to the leaf cone vertices.
    // Sway is proportional to height (vertex.y).
    float canopyHeight = 10.0;
    float windStrength = 0.5; // Max displacement in world units
    float windFrequency = 1.0;
    vec3 windDir = normalize(vec3(0.5, 0.0, 1.0)); // Wind direction (e.g., diagonal)

    // Calculate displacement factor: stronger higher up
    float swayFactor = (vertex.y / canopyHeight) * windStrength;

    // Use a sine wave (based on time and position) to create the oscillation
    float displacement = sin(uTime * windFrequency + vertex.x * 0.1 + vertex.z * 0.1) * swayFactor;

    // Apply displacement to the horizontal (XZ) plane in the direction of the wind
    vertex.xz += windDir.xz * displacement;

    // Update the final position
    gl_Position = gl_ModelViewProjectionMatrix * vertex;

    // For lighting in fragment shader
    Position = (gl_ModelViewMatrix * vertex).xyz;
}