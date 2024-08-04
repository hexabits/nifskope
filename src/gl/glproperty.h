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

#ifndef GLPROPERTY_H
#define GLPROPERTY_H

#include "icontrollable.h" // Inherited
#include "data/niftypes.h"
#include "model/nifmodel.h"

#include <QHash>
#include <QPersistentModelIndex>
#include <QString>


//! @file glproperty.h Property, PropertyList

typedef unsigned int GLenum;
typedef int GLint;
typedef unsigned int GLuint;
typedef float GLfloat;


class Material;
class NifModel;

//! Controllable properties attached to nodes and meshes
class Property : public IControllable
{
	friend class PropertyList;

protected:
	//! Protected constructor; see IControllable()
	Property( Scene * _scene, NifFieldConst _block ) : IControllable( _scene, _block ) {}

	int ref = 0;

public:
	/*! Creates a Property based on the specified index of the specified model
	 *
	 * @param scene The Scene the property is in
	 * @param nif	The model
	 * @param index The index NiProperty
	 */
	static Property * create( Scene * scene, const NifModel * nif, const QModelIndex & index );

	enum Type
	{
		Alpha, ZBuffer, MaterialProp, Texturing, Texture, Specular, Wireframe, VertexColor, Stencil, ShaderLighting
	};

	virtual Type type() const = 0;
	virtual QString typeId() const = 0;

	template <typename T> static Type _type();
	template <typename T> T * cast()
	{
		if ( type() == _type<T>() )
			return static_cast<T *>( this );

		return nullptr;
	}
};

//! Associate a Property subclass with a Property::Type
#define REGISTER_PROPERTY( CLASSNAME, TYPENAME ) template <> inline Property::Type Property::_type<CLASSNAME>() { return Property::TYPENAME; }


//! A list of [Properties](@ref Property)
class PropertyList final
{
public:
	PropertyList() {}
	PropertyList( const PropertyList & other ) { operator=( other ); }
	~PropertyList();

	void clear();

	PropertyList & operator=( const PropertyList & other );

	void add( Property * prop );
	void del( Property * prop );

	void validate();

	void merge( const PropertyList & list );

	const QMultiHash<Property::Type, Property *> & hash() const { return properties; }

	Property * get( const QModelIndex & iPropBlock ) const;

	template <typename T> T * get() const;
	template <typename T> bool contains() const;

private:
	QMultiHash<Property::Type, Property *> properties;

	void attach( Property * prop );
	void detach( Property * prop, int cnt );
};

template <typename T> inline T * PropertyList::get() const
{
	Property * p = properties.value( Property::_type<T>() );
	return p ? p->cast<T>() : nullptr;
}

template <typename T> inline bool PropertyList::contains() const
{
	return properties.contains( Property::_type<T>() );
}

inline void PropertyList::attach( Property * prop )
{
	++prop->ref;
	properties.insert( prop->type(), prop );
}

inline void PropertyList::detach( Property * prop, int cnt )
{
	Q_ASSERT( cnt > 0 && prop->ref >= cnt );
	prop->ref -= cnt;
	if ( prop->ref <= 0 )
		delete prop;
}


//! A Property that specifies alpha blending
class AlphaProperty final : public Property
{
public:
	AlphaProperty( Scene * scene, NifFieldConst block ) : Property( scene, block ) {}

	Type type() const override final { return Alpha; }
	QString typeId() const override final { return QStringLiteral("NiAlphaProperty"); }

	bool hasAlphaBlend() const { return alphaBlend; }
	bool hasAlphaTest() const { return alphaTest; }

	GLfloat alphaThreshold = 0;

	friend void glProperty( AlphaProperty * );

protected:
	void setController( const NifModel * nif, const QModelIndex & controller ) override final;
	void updateImpl( const NifModel * nif, const QModelIndex & index ) override final;

	bool alphaBlend = false, alphaTest = false, alphaSort = false;
	GLenum alphaSrc = 0, alphaDst = 0, alphaFunc = 0;
};

REGISTER_PROPERTY( AlphaProperty, Alpha )

//! A Property that specifies depth testing
class ZBufferProperty final : public Property
{
public:
	ZBufferProperty( Scene * scene, NifFieldConst block ) : Property( scene, block ) {}

	Type type() const override final { return ZBuffer; }
	QString typeId() const override final { return QStringLiteral("NiZBufferProperty"); }

	bool test() const { return depthTest; }
	bool mask() const { return depthMask; }

	friend void glProperty( ZBufferProperty * );

protected:
	bool depthTest = false;
	bool depthMask = false;
	GLenum depthFunc = 0;

	void updateImpl( const NifModel * nif, const QModelIndex & index ) override final;
};

REGISTER_PROPERTY( ZBufferProperty, ZBuffer )

//! A Property that specifies (multi-)texturing
class TexturingProperty final : public Property
{
	friend class TexFlipController;
	friend class TexTransController;

	//! The properties of each texture slot
	struct TexDesc
	{
		QPersistentModelIndex iSource;
		GLenum filter = 0;
		GLint wrapS = 0, wrapT = 0;
		int coordset = 0;
		float maxAniso = 1.0;

		bool hasTransform = false;

		Vector2 translation;
		Vector2 tiling;
		float rotation = 0;
		Vector2 center;
	};

public:
	//! Number of textures; base + dark + detail + gloss + glow + bump + 4 decals
	static constexpr int NUM_TEXTURES = 10;

	TexturingProperty( Scene * scene, NifFieldConst block ) : Property( scene, block ) {}

	Type type() const override final { return Texturing; }
	QString typeId() const override final { return QStringLiteral("NiTexturingProperty"); }

	friend void glProperty( TexturingProperty * );

	bool bind( int id, const QString & fname = QString() );

	bool bind( int id, const QVector<QVector<Vector2> > & texcoords );
	bool bind( int id, const QVector<QVector<Vector2> > & texcoords, int stage );

	QString fileName( int id ) const;
	int coordSet( int id ) const;

	static int getId( const QString & id );

protected:
	TexDesc textures[NUM_TEXTURES];

	void setController( const NifModel * nif, const QModelIndex & controller ) override final;
	void updateImpl( const NifModel * nif, const QModelIndex & index ) override final;
};

REGISTER_PROPERTY( TexturingProperty, Texturing )

//! A Property that specifies a texture
class TextureProperty final : public Property
{
	friend class TexFlipController;

public:
	TextureProperty( Scene * scene, NifFieldConst block ) : Property( scene, block ) {}

	Type type() const override final { return Texture; }
	QString typeId() const override final { return QStringLiteral("NiTextureProperty"); }

	friend void glProperty( TextureProperty * );

	bool bind();
	bool bind( const QVector<QVector<Vector2> > & texcoords );

	QString fileName() const;

protected:
	NifFieldConst imageBlock;

	void setController( const NifModel * nif, const QModelIndex & controller ) override final;
	void updateImpl( const NifModel * nif, const QModelIndex & index ) override final;
};

REGISTER_PROPERTY( TextureProperty, Texture )

//! A Property that specifies a material
class MaterialProperty final : public Property
{
	friend class AlphaController;
	friend class MaterialColorController;

public:
	MaterialProperty( Scene * scene, NifFieldConst block ) : Property( scene, block ) {}

	Type type() const override final { return MaterialProp; }
	QString typeId() const override final { return QStringLiteral("NiMaterialProperty"); }

	friend void glProperty( class MaterialProperty *, class SpecularProperty * );

	GLfloat alphaValue() const { return alpha; }

protected:
	Color4 ambient, diffuse, specular, emissive;
	GLfloat shininess = 0, alpha = 0;

	void setController( const NifModel * nif, const QModelIndex & controller ) override final;
	void updateImpl( const NifModel * nif, const QModelIndex & index ) override final;
};

REGISTER_PROPERTY( MaterialProperty, MaterialProp )

//! A Property that specifies specularity
class SpecularProperty final : public Property
{
public:
	SpecularProperty( Scene * scene, NifFieldConst block ) : Property( scene, block ) {}

	Type type() const override final { return Specular; }
	QString typeId() const override final { return QStringLiteral("NiSpecularProperty"); }

	friend void glProperty( class MaterialProperty *, class SpecularProperty * );

protected:
	bool spec = false;

	void updateImpl( const NifModel * nif, const QModelIndex & index ) override final;
};

REGISTER_PROPERTY( SpecularProperty, Specular )

//! A Property that specifies wireframe drawing
class WireframeProperty final : public Property
{
public:
	WireframeProperty( Scene * scene, NifFieldConst block ) : Property( scene, block ) {}

	Type type() const override final { return Wireframe; }
	QString typeId() const override final { return QStringLiteral("NiWireframeProperty"); }

	friend void glProperty( WireframeProperty * );

protected:
	bool wire = false;

	void updateImpl( const NifModel * nif, const QModelIndex & index ) override final;
};

REGISTER_PROPERTY( WireframeProperty, Wireframe )

//! A Property that specifies vertex color handling
class VertexColorProperty final : public Property
{
public:
	VertexColorProperty( Scene * scene, NifFieldConst block ) : Property( scene, block ) {}

	Type type() const override final { return VertexColor; }
	QString typeId() const override final { return QStringLiteral("NiVertexColorProperty"); }

	friend void glProperty( VertexColorProperty *, bool vertexcolors );

protected:
	int lightmode = 0;
	int vertexmode = 0;

	void updateImpl( const NifModel * nif, const QModelIndex & index ) override final;
};

REGISTER_PROPERTY( VertexColorProperty, VertexColor )

namespace Stencil
{
	enum TestFunc
	{
		TEST_NEVER,
		TEST_LESS,
		TEST_EQUAL,
		TEST_LESSEQUAL,
		TEST_GREATER,
		TEST_NOTEQUAL,
		TEST_GREATEREQUAL,
		TEST_ALWAYS,
		TEST_MAX
	};

	enum Action
	{
		ACTION_KEEP,
		ACTION_ZERO,
		ACTION_REPLACE,
		ACTION_INCREMENT,
		ACTION_DECREMENT,
		ACTION_INVERT,
		ACTION_MAX
	};

	enum DrawMode
	{
		DRAW_CCW_OR_BOTH,
		DRAW_CCW,
		DRAW_CW,
		DRAW_BOTH,
		DRAW_MAX
	};
}

//! A Property that specifies stencil testing
class StencilProperty final : public Property
{
public:
	StencilProperty( Scene * scene, NifFieldConst block ) : Property( scene, block ) {}

	Type type() const override final { return Stencil; }
	QString typeId() const override final { return QStringLiteral("NiStencilProperty"); }

	friend void glProperty( StencilProperty * );

protected:
	enum
	{
		ENABLE_MASK = 0x0001,
		FAIL_MASK = 0x000E,
		FAIL_POS = 1,
		ZFAIL_MASK = 0x0070,
		ZFAIL_POS = 4,
		ZPASS_MASK = 0x0380,
		ZPASS_POS = 7,
		DRAW_MASK = 0x0C00,
		DRAW_POS = 10,
		TEST_MASK = 0x7000,
		TEST_POS = 12
	};

	bool stencil = false;

	GLenum func = 0;
	GLuint ref = 0;
	GLuint mask = 0xffffffff;

	GLenum failop = 0;
	GLenum zfailop = 0;
	GLenum zpassop = 0;

	bool cullEnable = false;
	GLenum cullMode = 0;

	void updateImpl( const NifModel * nif, const QModelIndex & index ) override final;
};

REGISTER_PROPERTY( StencilProperty, Stencil )


enum TexClampMode : unsigned int
{
	CLAMP_S_CLAMP_T	= 0,
	CLAMP_S_WRAP_T	= 1,
	WRAP_S_CLAMP_T	= 2,
	WRAP_S_WRAP_T 	= 3,
	MIRRORED_S_MIRRORED_T = 4
};


struct UVScale
{
	float x;
	float y;

	UVScale() { reset(); }	
	void reset() { x = y = 1.0f; }
	void set( float _x, float _y ) { x = _x; y = _y; }
	void set( const Vector2 & v) { x = v[0]; y = v[1]; }
};

struct UVOffset
{
	float x;
	float y;

	UVOffset() { reset(); }
	void reset() { x = y = 0.0f; }
	void set(float _x, float _y) { x = _x; y = _y; }
	void set(const Vector2 & v) { x = v[0]; y = v[1]; }
};

enum class ShaderColorMode
{
	NO,
	YES,
	FROM_DATA // Always matches the vertex color flag in the parent shape
};


//! A Property that specifies shader lighting (Bethesda-specific)
class BSShaderProperty : public Property
{
public:
	BSShaderProperty( Scene * scene, NifFieldConst block ) : Property( scene, block ) { }
	~BSShaderProperty();

	Type type() const override final { return ShaderLighting; }
	QString typeId() const override { return QStringLiteral("BSShaderProperty"); }

	friend void glProperty( BSShaderProperty * );

	void clear() override;

	bool bind( int id, const QString & fname = QString(), TexClampMode mode = TexClampMode::WRAP_S_WRAP_T );
	bool bind( int id, const QVector<QVector<Vector2> > & texcoords );

	bool bindCube( int id, const QString & fname = QString() );

	//! Checks if the params of the shader depend on data from block
	bool isParamBlock( const QModelIndex & block ) const { return ( block == iBlock || block == iTextureSet ); }

	QString fileName( int id ) const;
	//int coordSet( int id ) const;

	static int getId( const QString & id );


	ShaderColorMode vertexColorMode = ShaderColorMode::FROM_DATA;
	bool hasVertexAlpha = false;

	bool depthTest = false;
	bool depthWrite = false;
	bool isDoubleSided = false;
	bool isVertexAlphaAnimation = false;

	UVScale uvScale;
	UVOffset uvOffset;
	TexClampMode clampMode = CLAMP_S_CLAMP_T;

	Material * getMaterial() const { return material; }

protected:
	QPersistentModelIndex iTextureSet; // TODO: Remove
	NifFieldConst textureBlock;
	bool hasRootMaterial = false;

	Material * material = nullptr;
	void setMaterial( Material * newMaterial );

	void updateImpl( const NifModel * nif, const QModelIndex & index ) override;
	virtual void resetData();
};

REGISTER_PROPERTY( BSShaderProperty, ShaderLighting )


//! A Property that inherits BSShaderLightingProperty (FO3-specific)
class BSShaderLightingProperty final : public BSShaderProperty
{
public:
	BSShaderLightingProperty( Scene * scene, NifFieldConst block ) : BSShaderProperty( scene, block ) { }

	QString typeId() const override final { return QStringLiteral("BSShaderLightingProperty"); }

protected:
	void updateImpl( const NifModel * nif, const QModelIndex & index ) override;
	void updateData();
};

REGISTER_PROPERTY( BSShaderLightingProperty, ShaderLighting )


//! A Property that inherits BSLightingShaderProperty (Skyrim-specific)
class BSLightingShaderProperty final : public BSShaderProperty
{
public:
	BSLightingShaderProperty( Scene * scene, NifFieldConst block ) : BSShaderProperty( scene, block ) { }

	QString typeId() const override final { return QStringLiteral("BSLightingShaderProperty"); }

	bool hasGlowMap = false;
	bool hasEmittance = false;
	bool hasSoftlight = false;
	bool hasBacklight = false;
	bool hasRimlight = false;
	bool hasSpecularMap = false;
	bool hasMultiLayerParallax = false;
	bool hasEnvironmentMap = false;
	bool useEnvironmentMask = false;
	bool hasHeightMap = false;
	bool hasRefraction = false;
	bool hasDetailMask = false;
	bool hasTintMask = false;
	bool hasTintColor = false;
	bool greyscaleColor = false;

	Color3 emissiveColor;
	float emissiveMult = 1.0;

	Color3 specularColor;
	float specularGloss = 80.0;
	float specularStrength = 1.0;

	Color3 tintColor;

	float alpha = 1.0;

	float lightingEffect1 = 0.0;
	float lightingEffect2 = 1.0;

	float environmentReflection = 0.0;

	// Multi-layer properties
	float innerThickness = 1.0;
	UVScale innerTextureScale;
	float outerRefractionStrength = 0.0;
	float outerReflectionStrength = 1.0;

	float fresnelPower = 5.0;
	float paletteScale = 1.0;
	float rimPower = 2.0;
	float backlightPower = 0.0;

protected:
	void setController( const NifModel * nif, const QModelIndex & controller ) override final;
	void updateImpl( const NifModel * nif, const QModelIndex & index ) override;
	void resetData() override;
	void updateData();
};

REGISTER_PROPERTY( BSLightingShaderProperty, ShaderLighting )


//! A Property that inherits BSEffectShaderProperty (Skyrim-specific)
class BSEffectShaderProperty final : public BSShaderProperty
{
public:
	BSEffectShaderProperty( Scene * scene, NifFieldConst block ) : BSShaderProperty( scene, block ) { }

	QString typeId() const override final { return QStringLiteral("BSEffectShaderProperty"); }

	float getAlpha() const { return emissiveColor.alpha(); }

	bool hasSourceTexture = false;
	bool hasGreyscaleMap = false;
	bool hasEnvironmentMap = false;
	bool hasNormalMap = false;
	bool hasEnvironmentMask = false;
	bool useFalloff = false;
	bool hasRGBFalloff = false;

	bool greyscaleColor = false;
	bool greyscaleAlpha = false;

	bool hasWeaponBlood = false;

	struct Falloff
	{
		float startAngle = 1.0f;
		float stopAngle = 0.0f;

		float startOpacity = 1.0f;
		float stopOpacity = 0.0f;

		float softDepth = 1.0f;
	};
	Falloff falloff;

	float lumEmittance = 0.0;

	Color4 emissiveColor;
	float emissiveMult = 1.0;

	float lightingInfluence = 0.0;
	float environmentReflection = 0.0;

protected:
	void setController( const NifModel * nif, const QModelIndex & controller ) override final;
	void updateImpl( const NifModel * nif, const QModelIndex & index ) override;
	void resetData() override;
	void updateData();
};

REGISTER_PROPERTY( BSEffectShaderProperty, ShaderLighting )


namespace WaterShaderFlags
{
	enum SF1 : unsigned int
	{
		SWSF1_UNKNOWN0 = 1,
		SWSF1_Bypass_Refraction_Map = 1 << 1,
		SWSF1_Water_Toggle = 1 << 2,
		SWSF1_UNKNOWN3 = 1 << 3,
		SWSF1_UNKNOWN4 = 1 << 4,
		SWSF1_UNKNOWN5 = 1 << 5,
		SWSF1_Highlight_Layer_Toggle = 1 << 6,
		SWSF1_Enabled = 1 << 7
	};
}

//! A Property that inherits BSWaterShaderProperty (Skyrim-specific)
class BSWaterShaderProperty final : public BSShaderProperty
{
public:
	BSWaterShaderProperty( Scene * scene, NifFieldConst block ) : BSShaderProperty( scene, block ) { }

	QString typeId() const override final { return QStringLiteral("BSWaterShaderProperty"); }

	unsigned int getWaterShaderFlags() const;

	void setWaterShaderFlags( unsigned int );

protected:
	WaterShaderFlags::SF1 waterShaderFlags = WaterShaderFlags::SF1(0);
};

REGISTER_PROPERTY( BSWaterShaderProperty, ShaderLighting )


#endif
