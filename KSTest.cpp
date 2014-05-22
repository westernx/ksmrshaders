#include "adskShader.h"

// Parameters struct
struct KSTestParameters {
	ADSK_BASE_SHADER_PARAMETERS
	miColor     ambient;
	miColor     diffuse;
};

const unsigned int KSTest_VERSION = 2;
typedef ShaderHelper<KSTestParameters> BaseShaderHelperType;

/*****************************************************************************
 * Wrapper class for custom phong shader, as an extension of the base
 * Material template.
 *****************************************************************************/
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


#define IF_PASSES if (numberOfFrameBuffers && MaterialBase::mFrameBufferWriteOperation)
#define WRITE_PASS(...) MaterialBase::writeToFrameBuffers(state, frameBufferInfo, passTypeInfo, __VA_ARGS__)

miBoolean KSTestClass::operator()(miColor *result, miState *state, KSTestParameters *params)
{

	// Bail on unspported calls.
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

	
	LightDataArray *inLightData = &MBS->lightData;
    miColor  *preShadowColor = &inLightData->preShadowColor;
	

	miTag *lights;
	int numLights;
    mi_instance_lightlist(&numLights, &lights, state);
    for (; numLights--; lights++) {

		int numSamples = 0;
		miColor diffSum = BLACK;

		sampleLightBegin(numberOfFrameBuffers, frameBufferInfo);

		miColor Cl;
		miVector L;
		miScalar dot_nl;
		while (mi_sample_light(&Cl, &L, &dot_nl, state, *lights, &numSamples)) {
	
			// Call to enable renderpass contributions for light shaders that were not developped using the AdskShaderSDK.    
			handleNonAdskLights(numberOfFrameBuffers, frameBufferInfo, Cl, *lights, state);
			
			LightDataArray *ldat = &MBS->lightData;
			miBoolean emitDiffuse = ldat ? ldat->lightDiffuse : miTRUE;

			// Add diffuse component
			// only if the light emits diffuse lights
			miColor unlitDiffuse = BLACK;
			if (emitDiffuse && dot_nl > 0) {
				unlitDiffuse = opaqueColor(dot_nl*(*Kd));
				IF_PASSES {
					WRITE_PASS(unlitDiffuse, DIFFUSE, true);
					WRITE_PASS(opaqueColor(dot_nl*WHITE), DIRECT_IRRADIANCE, true);
				}
				diffSum = diffSum + unlitDiffuse * Cl;
			}

			miColor unlitBeauty = opaqueColor(unlitDiffuse);

            WRITE_PASS(unlitBeauty, SHADOW, true, preShadowColor);
            WRITE_PASS(unlitBeauty, BEAUTY, true);
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

	} // for ( ; numLights--; lights++) 

	// Indirect illumination (IDD(?) and caustics).
	miColor Cid = BLACK;
	mi_compute_irradiance(&Cid, state);
	result->r += Cid.r * Kd->r;
	result->g += Cid.g * Kd->g;
	result->b += Cid.b * Kd->b;
	result->a  = 1;

    IF_PASSES {
		WRITE_PASS(opaqueColor(Cid), INDIRECT, false);
		WRITE_PASS(opaqueColor(Cid*(*Kd)), BEAUTY, false);
    }

	return(miTRUE);
}

// Use the EXPOSE macro to create Mental Ray compliant shader functions
//----------------------------------------------------------------------
EXPOSE(KSTest, miColor, );
