#version 450

layout(location = 0) out vec4 fragColor;

layout(set = 1, binding = 0) uniform Grid {
    vec2 gridSpacing;
    vec3 gridColor;
    vec3 backgroundColor;
} grid;

void main() {
    // Get the UV coordinates (0 to 1) of the fragment
    vec2 uv = gl_FragCoord.xy; // Or use passed-in UVs if available

    // Calculate whether we're close to a grid line
    float lineWidth = 0.01;  // Thickness of the grid lines
    float gridX = abs(fract(uv.x / grid.gridSpacing.x) - 0.5);
    float gridY = abs(fract(uv.y / grid.gridSpacing.y) - 0.5);

    // Determine if this fragment is part of a line or background
    float line = step(gridX, lineWidth) + step(gridY, lineWidth);
    vec3 color = mix(grid.backgroundColor, grid.gridColor, line);

    fragColor = vec4(vec3(1.0, 0.0, 0.0), 1.0);
}