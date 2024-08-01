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
bool checkSet( int s, const QVector<QVector<Vector2> > & texcoords )
{
	return s >= 0 && s < texcoords.count() && texcoords[s].count();
}

Property * Property::create( Scene * scene, const NifModel * nif, const QModelIndex & index )
{
	Property * property = 0;

	auto block = nif->field( index );
	if ( !block ) {
		// Do nothing
	} else if ( !block.isBlock() ) {
		block.reportError( tr("Could not create Property from a non-block field") );
	} else if ( block.hasName( "NiAlphaProperty" ) ) {
		property = new AlphaProperty( scene, block );
	} else if ( block.hasName( "NiZBufferProperty" ) ) {
		property = new ZBufferProperty( scene, block );
	} else if ( block.hasName( "NiTexturingProperty" ) ) {
		property = new TexturingProperty( scene, block );
	} else if ( block.hasName( "NiTextureProperty" ) ) {
		property = new TextureProperty( scene, block );
	} else if ( block.hasName( "NiMaterialProperty" ) ) {
		property = new MaterialProperty( scene, block );
	} else if ( block.hasName( "NiSpecularProperty" ) ) {
		property = new SpecularProperty( scene, block );
	} else if ( block.hasName( "NiWireframeProperty" ) ) {
		property = new WireframeProperty( scene, block );
	} else if ( block.hasName( "NiVertexColorProperty" ) ) {
		property = new VertexColorProperty( scene, block );
	} else if ( block.hasName( "NiStencilProperty" ) ) {
		property = new StencilProperty( scene, block );
	} else if ( block.hasName( "BSLightingShaderProperty" ) ) {
		property = new BSLightingShaderProperty( scene, block );
	} else if ( block.hasName( "BSEffectShaderProperty" ) ) {
		property = new BSEffectShaderProperty( scene, block );
	} else if ( block.hasName( "BSWaterShaderProperty" ) ) {
		property = new BSWaterShaderProperty( scene, block );
	} else if ( block.inherits( "BSShaderLightingProperty" ) ) {
		property = new BSShaderLightingProperty( scene, block );
	} else {
		block.reportError( tr("Could not create Property from a block of type '%1'").arg( block.name() ) );
	}

	if ( property )
		property->update( nif, index );

	return property;
}

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
		if ( nif->getBSVersion() >= 130 )
			alphaTest |= (flags == 20547);
	}
}

void AlphaProperty::setController( const NifModel * nif, const QModelIndex & controller )
{
	auto contrName = nif->itemName(controller);
	if ( contrName == "BSNiAlphaPropertyTestRefController" ) {
		Controller * ctrl = new AlphaController( this, controller );
		registerController(nif, ctrl);
	}
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
		if ( nif->checkVersion( 0x04010012, 0x14000005 ) ) {
			depthFunc = depthMap[ block["Function"].value<int>() & 0x07 ];
		} else if ( nif->checkVersion( 0x14010003, 0 ) ) {
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

/*
    TexturingProperty
*/

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
				textures[t].iSource = QModelIndex();
				continue;
			}

			textures[t].iSource  = texEntry.child("Source").linkBlock("NiSourceTexture").toIndex();
			textures[t].coordset = texEntry.child("UV Set").value<int>();
				
			int filterMode = 0, clampMode = 0;
			if ( nif->checkVersion( 0, 0x14010002 ) ) {
				filterMode = texEntry["Filter Mode"].value<int>();
				clampMode  = texEntry["Clamp Mode"].value<int>();
			} else if ( nif->checkVersion( 0x14010003, 0 ) ) {
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
			if ( nif->checkVersion( 0x14050004, 0 ) )
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
			mipmaps = scene->bindTexture( textures[ id ].iSource );

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
	if ( id >= 0 && id < NUM_TEXTURES ) {
		QModelIndex iSource = textures[id].iSource;
		auto nif = NifModel::fromValidIndex(iSource);
		if ( nif ) {
			return nif->get<QString>( iSource, "File Name" );
		}
	}

	return QString();
}

int TexturingProperty::coordSet( int id ) const
{
	if ( id >= 0 && id < NUM_TEXTURES ) {
		return textures[id].coordset;
	}

	return -1;
}


//! Set the appropriate Controller
void TexturingProperty::setController( const NifModel * nif, const QModelIndex & iController )
{
	auto contrName = nif->itemName(iController);
	if ( contrName == "NiFlipController" ) {
		Controller * ctrl = new TexFlipController( this, iController );
		registerController(nif, ctrl);
	} else if ( contrName == "NiTextureTransformController" ) {
		Controller * ctrl = new TexTransController( this, iController );
		registerController(nif, ctrl);
	}
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

/*
    TextureProperty
*/

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


void TextureProperty::setController( const NifModel * nif, const QModelIndex & iController )
{
	auto contrName = nif->itemName(iController);
	if ( contrName == "NiFlipController" ) {
		Controller * ctrl = new TexFlipController( this, iController );
		registerController(nif, ctrl);
	}
}

void glProperty( TextureProperty * p )
{
	if ( p && p->scene->hasOption(Scene::DoTexturing) && p->bind() ) {
		glEnable( GL_TEXTURE_2D );
	}
}

/*
    MaterialProperty
*/

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

void MaterialProperty::setController( const NifModel * nif, const QModelIndex & iController )
{
	auto contrName = nif->itemName(iController);
	if ( contrName == "NiAlphaController" ) {
		Controller * ctrl = new AlphaController( this, iController );
		registerController(nif, ctrl);
	} else if ( contrName == "NiMaterialColorController" ) {
		Controller * ctrl = new MaterialColorController( this, iController );
		registerController(nif, ctrl);
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

void SpecularProperty::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	Property::updateImpl( nif, index );

	if ( index == iBlock ) {
		spec = block["Flags"].value<int>() != 0;
	}
}

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

void VertexColorProperty::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	Property::updateImpl( nif, index );

	if ( index == iBlock ) {
		if ( nif->checkVersion( 0, 0x14010001 ) ) {
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
		if ( nif->checkVersion( 0, 0x14000005 ) ) {
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

/*
    BSShaderProperty
*/

typedef uint64_t ShaderFlagsType;

BSShaderProperty::~BSShaderProperty()
{
	if ( material )
		delete material;
}

void BSShaderProperty::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	Property::updateImpl( nif, index );

	if ( index == iBlock ) {
		textureBlock = block.child("Texture Set").linkBlock("BSShaderTextureSet");
		iTextureSet = textureBlock.toIndex();
		hasRootMaterial = block.child("Root Material").isValid();
	}
}

void BSShaderProperty::resetParams()
{
	uvScale.reset();
	uvOffset.reset();
	clampMode = WRAP_S_WRAP_T;

	vertexColorMode = ShaderColorMode::FROM_DATA;
	hasVertexAlpha = false;

	depthTest = false;
	depthWrite = false;
	isDoubleSided = false;
	isVertexAlphaAnimation = false;
}

void glProperty( BSShaderProperty * p )
{
	if ( p && p->scene->hasOption(Scene::DoTexturing) && p->bind(0) ) {
		glEnable( GL_TEXTURE_2D );
	}
}

void BSShaderProperty::clear()
{
	Property::clear();

	setMaterial(nullptr);
}

void BSShaderProperty::setMaterial( Material * newMaterial )
{
	if (newMaterial && !newMaterial->isValid()) {
		delete newMaterial;
		newMaterial = nullptr;
	}
	if ( material && material != newMaterial ) {
		delete material;
	}
	material = newMaterial;
}

bool BSShaderProperty::bind( int id, const QString & fname, TexClampMode mode )
{
	GLuint mipmaps = 0;

	if ( !fname.isEmpty() )
		mipmaps = scene->bindTexture( fname );
	else
		mipmaps = scene->bindTexture( this->fileName( id ) );

	if ( mipmaps == 0 )
		return false;


	switch ( mode )
	{
	case TexClampMode::CLAMP_S_CLAMP_T:
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP );
		break;
	case TexClampMode::CLAMP_S_WRAP_T:
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT );
		break;
	case TexClampMode::WRAP_S_CLAMP_T:
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP );
		break;
	case TexClampMode::MIRRORED_S_MIRRORED_T:
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT );
		break;
	case TexClampMode::WRAP_S_WRAP_T:
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
		glTexCoordPointer( 2, GL_FLOAT, 0, texcoords[ 0 ].data() );
		return true;
	}

	glDisable( GL_TEXTURE_2D );
	return false;
}

bool BSShaderProperty::bindCube( int id, const QString & fname )
{
	Q_UNUSED( id );

	GLuint result = 0;

	if ( !fname.isEmpty() )
		result = scene->bindTexture( fname );

	if ( result == 0 )
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

enum
{
	BGSM1_DIFFUSE = 0,
	BGSM1_NORMAL,
	BGSM1_SPECULAR,
	BGSM1_G2P,
	BGSM1_ENV,
	BGSM20_GLOW = 4,
	BGSM1_GLOW = 5,
	BGSM1_ENVMASK = 5,
	BGSM20_REFLECT,
	BGSM20_LIGHTING,

	BGSM1_MAX = 9,
	BGSM20_MAX = 10
};

QString BSShaderProperty::fileName( int id ) const
{
	// Fallout 4
	if ( hasRootMaterial ) {
		// BSLSP
		auto m = static_cast<ShaderMaterial *>(material);
		if ( m && m->isValid() ) {
			auto tex = m->textures();
			if ( tex.count() >= BGSM1_MAX ) {
				switch ( id ) {
				case 0: // Diffuse
					if ( !tex[BGSM1_DIFFUSE].isEmpty() )
						return tex[BGSM1_DIFFUSE];
					break;
				case 1: // Normal
					if ( !tex[BGSM1_NORMAL].isEmpty() )
						return tex[BGSM1_NORMAL];
					break;
				case 2: // Glow
					if ( tex.count() == BGSM1_MAX && m->bGlowmap && !tex[BGSM1_GLOW].isEmpty() )
						return tex[BGSM1_GLOW];

					if ( tex.count() == BGSM20_MAX && m->bGlowmap && !tex[BGSM20_GLOW].isEmpty() )
						return tex[BGSM20_GLOW];
					break;
				case 3: // Greyscale
					if ( m->bGrayscaleToPaletteColor && !tex[BGSM1_G2P].isEmpty() )
						return tex[BGSM1_G2P];
					break;
				case 4: // Cubemap
					if ( tex.count() == BGSM1_MAX && m->bEnvironmentMapping && !tex[BGSM1_ENV].isEmpty() )
						return tex[BGSM1_ENV];
					break;
				case 5: // Env Mask
					if ( m->bEnvironmentMapping && !tex[BGSM1_ENVMASK].isEmpty() )
						return tex[BGSM1_ENVMASK];
					break;
				case 7: // Specular
					if ( m->bSpecularEnabled && !tex[BGSM1_SPECULAR].isEmpty() )
						return tex[BGSM1_SPECULAR];
					break;
				}
			}
			if ( tex.count() >= BGSM20_MAX ) {
				switch ( id ) {
				case 8:
					if ( m->bSpecularEnabled && !tex[BGSM20_REFLECT].isEmpty() )
						return tex[BGSM20_REFLECT];
					break;
				case 9:
					if ( m->bSpecularEnabled && !tex[BGSM20_LIGHTING].isEmpty() )
						return tex[BGSM20_LIGHTING];
					break;
				}
			}
		}

		return QString();
	}

	// From textureBlock
	if ( textureBlock ) {
		return textureBlock["Textures"].child( id ).value<QString>();
	}

	// From material
	auto m = static_cast<EffectMaterial*>(material);
	if ( m ) {
		if (m->isValid()) {
			auto tex = m->textures();
			return tex[id];
		}

		return QString();
	}

	// Handle niobject name="BSEffectShaderProperty...
	switch ( id ) {
	case 0:
		return block.child("Source Texture").value<QString>();
	case 1:
		return block.child("Greyscale Texture").value<QString>();
	case 2:
		return block.child("Env Map Texture").value<QString>();
	case 3:
		return block.child("Normal Texture").value<QString>();
	case 4:
		return block.child("Env Mask Texture").value<QString>();
	case 6:
		return block.child("Reflectance Texture").value<QString>();
	case 7:
		return block.child("Lighting Texture").value<QString>();
	}

	return QString();
}

int BSShaderProperty::getId( const QString & id )
{
	const static QHash<QString, int> hash{
		{ QStringLiteral("base"),    0 },
		{ QStringLiteral("dark"),    1 },
		{ QStringLiteral("detail"),  2 },
		{ QStringLiteral("gloss"),   3 },
		{ QStringLiteral("glow"),    4 },
		{ QStringLiteral("bumpmap"), 5 },
		{ QStringLiteral("decal0"),  6 },
		{ QStringLiteral("decal1"),  7 }
	};

	return hash.value( id, -1 );
}


/*
	BSShaderLightingProperty
*/

void BSShaderLightingProperty::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	BSShaderProperty::updateImpl( nif, index );

	if ( index == iBlock || index == iTextureSet ) {
		updateParams(nif);
	}
}

enum class Fallout3_ShaderFlags1 : ShaderFlagsType // BSShaderFlags in nif.xml
{
	SPECULAR = 1u << 0,
	SKINNED = 1u << 1,
	LOWDETAIL = 1u << 2,
	VERTEX_ALPHA = 1u << 3,
	UNKNOWN_1 = 1u << 4,
	SINGLE_PASS = 1u << 5,
	EMPTY = 1u << 6,
	ENVMAP = 1u << 7,
	ALPHA_TEXTURE = 1u << 8,
	UNKNOWN_2 = 1u << 9,
	FACEGEN = 1u << 10,
	PARALLAX_15 = 1u << 11,
	UNKNOWN_3 = 1u << 12,
	NON_PROJECTIVE_SHADOWS = 1u << 13,
	UNKNOWN_4 = 1u << 14,
	REFRACTION = 1u << 15,
	FIRE_REFRACTION = 1u << 16,
	EYE_ENVMAP = 1u << 17,
	HAIR = 1u << 18,
	DYNAMIC_ALPHA = 1u << 19,
	LOCALMAP_HIDE_SECRET = 1u << 20,
	WINDOW_ENVMAP = 1u << 21,
	TREE_BILLBOARD = 1u << 22,
	SHADOW_FRUSTUM = 1u << 23,
	MULTIPLE_TEXTURES = 1u << 24,
	REMAPPABLE_TEXTURES = 1u << 25,
	DECAL_SINGLE_PASS = 1u << 26,
	DYNAMIC_DECAL_SINGLE_PASS = 1u << 27,
	PARALLAX_OCC = 1u << 28,
	EXTERNAL_EMITTANCE = 1u << 29,
	SHADOW_MAP = 1u << 30,
	ZBUFFER_TEST = 1u << 31,
};

enum class Fallout3_ShaderFlags2 : ShaderFlagsType // BSShaderFlags2 in nif.xml
{
	ZBUFFER_WRITE = 1u << 0,
	LOD_LANDSCAPE = 1u << 1,
	LOD_BUILDING = 1u << 2,
	NO_FADE = 1u << 3,
	REFRACTION_TINT = 1u << 4,
	VERTEX_COLORS = 1u << 5,
	UNKNOWN1 = 1u << 6,
	FIRST_LIGHT_IS_POINT_LIGHT = 1u << 7,
	SECOND_LIGHT = 1u << 8,
	THIRD_LIGHT = 1u << 9,
	VERTEX_LIGHTING = 1u << 10,
	UNIFORM_SCALE = 1u << 11,
	FIT_SLOPE = 1u << 12,
	BILLBOARD_AND_ENVMAP_LIGHT_FADE = 1u << 13,
	NO_LOD_LAND_BLEND = 1u << 14,
	ENVMAP_LIGHT_FADE = 1u << 15,
	WIREFRAME = 1u << 16,
	VATS_SELECTION = 1u << 17,
	SHOW_IN_LOCAL_MAP = 1u << 18,
	PREMULT_ALPHA = 1u << 19,
	SKIP_NORMAL_MAPS = 1u << 20,
	ALPHA_DECAL = 1u << 21,
	NO_TRANSPARENCY_MULTISAMPLING = 1u << 22,
	UNKNOWN2 = 1u << 23,
	UNKNOWN3 = 1u << 24,
	UNKNOWN4 = 1u << 25,
	UNKNOWN5 = 1u << 26,
	UNKNOWN6 = 1u << 27,
	UNKNOWN7 = 1u << 28,
	UNKNOWN8 = 1u << 29,
	UNKNOWN9 = 1u << 30,
	UNKNOWN10 = 1u << 31,
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
	bool vertexColors() const { return has( Fallout3_ShaderFlags2::VERTEX_COLORS ); }
	bool vertexAlpha() const { return has( Fallout3_ShaderFlags1::VERTEX_ALPHA ); }
	bool depthTest() const { return has( Fallout3_ShaderFlags1::ZBUFFER_TEST ); }
	bool depthWrite() const { return has( Fallout3_ShaderFlags2::ZBUFFER_WRITE ); }
};

void BSShaderLightingProperty::updateParams( const NifModel * nif )
{
	resetParams();

	Fallout3_ShaderFlags flags;
	NifFieldConst flagField;

	flagField = block["Shader Flags"];
	if ( flagField.hasStrType("BSShaderFlags") ) {
		flags.flags1 = flagField.value<uint>();
	} else if ( flagField ) {
		flagField.reportError( tr("Unsupported value type '%1'.").arg( flagField.strType() ) );
	}

	flagField = block["Shader Flags 2"];
	if ( flagField.hasStrType("BSShaderFlags2") ) {
		flags.flags2 = flagField.value<uint>();
	} else if ( flagField ) {
		flagField.reportError( tr("Unsupported value type '%1'.").arg( flagField.strType() ) );
	}

	// Gavrant: judging by the amount of vanilla FO3 shapes with colors in the data and w/o SF2_FO3_VERTEX_COLORS in the shader flags,
	// vertex colors are applied in the game even if SF2_FO3_VERTEX_COLORS is not set.
	vertexColorMode = flags.vertexColors() ? ShaderColorMode::YES : ShaderColorMode::FROM_DATA;
	hasVertexAlpha  = flags.vertexAlpha();

	depthTest  = flags.depthTest();
	depthWrite = flags.depthWrite();

	auto clampField = block["Texture Clamp Mode"];
	if ( clampField )
		clampMode = TexClampMode( clampField.value<uint>() );
}


/*
	BSLightingShaderProperty
*/

void BSLightingShaderProperty::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	BSShaderProperty::updateImpl( nif, index );

	if ( index == iBlock ) {
		setMaterial(name.endsWith(".bgsm", Qt::CaseInsensitive) ? new ShaderMaterial(name, scene->getGame()) : nullptr);
		updateParams(nif);
	}
	else if ( index == iTextureSet ) {
		updateParams(nif);
	}
}

void BSLightingShaderProperty::resetParams()
{
	BSShaderProperty::resetParams();

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

enum class Skyrim_ShaderFlags1 : ShaderFlagsType // SkyrimShaderPropertyFlags1 in nif.xml
{
	SPECULAR = 1u << 0,
	SKINNED = 1u << 1,
	TEMP_REFRACTION = 1u << 2,
	VERTEX_ALPHA = 1u << 3,
	GREYSCALE_TO_PALETTE_COLOR = 1u << 4,
	GREYSCALE_TO_PALETTE_ALPHA = 1u << 5,
	USE_FALLOFF = 1u << 6,
	ENVMAP = 1u << 7,
	RECIEVE_SHADOWS = 1u << 8,
	CAST_SHADOWS = 1u << 9,
	FACEGEN_DETAIL_MAP = 1u << 10,
	PARALLAX = 1u << 11,
	MODEL_SPACE_NORMALS = 1u << 12,
	NON_PROJECTIVE_SHADOWS = 1u << 13,
	LANDSCAPE = 1u << 14,
	REFRACTION = 1u << 15,
	FIRE_REFRACTION = 1u << 16,
	EYE_ENVMAP = 1u << 17,
	HAIR_SOFT_LIGHTING = 1u << 18,
	SCREENDOOR_ALPHA_FADE = 1u << 19,
	LOCALMAP_HIDE_SECRET = 1u << 20,
	FACEGEN_RGB_TINT = 1u << 21,
	OWN_EMIT = 1u << 22,
	PROJECTED_UV = 1u << 23,
	MULTIPLE_TEXTURES = 1u << 24,
	REMAPPABLE_TEXTURES = 1u << 25,
	DECAL = 1u << 26,
	DYNAMIC_DECAL = 1u << 27,
	PARALLAX_OCCLUSION = 1u << 28,
	EXTERNAL_EMITTANCE = 1u << 29,
	SOFT_EFFECT = 1u << 30,
	ZBUFFER_TEST = 1u << 31,
};

enum class Skyrim_ShaderFlags2 : ShaderFlagsType // SkyrimShaderPropertyFlags2 in nif.xml
{
	ZBUFFER_WRITE = 1u << 0,
	LOD_LANDSCAPE = 1u << 1,
	LOD_OBJECTS = 1u << 2,
	NO_FADE = 1u << 3,
	DOUBLE_SIDED = 1u << 4,
	VERTEX_COLORS = 1u << 5,
	GLOW_MAP = 1u << 6,
	ASSUME_SHADOWMASK = 1u << 7,
	PACKED_TANGENT = 1u << 8,
	MULTI_INDEX_SNOW = 1u << 9,
	VERTEX_LIGHTING = 1u << 10,
	UNIFORM_SCALE = 1u << 11,
	FIT_SLOPE = 1u << 12,
	BILLBOARD = 1u << 13,
	NO_LOD_LAND_BLEND = 1u << 14,
	ENVMAP_LIGHT_FADE = 1u << 15,
	WIREFRAME = 1u << 16,
	WEAPON_BLOOD = 1u << 17,
	HIDE_ON_LOCAL_MAP = 1u << 18,
	PREMULT_ALPHA = 1u << 19,
	CLOUD_LOD = 1u << 20,
	ANISOTROPIC_LIGHTING = 1u << 21,
	NO_TRANSPARENCY_MULTISAMPLING = 1u << 22,
	UNUSED01 = 1u << 23,
	MULTI_LAYER_PARALLAX = 1u << 24,
	SOFT_LIGHTING = 1u << 25,
	RIM_LIGHTING = 1u << 26,
	BACK_LIGHTING = 1u << 27,
	UNUSED02 = 1u << 28,
	TREE_ANIM = 1u << 29,
	EFFECT_LIGHTING = 1u << 30,
	HD_LOD_OBJECTS = 1u << 31,
};

enum class Fallout4_ShaderFlags1 : ShaderFlagsType // Fallout4ShaderPropertyFlags1 in nif.xml
{
	SPECULAR = 1u << 0,
	SKINNED = 1u << 1,
	TEMP_REFRACTION = 1u << 2,
	VERTEX_ALPHA = 1u << 3,
	GREYSCALE_TO_PALETTE_COLOR = 1u << 4,
	GREYSCALE_TO_PALETTE_ALPHA = 1u << 5,
	USE_FALLOFF = 1u << 6,
	ENVMAP = 1u << 7,
	RGB_FALLOFF = 1u << 8,
	CAST_SHADOWS = 1u << 9,
	FACE = 1u << 10,
	UI_MASK_RECTS = 1u << 11,
	MODEL_SPACE_NORMALS = 1u << 12,
	NON_PROJECTIVE_SHADOWS = 1u << 13,
	LANDSCAPE = 1u << 14,
	REFRACTION = 1u << 15,
	FIRE_REFRACTION = 1u << 16,
	EYE_ENVMAP = 1u << 17,
	HAIR = 1u << 18,
	SCREENDOOR_ALPHA_FADE = 1u << 19,
	LOCALMAP_HIDE_SECRET = 1u << 20,
	SKIN_TINT = 1u << 21,
	OWN_EMIT = 1u << 22,
	PROJECTED_UV = 1u << 23,
	MULTIPLE_TEXTURES = 1u << 24,
	TESSELLATE = 1u << 25,
	DECAL = 1u << 26,
	DYNAMIC_DECAL = 1u << 27,
	CHARACTER_LIGHTING = 1u << 28,
	EXTERNAL_EMITTANCE = 1u << 29,
	SOFT_EFFECT = 1u << 30,
	ZBUFFER_TEST = 1u << 31,
};

enum class Fallout4_ShaderFlags2 : ShaderFlagsType // Fallout4ShaderPropertyFlags2 in nif.xml
{
	ZBUFFER_WRITE = 1u << 0,
	LOD_LANDSCAPE = 1u << 1,
	LOD_OBJECTS = 1u << 2,
	NO_FADE = 1u << 3,
	DOUBLE_SIDED = 1u << 4,
	VERTEX_COLORS = 1u << 5,
	GLOW_MAP = 1u << 6,
	TRANSFORM_CHANGED = 1u << 7,
	DISMEMBERMENT_MEATCUFF = 1u << 8,
	TINT = 1u << 9,
	GRASS_VERTEX_LIGHTING = 1u << 10,
	GRASS_UNIFORM_SCALE = 1u << 11,
	GRASS_FIT_SLOPE = 1u << 12,
	GRASS_BILLBOARD = 1u << 13,
	NO_LOD_LAND_BLEND = 1u << 14,
	DISMEMBERMENT = 1u << 15,
	WIREFRAME = 1u << 16,
	WEAPON_BLOOD = 1u << 17,
	HIDE_ON_LOCAL_MAP = 1u << 18,
	PREMULT_ALPHA = 1u << 19,
	VATS_TARGET = 1u << 20,
	ANISOTROPIC_LIGHTING = 1u << 21,
	SKEW_SPECULAR_ALPHA = 1u << 22,
	MENU_SCREEN = 1u << 23,
	MULTI_LAYER_PARALLAX = 1u << 24,
	ALPHA_TEST = 1u << 25,
	GRADIENT_REMAP = 1u << 26,
	VATS_TARGET_DRAW_ALL = 1u << 27,
	PIPBOY_SCREEN = 1u << 28,
	TREE_ANIM = 1u << 29,
	EFFECT_LIGHTING = 1u << 30,
	REFRACTION_WRITES_DEPTH = 1u << 31,
};

class NewShaderFlags
{
public:
	bool isFO4 = false;
	ShaderFlagsType flags1 = 0;
	ShaderFlagsType flags2 = 0;

	void setVersion( bool _isFO4, bool isEffectsShader );

private:
	bool has( Skyrim_ShaderFlags1 f ) const { return ( flags1 & ShaderFlagsType(f) ); }
	bool has( Skyrim_ShaderFlags2 f ) const { return ( flags2 & ShaderFlagsType(f) ); }
	bool has( Fallout4_ShaderFlags1 f ) const { return ( flags1 & ShaderFlagsType(f) ); }
	bool has( Fallout4_ShaderFlags2 f ) const { return ( flags2 & ShaderFlagsType(f) ); }

	bool has( Skyrim_ShaderFlags1 f_sky, Fallout4_ShaderFlags1 f_fo4) const { return isFO4 ? has(f_fo4) : has(f_sky); }
	bool has( Skyrim_ShaderFlags2 f_sky, Fallout4_ShaderFlags2 f_fo4) const { return isFO4 ? has(f_fo4) : has(f_sky); }

public:
	bool vertexColors() const { return has( Skyrim_ShaderFlags2::VERTEX_COLORS, Fallout4_ShaderFlags2::VERTEX_COLORS ); }
	bool vertexAlpha() const { return has( Skyrim_ShaderFlags1::VERTEX_ALPHA, Fallout4_ShaderFlags1::VERTEX_ALPHA ); }
	bool treeAnim() const { return has( Skyrim_ShaderFlags2::TREE_ANIM, Fallout4_ShaderFlags2::TREE_ANIM ); }
	bool doubleSided() const { return has( Skyrim_ShaderFlags2::DOUBLE_SIDED, Fallout4_ShaderFlags2::DOUBLE_SIDED ); }
	bool depthTest() const { return has( Skyrim_ShaderFlags1::ZBUFFER_TEST, Fallout4_ShaderFlags1::ZBUFFER_TEST ); }
	bool depthWrite() const { return has( Skyrim_ShaderFlags2::ZBUFFER_WRITE, Fallout4_ShaderFlags2::ZBUFFER_WRITE ); }
	bool specular() const { return has( Skyrim_ShaderFlags1::SPECULAR, Fallout4_ShaderFlags1::SPECULAR ); }
	bool ownEmit() const { return has( Skyrim_ShaderFlags1::OWN_EMIT, Fallout4_ShaderFlags1::OWN_EMIT ); }
	bool envMap() const { return has( Skyrim_ShaderFlags1::ENVMAP, Fallout4_ShaderFlags1::ENVMAP ); }
	bool eyeEnvMap() const { return has( Skyrim_ShaderFlags1::EYE_ENVMAP, Fallout4_ShaderFlags1::EYE_ENVMAP ); }
	bool glowMap() const { return has( Skyrim_ShaderFlags2::GLOW_MAP, Fallout4_ShaderFlags2::GLOW_MAP ); }
	bool skyrimParallax() const { return ( !isFO4 && has( Skyrim_ShaderFlags1::PARALLAX ) ); }
	bool skyrimBackLighting() const { return ( !isFO4 && has( Skyrim_ShaderFlags2::BACK_LIGHTING ) ); }
	bool skyrimRimLighting() const { return ( !isFO4 && has( Skyrim_ShaderFlags2::RIM_LIGHTING ) ); }
	bool skyrimSoftLighting() const { return ( !isFO4 && has( Skyrim_ShaderFlags2::SOFT_LIGHTING ) ); }
	bool skyrimMultiLayerParalax() const { return ( !isFO4 && has( Skyrim_ShaderFlags2::MULTI_LAYER_PARALLAX ) ); }
	bool refraction() const { return has( Skyrim_ShaderFlags1::REFRACTION, Fallout4_ShaderFlags1::REFRACTION ); }
	bool greyscaleToPaletteColor() const { return has( Skyrim_ShaderFlags1::GREYSCALE_TO_PALETTE_COLOR, Fallout4_ShaderFlags1::GREYSCALE_TO_PALETTE_COLOR ); }
	bool greyscaleToPaletteAlpha() const { return has( Skyrim_ShaderFlags1::GREYSCALE_TO_PALETTE_ALPHA, Fallout4_ShaderFlags1::GREYSCALE_TO_PALETTE_ALPHA); }
	bool useFalloff() const { return has( Skyrim_ShaderFlags1::USE_FALLOFF, Fallout4_ShaderFlags1::USE_FALLOFF ); }
	bool rgbFalloff() const { return ( isFO4 && has( Fallout4_ShaderFlags1::RGB_FALLOFF ) ); }
	bool weaponBlood() const { return has( Skyrim_ShaderFlags2::WEAPON_BLOOD, Fallout4_ShaderFlags2::WEAPON_BLOOD ); }
	bool effectLighting() const { return has( Skyrim_ShaderFlags2::EFFECT_LIGHTING, Fallout4_ShaderFlags2::EFFECT_LIGHTING ); }
};

void NewShaderFlags::setVersion( bool _isFO4, bool isEffectsShader )
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

static const QMap<quint32, quint64> Fallout4_CRCFlagMap = {
	// SF1
	{ 1563274220u, quint64(Fallout4_ShaderFlags1::CAST_SHADOWS) },
	{ 1740048692u, quint64(Fallout4_ShaderFlags1::ZBUFFER_TEST) },
	{ 3744563888u, quint64(Fallout4_ShaderFlags1::SKINNED) },
	{ 2893749418u, quint64(Fallout4_ShaderFlags1::ENVMAP) },
	{ 2333069810u, quint64(Fallout4_ShaderFlags1::VERTEX_ALPHA) },
	{ 314919375u,  quint64(Fallout4_ShaderFlags1::FACE) },
	{ 442246519u,  quint64(Fallout4_ShaderFlags1::GREYSCALE_TO_PALETTE_COLOR) },
	{ 2901038324u, quint64(Fallout4_ShaderFlags1::GREYSCALE_TO_PALETTE_ALPHA) },
	{ 3849131744u, quint64(Fallout4_ShaderFlags1::DECAL) },
	{ 1576614759u, quint64(Fallout4_ShaderFlags1::DYNAMIC_DECAL) },
	{ 2262553490u, quint64(Fallout4_ShaderFlags1::OWN_EMIT) },
	{ 1957349758u, quint64(Fallout4_ShaderFlags1::REFRACTION) },
	{ 1483897208u, quint64(Fallout4_ShaderFlags1::SKIN_TINT) },
	{ 3448946507u, quint64(Fallout4_ShaderFlags1::RGB_FALLOFF) },
	{ 2150459555u, quint64(Fallout4_ShaderFlags1::EXTERNAL_EMITTANCE) },
	{ 2548465567u, quint64(Fallout4_ShaderFlags1::MODEL_SPACE_NORMALS) },
	{ 3980660124u, quint64(Fallout4_ShaderFlags1::USE_FALLOFF) },
	{ 3503164976u, quint64(Fallout4_ShaderFlags1::SOFT_EFFECT) },

	// SF2
	{ 3166356979u, quint64(Fallout4_ShaderFlags2::ZBUFFER_WRITE) << 32 },
	{ 2399422528u, quint64(Fallout4_ShaderFlags2::GLOW_MAP) << 32 },
	{ 759557230u,  quint64(Fallout4_ShaderFlags2::DOUBLE_SIDED) << 32 },
	{ 348504749u,  quint64(Fallout4_ShaderFlags2::VERTEX_COLORS) << 32 },
	{ 2994043788u, quint64(Fallout4_ShaderFlags2::NO_FADE) << 32 },
	{ 2078326675u, quint64(Fallout4_ShaderFlags2::WEAPON_BLOOD) << 32 },
	{ 3196772338u, quint64(Fallout4_ShaderFlags2::TRANSFORM_CHANGED) << 32 },
	{ 3473438218u, quint64(Fallout4_ShaderFlags2::EFFECT_LIGHTING) << 32 },
	{ 2896726515u, quint64(Fallout4_ShaderFlags2::LOD_OBJECTS) << 32 },

	// TODO
	{ 731263983u, 0 }, // PBR
	{ 902349195u, 0 }, // REFRACTION FALLOFF
	{ 3030867718u, 0 }, // INVERTED_FADE_PATTERN
	{ 1264105798u, 0 }, // HAIRTINT
	{ 3707406987u, 0 }, //  NO_EXPOSURE
};

void readNewShaderFlags( NewShaderFlags & flags, BSShaderProperty * prop, bool isEffectsShader, NifFieldConst shaderBlock, const NifModel * nif )
{
	// Read flags fields
	if ( nif->getBSVersion() >= 151 ) {
		flags.isFO4 = true;

		auto sfs = shaderBlock["SF1"].array<quint32>() + shaderBlock["SF2"].array<quint32>();
		quint64 allFlags = 0;
		for ( auto sf : sfs )
			allFlags |= Fallout4_CRCFlagMap.value( sf, 0 );
		flags.flags1 = allFlags & quint64(MAXUINT32);
		flags.flags2 = allFlags >> 32;
	} else {
		auto flagField1 = shaderBlock["Shader Flags 1"];
		auto flagField2 = shaderBlock["Shader Flags 2"];

		if ( flagField1.hasStrType("SkyrimShaderPropertyFlags1") ) {
			flags.setVersion( false, isEffectsShader );
			flags.flags1 = flagField1.value<uint>();
		} else if ( flagField1.hasStrType("Fallout4ShaderPropertyFlags1") ) {
			flags.setVersion( true, isEffectsShader );
			flags.flags1 = flagField1.value<uint>();
		} else {
			if ( flagField1 )
				flagField1.reportError( QString("Unsupported value type '%1'.").arg( flagField1.strType() ) );
			// Fallback setVersion
			flags.setVersion( !flagField1 && flagField2.hasStrType("Fallout4ShaderPropertyFlags2"), isEffectsShader );
		}

		if ( flagField2.hasStrType("SkyrimShaderPropertyFlags2") ) {
			if ( flags.isFO4 ) {
				flagField2.reportError( QString("Unexpected value type '%1'.").arg( flagField2.strType() ) );
			} else {
				flags.flags2 = flagField2.value<uint>();
			}
		} else if ( flagField2.hasStrType("Fallout4ShaderPropertyFlags2") ) {
			if ( flags.isFO4 ) {
				flags.flags2 = flagField2.value<uint>();
			} else {
				flagField2.reportError( QString("Unexpected value type '%1'.").arg( flagField2.strType() ) );
			}
		} else if ( flagField2 ) {
			flagField2.reportError( QString("Unsupported value type '%1'.").arg( flagField2.strType() ) );
		}
	}

	// Set common vertex flags in the property
	if ( nif->getBSVersion() >= 130 ) {
		//  Always do vertex colors, incl. alphas, for FO4 and newer if colors present
		prop->vertexColorMode = ShaderColorMode::FROM_DATA;
		prop->hasVertexAlpha = true;
	} else {
		prop->vertexColorMode = flags.vertexColors() ? ShaderColorMode::YES : ShaderColorMode::NO;
		prop->hasVertexAlpha = flags.vertexAlpha();
	}
	prop->isVertexAlphaAnimation = flags.treeAnim();
}


enum Skyrim_ShaderType : unsigned int // BSLightingShaderType in nif.xml
{
	SKY_SHADER_DEFAULT,
	SKY_SHADER_ENVMAP,
	SKY_SHADER_GLOW,
	SKY_SHADER_HEIGHTMAP,
	SKY_SHADER_FACE_TINT,
	SKY_SHADER_SKIN_TINT,
	SKY_SHADER_HAIR_TINT,
	SKY_SHADER_PARALLAX_OCC_MAT,
	SKY_SHADER_WORLD_MULTITEXTURE,
	SKY_SHADER_WORLDMAP1,
	SKY_SHADER_SNOW,
	SKY_SHADER_MULTILAYER_PARALLAX,
	SKY_SHADER_TREE_ANIM,
	SKY_SHADER_WORLDMAP2,
	SKY_SHADER_SPARKLE_SNOW,
	SKY_SHADER_WORLDMAP3,
	SKY_SHADER_EYE_ENVMAP,
	SKY_SHADER_CLOUD,
	SKY_SHADER_WORLDMAP4,
	SKY_SHADER_WORLD_LOD_MULTITEXTURE,
	SKY_SHADER_DISMEMBERMENT, // FO4?

	SKY_SHADER_END
};


void BSLightingShaderProperty::updateParams( const NifModel * nif )
{
	resetParams();

	NewShaderFlags flags;
	readNewShaderFlags( flags, this, false, block, nif );

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
			clampMode = TexClampMode::WRAP_S_WRAP_T;
		else if ( m->bTileU )
			clampMode = TexClampMode::WRAP_S_CLAMP_T;
		else if ( m->bTileV )
			clampMode = TexClampMode::CLAMP_S_WRAP_T;
		else
			clampMode = TexClampMode::CLAMP_S_CLAMP_T;

		fresnelPower = m->fFresnelPower;
		greyscaleColor = m->bGrayscaleToPaletteColor;
		paletteScale = m->fGrayscaleToPaletteScale;

		hasSpecularMap = m->bSpecularEnabled && (!m->textureList[2].isEmpty()
			|| (nif->getBSVersion() >= 151 && !m->textureList[7].isEmpty()));
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
		if ( nif->getBSVersion() >= 151 ) {
			shaderType = SKY_SHADER_ENVMAP;
		} else {
			shaderType = SKY_SHADER_DEFAULT;
			auto typeField = block["Shader Type"];
			if ( typeField.hasStrType("BSLightingShaderType") ) { // Skyrim or newer shader type
				auto v = typeField.value<uint>();
				if ( v < SKY_SHADER_END )
					shaderType = Skyrim_ShaderType( v );
				else {
					typeField.reportError( tr("Unsupported value %1").arg( v ) );
				}
			} else if ( typeField ) {
				typeField.reportError( tr("Unsupported value type '%1'.").arg( typeField.strType() ) );
			}
		}

		auto textures = textureBlock["Textures"].array<QString>();
		
		isDoubleSided = flags.doubleSided();
		depthTest = flags.depthTest();
		depthWrite = flags.depthWrite();

		alpha = block["Alpha"].value<float>();

		uvScale.set( block["UV Scale"].value<Vector2>() );
		uvOffset.set( block["UV Offset"].value<Vector2>() );
		clampMode = TexClampMode( block["Texture Clamp Mode"].value<uint>() );

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
		hasGlowMap = ( shaderType == SKY_SHADER_GLOW ) && flags.glowMap() && !textures.value( 2, "" ).isEmpty();

		// Version Dependent settings
		if ( nif->getBSVersion() < 130 ) {
			lightingEffect1 = block["Lighting Effect 1"].value<float>();
			lightingEffect2 = block["Lighting Effect 2"].value<float>();

			innerThickness = block.child("Parallax Inner Layer Thickness").value<float>();
			outerRefractionStrength = block.child("Parallax Refraction Scale").value<float>();
			outerReflectionStrength = block.child("Parallax Envmap Strength").value<float>();
			innerTextureScale.set( block.child("Parallax Inner Layer Texture Scale").value<Vector2>() );

			hasSpecularMap        = flags.specular() && !textures.value( 7, "" ).isEmpty();
			hasHeightMap          = (shaderType == SKY_SHADER_HEIGHTMAP) && flags.skyrimParallax() && !textures.value( 3, "" ).isEmpty();
			hasBacklight          = flags.skyrimBackLighting();
			hasRimlight           = flags.skyrimRimLighting();
			hasSoftlight          = flags.skyrimSoftLighting();
			hasMultiLayerParallax = flags.skyrimMultiLayerParalax();
			hasRefraction         = flags.refraction();

			hasTintMask = (shaderType == SKY_SHADER_FACE_TINT);
			hasDetailMask = hasTintMask;

			if ( shaderType == SKY_SHADER_HAIR_TINT ) {
				hasTintColor = true;
				tintColor = block["Hair Tint Color"].value<Color3>();
			} else if ( shaderType == SKY_SHADER_SKIN_TINT ) {
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
			( ( shaderType == SKY_SHADER_ENVMAP ) && flags.envMap() )
			|| ( ( shaderType == SKY_SHADER_EYE_ENVMAP ) && flags.eyeEnvMap() )
			|| ( nif->getBSVersion() == 100 && hasMultiLayerParallax );
		
		useEnvironmentMask = hasEnvironmentMap && !textures.value( 5, "" ).isEmpty();

		if ( shaderType == SKY_SHADER_ENVMAP )
			environmentReflection = block.child("Environment Map Scale").value<float>();
		else if ( shaderType == SKY_SHADER_EYE_ENVMAP )
			environmentReflection = block["Eye Cubemap Scale"].value<float>();
	}
}

void BSLightingShaderProperty::setController( const NifModel * nif, const QModelIndex & iController )
{
	auto contrName = nif->itemName(iController);
	if ( contrName == "BSLightingShaderPropertyFloatController" ) {
		Controller * ctrl = new LightingFloatController( this, iController );
		registerController(nif, ctrl);
	} else if ( contrName == "BSLightingShaderPropertyColorController" ) {
		Controller * ctrl = new LightingColorController( this, iController );
		registerController(nif, ctrl);
	}
}

/*
	BSEffectShaderProperty
*/

void BSEffectShaderProperty::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	BSShaderProperty::updateImpl( nif, index );

	if ( index == iBlock ) {
		setMaterial(name.endsWith(".bgem", Qt::CaseInsensitive) ? new EffectMaterial(name, scene->getGame()) : nullptr);
		updateParams(nif);
	}
	else if ( index == iTextureSet )
		updateParams(nif);
}

void BSEffectShaderProperty::resetParams() 
{
	BSShaderProperty::resetParams();

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

	falloff.startAngle = 1.0f;
	falloff.stopAngle = 0.0f;
	falloff.startOpacity = 1.0f;
	falloff.stopOpacity = 0.0f;
	falloff.softDepth = 1.0f;

	lumEmittance = 0.0;

	emissiveColor = Color4(0, 0, 0, 0);
	emissiveMult = 1.0;

	lightingInfluence = 0.0;
	environmentReflection = 0.0;
}

void BSEffectShaderProperty::updateParams( const NifModel * nif )
{
	resetParams();

	NewShaderFlags flags;
	readNewShaderFlags( flags, this, true, block, nif );

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
			clampMode = TexClampMode::WRAP_S_WRAP_T;
		else if ( m->bTileU )
			clampMode = TexClampMode::WRAP_S_CLAMP_T;
		else if ( m->bTileV )
			clampMode = TexClampMode::CLAMP_S_WRAP_T;
		else
			clampMode = TexClampMode::CLAMP_S_CLAMP_T;

		emissiveColor = Color4(m->cBaseColor, m->fAlpha);
		emissiveMult = m->fBaseColorScale;

		if ( m->bEffectLightingEnabled )
			lightingInfluence = m->fLightingInfluence;

		falloff.startAngle = m->fFalloffStartAngle;
		falloff.stopAngle = m->fFalloffStopAngle;
		falloff.startOpacity = m->fFalloffStartOpacity;
		falloff.stopOpacity = m->fFalloffStopOpacity;
		falloff.softDepth = m->fSoftDepth;

	} else { // m == nullptr
		
		hasSourceTexture = !block["Source Texture"].value<QString>().isEmpty();
		hasGreyscaleMap = !block["Greyscale Texture"].value<QString>().isEmpty();

		greyscaleAlpha = flags.greyscaleToPaletteAlpha();
		greyscaleColor = flags.greyscaleToPaletteColor();
		useFalloff = flags.useFalloff();

		depthTest = flags.depthTest();
		depthWrite = flags.depthWrite();
		isDoubleSided = flags.doubleSided();

		if ( nif->getBSVersion() < 130 ) {
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
		clampMode = TexClampMode( block["Texture Clamp Mode"].value<quint8>() );

		emissiveColor = block["Base Color"].value<Color4>();
		emissiveMult = block["Base Color Scale"].value<float>();

		if ( flags.effectLighting() )
			lightingInfluence = float( block["Lighting Influence"].value<quint8>() ) / 255.0;

		falloff.startAngle = block["Falloff Start Angle"].value<float>();
		falloff.stopAngle = block["Falloff Stop Angle"].value<float>();
		falloff.startOpacity = block["Falloff Start Opacity"].value<float>();
		falloff.stopOpacity = block["Falloff Stop Opacity"].value<float>();
		falloff.softDepth = block["Soft Falloff Depth"].value<float>();
	}
}

void BSEffectShaderProperty::setController( const NifModel * nif, const QModelIndex & iController )
{
	auto contrName = nif->itemName(iController);
	if ( contrName == "BSEffectShaderPropertyFloatController" ) {
		Controller * ctrl = new EffectFloatController( this, iController );
		registerController(nif, ctrl);
	} else if ( contrName == "BSEffectShaderPropertyColorController" ) {
		Controller * ctrl = new EffectColorController( this, iController );
		registerController(nif, ctrl);
	}
}

/*
	BSWaterShaderProperty
*/

unsigned int BSWaterShaderProperty::getWaterShaderFlags() const
{
	return (unsigned int)waterShaderFlags;
}

void BSWaterShaderProperty::setWaterShaderFlags( unsigned int val )
{
	waterShaderFlags = WaterShaderFlags::SF1( val );
}

