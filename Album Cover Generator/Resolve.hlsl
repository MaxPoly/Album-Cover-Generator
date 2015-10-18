struct VSOut {
	float4 position : SV_Position;
	float2 uv : TexCoord;
};

Texture2D tex;

SamplerState texSampler {
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = Clamp;
	AddressV = Clamp;
};

float4 main(VSOut input) : SV_Target {
	return tex.Sample(texSampler, input.uv);
}