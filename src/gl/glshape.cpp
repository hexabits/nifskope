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
#include "model/nifmodel.h"
#include "io/material.h"

#include <QDebug>
#include <QElapsedTimer>

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

	isLOD = false;
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

	// Validating vertex data
	normalizeVectorSize( norms, numVerts, hasVertexNormals );
	normalizeVectorSize( tangents, numVerts, hasVertexTangents );
	normalizeVectorSize( bitangents, numVerts, hasVertexBitangents );

	normalizeVectorSize( colors, numVerts, hasVertexColors );
	if ( isVertexAlphaAnimation ) {
		for ( auto & c : colors )
			c.setAlpha(1.0f);
	}

	for ( auto & uvset : coords )
		normalizeVectorSize( uvset, numVerts, hasVertexUVs );
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
		bssp = properties.get<BSShaderLightingProperty>();
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
	Node * bone = root ? root->findChild( bones.value( index.row() ) ) : 0;
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

	verts.clear();
	norms.clear();
	colors.clear();
	coords.clear();
	tangents.clear();
	bitangents.clear();
	triangles.clear();
	tristrips.clear();

	// Skinning data
	isSkinned = false;
	iSkin = iSkinData = iSkinPart = QModelIndex();

	// Skeleton data
	skeletonRoot = 0;
	skeletonTrans = Transform();

	bones.clear();
	weights.clear();
	partitions.clear();
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
		isVertexAlphaAnimation = bssp->isVertexAlphaAnimation;
	} else {
		depthTest = true;
		depthWrite = true;
		isDoubleSided = false;
		isVertexAlphaAnimation = false;
	}
}
