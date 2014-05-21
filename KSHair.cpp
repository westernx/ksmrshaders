// ==========================================================================
// Copyright 2008 Autodesk, Inc. All rights reserved.
//
// Use of this software is subject to the terms of the Autodesk
// license agreement provided at the time of installation or download,
// or which otherwise accompanies this software in either electronic
// or hard copy form.
// ==========================================================================

/*****************************************************************************
 * This example takes the mental ray maya_illum_phong devkit example and 
 * adds support for a subset of the Maya render passes. This differs from the
 * MayaPhong example in that here we define the light loop ourselves, while in
 * MayaPhong the AdskShaderSDK light loop is used and we only implement the
 * specular shading component.
 *
 * Because we write our own light loop, we are also responsible for
 * contributing to the appropriate render passes. See the "RENDER PASS
 * SPECIFIC" sections below. We contribute to ambient material color, diffuse
 * material color, diffuse, direct irradiance, specular, indirect and beauty
 * passes. Support for other passes could easily be added by computing the
 * correct value and contributing in the same manner. See adskShader.h for
 * more details on the other passes.
 *
 * This shader does not support per-light contributions defined by pass
 * contribution maps, however it does support per-object contributions as long
 * as the shader's shading group is exported with the Maya Shading Engine. For
 * details on supporting per-light contributions see adskShader.h
 *****************************************************************************/

#include "adskShader.h"

// Old headers.
// #include "shader.h"
// #include "mi_shader_if.h"

struct KSHairParameters {
	ADSK_BASE_SHADER_PARAMETERS
	miColor     ambience;
	miColor     ambient;
	miColor     diffuse;
	miColor     specular;
	miScalar    exponent;
	miInteger   mode;
	int		    i_light;
	int		    n_light;
	miTag		light[1];
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

	miBoolean operator()(miColor *pResult, miState *state, KSHairParameters *params);

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

	miScalar	 p = state->bary[1];	/* hair parameter           */
	miVector	 t = state->derivs[0];	/* tangent to hair          */
	miVector	 v = state->dir;	/* eye ray                  */

	miVector	 cross, hair_n, shading_n;  /* shading normal       */
	miColor		 sum;			/* light contribution       */
	miScalar	 blend;			/* shading normal blend     */
	miVector	 norm = state->normal;	/* for nulling/restoring    */


    // For mental images macros
	// MBS_SETUP(state)

	// Dealing with render passes.
    // PassTypeInfo* passTypeInfo;
    // FrameBufferInfo* frameBufferInfo;
    // unsigned int numberOfFrameBuffers = getFrameBufferInfo(state, passTypeInfo, frameBufferInfo);


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

	/* tangent is not normalized yet */
	mi_vector_normalize(&t);

	/* get parameters */
	*result    = *mi_eval_color(&paras->ambience);
	ambient    =  mi_eval_color(&paras->ambient);
	result->r *=  ambient->r;
	result->g *=  ambient->g;
	result->b *=  ambient->b;
	specular   =  mi_eval_color(&paras->specular);
	exponent   = *mi_eval_scalar(&paras->exponent);
	mode       = *mi_eval_integer(&paras->mode);

	i_light    = *mi_eval_integer(&paras->i_light);
	n_light    = *mi_eval_integer(&paras->n_light);
	light      =  mi_eval_tag(paras->light) + i_light;

	/* correct light list, if requested */
	if (mode == 1)
		mi_inclusive_lightlist(&n_light, &light, state);
	else if (mode == 2)
	 	mi_exclusive_lightlist(&n_light, &light, state);
	else if (mode == 4) {
	 	n_light = 0;
	 	light = 0;
	}

	/* modify diffuse colour to give darker colour near
	   root. this may obviate the need for real shadows,
	   which for hair can be very expensive. */
	mult = 0.5f + smoothstep(0.4f, 0.8f, p) * 0.5f;
	diffcol.r = diffuse->r * mult;
	diffcol.g = diffuse->g * mult;
	diffcol.b = diffuse->b * mult;

	/* calculate current opacity (0.5 at root, 1.0 at tip) */
	result->a = 1.0f - smoothstep(0.3f, 1.0f, p);

	/* prepare some values */
	mi_vector_neg(&v);

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


/*

miBoolean KSHairClass::operator()(miColor *pResult, miState *state, KSHairParameters *params)
{
	// Parameters.
	miColor *ambi, *diff, *spec;
	miScalar expo;
	miInteger mode;

	// Lights.
	miTag *lights;
	int numLights;
    LightDataArray *inLightData;			 


	// Check for illegal calls.
	if (state->type == miRAY_SHADOW || state->type == miRAY_DISPLACE ) {
		return miFALSE;
	}
 
 	// Evaluate parameters.
	ambi =  mi_eval_color(&params->ambient);
	diff =  mi_eval_color(&params->diffuse);
	spec =  mi_eval_color(&params->specular);
	expo = *mi_eval_scalar(&params->exponent);
	mode = *mi_eval_integer(*params->mode);

	*pResult    = *mi_eval_color(&params->ambience); // ambient term
	pResult->r *= ambi->r;
	pResult->g *= ambi->g;
	pResult->b *= ambi->b;

	// RENDER PASS SPECIFIC
	//   Write values to non-light dependant frame buffers
    if (numberOfFrameBuffers && MaterialBase::mFrameBufferWriteOperation)
    {
		MaterialBase::writeToFrameBuffers(state, frameBufferInfo, passTypeInfo, opaqueColor(*ambi), AMBIENT_MATERIAL_COLOR, false);
        MaterialBase::writeToFrameBuffers(state, frameBufferInfo, passTypeInfo, opaqueColor(*diff), DIFFUSE_MATERIAL_COLOR, false);
    }

	inLightData = &MBS->lightData;
    miColor  *preShadowColor = &inLightData->preShadowColor;

	// Get instance light list. *
    mi_instance_lightlist(&numLights, &lights, state);
    // Evaluate all lights in the list. *
    for ( ; numLights--; lights++)         
	{
		int    numSamples = 0;
		diffSum.r = diffSum.g = diffSum.b = 0;
		specSum.r = specSum.g = specSum.b = 0;

		// Function that initialize light sample accumulators which need to be called before the sample loop.
		sampleLightBegin(numberOfFrameBuffers, frameBufferInfo);

		while (mi_sample_light(
                &color, &dir, &dot_nl,
                state, *lights, &numSamples)) {
	
			// Call to enable renderpass contributions for light shaders that were not developped using the AdskShaderSDK.    
			handleNonAdskLights(numberOfFrameBuffers, frameBufferInfo, color, *lights, state);
			
			LightDataArray *ldat = &MBS->lightData;
			if (ldat){
			    emitDiffuse = ldat->lightDiffuse;
			    emitSpecular = ldat->lightSpecular;
			}

			// Add diffuse component
			// only if the light emits diffuse lights
			miColor unlitDiffuse = BLACK;
			if (emitDiffuse && dot_nl > 0) {
				unlitDiffuse = opaqueColor(dot_nl*(*diff));
				// Lambert's cosine law
				if (numberOfFrameBuffers && MaterialBase::mFrameBufferWriteOperation)
				{
					MaterialBase::writeToFrameBuffers(state, frameBufferInfo, passTypeInfo, unlitDiffuse, DIFFUSE, true);
					MaterialBase::writeToFrameBuffers(state, frameBufferInfo, passTypeInfo, opaqueColor(dot_nl*WHITE), DIRECT_IRRADIANCE, true);
				}

				//Compute values for the master beauty pass
				diffSum = diffSum + unlitDiffuse * color;
			}

			// Add specular component
			// only if the light emits specular lights
			miColor unlitSpecular = BLACK;
			if (emitSpecular) {
				// Phong's cosine power
				s = mi_phong_specular(expo, state, &dir);
				if (s > 0.0) {
					unlitSpecular = opaqueColor(s*(*spec));
					if (numberOfFrameBuffers && MaterialBase::mFrameBufferWriteOperation)
					{
						MaterialBase::writeToFrameBuffers(state, frameBufferInfo, passTypeInfo, unlitSpecular, SPECULAR, true);
					}
					specSum = specSum + unlitSpecular * color;
				}
			}

			miColor unlitBeauty = opaqueColor(unlitSpecular+unlitDiffuse);

            MaterialBase::writeToFrameBuffers(state, frameBufferInfo, passTypeInfo, 
                unlitBeauty, SHADOW, true, preShadowColor);
            MaterialBase::writeToFrameBuffers(state, frameBufferInfo, passTypeInfo, 
                MAYA_LUMINANCE(unlitBeauty), SHADOW_MONO, true, preShadowColor);
            MaterialBase::writeToFrameBuffers(state, frameBufferInfo, passTypeInfo, 
                unlitBeauty, BEAUTY, true );
		}

		// Function that take care of combining sample values into the material frame buffer values, 
		//    called after light sampling loop.
		sampleLightEnd(state,
			   numberOfFrameBuffers,
			   frameBufferInfo,
			   numSamples,
			   MaterialBase::mFrameBufferWriteOperation,
			   MaterialBase::mFrameBufferWriteFlags,
			   MaterialBase::mFrameBufferWriteFactor);

		if (numSamples > 0) {
			inv = 1.0f / numSamples;

			diffSum.r *= inv;
			diffSum.g *= inv;
			diffSum.b *= inv;

			specSum.r *= inv;
			specSum.g *= inv;
			specSum.b *= inv;

			pResult->r += diffSum.r + specSum.r;
			pResult->g += diffSum.g + specSum.g;
			pResult->b += diffSum.b + specSum.b;
		}
	} // for ( ; numLights--; lights++) *

	// add contribution from indirect illumination (caustics)
	mi_compute_irradiance(&color, state);
	pResult->r += color.r * diff->r;
	pResult->g += color.g * diff->g;
	pResult->b += color.b * diff->b;
	pResult->a  = 1;

	// RENDER PASS SPECIFIC
	//   Write indirect illumination and final beauty to frame buffers
    if (numberOfFrameBuffers && MaterialBase::mFrameBufferWriteOperation)
    {
		MaterialBase::writeToFrameBuffers(state, frameBufferInfo, passTypeInfo, opaqueColor(color), INDIRECT, false);
		MaterialBase::writeToFrameBuffers(state, frameBufferInfo, passTypeInfo, opaqueColor(color*(*diff)), BEAUTY, false);
    }

	return(miTRUE);
}

*/
