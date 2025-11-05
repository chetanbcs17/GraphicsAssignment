#version 120

attribute vec3 aOffset;      
attribute vec3 aVelocity;    
attribute float aRotation;   
attribute vec2 aTexOffset;   

uniform float uTime;         
uniform vec3 uImpactPoint;   
uniform mat4 uModelView;

varying vec2 vTexCoord;
varying float vAlpha;

void main()
{
    // Time-based animation
    float t = uTime;
    float gravity = -9.8;
    
    // Calculate particle position
    vec3 pos = gl_Vertex.xyz + aOffset;
    
    // Apply physics
    pos += aVelocity * t;
    pos.y += 0.5 * gravity * t * t;  // Gravity
    
    // Rotate particle
    float angle = aRotation * t * 3.14159;
    float c = cos(angle);
    float s = sin(angle);
    
    // Apply rotation around impact point
    vec3 offset = pos - uImpactPoint;
    pos.x = uImpactPoint.x + offset.x * c - offset.z * s;
    pos.z = uImpactPoint.z + offset.x * s + offset.z * c;
    
    // Pass to fragment shader
    vTexCoord = gl_MultiTexCoord0.xy + aTexOffset;
    
    // Fade out over time
    vAlpha = 1.0 - t;
    
    gl_Position = gl_ProjectionMatrix * uModelView * vec4(pos, 1.0);
}