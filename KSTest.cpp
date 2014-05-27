#include "adskShader.h"

// Parameters struct
struct KSTestParameters {
	ADSK_BASE_SHADER_PARAMETERS
	miColor ambient;
	miColor diffuse;
	miColor specular;
	miScalar exponent;
	miColor transparency;
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


#define IF_PASSES if (numberOfFrameBuffers && MaterialBase::mFrameBufferWriteOperation)
#define WRITE_PASS(pass, value) MaterialBase::writeToFrameBuffers(state, frameBufferInfo, passTypeInfo, (value), pass, false)

miBoolean KSTestClass::operator()(miColor *result, miState *state, KSTestParameters *params)
{

	if (state->type == miRAY_SHADOW) {

		miColor Kt = opaqueColor(*mi_eval_color(&params->transparency));

		if (state->options->shadow == 's') {
			// Segmented shadows! We are responsible for linking to the next
			// shader in the chain.
			mi_trace_shadow_seg(result, state);
		}

		// We still do the same thing to the result.
		*result = *result * Kt;
		return miTRUE;
	
	}

	if (state->type != miRAY_EYE) {
		return miFALSE;
	}
	
	// Initialize "mayabase state" into a MBS variable.
	MBS_SETUP(state)

	// Setup framebuffers.
    PassTypeInfo* passTypeInfo;
    FrameBufferInfo* frameBufferInfo;
    unsigned int numberOfFrameBuffers = getFrameBufferInfo(state, passTypeInfo, frameBufferInfo);

 	// Fetch parameters.
	miColor Ka = opaqueColor(*mi_eval_color(&params->ambient));
	miColor Kd = opaqueColor(*mi_eval_color(&params->diffuse));
	miColor Ks = opaqueColor(*mi_eval_color(&params->specular));
	miScalar exponent = *mi_eval_scalar(&params->exponent);
	miColor Kt = opaqueColor(*mi_eval_color(&params->transparency));

	result->r = result->g = result->b = 0.0;
	result->a = 1.0;

    IF_PASSES {
		WRITE_PASS(AMBIENT_MATERIAL_COLOR, Ka);
        WRITE_PASS(DIFFUSE_MATERIAL_COLOR, Kd);
    }

	
	miTag *lights;
	int numLights;
    mi_instance_lightlist(&numLights, &lights, state);
    for (; numLights--; lights++) {

		int numSamples = 0;
		miColor beautySum = BLACK;

		sampleLightBegin(numberOfFrameBuffers, frameBufferInfo);

		miColor Cl;
		miVector L;
		miScalar dotNL;
		while (mi_sample_light(&Cl, &L, &dotNL, state, *lights, &numSamples)) {
	
			LightDataArray *lightData = &MBS->lightData;

			// Call to enable renderpass contributions for light shaders that were not developped using the AdskShaderSDK.    
			handleNonAdskLights(numberOfFrameBuffers, frameBufferInfo, Cl, *lights, state);

			// Normalize what the light gives us.
			Cl = opaqueColor(Cl);
			miColor Cl_ns = opaqueColor(lightData->preShadowColor);
			dotNL = dotNL > 0 ? dotNL : 0;

			// Irradiance.
			miColor irradiance    = dotNL * Cl;
			miColor irradiance_ns = dotNL * Cl_ns;
			IF_PASSES {
				WRITE_PASS(DIRECT_IRRADIANCE, irradiance);
				WRITE_PASS(DIRECT_IRRADIANCE_NO_SHADOW, irradiance_ns);
	            WRITE_PASS(RAW_SHADOW, irradiance_ns - irradiance);
			}

			// Diffuse.
			miColor diffuse = BLACK;
			miColor diffuse_ns = BLACK;
			if (lightData->lightDiffuse) {
				diffuse    = Kd * irradiance;
				diffuse_ns = Kd * irradiance_ns;
				IF_PASSES {
					WRITE_PASS(DIFFUSE, diffuse);
					WRITE_PASS(DIFFUSE_NO_SHADOW, diffuse_ns);
				}
				beautySum = beautySum + diffuse;
			}

			// Specular.
			miColor specular = BLACK;
			miColor specular_ns = BLACK;
			if (lightData->lightSpecular) {
				miScalar s = mi_phong_specular(exponent, state, &L);
				if (s > 0.0) {
					specular    = s * Ks * irradiance;
					specular_ns = s * Ks * irradiance_ns;
					IF_PASSES {
						WRITE_PASS(SPECULAR, specular);
						WRITE_PASS(SPECULAR_NO_SHADOW, specular_ns);
					}
					beautySum = beautySum + specular;
				}
			}

			// Beauty pass.
			IF_PASSES {
				miColor beauty    = diffuse + specular;
				miColor beauty_ns = diffuse_ns + specular_ns;
			    WRITE_PASS(BEAUTY, beauty);
			    WRITE_PASS(BEAUTY_NO_SHADOW, beauty_ns);
	            WRITE_PASS(SHADOW, beauty_ns - beauty);
			}

		}

		// Accumulate sample values into the material frame buffer values.
		sampleLightEnd(
			state,
			numberOfFrameBuffers,
			frameBufferInfo,
			numSamples,
			MaterialBase::mFrameBufferWriteOperation,
			MaterialBase::mFrameBufferWriteFlags,
			MaterialBase::mFrameBufferWriteFactor
		);
		if (numSamples > 0) {
			*result = *result + beautySum * (1.0 / numSamples);
		}

	}

	miColor Cidd = BLACK;
	mi_compute_irradiance(&Cidd, state);
    IF_PASSES {
		WRITE_PASS(INDIRECT, opaqueColor(Cidd));
    }


    *result = *result + Cidd * Kd;

    if (Kt.r || Kt.g || Kt.b) {
	    miColor nextRes = BLACK;
	    nextRes.a = 0;
	    mi_trace_continue(&nextRes, state);
	    *result = (WHITE - Kt) * *result + Kt * nextRes;
    }
    result->a = 1.0;

	return(miTRUE);
}

// Use the EXPOSE macro to create Mental Ray compliant shader functions
//----------------------------------------------------------------------
EXPOSE(KSTest, miColor, );
