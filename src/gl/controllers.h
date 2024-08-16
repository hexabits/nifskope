/***** BEGIN LICENSE BLOCK *****

BSD License

Copyright (c) 2005-2015, NIF File Format Library and Tools
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.
3. The name of the NIF File Format Library and Tools project may not be
used to endorse or promote products derived from this software
without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

***** END LICENCE BLOCK *****/

#ifndef CONTROLLERS_H
#define CONTROLLERS_H

#include "gl/glcontroller.h" // Inherited

#include "gl/glnode.h"
#include "gl/glshape.h"
#include "gl/glparticles.h"
#include "gl/glproperty.h"


//! @file controllers.h Implementations of specific controllers

// Controller for `NiControllerManager` blocks
class ControllerManager final : public Controller
{
	QPointer<Node> parent;

public:
	ControllerManager( Node * _parent, NifFieldConst ctrlBlock );

	bool hasParent() const { return !parent.isNull(); }

	void updateTime( float ) override final {}

	void setSequence( const QString & seqName ) override final;

protected:
	void updateImpl( NifFieldConst changedBlock ) override final;
};


using ITransformInterpolator = IControllerInterpolatorTyped<Node>;

// Interpolator for 'NiTransformInterpolator' and 'NiKeyframeController'
class TransformInterpolator final : public ITransformInterpolator
{
	ValueInterpolatorVector3 translation;
	ValueInterpolatorMatrix rotation;
	ValueInterpolatorFloat scale;

public:
	TransformInterpolator( NifFieldConst _interpolatorBlock, Node * node, Controller * _parentController )
		: ITransformInterpolator( _interpolatorBlock, node, _parentController ) {}

protected:
	void updateDataImpl() override final;
	void applyTransformImpl( float time ) override final;
};

// Interpolator for 'NiBSplineCompTransformInterpolator'
class BSplineInterpolator final : public ITransformInterpolator
{
	float startTime = 0, stopTime = 0;

	struct SplineVars
	{
		uint off = USHRT_MAX;
		float mult = 0.0f;
		float bias = 0.0f;
	};
	SplineVars rotateVars, translationVars, scaleVars;

	NifFieldConst controlPointsRoot;
	uint nControlPoints = 0;
	int degree = 3;

public:
	BSplineInterpolator( NifFieldConst _interpolatorBlock, Node * node, Controller * _parentController )
		: ITransformInterpolator( _interpolatorBlock, node, _parentController ) {}

protected:
	void updateDataImpl() override final;
	void applyTransformImpl( float time ) override final;

private:
	template <typename T> bool interpolateValue( T & value, float interval, const SplineVars & vars ) const;
};

// Controller for `NiTransformController` and 'NiKeyframeController' blocks
DECLARE_INTERPOLATED_CONTROLLER( TransformController, Node, ITransformInterpolator )


// Controller for `NiMultiTargetTransformController` blocks
class MultiTargetTransformController final : public Controller
{
	QPointer<Node> parent;
	NodeList targetNodes;
	QVector<ITransformInterpolator *> transforms;

public:
	MultiTargetTransformController( Node * node, NifFieldConst ctrlBlock );
	virtual ~MultiTargetTransformController() { clearTransforms(); }

	bool hasParent() const { return !parent.isNull(); }

	void updateTime( float time ) override final;

	bool setNodeInterpolator( Node * node, NifFieldConst interpolarorBlock );

protected:
	void updateImpl( NifFieldConst changedBlock ) override final;

private:
	void clearTransforms();
	void removeTransformAt( int i );
};


// Interpolator for `NiVisController` blocks
class VisibilityInterpolator final : public IControllerInterpolatorTyped<Node>
{
	ValueInterpolatorBool interpolator;

public:
	VisibilityInterpolator( NifFieldConst _interpolatorBlock, Node * node, Controller * _parentController )
		: IControllerInterpolatorTyped( _interpolatorBlock, node, _parentController ) {}

protected:
	void updateDataImpl() override final;
	void applyTransformImpl( float time ) override final;
};

// Controller for `NiVisController` blocks
DECLARE_INTERPOLATED_CONTROLLER( VisibilityController, Node, VisibilityInterpolator )


// Interpolator  for `NiGeomMorpherController` blocks
class MorphInterpolator final : public IControllerInterpolatorTyped<Shape>
{
	const NifFieldConst morphDataEntry;
	QVector<Vector3> verts;
	ValueInterpolatorFloat interpolator;

public:
	MorphInterpolator( NifFieldConst _interpolatorBlock, Shape * shape, Controller * _parentController, NifFieldConst _morphDataEntry, NifFieldConst vertsRoot );

protected:
	void updateDataImpl() override final;
	void applyTransformImpl( float time ) override final;
};

// Controller for `NiGeomMorpherController` blocks
class MorphController final : public Controller
{
	QPointer<Shape> target;
	NifFieldConst dataBlock;
	QVector<MorphInterpolator *> morphs;
	QVector<Vector3> verts;

public:
	MorphController( Shape * shape, NifFieldConst ctrlBlock );
	virtual ~MorphController() { clearMorphs(); }

	bool hasTarget() const { return !target.isNull(); }

	void updateTime( float time ) override final;

protected:
	void updateImpl( NifFieldConst changedBlock ) override final;

private:
	void clearMorphs();
};


// Interpolator for `NiUVController` blocks
class UVInterpolator final : public IControllerInterpolatorTyped<Shape>
{
	static constexpr int UV_GROUPS_COUNT = 4;

	QVector<ValueInterpolatorFloat> interpolators;

public:
	UVInterpolator( NifFieldConst _interpolatorBlock, Shape * shape, Controller * _parentController )
		: IControllerInterpolatorTyped( _interpolatorBlock, shape, _parentController ), interpolators( UV_GROUPS_COUNT ) {}

protected:
	void updateDataImpl() override final;
	void applyTransformImpl( float time ) override final;
};

// Controller for `NiUVController` blocks
DECLARE_INTERPOLATED_CONTROLLER( UVController, Shape, UVInterpolator )


// Interpolator for `NiParticleSystemController` and other blocks
class ParticleInterpolator final : public IControllerInterpolatorTyped<Particles>
{
	struct Particle
	{
		Vector3 position;
		Vector3 velocity;
		Vector3 unknown;
		float lifetime = 0;
		float lifespan = 0;
		float lasttime = 0;
		short y = 0;
		ushort vertex = 0;
	};
	QVector<Particle> particles;

	struct Gravity
	{
		float force;
		int type;
		Vector3 position;
		Vector3 direction;

		Gravity( NifFieldConst block );
	};
	QVector<Gravity> gravities;

	QPointer<Node> emitNode;
	float emitStart = 0, emitStop = 0, emitRate = 0, emitLast = 0, emitAccu = 0;
	Vector3 emitRadius;

	float spd = 0, spdRnd = 0;
	float ttl = 0, ttlRnd = 0;

	float inc = 0, incRnd = 0;
	float dec = 0, decRnd = 0;

	float size = 0;
	float grow = 0;
	float fade = 0;

	ValueInterpolatorColor4 colorInterpolator;

public:
	ParticleInterpolator( NifFieldConst _interpolatorBlock, Particles * particlesControllable, Controller * _parentController )
		: IControllerInterpolatorTyped( _interpolatorBlock, particlesControllable, _parentController ) {}

protected:
	void updateDataImpl() override final;
	void applyTransformImpl( float time ) override final;

private:
	void startParticle( Particle & p, float localTime );
	void moveParticle( Particle & p, float deltaTime );
	void sizeParticle( Particle & p, float & size );
	void colorParticle( Particle & p, Color4 & color );
};

// Controller for `NiParticleSystemController` and other blocks
DECLARE_INTERPOLATED_CONTROLLER( ParticleController, Particles, ParticleInterpolator )


// Interpolator for 'NiAlphaController' blocks (MaterialProperty)
class AlphaInterpolator_Material final : public IControllerInterpolatorTyped<MaterialProperty>
{
	ValueInterpolatorFloat interpolator;

public:
	AlphaInterpolator_Material( NifFieldConst _interpolatorBlock, MaterialProperty * prop, Controller * _parentController )
		: IControllerInterpolatorTyped( _interpolatorBlock, prop, _parentController ) {}

protected:
	void updateDataImpl() override final;
	void applyTransformImpl( float time ) override final;
};

// Controller for 'NiAlphaController' blocks (MaterialProperty)
DECLARE_INTERPOLATED_CONTROLLER( AlphaController_Material, MaterialProperty, AlphaInterpolator_Material )


// Interpolator for 'BSNiAlphaPropertyTestRefController' blocks (AlphaProperty)
class AlphaInterpolator_Alpha final : public IControllerInterpolatorTyped<AlphaProperty>
{
	ValueInterpolatorFloat interpolator;

public:
	AlphaInterpolator_Alpha( NifFieldConst _interpolatorBlock, AlphaProperty * prop, Controller * _parentController )
		: IControllerInterpolatorTyped( _interpolatorBlock, prop, _parentController ) {}

protected:
	void updateDataImpl() override final;
	void applyTransformImpl( float time ) override final;
};

// Controller for 'BSNiAlphaPropertyTestRefController' blocks (AlphaProperty)
DECLARE_INTERPOLATED_CONTROLLER( AlphaController_Alpha, AlphaProperty, AlphaInterpolator_Alpha )


// Interpolator for 'NiMaterialColorController' blocks
class MaterialColorInterpolator final : public IControllerInterpolatorTyped<MaterialProperty>
{
	enum class ColorType
	{
		Ambient = 0,
		Diffuse = 1,
		Specular = 2,
		SelfIllum = 3,
	};
	ColorType colorType = ColorType::Ambient; //!< The color slot being controlled

	ValueInterpolatorColor3 interpolator;

public:
	MaterialColorInterpolator( NifFieldConst _interpolatorBlock, MaterialProperty * prop, Controller * _parentController )
		: IControllerInterpolatorTyped( _interpolatorBlock, prop, _parentController ) {}

protected:
	void updateDataImpl() override final;
	void applyTransformImpl( float time ) override final;
};

// Controller for 'NiMaterialColorController' blocks
DECLARE_INTERPOLATED_CONTROLLER( MaterialColorController, MaterialProperty, MaterialColorInterpolator )


// Common data for texture flip interpolators
struct TextureFlipData
{
	bool hasDelta = false;
	float delta = 0;
	int slot = 0;

	QVector<NifFieldConst> sources;
	ValueInterpolatorFloat interpolator;

	void updateData( IControllerInterpolator * ctrlInterpolator, const QString & sourcesName, const QString & sourceBlockType );
	void interpolate( NifFieldConst & sourceBlock, float time );
};

// Interpolator for 'NiFlipController' blocks (TexturingProperty)
class TextureFlipInterpolator_Texturing final : public IControllerInterpolatorTyped<TexturingProperty>
{
	TextureFlipData data;

public:
	TextureFlipInterpolator_Texturing( NifFieldConst _interpolatorBlock, TexturingProperty * prop, Controller * _parentController )
		: IControllerInterpolatorTyped( _interpolatorBlock, prop, _parentController ) {}

protected:
	void updateDataImpl() override final;
	void applyTransformImpl( float time ) override final;
};

// Controller for 'NiFlipController' blocks (TexturingProperty)
DECLARE_INTERPOLATED_CONTROLLER( TextureFlipController_Texturing, TexturingProperty, TextureFlipInterpolator_Texturing )

// Interpolator for 'NiFlipController' blocks (TextureProperty)
class TextureFlipInterpolator_Texture final : public IControllerInterpolatorTyped<TextureProperty>
{
	TextureFlipData data;

public:
	TextureFlipInterpolator_Texture( NifFieldConst _interpolatorBlock, TextureProperty * prop, Controller * _parentController )
		: IControllerInterpolatorTyped( _interpolatorBlock, prop, _parentController ) {}

protected:
	void updateDataImpl() override final;
	void applyTransformImpl( float time ) override final;
};

// Controller for 'NiFlipController' blocks (TextureProperty)
DECLARE_INTERPOLATED_CONTROLLER( TextureFlipController_Texture, TextureProperty, TextureFlipInterpolator_Texture )


// Interpolator for 'NiTextureTransformController' blocks
class TextureTransformInterpolator final : public IControllerInterpolatorTyped<TexturingProperty>
{
	ValueInterpolatorFloat interpolator;

	enum class OperationType
	{
		TranslateU = 0,
		TranslateV = 1,
		Rotate = 2,
		ScaleU = 3,
		ScaleV = 4,
	};
	OperationType operationType = OperationType::TranslateU;
	int textureSlot = 0;

public:
	TextureTransformInterpolator( NifFieldConst _interpolatorBlock, TexturingProperty * prop, Controller * _parentController )
		: IControllerInterpolatorTyped( _interpolatorBlock, prop, _parentController ) {}

protected:
	void updateDataImpl() override final;
	void applyTransformImpl( float time ) override final;
};

// Controller for 'NiTextureTransformController' blocks
DECLARE_INTERPOLATED_CONTROLLER( TextureTransformController, TexturingProperty, TextureTransformInterpolator )


// Interpolator for 'BSEffectShaderPropertyFloatController' blocks
class EffectFloatInterpolator final : public IControllerInterpolatorTyped<BSEffectShaderProperty>
{
	ValueInterpolatorFloat interpolator;

	enum class ValueType
	{
		Emissive_Multiple = 0,
		Falloff_Start_Angle = 1,
		Falloff_Stop_Angle = 2,
		Falloff_Start_Opacity = 3,
		Falloff_Stop_Opacity = 4,
		Alpha = 5,
		U_Offset = 6,
		U_Scale = 7,
		V_Offset = 8,
		V_Scale = 9
	};
	ValueType valueType = ValueType::Emissive_Multiple;

public:
	EffectFloatInterpolator(  NifFieldConst _interpolatorBlock, BSEffectShaderProperty * prop, Controller * _parentController )
		: IControllerInterpolatorTyped( _interpolatorBlock, prop, _parentController ) {}

protected:
	void updateDataImpl() override final;
	void applyTransformImpl( float time ) override final;
};

// Controller for 'BSEffectShaderPropertyFloatController' blocks
DECLARE_INTERPOLATED_CONTROLLER( EffectFloatController, BSEffectShaderProperty, EffectFloatInterpolator )


// Interpolator for 'BSEffectShaderPropertyColorController' blocks
class EffectColorInterpolator final : public IControllerInterpolatorTyped<BSEffectShaderProperty>
{
	ValueInterpolatorVector3 interpolator;
	int colorType = 0;

public:
	EffectColorInterpolator( NifFieldConst _interpolatorBlock, BSEffectShaderProperty * prop, Controller * _parentController )
		: IControllerInterpolatorTyped( _interpolatorBlock, prop, _parentController ) {}

protected:
	void updateDataImpl() override final;
	void applyTransformImpl( float time ) override final;
};

// Controller for 'BSEffectShaderPropertyColorController' blocks
DECLARE_INTERPOLATED_CONTROLLER( EffectColorController, BSEffectShaderProperty, EffectColorInterpolator )


// Interpolator for 'BSLightingShaderPropertyFloatController' blocks
class LightingFloatInterpolator final : public IControllerInterpolatorTyped<BSLightingShaderProperty>
{
	ValueInterpolatorFloat interpolator;

	enum class ValueType
	{
		Refraction_Strength = 0,
		Reflection_Strength = 8,
		Glossiness = 9,
		Specular_Strength = 10,
		Emissive_Multiple = 11,
		Alpha = 12,
		U_Offset = 20,
		U_Scale = 21,
		V_Offset = 22,
		V_Scale = 23
	};
	ValueType valueType = ValueType::Refraction_Strength;

public:
	LightingFloatInterpolator( NifFieldConst _interpolatorBlock, BSLightingShaderProperty * prop, Controller * _parentController )
		: IControllerInterpolatorTyped( _interpolatorBlock, prop, _parentController ) {}

protected:
	void updateDataImpl() override final;
	void applyTransformImpl( float time ) override final;
};

// Controller for 'BSLightingShaderPropertyFloatController' blocks
DECLARE_INTERPOLATED_CONTROLLER( LightingFloatController, BSLightingShaderProperty, LightingFloatInterpolator )


// Interpolator for 'BSLightingShaderPropertyColorController' blocks
class LightingColorInterpolator final : public  IControllerInterpolatorTyped<BSLightingShaderProperty>
{
	ValueInterpolatorVector3 interpolator;
	int colorType = 0;

public:
	LightingColorInterpolator( NifFieldConst _interpolatorBlock, BSLightingShaderProperty * prop, Controller * _parentController )
		: IControllerInterpolatorTyped( _interpolatorBlock, prop, _parentController ) {}

protected:
	void updateDataImpl() override final;
	void applyTransformImpl( float time ) override final;
};

// Controller for 'BSLightingShaderPropertyColorController' blocks
DECLARE_INTERPOLATED_CONTROLLER( LightingColorController, BSLightingShaderProperty, LightingColorInterpolator )

#endif // CONTROLLERS_H
