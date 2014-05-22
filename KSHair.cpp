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
	miInteger	 i_light;
	miInteger	 n_light;
	miTag*		 light;
	miScalar	 mult;			/* diffuse multiplier       */
	miColor		 diffcol;		/* for diffuse modification */
	miInteger	 samples;		/* for light sampling       */
	miColor		 lightcol;		/* light colour             */
	miScalar	 dot_nl;		/* for diffuse colour       */
	miScalar	 dot_th;		/* for specular colour      */
	miVector	 l;			/* light direction          */
	miVector	 h;			/* halfway vector           */
	miScalar	 spec;			/* specular factor          */


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
	miVector	 t = state->derivs[0]; // Tangent to hair.
	miVector	 v = state->dir;	   // Eye ray.
	mi_vector_normalize(&t);
	mi_vector_neg(&v);

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
	mi_vector_prod(&cross, &state->normal_geom, &t);
	mi_vector_prod(&hair_n, &t, &cross);
	blend = mi_vector_dot(&state->normal_geom, &t);
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

	/* loop over lights */
	if (mode == 4 || n_light) {
		for (mi::shader::LightIterator iter(state, light, n_light);
		     !iter.at_end(); ++iter) {
			/* initially colour and samples */
			sum.r = sum.g = sum.b = 0.0f;

			/* potentially multiply sample each light */
			while (iter->sample()) {
				/* calculate dot_nl from our shading normal.  */
				/* clamp to 0.0-1.0 range, to give good match */
				/* with surface shading of base surface.      */
				l = iter->get_direction();
				dot_nl = mi_vector_dot(&shading_n, &l);
				if (dot_nl < 0.0f)
					dot_nl = 0.0f;
				else if (dot_nl > 1.0f)
					dot_nl = 1.0f;

				/* diffuse term */
				iter->get_contribution(&lightcol);
				sum.r += dot_nl * diffcol.r * lightcol.r;
				sum.g += dot_nl * diffcol.g * lightcol.g;
				sum.b += dot_nl * diffcol.b * lightcol.b;

				/* find the halfway vector h */
				mi_vector_add(&h, &v, &l);
				mi_vector_normalize(&h);

				/* specular coefficient from auk paper */
				dot_th = mi_vector_dot(&t, &h);
				spec = pow(1.0 - dot_th*dot_th, 0.5*exponent);

				/* specular colour */
				if (spec > 0.0) {
					sum.r += spec * specular->r * lightcol.r;
					sum.g += spec * specular->g * lightcol.g;
					sum.b += spec * specular->b * lightcol.b;
				}
			}

			/* add it in */
			samples = iter->get_number_of_samples();
			if (samples) {
				result->r += sum.r / samples;
				result->g += sum.g / samples;
				result->b += sum.b / samples;
			}
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

