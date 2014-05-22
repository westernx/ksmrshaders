#include "adskShader.h"


struct KSHairParameters {
	ADSK_BASE_SHADER_PARAMETERS
	miColor     ambience;
	miColor     ambient;
	miColor     diffuse;
	miColor     specular;
	miScalar    exponent;
	miInteger   mode;
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


struct ksColor : public miColor {
public:

	ksColor() {}

	ksColor(miColor const &other) {
		r = other.r; g = other.g; b = other.b; a = other.a;
	}

	ksColor(miScalar r_, miScalar g_, miScalar b_, miScalar a_) {
		r = r_; g = g_; b = b_; a = a_;
	}

	ksColor operator*(miScalar v) {
		return ksColor(r * v, g * v, b * v, a * v);
	}

	void operator+=(ksColor const &other) {
		r += other.r; g += other.g; b += other.b; a += other.a;
	}

	ksColor operator+(ksColor const &other) {
		ksColor res(*this);
		res += other;
		return res;
	}

	void operator*=(ksColor const &other) {
		r *= other.r; g *= other.g; b *= other.b; a *= other.a;
	}

	ksColor operator*(ksColor const &other) {
		ksColor res(*this);
		res *= other;
		return res;
	}
	

};


miBoolean KSHairClass::operator()(miColor *result, miState *state, KSHairParameters *paras) {

	/* shader parameters */
	miColor		*ambient;
	miColor		*diffuse;
	miColor		*specular;
	miScalar	 exponent;
	miInteger	 mode;
	miInteger	 n_light;
	miTag*		 light;
	miScalar	 mult;			/* diffuse multiplier       */
	miColor		 diffcol;		/* for diffuse modification */
	miInteger	 samples;		/* for light sampling       */
	miScalar	 dot_nl;		/* for diffuse colour       */
	miScalar	 dot_th;		/* for specular colour      */
	miScalar	 spec;			/* specular factor          */
	miColor Cl;
	miVector L, H;

	miVector	 cross, hair_n, shading_n;  /* shading normal       */
	miColor		 sum;			/* light contribution       */
	miScalar	 blend;			/* shading normal blend     */
	miVector	 norm = state->normal;	/* for nulling/restoring    */


    // We can't displace.
    if(state->type == miRAY_DISPLACE)
        return miFALSE;
        
	// Shortcut if we are a shadow shader.
	diffuse = mi_eval_color(&paras->diffuse);
	if (state->type == miRAY_SHADOW) {
		result->r *= diffuse->r;
		result->g *= diffuse->g;
		result->b *= diffuse->b;
		return miTRUE;
	}

    // For mental images macros.
    // XXX: Do we really need this?!
	MBS_SETUP(state)

	// Dealing with render passes.
    PassTypeInfo* passTypeInfo;
    FrameBufferInfo* frameBufferInfo;
    unsigned int numberOfFrameBuffers = getFrameBufferInfo(state, passTypeInfo, frameBufferInfo);

	// Shader parameters.
	*result    = *mi_eval_color(&paras->ambience);
	ambient    =  mi_eval_color(&paras->ambient);
	specular   =  mi_eval_color(&paras->specular);
	exponent   = *mi_eval_scalar(&paras->exponent);
	mode       = *mi_eval_integer(&paras->mode);

    // Hair parameters.
	miScalar	 p = state->bary[1];   // 0 to 1 along hair.
	miVector	 T = state->derivs[0]; // Tangent to hair.
	miVector	 V = state->dir;	   // Eye ray.
	mi_vector_normalize(&T);
	mi_vector_neg(&V);

	// Darker colours near the root.
	mult = 0.5f + smoothstep(0.4f, 0.8f, p) * 0.5f;
	diffcol.r = diffuse->r * mult;
	diffcol.g = diffuse->g * mult;
	diffcol.b = diffuse->b * mult;

	// Base opacity (0.5 at root, 1.0 at tip).
	result->r *= ambient->r;
	result->g *= ambient->g;
	result->b *= ambient->b;
	result->a = 1.0f - smoothstep(0.3f, 1.0f, p);

	/* get shading normal */
	mi_vector_prod(&cross, &state->normal_geom, &T);
	mi_vector_prod(&hair_n, &T, &cross);
	blend = mi_vector_dot(&state->normal_geom, &T);
	shading_n.x = (1.0f-blend)*hair_n.x + blend*state->normal_geom.x;
	shading_n.y = (1.0f-blend)*hair_n.y + blend*state->normal_geom.y;
	shading_n.z = (1.0f-blend)*hair_n.z + blend*state->normal_geom.z;
	mi_vector_normalize(&shading_n);

	/* null state->normal for now, for sampling lights       */
	/* we leave state->pri to avoid losing self-intersection */
	/* handling.                                             */
	state->normal.x = state->normal.y = state->normal.z = 0.0f;

	// Get the light list.
	mi_instance_lightlist(&n_light, &light, state);
    for (; n_light--; light++)         
	{

		samples = 0;
		sum.r = sum.g = sum.b = 0;

		// Function that initialize light sample accumulators which need to be called before the sample loop.
		sampleLightBegin(numberOfFrameBuffers, frameBufferInfo);

		while (mi_sample_light(
                &Cl, &L, &dot_nl,
                state, *light, &samples)) {
	
			// Call to enable renderpass contributions for light shaders that were not developped using the AdskShaderSDK.    
			handleNonAdskLights(numberOfFrameBuffers, frameBufferInfo, Cl, *light, state);

			// Do it ourselves.
			dot_nl = mi_vector_dot(&shading_n, &L);
			if (dot_nl < 0.0f)
				dot_nl = 0.0f;
			else if (dot_nl > 1.0f)
				dot_nl = 1.0f;

			sum.r += dot_nl * diffcol.r * Cl.r;
			sum.g += dot_nl * diffcol.g * Cl.g;
			sum.b += dot_nl * diffcol.b * Cl.b;

			/* find the halfway vector h */
			mi_vector_add(&H, &V, &L);
			mi_vector_normalize(&H);

			/* specular coefficient from auk paper */
			dot_th = mi_vector_dot(&T, &H);
			spec = pow(1.0 - dot_th*dot_th, 0.5*exponent);

			/* specular colour */
			if (spec > 0.0) {
				sum.r += spec * specular->r * Cl.r;
				sum.g += spec * specular->g * Cl.g;
				sum.b += spec * specular->b * Cl.b;
			}
		}

		// Function that take care of combining sample values into the material frame buffer values, 
		//    called after light sampling loop.
		sampleLightEnd(state,
			   numberOfFrameBuffers,
			   frameBufferInfo,
			   samples,
			   MaterialBase::mFrameBufferWriteOperation,
			   MaterialBase::mFrameBufferWriteFlags,
			   MaterialBase::mFrameBufferWriteFactor);

		if (samples) {
			result->r += sum.r / samples;
			result->g += sum.g / samples;
			result->b += sum.b / samples;
		}
	}

	/* restore state->normal */
	state->normal = norm;

	/* if we are translucent, trace more rays */
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

