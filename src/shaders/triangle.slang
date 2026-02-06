struct VertexOutput {
	float4 position : SV_Position;
	float3 color : COLOR;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID) {
	VertexOutput output;
	float2 positions[3] = {
		float2(0.0, -0.5),
		float2(0.5, 0.5),
		float2(-0.5, 0.5)
	};
	float3 colors[3] = {
		float3(1.0, 0.0, 0.0),
		float3(0.0, 1.0, 0.0),
		float3(0.0, 0.0, 1.0)
	};

	output.position = float4(positions[vertexId], 0.0, 1.0);
	output.color = colors[vertexId];
	return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
	return float4(input.color, 1.0);
}
