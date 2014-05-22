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

	miVector	 cross, hair_n, N;  /* shading normal       */
	miColor		 sum;			/* light contribution       */
	miScalar	 blend;			/* shading normal blend     */
	miVector	 norm = state->normal;	/* for nulling/restoring    */


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

    // Hair parameters.
	miScalar	 p = state->bary[1];   // 0 to 1 along hair.
	miVector	 T = state->derivs[0]; // Tangent to hair.
	miVector	 V = state->dir;	   // Eye ray.
	mi_vector_normalize(&T);
	mi_vector_neg(&V);

	// Darker colours near the root.
	miScalar root_mult = 0.5f + smoothstep(0.4f, 0.8f, p) * 0.5f;
	Kd.r *= root_mult;
	Kd.g *= root_mult;
	Kd.b *= root_mult;

	// Base opacity (0.5 at root, 1.0 at tip).
	result->r *= Ka.r;
	result->g *= Ka.g;
	result->b *= Ka.b;
	result->a = 1.0f - smoothstep(0.3f, 1.0f, p);

	// Calculate our shading normal.
	mi_vector_prod(&cross, &state->normal_geom, &T);
	mi_vector_prod(&hair_n, &T, &cross);
	blend = mi_vector_dot(&state->normal_geom, &T);
	N.x = (1.0f-blend)*hair_n.x + blend*state->normal_geom.x;
	N.y = (1.0f-blend)*hair_n.y + blend*state->normal_geom.y;
	N.z = (1.0f-blend)*hair_n.z + blend*state->normal_geom.z;
	mi_vector_normalize(&N);

	// Null our the normal so that lights do not take it into account.
	state->normal.x = state->normal.y = state->normal.z = 0.0f;

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

	// Restore the default normal.
	state->normal = norm;

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

