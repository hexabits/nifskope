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

#include "glproperty.h"

#include "message.h"
#include "gl/controllers.h"
#include "gl/glscene.h"
#include "gl/gltex.h"
#include "io/material.h"

#include <QOpenGLContext>


//! @file glproperty.cpp Encapsulation of NiProperty blocks defined in nif.xml

//! Helper function that checks texture sets
static inline bool checkSet( int s, const QVector<QVector<Vector2> > & texcoords )
{
	return s >= 0 && s < texcoords.count() && texcoords[s].count();
}

Property * Property::create( Scene * scene, const NifModel * nif, const QModelIndex & index )
{
	Property * property = nullptr;

	static const QSet<QString> oldShaderTypes = {
		// Fallout 3 - lighting shaders
		QStringLiteral("BSShaderLightingProperty"),
		QStringLiteral("BSShaderNoLightingProperty"),
		QStringLiteral("BSShaderPPLightingProperty"),
		QStringLiteral("Lighting30ShaderProperty"),
		QStringLiteral("SkyShaderProperty"),
		QStringLiteral("TileShaderProperty"),

		// Fallout 3 - other shaders
		QStringLiteral("WaterShaderProperty"),
		QStringLiteral("TallGrassShaderProperty"),

		// Other ancient shaders from nif.xml
		QStringLiteral("DistantLODShaderProperty"),
		QStringLiteral("HairShaderProperty"),
		QStringLiteral("BSDistantTreeShaderProperty"),
		QStringLiteral("VolumetricFogShaderProperty"),
	};

	auto block = nif->field( index );
	if ( !block ) {
		// Do nothing

	} else if ( !block.isBlock() ) {
		nif->reportError( tr("Property::create: item '%1' is not a block.").arg( block.repr() ) );

	} else if ( block.hasName("NiAlphaProperty") ) {
		property = new AlphaProperty( scene, block );

	} else if ( block.hasName("NiZBufferProperty") ) {
		property = new ZBufferProperty( scene, block );

	} else if ( block.hasName("NiTexturingProperty") ) {
		property = new TexturingProperty( scene, block );

	} else if ( block.hasName("NiTextureProperty") ) {
		property = new TextureProperty( scene, block );

	} else if ( block.hasName("NiMaterialProperty") ) {
		property = new MaterialProperty( scene, block );

	} else if ( block.hasName("NiSpecularProperty") ) {
		property = new SpecularProperty( scene, block );

	} else if ( block.hasName("NiWireframeProperty") ) {
		property = new WireframeProperty( scene, block );

	} else if ( block.hasName("NiVertexColorProperty") ) {
		property = new VertexColorProperty( scene, block );

	} else if ( block.hasName("NiStencilProperty") ) {
		property = new StencilProperty( scene, block );

	} else if ( block.hasName("BSLightingShaderProperty") ) {
		property = new BSLightingShaderProperty( scene, block );

	} else if ( block.hasName("BSEffectShaderProperty") ) {
		property = new BSEffectShaderProperty( scene, block );

	} else if ( block.hasName("BSWaterShaderProperty", "BSSkyShaderProperty") ) {
		property = new SkyrimSimpleShaderProperty( scene, block );

	} else if ( oldShaderTypes.contains( block.name() ) ) {
		property = new BSShaderProperty( scene, block );

	} else {
		nif->reportError( tr("Property::create: Could not create Property from a block of type '%1'.").arg( block.name() ) );
	}

	if ( property )
		property->update();

	return property;
}


// PropertyList class

PropertyList::~PropertyList()
{
	clear();
}

void PropertyList::clear()
{
	for ( Property * p : properties )
		detach( p, 1 );
	properties.clear();
}

PropertyList & PropertyList::operator=( const PropertyList & other )
{
	clear();
	for ( Property * p : other.properties )
		attach( p );
	return *this;
}

void PropertyList::add( Property * prop )
{
	if ( prop && !properties.contains( prop->type(), prop ) ) 
		attach( prop );
}

void PropertyList::del( Property * prop )
{
	if ( prop ) {
		int cnt = properties.remove( prop->type(), prop );
		if ( cnt > 0 )
			detach( prop, cnt );
	}
}

void PropertyList::validate()
{
	QList<Property *> rem;
	for ( Property * p : properties ) {
		if ( !p->isValid() )
			rem.append( p );
	}
	for ( Property * p : rem )
		del( p );
}

void PropertyList::merge( const PropertyList & other )
{
	for ( Property * p : other.properties ) {
		if ( !properties.contains( p->type() ) )
			attach( p );
	}
}

Property * PropertyList::get( const QModelIndex & iPropBlock ) const
{
	if ( iPropBlock.isValid() ) {
		for ( Property * p : properties ) {
			if ( p->index() == iPropBlock )
				return p;
		}
	}

	return nullptr;
}


// AlphaProperty class

void AlphaProperty::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	Property::updateImpl( nif, index );

	if ( index == iBlock ) {
		auto flags = block["Flags"].value<ushort>();

		alphaBlend = flags & 1;

		static const GLenum blendMap[16] = {
			GL_ONE, GL_ZERO, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR,
			GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
			GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_SRC_ALPHA_SATURATE, GL_ONE,
			GL_ONE, GL_ONE, GL_ONE, GL_ONE
		};

		alphaSrc = blendMap[ ( flags >> 1 ) & 0x0f ];
		alphaDst = blendMap[ ( flags >> 5 ) & 0x0f ];

		static const GLenum testMap[8] = {
			GL_ALWAYS, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_NEVER
		};

		alphaTest = flags & ( 1 << 9 );
		alphaFunc = testMap[ ( flags >> 10 ) & 0x7 ];
		alphaThreshold = float( block["Threshold"].value<int>() ) / 255.0;

		alphaSort = ( flags & 0x2000 ) == 0;

		// Temporary Weapon Blood fix for FO4
		if ( modelBSVersion() >= 130 )
			alphaTest |= (flags == 20547);
	}
}

Controller * AlphaProperty::createController( NifFieldConst controllerBlock )
{
	if ( controllerBlock.hasName("BSNiAlphaPropertyTestRefController") )
		return new AlphaController_Alpha( this, controllerBlock );

	return nullptr;
}

void glProperty( AlphaProperty * p )
{
	if ( p && p->alphaBlend && p->scene->hasOption(Scene::DoBlending) ) {
		glEnable( GL_BLEND );
		glBlendFunc( p->alphaSrc, p->alphaDst );
	} else {
		glDisable( GL_BLEND );
	}

	if ( p && p->alphaTest && p->scene->hasOption(Scene::DoBlending) ) {
		glEnable( GL_ALPHA_TEST );
		glAlphaFunc( p->alphaFunc, p->alphaThreshold );
	} else {
		glDisable( GL_ALPHA_TEST );
	}
}


// ZBufferProperty class

void ZBufferProperty::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	Property::updateImpl( nif, index );

	if ( index == iBlock ) {
		auto flags = block["Flags"].value<ushort>();
		depthTest = flags & 1;
		depthMask = flags & 2;
		static const GLenum depthMap[8] = {
			GL_ALWAYS, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_NEVER
		};

		// This was checking version 0x10000001 ?
		if ( modelVersionInRange( 0x04010012, 0x14000005 ) ) {
			depthFunc = depthMap[ block["Function"].value<int>() & 0x07 ];
		} else if ( modelVersion() >= 0x14010003 ) {
			depthFunc = depthMap[ (flags >> 2 ) & 0x07 ];
		} else {
			depthFunc = GL_LEQUAL;
		}
	}
}

void glProperty( ZBufferProperty * p )
{
	if ( p ) {
		if ( p->depthTest ) {
			glEnable( GL_DEPTH_TEST );
			glDepthFunc( p->depthFunc );
		} else {
			glDisable( GL_DEPTH_TEST );
		}

		glDepthMask( p->depthMask ? GL_TRUE : GL_FALSE );
	} else {
		glEnable( GL_DEPTH_TEST );
		glDepthFunc( GL_LESS );
		glDepthMask( GL_TRUE );
		glDepthFunc( GL_LEQUAL );
	}
}


// TexturingProperty class

void TexturingProperty::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	Property::updateImpl( nif, index );

	if ( index == iBlock ) {
		static const QString FIELD_NAMES[NUM_TEXTURES] = {
			QStringLiteral("Base Texture"),
			QStringLiteral("Dark Texture"),
			QStringLiteral("Detail Texture"),
			QStringLiteral("Gloss Texture"),
			QStringLiteral("Glow Texture"),
			QStringLiteral("Bump Map Texture"),
			QStringLiteral("Decal 0 Texture"),
			QStringLiteral("Decal 1 Texture"),
			QStringLiteral("Decal 2 Texture"),
			QStringLiteral("Decal 3 Texture")
		};

		for ( int t = 0; t < NUM_TEXTURES; t++ ) {
			auto texEntry = block.child( FIELD_NAMES[t] );
			if ( !texEntry ) {
				textures[t].sourceBlock = NifFieldConst();
				continue;
			}

			textures[t].sourceBlock = texEntry.child("Source").linkBlock("NiSourceTexture");
			textures[t].coordset = texEntry.child("UV Set").value<int>();
				
			int filterMode = 0, clampMode = 0;
			if ( modelVersion() <= 0x14010002 ) {
				filterMode = texEntry["Filter Mode"].value<int>();
				clampMode  = texEntry["Clamp Mode"].value<int>();
			} else {
				auto flags = texEntry["Flags"].value<ushort>();
				filterMode = ((flags & 0x0F00) >> 0x08);
				clampMode  = ((flags & 0xF000) >> 0x0C);
				textures[t].coordset = (flags & 0x00FF);
			}

			float af = 1.0;
			float max_af = get_max_anisotropy();
			// Let User Settings decide for trilinear
			if ( filterMode == GL_LINEAR_MIPMAP_LINEAR )
				af = max_af;

			// Override with value in NIF for 20.5+
			if ( modelVersion() >= 0x14050004 )
				af = std::min( max_af, float( texEntry["Max Anisotropy"].value<ushort>() ) );

			textures[t].maxAniso = std::max( 1.0f, std::min( af, max_af ) );

			// See OpenGL docs on glTexParameter and GL_TEXTURE_MIN_FILTER option
			// See also http://gregs-blog.com/2008/01/17/opengl-texture-filter-parameters-explained/
			switch ( filterMode ) {
			case 0:
				textures[t].filter = GL_NEAREST;
				break;             // nearest
			case 1:
				textures[t].filter = GL_LINEAR;
				break;             // bilinear
			case 2:
				textures[t].filter = GL_LINEAR_MIPMAP_LINEAR;
				break;             // trilinear
			case 3:
				textures[t].filter = GL_NEAREST_MIPMAP_NEAREST;
				break;             // nearest from nearest
			case 4:
				textures[t].filter = GL_NEAREST_MIPMAP_LINEAR;
				break;             // interpolate from nearest
			case 5:
				textures[t].filter = GL_LINEAR_MIPMAP_NEAREST;
				break;             // bilinear from nearest
			default:
				textures[t].filter = GL_LINEAR;
				break;
			}

			switch ( clampMode ) {
			case 0:
				textures[t].wrapS = GL_CLAMP;
				textures[t].wrapT = GL_CLAMP;
				break;
			case 1:
				textures[t].wrapS = GL_CLAMP;
				textures[t].wrapT = GL_REPEAT;
				break;
			case 2:
				textures[t].wrapS = GL_REPEAT;
				textures[t].wrapT = GL_CLAMP;
				break;
			default:
				textures[t].wrapS = GL_REPEAT;
				textures[t].wrapT = GL_REPEAT;
				break;
			}

			textures[t].hasTransform = texEntry.child("Has Texture Transform").value<int>();

			if ( textures[t].hasTransform ) {
				textures[t].translation = texEntry["Translation"].value<Vector2>();
				textures[t].tiling      = texEntry["Scale"].value<Vector2>();
				textures[t].rotation    = texEntry["Rotation"].value<float>();
				textures[t].center      = texEntry["Center"].value<Vector2>();
			} else {
				// we don't really need to set these since they won't be applied in bind() unless hasTransform is set
				textures[t].translation = Vector2();
				textures[t].tiling      = Vector2( 1.0, 1.0 );
				textures[t].rotation    = 0.0;
				textures[t].center      = Vector2( 0.5, 0.5 );
			}
		}
	}
}

bool TexturingProperty::bind( int id, const QString & fname )
{
	GLuint mipmaps = 0;

	if ( id >= 0 && id < NUM_TEXTURES ) {
		if ( !fname.isEmpty() )
			mipmaps = scene->bindTexture( fname );
		else
			mipmaps = scene->bindTexture( textures[id].sourceBlock.toIndex() );

		if ( mipmaps == 0 )
			return false;

		glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, textures[id].maxAniso );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmaps > 1 ? textures[id].filter : GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, textures[id].wrapS );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, textures[id].wrapT );
		glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
		glMatrixMode( GL_TEXTURE );
		glLoadIdentity();

		if ( textures[id].hasTransform ) {
			// Sign order is important here: get it backwards and we rotate etc.
			// around (-center, -center)
			glTranslatef( textures[id].center[0], textures[id].center[1], 0 );

			// rotation appears to be in radians
			glRotatef( rad2deg( textures[id].rotation ), 0, 0, 1 );
			// It appears that the scaling here is relative to center
			glScalef( textures[id].tiling[0], textures[id].tiling[1], 1 );
			glTranslatef( textures[id].translation[0], textures[id].translation[1], 0 );

			glTranslatef( -textures[id].center[0], -textures[id].center[1], 0 );
		}

		glMatrixMode( GL_MODELVIEW );
		return true;
	}

	return false;
}

bool TexturingProperty::bind( int id, const QVector<QVector<Vector2> > & texcoords )
{
	if ( checkSet( textures[id].coordset, texcoords ) && bind( id ) ) {
		glEnable( GL_TEXTURE_2D );
		glEnableClientState( GL_TEXTURE_COORD_ARRAY );
		glTexCoordPointer( 2, GL_FLOAT, 0, texcoords[ textures[id].coordset ].data() );
		return true;
	} else {
		glDisable( GL_TEXTURE_2D );
		return false;
	}
}

bool TexturingProperty::bind( int id, const QVector<QVector<Vector2> > & texcoords, int stage )
{
	return ( activateTextureUnit( stage ) && bind( id, texcoords ) );
}

QString TexturingProperty::fileName( int id ) const
{
	if ( id >= 0 && id < NUM_TEXTURES )
		return textures[id].sourceBlock.child("File Name").value<QString>();

	return QString();
}

int TexturingProperty::coordSet( int id ) const
{
	if ( id >= 0 && id < NUM_TEXTURES ) {
		return textures[id].coordset;
	}

	return -1;
}

Controller * TexturingProperty::createController( NifFieldConst controllerBlock )
{
	if ( controllerBlock.hasName("NiFlipController") )
		return new TextureFlipController_Texturing( this, controllerBlock );

	if ( controllerBlock.hasName("NiTextureTransformController") )
		return new TextureTransformController( this, controllerBlock );

	return nullptr;
}

int TexturingProperty::getId( const QString & texname )
{
	const static QHash<QString, int> hash{
		{ "base",   0 },
		{ "dark",   1 },
		{ "detail", 2 },
		{ "gloss",  3 },
		{ "glow",   4 },
		{ "bumpmap", 5 },
		{ "decal0", 6 },
		{ "decal1", 7 },
		{ "decal2", 8 },
		{ "decal3", 9 }
	};

	return hash.value( texname, -1 );
}

void glProperty( TexturingProperty * p )
{
	if ( p && p->scene->hasOption(Scene::DoTexturing) && p->bind(0) ) {
		glEnable( GL_TEXTURE_2D );
	}
}


// TextureProperty

void TextureProperty::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	Property::updateImpl( nif, index );

	if ( index == iBlock ) {
		imageBlock = block["Image"].linkBlock( "NiImage" );
	}
}

bool TextureProperty::bind()
{
	if ( GLuint mipmaps = scene->bindTexture( fileName() ) ) {
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmaps > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR );
		glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
		glMatrixMode( GL_TEXTURE );
		glLoadIdentity();
		glMatrixMode( GL_MODELVIEW );
		return true;
	}

	return false;
}

bool TextureProperty::bind( const QVector<QVector<Vector2> > & texcoords )
{
	if ( checkSet( 0, texcoords ) && bind() ) {
		glEnable( GL_TEXTURE_2D );
		glEnableClientState( GL_TEXTURE_COORD_ARRAY );
		glTexCoordPointer( 2, GL_FLOAT, 0, texcoords[ 0 ].data() );
		return true;
	} else {
		glDisable( GL_TEXTURE_2D );
		return false;
	}
}

QString TextureProperty::fileName() const
{
	return imageBlock.child("File Name").value<QString>();
}

Controller * TextureProperty::createController( NifFieldConst controllerBlock )
{
	if ( controllerBlock.hasName("NiFlipController") )
		return new TextureFlipController_Texture( this, controllerBlock );

	return nullptr;
}

void glProperty( TextureProperty * p )
{
	if ( p && p->scene->hasOption(Scene::DoTexturing) && p->bind() ) {
		glEnable( GL_TEXTURE_2D );
	}
}


// MaterialProperty and SpecularProperty classes

void MaterialProperty::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	Property::updateImpl( nif, index );

	if ( index == iBlock ) {
		alpha = std::clamp( block["Alpha"].value<float>(), 0.0f, 1.0f );

		ambient  = Color4( block.child("Ambient Color").value<Color3>() );
		diffuse  = Color4( block.child("Diffuse Color").value<Color3>() );
		specular = Color4( block.child("Specular Color").value<Color3>() );
		emissive = Color4( block.child("Emissive Color").value<Color3>() );

		// OpenGL needs shininess clamped otherwise it generates GL_INVALID_VALUE
		shininess = std::clamp( block["Glossiness"].value<float>(), 0.0f, 128.0f );
	}
}

Controller * MaterialProperty::createController( NifFieldConst controllerBlock )
{
	if ( controllerBlock.hasName("NiAlphaController") )
		return new AlphaController_Material( this, controllerBlock );

	if ( controllerBlock.hasName("NiMaterialColorController") )
		return new MaterialColorController( this, controllerBlock );

	return nullptr;
}

void SpecularProperty::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	Property::updateImpl( nif, index );

	if ( index == iBlock ) {
		spec = block["Flags"].value<int>() != 0;
	}
}

void glProperty( MaterialProperty * p, SpecularProperty * s )
{
	if ( p ) {
		glMaterial( GL_FRONT_AND_BACK, GL_AMBIENT, p->ambient.blend( p->alpha ) );
		glMaterial( GL_FRONT_AND_BACK, GL_DIFFUSE, p->diffuse.blend( p->alpha ) );
		glMaterial( GL_FRONT_AND_BACK, GL_EMISSION, p->emissive.blend( p->alpha ) );

		if ( !s || s->spec ) {
			glMaterialf( GL_FRONT_AND_BACK, GL_SHININESS, p->shininess );
			glMaterial( GL_FRONT_AND_BACK, GL_SPECULAR, p->specular.blend( p->alpha ) );
		} else {
			glMaterialf( GL_FRONT_AND_BACK, GL_SHININESS, 0.0 );
			glMaterial( GL_FRONT_AND_BACK, GL_SPECULAR, Color4( 0.0, 0.0, 0.0, p->alpha ) );
		}
	} else {
		Color4 a( 0.4f, 0.4f, 0.4f, 1.0f );
		Color4 d( 0.8f, 0.8f, 0.8f, 1.0f );
		Color4 s( 1.0f, 1.0f, 1.0f, 1.0f );
		glMaterialf( GL_FRONT_AND_BACK, GL_SHININESS, 33.0f );
		glMaterial( GL_FRONT_AND_BACK, GL_AMBIENT, a );
		glMaterial( GL_FRONT_AND_BACK, GL_DIFFUSE, d );
		glMaterial( GL_FRONT_AND_BACK, GL_SPECULAR, s );
	}
}


// WireframeProperty class

void WireframeProperty::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	Property::updateImpl( nif, index );

	if ( index == iBlock ) {
		wire = block["Flags"].value<int>() != 0;
	}
}

void glProperty( WireframeProperty * p )
{
	if ( p && p->wire ) {
		glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
		glLineWidth( 1.0 );
	} else {
		glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	}
}


// VertexColorProperty class

void VertexColorProperty::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	Property::updateImpl( nif, index );

	if ( index == iBlock ) {
		if ( modelVersion() <= 0x14010001 ) {
			vertexmode = block["Vertex Mode"].value<int>();
			// 0 : source ignore
			// 1 : source emissive
			// 2 : source ambient + diffuse
			lightmode = block["Lighting Mode"].value<int>();
			// 0 : emissive
			// 1 : emissive + ambient + diffuse
		} else {
			auto flags = block["Flags"].value<quint16>();
			vertexmode = (flags & 0x0030) >> 4;
			lightmode = (flags & 0x0008) >> 3;
		}
	}
}

void glProperty( VertexColorProperty * p, bool vertexcolors )
{
	// FIXME

	if ( !vertexcolors ) {
		glDisable( GL_COLOR_MATERIAL );
		glColor( Color4( 1.0, 1.0, 1.0, 1.0 ) );
		return;
	}

	if ( p ) {
		//if ( p->lightmode )
		{
			switch ( p->vertexmode ) {
			case 0:
				glDisable( GL_COLOR_MATERIAL );
				glColor( Color4( 1.0, 1.0, 1.0, 1.0 ) );
				return;
			case 1:
				glEnable( GL_COLOR_MATERIAL );
				glColorMaterial( GL_FRONT_AND_BACK, GL_EMISSION );
				return;
			case 2:
			default:
				glEnable( GL_COLOR_MATERIAL );
				glColorMaterial( GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE );
				return;
			}
		}
		//else
		//{
		//	glDisable( GL_LIGHTING );
		//	glDisable( GL_COLOR_MATERIAL );
		//}
	} else {
		glEnable( GL_COLOR_MATERIAL );
		glColorMaterial( GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE );
	}
}


// StencilProperty class

void StencilProperty::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	using namespace Stencil;
	Property::updateImpl( nif, index );

	if ( index == iBlock ) {
		static const GLenum funcMap[TEST_MAX] = {
			GL_NEVER, GL_GEQUAL, GL_NOTEQUAL, GL_GREATER, GL_LEQUAL, GL_EQUAL, GL_LESS, GL_ALWAYS
		};

		static const GLenum opMap[ACTION_MAX] = {
			GL_KEEP, GL_ZERO, GL_REPLACE, GL_INCR, GL_DECR, GL_INVERT
		};

		int drawMode = 0;
		if ( modelVersion() <= 0x14000005 ) {
			drawMode = block["Draw Mode"].value<int>();
			func     = funcMap[std::min( block["Stencil Function"].value<quint32>(), quint32(TEST_MAX) - 1 )];
			failop   = opMap[std::min( block["Fail Action"].value<quint32>(), quint32(ACTION_MAX) - 1 )];
			zfailop  = opMap[std::min( block["Z Fail Action"].value<quint32>(), quint32(ACTION_MAX) - 1 )];
			zpassop  = opMap[std::min( block["Pass Action"].value<quint32>(), quint32(ACTION_MAX) - 1 )];
			stencil  = ( block["Stencil Enabled"].value<quint8>() & ENABLE_MASK );
		} else {
			auto flags = block["Flags"].value<int>();
			drawMode = (flags & DRAW_MASK) >> DRAW_POS;
			func     = funcMap[(flags & TEST_MASK) >> TEST_POS];
			failop   = opMap[(flags & FAIL_MASK) >> FAIL_POS];
			zfailop  = opMap[(flags & ZFAIL_MASK) >> ZFAIL_POS];
			zpassop  = opMap[(flags & ZPASS_MASK) >> ZPASS_POS];
			stencil  = (flags & ENABLE_MASK);
		}

		switch ( drawMode ) {
		case DRAW_CW:
			cullEnable = true;
			cullMode = GL_FRONT;
			break;
		case DRAW_BOTH:
			cullEnable = false;
			cullMode = GL_BACK;
			break;
		case DRAW_CCW:
		default:
			cullEnable = true;
			cullMode = GL_BACK;
			break;
		}

		ref = block.child("Stencil Ref").value<quint32>();
		mask = block.child("Stencil Mask").value<quint32>();
	}
}

void glProperty( StencilProperty * p )
{
	if ( p ) {
		if ( p->cullEnable )
			glEnable( GL_CULL_FACE );
		else
			glDisable( GL_CULL_FACE );

		glCullFace( p->cullMode );

		if ( p->stencil ) {
			glEnable( GL_STENCIL_TEST );
			glStencilFunc( p->func, p->ref, p->mask );
			glStencilOp( p->failop, p->zfailop, p->zpassop );
		} else {
			glDisable( GL_STENCIL_TEST );
		}
	} else {
		glEnable( GL_CULL_FACE );
		glCullFace( GL_BACK );
		glDisable( GL_STENCIL_TEST );
	}
}


// Shader flags

typedef uint32_t ShaderFlagsType;


// Fallout 3 shader flags

enum class Fallout3_ShaderFlags1 : ShaderFlagsType // BSShaderFlags in nif.xml
{
	Specular = 1u << 0,
	Skinned = 1u << 1,
	LowDetail = 1u << 2,
	VertexAlpha = 1u << 3,
	Unknown4 = 1u << 4,
	SinglePass = 1u << 5,
	Empty = 1u << 6,
	EnvMap = 1u << 7,
	AlphaTexture = 1u << 8,
	Unknown9 = 1u << 9,
	FaceGen = 1u << 10,
	Parallax15 = 1u << 11,
	Unknown12 = 1u << 12,
	NonProjectiveShadows = 1u << 13,
	Unknown14 = 1u << 14,
	Refraction = 1u << 15,
	FireRefraction = 1u << 16,
	EyeEnvMap = 1u << 17,
	Hair = 1u << 18,
	DynamicAlpha = 1u << 19,
	LocalMapHideSecret = 1u << 20,
	WindowEnvMap = 1u << 21,
	TreeBillboard = 1u << 22,
	ShadowFrustrum = 1u << 23,
	MultipleTextures = 1u << 24,
	RemappableTextures = 1u << 25,
	DecalSinglePass = 1u << 26,
	DynamicDecalSinglePass = 1u << 27,
	ParallaxOcclusion = 1u << 28,
	ExternalEmittance = 1u << 29,
	ShadowMap = 1u << 30,
	ZBufferTest = 1u << 31,
};

enum class Fallout3_ShaderFlags2 : ShaderFlagsType // BSShaderFlags2 in nif.xml
{
	ZBufferWrite = 1u << 0,
	LODLandscape = 1u << 1,
	LODBuilding = 1u << 2,
	NoFade = 1u << 3,
	RefractionTint = 1u << 4,
	VertexColors = 1u << 5,
	Unknown6 = 1u << 6,
	FirstLightIsPointLight = 1u << 7,
	SecondLight = 1u << 8,
	ThirdLight = 1u << 9,
	VertexLighting = 1u << 10,
	UniformScale = 1u << 11,
	FitSlope = 1u << 12,
	BillboardAndEnvMapLightFade = 1u << 13,
	NoLODLandBlend = 1u << 14,
	EnvMapLightFade = 1u << 15,
	Wireframe = 1u << 16,
	VATSSelection = 1u << 17,
	ShowInLocalMap = 1u << 18,
	PremultAlpha = 1u << 19,
	SkipNormalMaps = 1u << 20,
	AlphaDecal = 1u << 21,
	NoTraansparencyMultisampling = 1u << 22,
	Unknown23 = 1u << 23,
	Unknown24 = 1u << 24,
	Unknown25 = 1u << 25,
	Unknown26 = 1u << 26,
	Unknown27 = 1u << 27,
	Unknown28 = 1u << 28,
	Unknown29 = 1u << 29,
	Unknown30 = 1u << 30,
	Unknown31 = 1u << 31,
};

class Fallout3_ShaderFlags
{
public:
	ShaderFlagsType flags1 = 0x82000000;
	ShaderFlagsType flags2 = 0x1;

private:
	bool has( Fallout3_ShaderFlags1 f ) const { return flags1 & ShaderFlagsType(f); }
	bool has( Fallout3_ShaderFlags2 f ) const { return flags2 & ShaderFlagsType(f); }
public:
	bool vertexColors() const { return has( Fallout3_ShaderFlags2::VertexColors ); }
	bool vertexAlpha() const { return has( Fallout3_ShaderFlags1::VertexAlpha ); }
	bool depthTest() const { return has( Fallout3_ShaderFlags1::ZBufferTest ); }
	bool depthWrite() const { return has( Fallout3_ShaderFlags2::ZBufferWrite ); }
};


// New shader flags (Skyrim and FO4)

enum class Skyrim_ShaderFlags1 : ShaderFlagsType // SkyrimShaderPropertyFlags1 in nif.xml
{
	Specular = 1u << 0,
	Skinned = 1u << 1,
	TempRefraction = 1u << 2,
	VertexAlpha = 1u << 3,
	GreyscaleToPaletteColor = 1u << 4,
	GreyscaleToPaletteAlpha = 1u << 5,
	UseFalloff = 1u << 6,
	EnvMap = 1u << 7,
	RecieveShadows = 1u << 8,
	CastShadows = 1u << 9,
	FaceGenDetailMap = 1u << 10,
	Parallax = 1u << 11,
	ModelSpaceNormals = 1u << 12,
	NonProjectiveShadows = 1u << 13,
	Landscape = 1u << 14,
	Refraction = 1u << 15,
	FireRefraction = 1u << 16,
	EyeEnvMap = 1u << 17,
	HairSoftLighting = 1u << 18,
	ScreendoorAlphaFade = 1u << 19,
	LocalMapHideSecret = 1u << 20,
	FaceGenRGBTint = 1u << 21,
	OwnEmit = 1u << 22,
	ProjectedUV = 1u << 23,
	MultipleTextures = 1u << 24,
	RemappableTextures = 1u << 25,
	Decal = 1u << 26,
	DynamicDecal = 1u << 27,
	ParallaxOcclusion = 1u << 28,
	ExternalEmittance = 1u << 29,
	SoftEffect = 1u << 30,
	ZBufferTest = 1u << 31,
};

enum class Skyrim_ShaderFlags2 : ShaderFlagsType // SkyrimShaderPropertyFlags2 in nif.xml
{
	ZBufferWrite = 1u << 0,
	LODLandscape = 1u << 1,
	LODObjects = 1u << 2,
	NoFade = 1u << 3,
	DoubleSided = 1u << 4,
	VertexColors = 1u << 5,
	GlowMap = 1u << 6,
	AssumeShadowMask = 1u << 7,
	PackedTangent = 1u << 8,
	MultiIndexSnow = 1u << 9,
	VertexLighting = 1u << 10,
	UniformScale = 1u << 11,
	FitSlope = 1u << 12,
	Billboard = 1u << 13,
	NoLODLandBlend = 1u << 14,
	EnvMapLightFade = 1u << 15,
	Wireframe = 1u << 16,
	WeaponBlood = 1u << 17,
	HideOnLocalMap = 1u << 18,
	PremultAlpha = 1u << 19,
	CloudLOD = 1u << 20,
	AnisotropicLighting = 1u << 21,
	NoTraansparencyMultisampling = 1u << 22,
	Unused23 = 1u << 23,
	MultiLayerParallax = 1u << 24,
	SoftLighting = 1u << 25,
	RimLighting = 1u << 26,
	BackLighting = 1u << 27,
	Unused28 = 1u << 28,
	TreeAnim = 1u << 29,
	EffectLighting = 1u << 30,
	HiDefLODObjects = 1u << 31,
};

enum class Fallout4_ShaderFlags1 : ShaderFlagsType // Fallout4ShaderPropertyFlags1 in nif.xml
{
	Specular = 1u << 0,
	Skinned = 1u << 1,
	TempRefraction = 1u << 2,
	VertexAlpha = 1u << 3,
	GreyscaleToPaletteColor = 1u << 4,
	GreyscaleToPaletteAlpha = 1u << 5,
	UseFalloff = 1u << 6,
	EnvMap = 1u << 7,
	RGBFalloff = 1u << 8,
	CastShadows = 1u << 9,
	Face = 1u << 10,
	UIMaskRects = 1u << 11,
	ModelSpaceNormals = 1u << 12,
	NonProjectiveShadows = 1u << 13,
	Landscape = 1u << 14,
	Refraction = 1u << 15,
	FireRefraction = 1u << 16,
	EyeEnvMap = 1u << 17,
	Hair = 1u << 18,
	ScreendoorAlphaFade = 1u << 19,
	LocalMapHideSecret = 1u << 20,
	SkinTint = 1u << 21,
	OwnEmit = 1u << 22,
	ProjectedUV = 1u << 23,
	MultipleTextures = 1u << 24,
	Tessellate = 1u << 25,
	Decal = 1u << 26,
	DynamicDecal = 1u << 27,
	CharacterLighting = 1u << 28,
	ExternalEmittance = 1u << 29,
	SoftEffect = 1u << 30,
	ZBufferTest = 1u << 31,
};

enum class Fallout4_ShaderFlags2 : ShaderFlagsType // Fallout4ShaderPropertyFlags2 in nif.xml
{
	ZBufferWrite = 1u << 0,
	LODLandscape = 1u << 1,
	LODObjects = 1u << 2,
	NoFade = 1u << 3,
	DoubleSided = 1u << 4,
	VertexColors = 1u << 5,
	GlowMap = 1u << 6,
	TransformChanged = 1u << 7,
	DismembermentMeatcuff = 1u << 8,
	Tint = 1u << 9,
	GrassVertexLighting = 1u << 10,
	GrassUniformScale = 1u << 11,
	GrassFitSlope = 1u << 12,
	GrassBillboard = 1u << 13,
	NoLODLandBlend = 1u << 14,
	Dismemberment = 1u << 15,
	Wireframe = 1u << 16,
	WeaponBlood = 1u << 17,
	HideOnLocalMap = 1u << 18,
	PremultAlpha = 1u << 19,
	VATSTarget = 1u << 20,
	AnisotropicLighting = 1u << 21,
	SkewSpecularAlpha = 1u << 22,
	MenuScreen = 1u << 23,
	MultiLayerParallax = 1u << 24,
	AlphaTest = 1u << 25,
	GradientRemap = 1u << 26,
	VATSTargetDrawAll = 1u << 27,
	PipboyScreen = 1u << 28,
	TreeAnim = 1u << 29,
	EffectLighting = 1u << 30,
	RefractionWritesDepth = 1u << 31,
};

class NewShaderFlags
{
public:
	bool isFO4 = false;
	ShaderFlagsType flags1 = 0;
	ShaderFlagsType flags2 = 0;

	void setFO4( bool _isFO4, bool isEffectsShader );

private:
	bool has( Skyrim_ShaderFlags1 f ) const { return ( flags1 & ShaderFlagsType(f) ); }
	bool has( Skyrim_ShaderFlags2 f ) const { return ( flags2 & ShaderFlagsType(f) ); }
	bool has( Fallout4_ShaderFlags1 f ) const { return ( flags1 & ShaderFlagsType(f) ); }
	bool has( Fallout4_ShaderFlags2 f ) const { return ( flags2 & ShaderFlagsType(f) ); }

	bool has( Skyrim_ShaderFlags1 f_sky, Fallout4_ShaderFlags1 f_fo4) const { return isFO4 ? has(f_fo4) : has(f_sky); }
	bool has( Skyrim_ShaderFlags2 f_sky, Fallout4_ShaderFlags2 f_fo4) const { return isFO4 ? has(f_fo4) : has(f_sky); }

public:
	bool vertexColors() const { return has( Skyrim_ShaderFlags2::VertexColors, Fallout4_ShaderFlags2::VertexColors ); }
	bool vertexAlpha() const { return has( Skyrim_ShaderFlags1::VertexAlpha, Fallout4_ShaderFlags1::VertexAlpha ); }
	bool treeAnim() const { return has( Skyrim_ShaderFlags2::TreeAnim, Fallout4_ShaderFlags2::TreeAnim ); }
	bool doubleSided() const { return has( Skyrim_ShaderFlags2::DoubleSided, Fallout4_ShaderFlags2::DoubleSided ); }
	bool depthTest() const { return has( Skyrim_ShaderFlags1::ZBufferTest, Fallout4_ShaderFlags1::ZBufferTest ); }
	bool depthWrite() const { return has( Skyrim_ShaderFlags2::ZBufferWrite, Fallout4_ShaderFlags2::ZBufferWrite ); }
	bool specular() const { return has( Skyrim_ShaderFlags1::Specular, Fallout4_ShaderFlags1::Specular ); }
	bool ownEmit() const { return has( Skyrim_ShaderFlags1::OwnEmit, Fallout4_ShaderFlags1::OwnEmit ); }
	bool envMap() const { return has( Skyrim_ShaderFlags1::EnvMap, Fallout4_ShaderFlags1::EnvMap ); }
	bool eyeEnvMap() const { return has( Skyrim_ShaderFlags1::EyeEnvMap, Fallout4_ShaderFlags1::EyeEnvMap ); }
	bool glowMap() const { return has( Skyrim_ShaderFlags2::GlowMap, Fallout4_ShaderFlags2::GlowMap ); }
	bool skyrimParallax() const { return ( !isFO4 && has( Skyrim_ShaderFlags1::Parallax ) ); }
	bool skyrimBackLighting() const { return ( !isFO4 && has( Skyrim_ShaderFlags2::BackLighting ) ); }
	bool skyrimRimLighting() const { return ( !isFO4 && has( Skyrim_ShaderFlags2::RimLighting ) ); }
	bool skyrimSoftLighting() const { return ( !isFO4 && has( Skyrim_ShaderFlags2::SoftLighting ) ); }
	bool skyrimMultiLayerParalax() const { return ( !isFO4 && has( Skyrim_ShaderFlags2::MultiLayerParallax ) ); }
	bool refraction() const { return has( Skyrim_ShaderFlags1::Refraction, Fallout4_ShaderFlags1::Refraction ); }
	bool greyscaleToPaletteColor() const { return has( Skyrim_ShaderFlags1::GreyscaleToPaletteColor, Fallout4_ShaderFlags1::GreyscaleToPaletteColor ); }
	bool greyscaleToPaletteAlpha() const { return has( Skyrim_ShaderFlags1::GreyscaleToPaletteAlpha, Fallout4_ShaderFlags1::GreyscaleToPaletteAlpha ); }
	bool useFalloff() const { return has( Skyrim_ShaderFlags1::UseFalloff, Fallout4_ShaderFlags1::UseFalloff ); }
	bool rgbFalloff() const { return ( isFO4 && has( Fallout4_ShaderFlags1::RGBFalloff ) ); }
	bool weaponBlood() const { return has( Skyrim_ShaderFlags2::WeaponBlood, Fallout4_ShaderFlags2::WeaponBlood ); }
	bool effectLighting() const { return has( Skyrim_ShaderFlags2::EffectLighting, Fallout4_ShaderFlags2::EffectLighting ); }
};

void NewShaderFlags::setFO4( bool _isFO4, bool isEffectsShader )
{
	isFO4 = _isFO4;
	if ( isEffectsShader ) {
		flags1 = 0x80000000; 
		flags2 = 0x20;
	} else if ( isFO4 ) {
		flags1 = 0x80400201;
		flags2 = 1;
	} else {
		flags1 = 0x82400301;
		flags2 = 0x8021;
	}
}

static const QMap<uint32_t, uint64_t> Fallout4_CRCFlagMap = {
	// SF1
	{ 1563274220u, uint64_t(Fallout4_ShaderFlags1::CastShadows) },
	{ 1740048692u, uint64_t(Fallout4_ShaderFlags1::ZBufferTest) },
	{ 3744563888u, uint64_t(Fallout4_ShaderFlags1::Skinned) },
	{ 2893749418u, uint64_t(Fallout4_ShaderFlags1::EnvMap) },
	{ 2333069810u, uint64_t(Fallout4_ShaderFlags1::VertexAlpha) },
	{ 314919375u,  uint64_t(Fallout4_ShaderFlags1::Face) },
	{ 442246519u,  uint64_t(Fallout4_ShaderFlags1::GreyscaleToPaletteColor) },
	{ 2901038324u, uint64_t(Fallout4_ShaderFlags1::GreyscaleToPaletteAlpha) },
	{ 3849131744u, uint64_t(Fallout4_ShaderFlags1::Decal) },
	{ 1576614759u, uint64_t(Fallout4_ShaderFlags1::DynamicDecal) },
	{ 2262553490u, uint64_t(Fallout4_ShaderFlags1::OwnEmit) },
	{ 1957349758u, uint64_t(Fallout4_ShaderFlags1::Refraction) },
	{ 1483897208u, uint64_t(Fallout4_ShaderFlags1::SkinTint) },
	{ 3448946507u, uint64_t(Fallout4_ShaderFlags1::RGBFalloff) },
	{ 2150459555u, uint64_t(Fallout4_ShaderFlags1::ExternalEmittance) },
	{ 2548465567u, uint64_t(Fallout4_ShaderFlags1::ModelSpaceNormals) },
	{ 3980660124u, uint64_t(Fallout4_ShaderFlags1::UseFalloff) },
	{ 3503164976u, uint64_t(Fallout4_ShaderFlags1::SoftEffect) },

	// SF2
	{ 3166356979u, uint64_t(Fallout4_ShaderFlags2::ZBufferWrite) << 32 },
	{ 2399422528u, uint64_t(Fallout4_ShaderFlags2::GlowMap) << 32 },
	{ 759557230u,  uint64_t(Fallout4_ShaderFlags2::DoubleSided) << 32 },
	{ 348504749u,  uint64_t(Fallout4_ShaderFlags2::VertexColors) << 32 },
	{ 2994043788u, uint64_t(Fallout4_ShaderFlags2::NoFade) << 32 },
	{ 2078326675u, uint64_t(Fallout4_ShaderFlags2::WeaponBlood) << 32 },
	{ 3196772338u, uint64_t(Fallout4_ShaderFlags2::TransformChanged) << 32 },
	{ 3473438218u, uint64_t(Fallout4_ShaderFlags2::EffectLighting) << 32 },
	{ 2896726515u, uint64_t(Fallout4_ShaderFlags2::LODObjects) << 32 },

	// TODO
	{ 731263983u, 0 }, // PBR
	{ 902349195u, 0 }, // REFRACTION FALLOFF
	{ 3030867718u, 0 }, // INVERTED_FADE_PATTERN
	{ 1264105798u, 0 }, // HAIRTINT
	{ 3707406987u, 0 }, //  NO_EXPOSURE
};

static void readNewShaderFlags( NewShaderFlags & flags, BSShaderProperty * prop, bool isEffectsShader )
{
	// Read flags fields
	if ( prop->modelBSVersion() >= 151 ) {
		flags.isFO4 = true;

		auto sfs = prop->block["SF1"].array<ShaderFlagsType>() + prop->block["SF2"].array<ShaderFlagsType>();
		uint64_t allFlags = 0;
		for ( auto sf : sfs )
			allFlags |= Fallout4_CRCFlagMap.value( sf, 0 );
		flags.flags1 = allFlags & uint64_t(MAXUINT32);
		flags.flags2 = allFlags >> 32;

	} else { // bsVersion < 151
		auto flagField1 = prop->block["Shader Flags 1"];
		auto flagField2 = prop->block["Shader Flags 2"];

		if ( flagField1.hasStrType("SkyrimShaderPropertyFlags1") ) {
			flags.setFO4( false, isEffectsShader );
			flags.flags1 = flagField1.value<ShaderFlagsType>();
		} else if ( flagField1.hasStrType("Fallout4ShaderPropertyFlags1") ) {
			flags.setFO4( true, isEffectsShader );
			flags.flags1 = flagField1.value<ShaderFlagsType>();
		} else {
			if ( flagField1 )
				flagField1.reportError( Property::tr("Unsupported value type '%1'.").arg( flagField1.strType() ) );
			// Fallback setVersion
			flags.setFO4( !flagField1 && flagField2.hasStrType("Fallout4ShaderPropertyFlags2"), isEffectsShader );
		}

		if ( flagField2.hasStrType("SkyrimShaderPropertyFlags2") ) {
			if ( flags.isFO4 ) {
				flagField2.reportError( Property::tr("Unexpected value type '%1'.").arg( flagField2.strType() ) );
			} else {
				flags.flags2 = flagField2.value<ShaderFlagsType>();
			}
		} else if ( flagField2.hasStrType("Fallout4ShaderPropertyFlags2") ) {
			if ( flags.isFO4 ) {
				flags.flags2 = flagField2.value<ShaderFlagsType>();
			} else {
				flagField2.reportError( Property::tr("Unexpected value type '%1'.").arg( flagField2.strType() ) );
			}
		} else if ( flagField2 ) {
			flagField2.reportError( Property::tr("Unsupported value type '%1'.").arg( flagField2.strType() ) );
		}
	}

	// Set common vertex flags in the property
	if ( prop->modelBSVersion() >= 130 ) {
		//  Always do vertex colors, incl. alphas, for FO4 and newer if colors present
		prop->vertexColorMode = ShaderColorMode::FromData;
		prop->hasVertexAlpha = true;
	} else {
		prop->vertexColorMode = flags.vertexColors() ? ShaderColorMode::Yes : ShaderColorMode::No;
		prop->hasVertexAlpha = flags.vertexAlpha();
	}
	prop->isVertexAlphaAnimation = flags.treeAnim();
}


// BSShaderProperty class

BSShaderProperty::~BSShaderProperty()
{
	if ( material )
		delete material;
}

void BSShaderProperty::clear()
{
	Property::clear();

	setMaterial( nullptr );
}

bool BSShaderProperty::isTranslucent() const
{
	return false;
}

bool BSShaderProperty::bind( int id, const QString & fname, TextureClampMode mode )
{
	auto mipmaps = scene->bindTexture( fname.isEmpty() ? fileName(id) : fname );
	if ( mipmaps == 0 )
		return false;

	switch ( mode )
	{
	case TextureClampMode::ClampS_ClampT:
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP );
		break;
	case TextureClampMode::ClampS_WrapT:
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT );
		break;
	case TextureClampMode::WrapS_ClampT:
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP );
		break;
	case TextureClampMode::MirrorS_MirrorT:
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT );
		break;
	case TextureClampMode::WrapS_WrapT:
	default:
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT );
		break;
	}

	glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, get_max_anisotropy() );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmaps > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR );
	glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	glMatrixMode( GL_TEXTURE );
	glLoadIdentity();
	glMatrixMode( GL_MODELVIEW );
	return true;
}

bool BSShaderProperty::bind( int id, const QVector<QVector<Vector2> > & texcoords )
{
	if ( checkSet( 0, texcoords ) && bind( id ) ) {
		glEnable( GL_TEXTURE_2D );
		glEnableClientState( GL_TEXTURE_COORD_ARRAY );
		glTexCoordPointer( 2, GL_FLOAT, 0, texcoords[0].data() );
		return true;
	}

	glDisable( GL_TEXTURE_2D );
	return false;
}

bool BSShaderProperty::bindCube( const QString & fname )
{
	if ( fname.isEmpty() || scene->bindTexture( fname ) == 0 )
		return false;

	glEnable( GL_TEXTURE_CUBE_MAP_SEAMLESS );

	glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
	glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	glMatrixMode( GL_TEXTURE );
	glLoadIdentity();
	glMatrixMode( GL_MODELVIEW );

	return true;
}

void BSShaderProperty::setMaterial( Material * newMaterial )
{
	if ( newMaterial && !newMaterial->isValid() ) {
		delete newMaterial;
		newMaterial = nullptr;
	}
	if ( material && material != newMaterial ) {
		delete material;
	}
	material = newMaterial;
}

Material * BSShaderProperty::createMaterial()
{
	return nullptr;
}

void BSShaderProperty::setTexturePath( int id, const QString & texPath )
{
	Q_ASSERT( id >= 0 );

	int nPaths = texturePaths.count();
	if ( id < nPaths ) {
		texturePaths[id] = texPath;
	} else if ( !texPath.isEmpty() ) {
		if ( id > nPaths )
			texturePaths.resize( id ); // Get them up to (id) count so the next append would get (id) index
		texturePaths.append( texPath );
	}
}

void BSShaderProperty::setTexturePathsFromTextureBlock()
{
	texturePaths = textureBlock["Textures"].array<QString>();
}

void BSShaderProperty::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	Property::updateImpl( nif, index );

	if ( index == iBlock ) {
		textureBlock = block.child("Texture Set").linkBlock("BSShaderTextureSet");
		iTextureSet = textureBlock.toIndex();

		setMaterial( createMaterial() );

		resetData();
		updateData();
	
	} else if ( index == iTextureSet ) {
		resetData();
		updateData();
	}
}

void BSShaderProperty::resetData()
{
	uvScale.reset();
	uvOffset.reset();
	clampMode = TextureClampMode::WrapS_WrapT;

	vertexColorMode = ShaderColorMode::FromData;
	hasVertexAlpha = false;

	depthTest = false;
	depthWrite = false;
	isDoubleSided = false;
	isVertexAlphaAnimation = false;

	texturePaths.clear();
}

void BSShaderProperty::updateData()
{
	Fallout3_ShaderFlags flags;
	NifFieldConst flagField;

	flagField = block.child("Shader Flags");
	if ( flagField.hasStrType("BSShaderFlags") ) {
		flags.flags1 = flagField.value<ShaderFlagsType>();
	} else if ( flagField ) {
		flagField.reportError( tr("Unsupported value type '%1'.").arg( flagField.strType() ) );
	}

	flagField = block.child("Shader Flags 2");
	if ( flagField.hasStrType("BSShaderFlags2") ) {
		flags.flags2 = flagField.value<ShaderFlagsType>();
	} else if ( flagField ) {
		flagField.reportError( tr("Unsupported value type '%1'.").arg( flagField.strType() ) );
	}

	// Judging by the amount of vanilla FO3 shapes with colors in the data and w/o Fallout3_ShaderFlags2::VertexColors in the shader flags,
	// vertex colors are applied in the game even if Fallout3_ShaderFlags2::VertexColors is not set.
	vertexColorMode = flags.vertexColors() ? ShaderColorMode::Yes : ShaderColorMode::FromData;
	
	if ( block.inherits("WaterShaderProperty") ) {
		hasVertexAlpha = true;
	} else {
		hasVertexAlpha = flags.vertexAlpha();
	}

	depthTest  = flags.depthTest();
	depthWrite = flags.depthWrite();

	auto clampField = block.child("Texture Clamp Mode");
	if ( clampField )
		clampMode = TextureClampMode( clampField.value<uint>() );

	// Textures
	if ( textureBlock ) {
		setTexturePathsFromTextureBlock();

	} else if ( block.hasName("SkyShaderProperty", "TileShaderProperty", "TallGrassShaderProperty") ) {
		setTexturePath( 0, block["File Name"] );

	} else if ( block.hasName("BSShaderNoLightingProperty") ) {
		setTexturePath( 2, block["File Name"] ); // The texture glow map
	}	
}

void glProperty( BSShaderProperty * p )
{
	if ( p && p->scene->hasOption(Scene::DoTexturing) && p->bind(0) ) {
		glEnable( GL_TEXTURE_2D );
	}
}


// BSLightingShaderProperty class

bool BSLightingShaderProperty::isTranslucent() const
{
	return alpha < 1.0 || hasRefraction;
}

Material * BSLightingShaderProperty::createMaterial()
{
	if ( name.endsWith(".bgsm", Qt::CaseInsensitive) )
		return new ShaderMaterial( name, scene->getGame() );

	return nullptr;
}

void BSLightingShaderProperty::resetData()
{
	BSShaderProperty::resetData();

	hasGlowMap = false;
	hasEmittance = false;
	hasSoftlight = false;
	hasBacklight = false;
	hasRimlight = false;
	hasSpecularMap = false;
	hasMultiLayerParallax = false;
	hasEnvironmentMap = false;
	useEnvironmentMask = false;
	hasHeightMap = false;
	hasRefraction = false;
	hasDetailMask = false;
	hasTintMask = false;
	hasTintColor = false;
	greyscaleColor = false;

	emissiveColor = Color3(0, 0, 0);
	emissiveMult = 1.0;

	specularColor = Color3(0, 0, 0);
	specularGloss = 0;
	specularStrength = 0;

	tintColor = Color3(0, 0, 0);

	alpha = 1.0;

	lightingEffect1 = 0.0;
	lightingEffect2 = 1.0;

	environmentReflection = 0.0;

	// Multi-layer properties
	innerThickness = 1.0;
	innerTextureScale.reset();
	outerRefractionStrength = 0.0;
	outerReflectionStrength = 1.0;

	fresnelPower = 5.0;
	paletteScale = 1.0;
	rimPower = 2.0;
	backlightPower = 0.0;
}

enum class Skyrim_ShaderType : uint32_t // BSLightingShaderType in nif.xml
{
	Default = 0,
	EnvMap = 1,
	Glow = 2,
	HeightMap = 3,
	FaceTint = 4,
	SkinTint = 5,
	HairTint = 6,
	ParallaxOcclusion = 7,
	MultiTextureLandscape = 8,
	LODLandscape = 9,
	Snow = 10,
	MultiLayerParallax = 11,
	TreeAnim = 12,
	LODObjects = 13,
	SnowSparkle = 14,
	LODObjectsHD = 15,
	EyeEnvMap = 16,
	Cloud = 17,
	LODLandscapeNoise = 18,
	MultiTextureLandscapeLODBlend = 19,
	Dismemberment = 20, // FO4
};

void BSLightingShaderProperty::updateData()
{
	NewShaderFlags flags;
	readNewShaderFlags( flags, this, false );

	ShaderMaterial * m = ( material && material->isValid() ) ? static_cast<ShaderMaterial*>(material) : nullptr;
	if ( m ) {
		alpha = m->fAlpha;

		uvScale.set(m->fUScale, m->fVScale);
		uvOffset.set(m->fUOffset, m->fVOffset);

		specularColor = Color3(m->cSpecularColor);
		specularGloss = m->fSmoothness;
		specularStrength = m->fSpecularMult;

		emissiveColor = Color3(m->cEmittanceColor);
		emissiveMult = m->fEmittanceMult;

		if ( m->bTileU && m->bTileV )
			clampMode = TextureClampMode::WrapS_WrapT;
		else if ( m->bTileU )
			clampMode = TextureClampMode::WrapS_ClampT;
		else if ( m->bTileV )
			clampMode = TextureClampMode::ClampS_WrapT;
		else
			clampMode = TextureClampMode::ClampS_ClampT;

		fresnelPower = m->fFresnelPower;
		greyscaleColor = m->bGrayscaleToPaletteColor;
		paletteScale = m->fGrayscaleToPaletteScale;

		hasSpecularMap = m->bSpecularEnabled && ( !m->textureList[2].isEmpty() || (modelBSVersion() >= 151 && !m->textureList[7].isEmpty()) );
		hasGlowMap = m->bGlowmap;
		hasEmittance = m->bEmitEnabled;
		hasBacklight = m->bBackLighting;
		hasRimlight = m->bRimLighting;
		hasSoftlight = m->bSubsurfaceLighting;
		rimPower = m->fRimPower;
		backlightPower = m->fBacklightPower;
		isDoubleSided = m->bTwoSided;
		depthTest = m->bZBufferTest;
		depthWrite = m->bZBufferWrite;

		hasEnvironmentMap = m->bEnvironmentMapping || m->bPBR;
		useEnvironmentMask = hasEnvironmentMap && !m->bGlowmap && !m->textureList[5].isEmpty();
		environmentReflection = m->fEnvironmentMappingMaskScale;

		if ( hasSoftlight )
			lightingEffect1 = m->fSubsurfaceLightingRolloff;

	} else { // m == nullptr

		Skyrim_ShaderType shaderType;
		if ( modelBSVersion() >= 151 ) {
			shaderType = Skyrim_ShaderType::EnvMap;
		} else {
			shaderType = Skyrim_ShaderType::Default;
			auto typeField = block["Shader Type"];
			if ( typeField.hasStrType("BSLightingShaderType") ) { // Skyrim or newer shader type
				shaderType = Skyrim_ShaderType( typeField.value<uint>() );
			} else if ( typeField ) {
				typeField.reportError( tr("Unsupported value type '%1'.").arg( typeField.strType() ) );
			}
		}

		auto texturesRoot = textureBlock["Textures"];
		auto hasTexture = [texturesRoot](int index) -> bool {
			return !texturesRoot.child( index ).value<QString>().isEmpty();
		};
		
		isDoubleSided = flags.doubleSided();
		depthTest = flags.depthTest();
		depthWrite = flags.depthWrite();

		alpha = block["Alpha"].value<float>();

		uvScale.set( block["UV Scale"].value<Vector2>() );
		uvOffset.set( block["UV Offset"].value<Vector2>() );
		clampMode = TextureClampMode( block["Texture Clamp Mode"].value<uint>() );

		// Specular
		if ( flags.specular() ) {
			specularColor = block["Specular Color"].value<Color3>();
			specularGloss = block.child("Glossiness").value<float>();
			if ( specularGloss == 0.0f ) // FO4
				specularGloss = block.child("Smoothness").value<float>();
			specularStrength = block["Specular Strength"].value<float>();
		}

		// Emissive
		emissiveColor = block["Emissive Color"].value<Color3>();
		emissiveMult = block["Emissive Multiple"].value<float>();

		hasEmittance = flags.ownEmit();
		hasGlowMap = ( shaderType == Skyrim_ShaderType::Glow ) && flags.glowMap() && hasTexture(2);

		// Version Dependent settings
		if ( modelBSVersion() < 130 ) {
			lightingEffect1 = block["Lighting Effect 1"].value<float>();
			lightingEffect2 = block["Lighting Effect 2"].value<float>();

			innerThickness = block.child("Parallax Inner Layer Thickness").value<float>();
			outerRefractionStrength = block.child("Parallax Refraction Scale").value<float>();
			outerReflectionStrength = block.child("Parallax Envmap Strength").value<float>();
			innerTextureScale.set( block.child("Parallax Inner Layer Texture Scale").value<Vector2>() );

			hasSpecularMap        = flags.specular() && hasTexture(7);
			hasHeightMap          = (shaderType == Skyrim_ShaderType::HeightMap) && flags.skyrimParallax() && hasTexture(3);
			hasBacklight          = flags.skyrimBackLighting();
			hasRimlight           = flags.skyrimRimLighting();
			hasSoftlight          = flags.skyrimSoftLighting();
			hasMultiLayerParallax = flags.skyrimMultiLayerParalax();
			hasRefraction         = flags.refraction();

			hasTintMask = (shaderType == Skyrim_ShaderType::FaceTint);
			hasDetailMask = hasTintMask;

			if ( shaderType == Skyrim_ShaderType::HairTint ) {
				hasTintColor = true;
				tintColor = block["Hair Tint Color"].value<Color3>();
			} else if ( shaderType == Skyrim_ShaderType::SkinTint ) {
				hasTintColor = true;
				tintColor = block["Skin Tint Color"].value<Color3>();
			}
		} else {
			hasSpecularMap  = flags.specular();
			greyscaleColor  = flags.greyscaleToPaletteColor();
			paletteScale    = block["Grayscale to Palette Scale"].value<float>();
			lightingEffect1 = block.child("Subsurface Rolloff").value<float>();
			backlightPower  = block.child("Backlight Power").value<float>();
			fresnelPower    = block["Fresnel Power"].value<float>();
		}

		// Environment Map, Mask and Reflection Scale
		hasEnvironmentMap = 
			( shaderType == Skyrim_ShaderType::EnvMap && flags.envMap() )
			|| ( shaderType == Skyrim_ShaderType::EyeEnvMap && flags.eyeEnvMap() )
			|| ( modelBSVersion() == 100 && hasMultiLayerParallax );
		
		useEnvironmentMask = hasEnvironmentMap && hasTexture(5);

		if ( shaderType == Skyrim_ShaderType::EnvMap )
			environmentReflection = block.child("Environment Map Scale").value<float>();
		else if ( shaderType == Skyrim_ShaderType::EyeEnvMap )
			environmentReflection = block["Eye Cubemap Scale"].value<float>();
	}

	// Textures
	if ( block.child("Root Material") ) {
		if ( m ) {
			constexpr int BGSM1_MAX = 9;
			constexpr int BGSM20_MAX = 10;

			auto tex = m->textures();
			auto nMatTextures = tex.count();
			if ( nMatTextures >= BGSM1_MAX ) {
				setTexturePath( 0, tex[0] ); // Diffuse
				setTexturePath( 1, tex[1] ); // Normal
				if ( m->bGlowmap ) {
					if ( nMatTextures == BGSM1_MAX ) {
						setTexturePath( 2, tex[5] ); // Glow
					} else if ( nMatTextures == BGSM20_MAX ) {
						setTexturePath( 2, tex[4] ); // Glow
					}
				}
				if ( m->bGrayscaleToPaletteColor ) {
					setTexturePath( 3, tex[3] ); // Greyscale
				}
				if ( m->bEnvironmentMapping ) {
					if ( nMatTextures == BGSM1_MAX )
						setTexturePath( 4, tex[4] ); // Cubemap
					setTexturePath( 5, tex[5] ); // Env Mask
				}
				if ( m->bSpecularEnabled ) {
					setTexturePath( 7, tex[2] ); // Specular
					if ( nMatTextures >= BGSM20_MAX ) {
						setTexturePath( 8, tex[6] ); // Reflect
						setTexturePath( 9, tex[7] ); // Lighting
					}
				}
			}
		}

	} else {
		setTexturePathsFromTextureBlock();
	}
}

Controller * BSLightingShaderProperty::createController( NifFieldConst controllerBlock )
{
	if ( controllerBlock.hasName("BSLightingShaderPropertyFloatController") )
		return new LightingFloatController( this, controllerBlock );

	if ( controllerBlock.hasName("BSLightingShaderPropertyColorController") )
		return new LightingColorController( this, controllerBlock );

	return nullptr;
}


// BSEffectShaderProperty class

bool BSEffectShaderProperty::isTranslucent() const
{
	return alpha() < 1.0;
}

Material * BSEffectShaderProperty::createMaterial()
{
	if ( name.endsWith(".bgem", Qt::CaseInsensitive) )
		return new EffectMaterial( name, scene->getGame() );

	return nullptr;
}

void BSEffectShaderProperty::resetData() 
{
	BSShaderProperty::resetData();

	hasSourceTexture = false;
	hasGreyscaleMap = false;
	hasEnvironmentMap = false;
	hasEnvironmentMask = false;
	hasNormalMap = false;
	useFalloff = false;
	hasRGBFalloff = false;

	greyscaleColor = false;
	greyscaleAlpha = false;

	hasWeaponBlood = false;

	falloff.startAngle   = 1.0f;
	falloff.stopAngle    = 0.0f;
	falloff.startOpacity = 1.0f;
	falloff.stopOpacity  = 0.0f;
	falloff.softDepth    = 1.0f;

	lumEmittance = 0.0;

	emissiveColor = Color4(0, 0, 0, 0);
	emissiveMult = 1.0;

	lightingInfluence = 0.0;
	environmentReflection = 0.0;
}

void BSEffectShaderProperty::updateData()
{
	NewShaderFlags flags;
	readNewShaderFlags( flags, this, true );

	EffectMaterial * m = ( material && material->isValid() ) ? static_cast<EffectMaterial*>(material) : nullptr;
	if ( m ) {
		hasSourceTexture = !m->textureList[0].isEmpty();
		hasGreyscaleMap = !m->textureList[1].isEmpty();
		hasEnvironmentMap = !m->textureList[2].isEmpty();
		hasNormalMap = !m->textureList[3].isEmpty();
		hasEnvironmentMask = !m->textureList[4].isEmpty();

		environmentReflection = m->fEnvironmentMappingMaskScale;

		greyscaleAlpha = m->bGrayscaleToPaletteAlpha;
		greyscaleColor = m->bGrayscaleToPaletteColor;
		useFalloff = m->bFalloffEnabled;
		hasRGBFalloff = m->bFalloffColorEnabled;

		depthTest = m->bZBufferTest;
		depthWrite = m->bZBufferWrite;
		isDoubleSided = m->bTwoSided;

		lumEmittance = m->fLumEmittance;

		uvScale.set(m->fUScale, m->fVScale);
		uvOffset.set(m->fUOffset, m->fVOffset);

		if ( m->bTileU && m->bTileV )
			clampMode = TextureClampMode::WrapS_WrapT;
		else if ( m->bTileU )
			clampMode = TextureClampMode::WrapS_ClampT;
		else if ( m->bTileV )
			clampMode = TextureClampMode::ClampS_WrapT;
		else
			clampMode = TextureClampMode::ClampS_ClampT;

		emissiveColor = Color4(m->cBaseColor, m->fAlpha);
		emissiveMult = m->fBaseColorScale;

		if ( m->bEffectLightingEnabled )
			lightingInfluence = m->fLightingInfluence;

		falloff.startAngle   = m->fFalloffStartAngle;
		falloff.stopAngle    = m->fFalloffStopAngle;
		falloff.startOpacity = m->fFalloffStartOpacity;
		falloff.stopOpacity  = m->fFalloffStopOpacity;
		falloff.softDepth    = m->fSoftDepth;

	} else { // m == nullptr
		
		hasSourceTexture = !block["Source Texture"].value<QString>().isEmpty();
		hasGreyscaleMap = !block["Greyscale Texture"].value<QString>().isEmpty();

		greyscaleAlpha = flags.greyscaleToPaletteAlpha();
		greyscaleColor = flags.greyscaleToPaletteColor();
		useFalloff = flags.useFalloff();

		depthTest = flags.depthTest();
		depthWrite = flags.depthWrite();
		isDoubleSided = flags.doubleSided();

		if ( modelBSVersion() < 130 ) {
			hasWeaponBlood = flags.weaponBlood();
		} else {
			hasEnvironmentMap = !block["Env Map Texture"].value<QString>().isEmpty();
			hasEnvironmentMask = !block["Env Mask Texture"].value<QString>().isEmpty();
			hasNormalMap = !block["Normal Texture"].value<QString>().isEmpty();

			environmentReflection = block["Environment Map Scale"].value<float>();

			// Receive Shadows -> RGB Falloff for FO4
			hasRGBFalloff = flags.rgbFalloff();
		}

		uvScale.set( block["UV Scale"].value<Vector2>() );
		uvOffset.set( block["UV Offset"].value<Vector2>() );
		clampMode = TextureClampMode( block["Texture Clamp Mode"].value<quint8>() );

		emissiveColor = block["Base Color"].value<Color4>();
		emissiveMult = block["Base Color Scale"].value<float>();

		if ( flags.effectLighting() )
			lightingInfluence = float( block["Lighting Influence"].value<quint8>() ) / 255.0;

		falloff.startAngle   = block["Falloff Start Angle"].value<float>();
		falloff.stopAngle    = block["Falloff Stop Angle"].value<float>();
		falloff.startOpacity = block["Falloff Start Opacity"].value<float>();
		falloff.stopOpacity  = block["Falloff Stop Opacity"].value<float>();
		falloff.softDepth    = block["Soft Falloff Depth"].value<float>();
	}

	// Textures
	if ( material ) {
		if ( m )
			texturePaths = m->textures().toVector();
	} else {
		setTexturePath( 0, block["Source Texture"] );
		setTexturePath( 1, block["Greyscale Texture"] );
		setTexturePath( 2, block.child("Env Map Texture") );
		setTexturePath( 3, block.child("Normal Texture") );
		setTexturePath( 4, block.child("Env Mask Texture") );
		setTexturePath( 6, block.child("Reflectance Texture") );
		setTexturePath( 7, block.child("Lighting Texture") );
	}
}

Controller * BSEffectShaderProperty::createController( NifFieldConst controllerBlock )
{
	if ( controllerBlock.hasName("BSEffectShaderPropertyFloatController") )
		return new EffectFloatController( this, controllerBlock );

	if ( controllerBlock.hasName("BSEffectShaderPropertyColorController") )
		return new EffectColorController( this, controllerBlock );

	return nullptr;
}


// SkyrimSimpleShaderProperty class

bool SkyrimSimpleShaderProperty::isTranslucent() const
{
	return block.inherits("BSSkyShaderProperty");
}

void SkyrimSimpleShaderProperty::updateData()
{
	NewShaderFlags flags;
	readNewShaderFlags( flags, this, false );

	depthTest = flags.depthTest();
	depthWrite = flags.depthWrite();
	isDoubleSided = flags.doubleSided();

	uvScale.set( block["UV Scale"].value<Vector2>() );
	uvOffset.set( block["UV Offset"].value<Vector2>() );

	if ( block.inherits("BSSkyShaderProperty") ) {
		setTexturePath( 0, block["Source Texture"] );
	}
}
