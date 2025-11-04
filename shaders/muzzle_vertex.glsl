#version 120

varying vec2 vTexCoord;
varying vec3 vPosition;

void main()
{
    vTexCoord = gl_MultiTexCoord0.xy;
    vPosition = gl_Vertex.xyz;
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}