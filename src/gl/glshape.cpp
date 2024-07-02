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

#include "glshape.h"

#include "gl/controllers.h"
#include "gl/glscene.h"
#include "io/material.h"

Shape::Shape( Scene * s, const QModelIndex & b ) : Node( s, b )
{
	shapeNumber = s->shapes.count();
}

void Shape::clear()
{
	Node::clear();

	resetBlockData();

	transVerts.clear();
	transNorms.clear();
	transColors.clear();
	transTangents.clear();
	transBitangents.clear();
	sortedTriangles.clear();

	bssp = nullptr;
	bslsp = nullptr;
	bsesp = nullptr;
	alphaProperty = nullptr;

	isDoubleSided = false;
}

void Shape::transform()
{
	if ( needUpdateData )
		updateData( NifModel::fromValidIndex( iBlock ) );

	Node::transform();
}

template <typename T> inline void normalizeVectorSize(QVector<T> & v, int nRequiredSize, bool hasVertexData)
{
	if ( hasVertexData ) {
		if ( v.count() < nRequiredSize )
			v.resize( nRequiredSize );
	} else {
		v.clear();
	}
}

void Shape::updateData( const NifModel* nif )
{
	needUpdateData = false;

	if ( !nif ) {
		clear();
		return;
	}

	needUpdateBounds = true; // Force update bounds
	resetBlockData();

	updateDataImpl(nif);

	numVerts = verts.count();

	// Validate vertex data
	normalizeVectorSize( norms, numVerts, hasVertexNormals );
	normalizeVectorSize( tangents, numVerts, hasVertexTangents );
	normalizeVectorSize( bitangents, numVerts, hasVertexBitangents );

	for ( auto & uvset : coords )
		normalizeVectorSize( uvset, numVerts, hasVertexUVs );

	// Validate triangle data
	int nTotalTris = triangles.count();
	int nValidTris = 0;
	if ( nTotalTris > 0 ) {
		triangleMap.fill( -1, nTotalTris );
		for ( int i = 0; i < nTotalTris; i++ ) {
			const auto & t = triangles[i];
			if ( t[0] < numVerts && t[1] < numVerts && t[2] < numVerts )
				triangleMap[i] = nValidTris++;
		}

		if (nValidTris < nTotalTris) {
			if ( nValidTris > 0 ) {
				for ( int i = nTotalTris - 1; i >= 0; i-- ) {
					if ( triangleMap[i] < 0 )
						triangles.remove(i);
				}
			} else {
				triangles.clear();
			}
		}
	}

	// "Normalize" all triangleRanges so they would not point to invalid triangles.
	for ( TriangleRange * r : triangleRanges ) {
		int iFirst = std::max( r->start, 0 );
		int iLast = ( nValidTris > 0 ) ? ( std::min( r->start + r->length, nTotalTris ) - 1 ) : -1;

		if ( nValidTris < nTotalTris ) {
			while ( iFirst <= iLast && triangleMap[iFirst] < 0 )
				iFirst++;
			while ( iLast >= iFirst && triangleMap[iLast] < 0 )
				iLast--;
		}

		if ( iFirst <= iLast ) {
			r->realStart  = triangleMap[iFirst];
			r->realLength = triangleMap[iLast] - r->realStart + 1;
		} else {
			r->realStart  = 0; // Whatever...
			r->realLength = 0;
		}
	}

	if ( isLOD )
		emit nif->lodSliderChanged( true );
}

void Shape::setController( const NifModel * nif, const QModelIndex & iController )
{
	QString contrName = nif->itemName(iController);
	if ( contrName == "NiGeomMorpherController" ) {
		Controller * ctrl = new MorphController( this, iController );
		registerController(nif, ctrl);
	} else if ( contrName == "NiUVController" ) {
		Controller * ctrl = new UVController( this, iController );
		registerController(nif, ctrl);
	} else {
		Node::setController( nif, iController );
	}
}

void Shape::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	Node::updateImpl( nif, index );

	if ( index == iBlock ) {
		shader = ""; // Reset stored shader so it can reassess conditions

		bslsp = nullptr;
		bsesp = nullptr;
		bssp = properties.get<BSShaderProperty>();
		if ( bssp ) {
			auto shaderType = bssp->typeId();
			if ( shaderType == "BSLightingShaderProperty" )
				bslsp = bssp->cast<BSLightingShaderProperty>();
			else if ( shaderType == "BSEffectShaderProperty" )
				bsesp = bssp->cast<BSEffectShaderProperty>();
		}

		alphaProperty = properties.get<AlphaProperty>();

		needUpdateData = true;
		updateShader();

	} else if ( isSkinned && (index == iSkin || index == iSkinData || index == iSkinPart) ) {
		needUpdateData = true;

	} else if ( (bssp && bssp->isParamBlock(index)) || (alphaProperty && index == alphaProperty->index()) ) {
		updateShader();
	
	}
}

void Shape::boneSphere( const NifModel * nif, const QModelIndex & index ) const
{
	Node * root = findParent( 0 );
	Node * bone = root ? root->findChild( bones.value( index.row() ).nodeLink ) : 0;
	if ( !bone )
		return;

	Transform boneT = Transform( nif, index );
	Transform t = scene->hasOption(Scene::DoSkinning) ? viewTrans() : Transform();
	t = t * skeletonTrans * bone->localTrans( 0 ) * boneT;

	auto bSphere = BoundSphere( nif, index );
	if ( bSphere.radius > 0.0 ) {
		glColor4f( 1, 1, 1, 0.33f );
		auto pos = boneT.rotation.inverted() * (bSphere.center - boneT.translation);
		drawSphereSimple( t * pos, bSphere.radius, 36 );
	}
}

void Shape::reportCountMismatch( NifFieldConst rootEntry1, int entryCount1, NifFieldConst rootEntry2, int entryCount2, NifFieldConst reportEntry ) const
{
	if ( rootEntry1 && rootEntry2 && entryCount1 != entryCount2 ) {
		reportEntry.reportError( 
			tr("The number of entries in %1 (%2) does not match that in %3 (%4)")
				.arg( rootEntry1.repr( reportEntry ) )
				.arg( entryCount1 )
				.arg( rootEntry2.repr( reportEntry ) )
				.arg( entryCount2 ) 
		);
	}
}

TriangleRange * Shape::addTriangleRange( NifFieldConst rangeRootField, int iStart, int nTris )
{
	TriangleRange * r = new TriangleRange( rangeRootField, iStart, nTris );
	triangleRanges.append( r );
	return r;
}

TriangleRange * Shape::addTriangles( NifFieldConst rangeRootField, const QVector<Triangle> & tris)
{
	if ( tris.count() > 0 ) {
		int iStart = triangles.count();
		triangles << tris;
		return addTriangleRange( rangeRootField, iStart, triangles.count() - iStart );
	}

	return nullptr;
}

void Shape::drawTriangles( int iStartOffset, int nTris ) const
{
	if ( nTris > 0)
		glDrawElements( GL_TRIANGLES, nTris * 3, GL_UNSIGNED_SHORT, triangles.constData() + iStartOffset );
}

void Shape::initSkinBones( NifFieldConst nodeMapRoot, NifFieldConst nodeListRoot, NifFieldConst block )
{
	reportCountMismatch( nodeMapRoot, nodeListRoot, block );
	int nTotalBones = std::max( nodeMapRoot.childCount(), nodeListRoot.childCount() );
	bones.reserve( nTotalBones );
	for ( int bind = 0; bind < nTotalBones; bind++ )
		bones << SkinBone( nodeListRoot.child(bind), nodeMapRoot.child(bind).link() );
}

void Shape::applySkinningTransforms( const Transform & baseTransform )
{
	transformRigid = false;

	transVerts.resize( numVerts );
	transVerts.fill( Vector3() );
	transNorms.resize( norms.count() );
	transNorms.fill( Vector3() );
	transTangents.resize( tangents.count() );
	transTangents.fill( Vector3() );
	transBitangents.resize( bitangents.count() );
	transBitangents.fill( Vector3() );

	Node * root = findParent( skeletonRoot );
	for ( SkinBone & bone : bones ) {
		Node * boneNode = root ? root->findChild( bone.nodeLink ) : nullptr;
		bone.transform = boneNode ?  ( baseTransform * boneNode->localTrans( skeletonRoot ) * bone.baseTransform ) : baseTransform;

		for ( const VertexWeight & vw : bone.vertexWeights ) {
			transVerts[vw.vertex] += bone.transform * verts[vw.vertex] * vw.weight;
			if ( hasVertexNormals )
				transNorms[vw.vertex] += bone.transform.rotation * norms[vw.vertex] * vw.weight;
			if ( hasVertexTangents )
				transTangents[vw.vertex] += bone.transform.rotation * tangents[vw.vertex] * vw.weight;
			if ( hasVertexBitangents )
				transBitangents[vw.vertex] += bone.transform.rotation * bitangents[vw.vertex] * vw.weight;
		}
	}

	for ( auto & v : transNorms )
		v.normalize();
	for ( auto & v : transTangents )
		v.normalize();
	for ( auto & v : transBitangents )
		v.normalize();

	boundSphere = BoundSphere( transVerts );
	boundSphere.applyInv( viewTrans() );
	needUpdateBounds = false;
}

void Shape::applyRigidTransforms()
{
	transformRigid = true;

	transVerts = verts;
	transNorms = norms;
	transTangents = tangents;
	transBitangents = bitangents;
}

void Shape::applyColorTransforms( float alphaBlend )
{
	auto shaderColorMode = bssp ? bssp->vertexColorMode : ShaderColorMode::FROM_DATA;
	bool doVCs = ( shaderColorMode == ShaderColorMode::FROM_DATA ) ? hasVertexColors : ( shaderColorMode == ShaderColorMode::YES );

	if ( doVCs && numVerts > 0 ) {
		transColors = colors;
		if ( alphaBlend != 1.0f ) {
			for ( auto & c : transColors )
				c.setAlpha( c.alpha() * alphaBlend );
		} else if ( bssp && ( bssp->isVertexAlphaAnimation || !bssp->hasVertexAlpha ) ) {
			for ( auto & c : transColors )
				c.setAlpha( 1.0f );
		}
	} else {
		transColors.clear();
	}

	// Cover any mismatches between shader and data flags or wrong color numbers in the data by appending "bad colors" until transColors.count() == numVerts.
	if ( transColors.count() < numVerts && ( doVCs || hasVertexColors ) ) {
		transColors.reserve( numVerts );
		for ( int i = transColors.count(); i < numVerts; i++ )
			transColors << Color4( 0, 0, 0, 1 );
	}
}

void Shape::resetBlockData()
{
	// Vertex data
	numVerts = 0;

	iData = iTangentData = QModelIndex();

	hasVertexNormals    = false;
	hasVertexTangents   = false;
	hasVertexBitangents = false;
	hasVertexUVs        = false;
	hasVertexColors     = false;
	
	isLOD = false;

	verts.clear();
	norms.clear();
	colors.clear();
	coords.clear();
	tangents.clear();
	bitangents.clear();
	triangles.clear();
	triangleMap.clear();
	lodRanges.clear();
	qDeleteAll(triangleRanges);
	triangleRanges.clear();
	tristrips.clear();

	// Skinning data
	isSkinned = false;
	iSkin = iSkinData = iSkinPart = QModelIndex();

	// Skeleton data
	skeletonRoot = 0;
	skeletonTrans = Transform();

	bones.clear();
}

void Shape::updateShader()
{
	if ( bslsp )
		translucent = (bslsp->alpha < 1.0) || bslsp->hasRefraction;
	else if ( bsesp )
		translucent = (bsesp->getAlpha() < 1.0) && !alphaProperty;
	else
		translucent = false;

	drawInSecondPass = false;
	if ( translucent )
		drawInSecondPass = true;
	else if ( alphaProperty && (alphaProperty->hasAlphaBlend() || alphaProperty->hasAlphaTest()) )
		drawInSecondPass = true;
	else if ( bssp ) {
		Material * mat = bssp->getMaterial();
		if ( mat && (mat->hasAlphaBlend() || mat->hasAlphaTest() || mat->hasDecal()) )
			drawInSecondPass = true;
	}

	if ( bssp ) {
		depthTest = bssp->depthTest;
		depthWrite = bssp->depthWrite;
		isDoubleSided = bssp->isDoubleSided;
	} else {
		depthTest = true;
		depthWrite = true;
		isDoubleSided = false;
	}
}
