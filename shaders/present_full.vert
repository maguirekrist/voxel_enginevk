#version 450

layout(location = 0) out vec2 texCoord;

void main() {
    // Define positions of a full-screen quad using the built-in gl_VertexIndex
    vec2 positions[3] = vec2[3](
        vec2(-1.0, -1.0),  // Bottom-left
        vec2( 3.0, -1.0),  // Bottom-right (expanded beyond to cover full screen with a single triangle)
        vec2(-1.0,  3.0)   // Top-left
    );

    // Define texture coordinates matching the vertices
    vec2 uvs[3] = vec2[3](
        vec2(0.0, 0.0),  // Bottom-left
        vec2(2.0, 0.0),  // Bottom-right
        vec2(0.0, 2.0)   // Top-left
    );

    // Output vertex position in clip space
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    texCoord = uvs[gl_VertexIndex];
}