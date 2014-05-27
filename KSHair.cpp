#include "adskShader.h"

// Parameters struct
struct KSHairParameters {
	ADSK_BASE_SHADER_PARAMETERS
	miColor ambient;
	miColor diffuse;
	miColor specular;
	miScalar exponent;
	miColor transparency;
};

const unsigned int KSHair_VERSION = 3;
typedef ShaderHelper<KSHairParameters> BaseShaderHelperType;


class KSHairClass : public Material<KSHairParameters, BaseShaderHelperType, KSHair_VERSION>
{
public:

	static void init(miState *state, KSHairParameters *params) {}
	static void exit(miState *state, KSHairParameters *params) {}

	KSHairClass(miState *state, KSHairParameters *params) :
		Material<KSHairParameters, BaseShaderHelperType, KSHair_VERSION>(state, params)
		{}
	~KSHairClass() {}

	miBoolean operator()(miColor *result, miState *state, KSHairParameters *params);

private:

	// short cut definition for base class
	typedef Material<KSHairParameters, BaseShaderHelperType, KSHair_VERSION> MaterialBase;

};


#define IF_PASSES if (numberOfFrameBuffers && MaterialBase::mFrameBufferWriteOperation)
#define WRITE_PASS(pass, value) MaterialBase::writeToFrameBuffers(state, frameBufferInfo, passTypeInfo, (value), pass, false)

miBoolean KSHairClass::operator()(miColor *result, miState *state, KSHairParameters *params)
{

	if (state->type == miRAY_SHADOW) {

		miColor Kt = opaqueColor(*mi_eval_color(&params->transparency));

		if (state->options->shadow == 's') {
			// Segmented shadows! We are responsible for linking to the next
			// shader in the chain.
			mi_trace_shadow_seg(result, state);
		}

		// Modulate the light intensity.
		*result = *result * Kt;
		return miTRUE;

	}

	if (state->type != miRAY_EYE && state->type != miRAY_TRANSPARENT) {
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

    IF_PASSES {
		WRITE_PASS(AMBIENT_MATERIAL_COLOR, Ka);
        WRITE_PASS(DIFFUSE_MATERIAL_COLOR, Kd);
    }

    // Tangent along hair.
	miVector T = state->derivs[0];
	mi_vector_normalize(&T);

	// The view vector points towards the intersection point, so we flip it so
	// that it points towards the viewer (or source of the ray).
	miVector V = state->dir;
	mi_vector_neg(&V);

	// Just use the state's vector.
	miVector N = state->normal;

	// We must clear the primitive so that it doesn't think there is an
	// intersection, so that the various lights won't check against the
	// normal to see if it is pointed in the "wrong" direction.
	miRc_intersection *saved_pri = state->pri;
	state->pri = NULL;
	
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

			// We want to calculate dotNL ourselves, because our N is different.
			dotNL = mi_vector_dot(&N, &L);
			if (dotNL < 0.0f)
				dotNL = 0.0f;
			else if (dotNL > 1.0f)
				dotNL = 1.0f;

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

				// Spec intensity from Halfway vector.
				miVector H;
				mi_vector_add(&H, &V, &L);
				mi_vector_normalize(&H);
				miScalar dotTH = mi_vector_dot(&T, &H);
				miScalar s = pow(1.0 - dotTH * dotTH, 0.5 * exponent);
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

    // Restore state.
    state->pri = saved_pri;

    *result = *result + Cidd * Kd;

    if (Kt.r || Kt.g || Kt.b) {
	    miColor nextRes = BLACK;
	    nextRes.a = 0;
	    mi_trace_transparent(&nextRes, state);
	    *result = (WHITE - Kt) * *result + Kt * nextRes;
    }
    result->a = 1.0;

	return(miTRUE);
}

// Use the EXPOSE macro to create Mental Ray compliant shader functions
//----------------------------------------------------------------------
EXPOSE(KSHair, miColor, );
