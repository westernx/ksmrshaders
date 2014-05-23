#include "adskShader.h"


struct KSHairParameters {
	ADSK_BASE_SHADER_PARAMETERS
	miColor     ambience;
	miColor     Ka;
	miColor     Kd;
	miColor     Ks;
	miScalar    exponent;
};


const unsigned int KSHAIR_VERSION = 1;
typedef ShaderHelper<KSHairParameters> BaseShaderHelperType;


class KSHairClass : public Material<KSHairParameters, BaseShaderHelperType, KSHAIR_VERSION> {
public:

	// Per-render init.
	static void init(miState *state, KSHairParameters *params) {}
	static void exit(miState *state, KSHairParameters *params) {}

	// Per-instance init.
	KSHairClass(miState *state, KSHairParameters *params) :
		Material<KSHairParameters, BaseShaderHelperType, KSHAIR_VERSION>(state, params)
		{}
	~KSHairClass() {}

	miBoolean operator()(miColor *result, miState *state, KSHairParameters *params);

private:

	typedef Material<KSHairParameters, BaseShaderHelperType, KSHAIR_VERSION> MaterialBase;

};


static float smoothstep(float min, float max, float x) {

	float p, term, term2;

	/* first clamp the parameter to [min,max]. */
	if (x < min) {
		p = min;
	} else if (x > max) {
		p = max;
	} else {
		p = x;
	}

	/* now calculate smoothstep value:
	   -2term^3 + 3term^2, where term = (p-min)/(max-min) */
	term  = (p - min) / (max - min);
	term2 = term * term;
	return -2 * term * term2 + 3 * term2;

}

// Some helpers.
#define IF_PASSES if (numberOfFrameBuffers && MaterialBase::mFrameBufferWriteOperation)
#define WRITE_PASS(...) MaterialBase::writeToFrameBuffers(state, frameBufferInfo, passTypeInfo, __VA_ARGS__)


miBoolean KSHairClass::operator()(miColor *result, miState *state, KSHairParameters *paras) {

	/* shader parameters */
	miScalar	 exponent;
	miInteger	 n_light;
	miTag*		 light;
	miScalar	 dot_nl;		/* for Kd colour       */
	miScalar	 dot_th;		/* for Ks colour      */
	miScalar	 spec;			/* Ks factor          */
	miColor Cl;
	miVector L, H;

	miColor		 sum;			/* light contribution       */


    // We can't displace.
    if (state->type == miRAY_DISPLACE)
        return miFALSE;
        
	// Shortcut if we are a shadow shader.
	miColor Kd = *mi_eval_color(&paras->Kd);
	if (state->type == miRAY_SHADOW) {
		result->r *= Kd.r;
		result->g *= Kd.g;
		result->b *= Kd.b;
		return miTRUE;
	}

    // For using MBS (which we currently are not).
	// MBS_SETUP(state)

	// Dealing with render passes.
    PassTypeInfo* passTypeInfo;
    FrameBufferInfo* frameBufferInfo;
    unsigned int numberOfFrameBuffers = getFrameBufferInfo(state, passTypeInfo, frameBufferInfo);

	// Shader parameters.
	*result  = *mi_eval_color(&paras->ambience);
	miColor Ka = *mi_eval_color(&paras->Ka);
	miColor Ks = *mi_eval_color(&paras->Ks);
	exponent = *mi_eval_scalar(&paras->exponent);

	IF_PASSES {
		WRITE_PASS(opaqueColor(Ka), AMBIENT_MATERIAL_COLOR, false);
        WRITE_PASS(opaqueColor(Kd), DIFFUSE_MATERIAL_COLOR, false);
    }

	/*
	miScalar p = state->bary[1]; // 0 to 1 along hair.
	*/

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
	miRc_intersection *saved_pri = state->pri = NULL;

	// Iterate across lights.
	mi_instance_lightlist(&n_light, &light, state);
    for (; n_light--; light++)         
	{

		// Samples MUST be set to 0, or MentalRay segfaults...
		miInteger samples = 0;
		miColor diff_sum = BLACK;
		miColor spec_sum = BLACK;

		// Sample the light.
		sampleLightBegin(numberOfFrameBuffers, frameBufferInfo);
		while (mi_sample_light(&Cl, &L, &dot_nl, state, *light, &samples)) {
	    
			handleNonAdskLights(numberOfFrameBuffers, frameBufferInfo, Cl, *light, state);

			// We want to calculate dot_nl ourselves, because our N is different.
			dot_nl = mi_vector_dot(&N, &L);
			if (dot_nl < 0.0f)
				dot_nl = 0.0f;
			else if (dot_nl > 1.0f)
				dot_nl = 1.0f;

			IF_PASSES {
				WRITE_PASS(opaqueColor(dot_nl * Cl), DIRECT_IRRADIANCE, true);
			}

			// Diffuse calculation.
			miColor sample_diffuse = Kd * Cl * dot_nl;
			IF_PASSES {
				WRITE_PASS(sample_diffuse, DIFFUSE, true);
			}
			diff_sum.r += sample_diffuse.r;
			diff_sum.g += sample_diffuse.g;
			diff_sum.b += sample_diffuse.b;

			// Spec intensity from Halfway vector.
			mi_vector_add(&H, &V, &L);
			mi_vector_normalize(&H);
			dot_th = mi_vector_dot(&T, &H);
			spec = pow(1.0 - dot_th * dot_th, 0.5 * exponent);

			// Specular calculation.
			if (spec > 0.0) {
				miColor sample_specular = Ks * Cl * spec;
				IF_PASSES {
					WRITE_PASS(sample_specular, SPECULAR, true);
				}
				spec_sum.r += sample_specular.r;
				spec_sum.g += sample_specular.g;
				spec_sum.b += sample_specular.b;
			}
		}

		// Accumulate the samples into the frame buffers.
		sampleLightEnd(state, numberOfFrameBuffers, frameBufferInfo, samples,
			   MaterialBase::mFrameBufferWriteOperation,
			   MaterialBase::mFrameBufferWriteFlags,
			   MaterialBase::mFrameBufferWriteFactor
		);

		// Accumulate our own samples.
		if (samples) {
			result->r += (diff_sum.r + spec_sum.r) / samples;
			result->g += (diff_sum.g + spec_sum.g) / samples;
			result->b += (diff_sum.b + spec_sum.b) / samples;
		}
	}

	state->pri = saved_pri;

	/*
	miColor Cidd = BLACK;
	mi_compute_irradiance(&Cidd, state);
    IF_PASSES {
		WRITE_PASS(opaqueColor(Cidd), INDIRECT, false);
		WRITE_PASS(opaqueColor(Cidd * Kd), BEAUTY, false);
    }
	result->r += Cidd.r * Kd.r;
	result->g += Cidd.g * Kd.g;
	result->b += Cidd.b * Kd.b;
	*/

	// If we are translucent, trace more rays.
	// XXX: Does the renderer not handle this?!
	if (result->a < 0.9999) {
		miScalar alphafactor;
		miColor col = {0.0, 0.0, 0.0, 0.0};
		mi_trace_transparent(&col, state);
		alphafactor = 1.0f - result->a;
		result->r   = alphafactor * col.r + result->a * result->r;
		result->g   = alphafactor * col.g + result->a * result->g;
		result->b   = alphafactor * col.b + result->a * result->b;
		result->a  += alphafactor * col.a;
	}

	return miTRUE;
}



EXPOSE(KSHair, miColor, );

