StructuredBuffer<float2> inPos;
StructuredBuffer<float3> inColor;

struct VertexOutput {
	float4 position : SV_Position;
	float3 color : COLOR;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID) {
	VertexOutput output;
	output.position = float4(inPos[vertexId], 0.0, 1.0);
	output.color = inColor[vertexId];
	return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
	return float4(input.color, 1.0);
}
