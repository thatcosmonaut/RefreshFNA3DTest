#version 450

layout(set = 3, binding = 0) uniform UniformBlock
{
    float time;
} Uniforms;

layout(location = 0) in vec2 fragCoord;

layout(location = 0) out vec4 FragColor;

void main()
{
    // Time varying pixel color
    vec3 col = 0.5 + 0.5 * cos(Uniforms.time + fragCoord.xyx + vec3(0, 2, 4));

    FragColor = vec4(col, 1.0);
}
