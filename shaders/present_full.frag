#version 450
// Fragment shader for final render pass
layout(set = 0, binding = 0) uniform sampler2D offscreenTexture;
layout(location = 0) out vec4 outColor;

void main() {
    // Sample the off-screen image
    vec2 texSize = vec2(textureSize(offscreenTexture, 0)); 
    vec2 uv = gl_FragCoord.xy / texSize;
    outColor = texture(offscreenTexture, uv);
}