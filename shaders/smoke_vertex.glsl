#version 120

attribute vec2 aPosition;
attribute vec2 aTexCoord;

varying vec2 vTexCoord;

void main() {
    vTexCoord = aTexCoord;
    gl_Position = gl_ModelViewProjectionMatrix * vec4(aPosition, 0.0, 1.0);
}