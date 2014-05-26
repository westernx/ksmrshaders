#include "adskShader.h"

// Parameters struct
struct KSTestParameters {
	ADSK_BASE_SHADER_PARAMETERS
	miColor     ambient;
	miColor     diffuse;
};

const unsigned int KSTest_VERSION = 2;
typedef ShaderHelper<KSTestParameters> BaseShaderHelperType;


class KSTestClass : public Material<KSTestParameters, BaseShaderHelperType, KSTest_VERSION>
{
public:

	static void init(miState *state, KSTestParameters *params) {}
	static void exit(miState *state, KSTestParameters *params) {}

	KSTestClass(miState *state, KSTestParameters *params) :
		Material<KSTestParameters, BaseShaderHelperType, KSTest_VERSION>(state, params)
		{}
	~KSTestClass() {}

	miBoolean operator()(miColor *result, miState *state, KSTestParameters *params);

private:

	// short cut definition for base class
	typedef Material<KSTestParameters, BaseShaderHelperType, KSTest_VERSION> MaterialBase;

};


inline miColor operator/(const miColor &lhs, const miColor &rhs) {
	miColor res;
	res.r = rhs.r ? lhs.r / rhs.r : 0;
	res.g = rhs.g ? lhs.g / rhs.g : 0;
	res.b = rhs.b ? lhs.b / rhs.b : 0;
	res.a = rhs.a ? lhs.a / rhs.a : 0;
	return res;
}


#define IF_PASSES if (numberOfFrameBuffers && MaterialBase::mFrameBufferWriteOperation)
#define WRITE_PASS(...) MaterialBase::writeToFrameBuffers(state, frameBufferInfo, passTypeInfo, __VA_ARGS__)

miBoolean KSTestClass::operator()(miColor *result, miState *state, KSTestParameters *params)
{

	if (state->type == miRAY_SHADOW || state->type == miRAY_DISPLACE ) {
		return(miFALSE);
	}
	
	// Initialize "mayabase state" into a MBS variable.
	MBS_SETUP(state)

	// Setup framebuffers.
    PassTypeInfo* passTypeInfo;
    FrameBufferInfo* frameBufferInfo;
    unsigned int numberOfFrameBuffers = getFrameBufferInfo(state, passTypeInfo, frameBufferInfo);

 
	miColor *Ka = mi_eval_color(&params->ambient);
	miColor *Kd = mi_eval_color(&params->diffuse);

	result->r = result->g = result->b = 0.0;
	result->a = 1.0;

    IF_PASSES {
		WRITE_PASS(opaqueColor(*Ka), AMBIENT_MATERIAL_COLOR, false);
        WRITE_PASS(opaqueColor(*Kd), DIFFUSE_MATERIAL_COLOR, false);
    }

	
	miTag *lights;
	int numLights;
    mi_instance_lightlist(&numLights, &lights, state);
    for (; numLights--; lights++) {

		int numSamples = 0;
		miColor diffSum = BLACK;

		sampleLightBegin(numberOfFrameBuffers, frameBufferInfo);

		miColor Cl;
		miVector L;
		miScalar dotNL;
		while (mi_sample_light(&Cl, &L, &dotNL, state, *lights, &numSamples)) {
	
			LightDataArray *lightData = &MBS->lightData;

			// Call to enable renderpass contributions for light shaders that were not developped using the AdskShaderSDK.    
			handleNonAdskLights(numberOfFrameBuffers, frameBufferInfo, Cl, *lights, state);

			dotNL = dotNL > 0 ? dotNL : 0;

			// Only bother with diffuse if the light provides it AND we are
			// facing roughly the right direction.
			miColor unlitDiffuse = opaqueColor(dotNL*(*Kd));
			miColor litDiffuse = opaqueColor(unlitDiffuse * Cl);
			if (lightData->lightDiffuse) {
				IF_PASSES {

					WRITE_PASS(opaqueColor(dotNL * Cl), DIRECT_IRRADIANCE, false);
					WRITE_PASS(opaqueColor(dotNL * lightData->preShadowColor), DIRECT_IRRADIANCE_NO_SHADOW, false);

					WRITE_PASS(litDiffuse, DIFFUSE, false);
					WRITE_PASS((*Kd) * dotNL * lightData->preShadowColor, DIFFUSE_NO_SHADOW, false);
		            WRITE_PASS(litDiffuse, BEAUTY, false);

	            	WRITE_PASS(dotNL * (*Kd) * (lightData->preShadowColor - Cl), SHADOW, false);
	            	WRITE_PASS(dotNL * (lightData->preShadowColor - Cl), RAW_SHADOW, false);
				}
				diffSum = diffSum + litDiffuse;
			}

		}

		// Accumulate sample values into the material frame buffer values.
		sampleLightEnd(state,
			   numberOfFrameBuffers,
			   frameBufferInfo,
			   numSamples,
			   MaterialBase::mFrameBufferWriteOperation,
			   MaterialBase::mFrameBufferWriteFlags,
			   MaterialBase::mFrameBufferWriteFactor);

		if (numSamples > 0) {
			float inv = 1.0f / numSamples;

			diffSum.r *= inv;
			diffSum.g *= inv;
			diffSum.b *= inv;

			result->r += diffSum.r;
			result->g += diffSum.g;
			result->b += diffSum.b;
		}

	}

	return(miTRUE);
}

// Use the EXPOSE macro to create Mental Ray compliant shader functions
//----------------------------------------------------------------------
EXPOSE(KSTest, miColor, );
