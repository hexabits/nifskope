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
#include "gl/renderer.h"
#include "io/material.h"
#include "lib/nvtristripwrapper.h"


Shape::Shape( Scene * s, const QModelIndex & b ) : Node( s, b )
{
	shapeNumber = s->shapes.count();
}

Shape::~Shape()
{
	qDeleteAll( selections );
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

bool Shape::isEditorMarker() const
{
	// TODO: replace it with a check if BSXFlags has "EditorMarkers present" flag?
	return name.contains( QStringLiteral("EditorMarker") );
}

bool Shape::doSkinning() const
{
	return isSkinned && bones.count() && scene->hasOption(Scene::DoSkinning);
}

void Shape::fillViewModeWeights( double * outWeights, bool & outIsSkinned, const int * modeAxes )
{
	if ( isEditorMarker() || isHidden() )
		return;

	if ( needUpdateData )
		updateData( NifModel::fromValidIndex( iBlock ) );

	if ( doSkinning() && ( triangles.count() || stripTriangles.count() ) )
		outIsSkinned = true;

	Transform vertTransform = worldTrans();

	auto processTri = [this, &outWeights, vertTransform]( const Triangle & t, const int * modeAxes ) {
		auto p1 = vertTransform * verts[ t[0] ];
		auto p2 = vertTransform * verts[ t[1] ];
		auto p3 = vertTransform * verts[ t[2] ];

		// resv = triangle normal vector * triangle area (to give bigger triangles more weight in the result)
		// where:
		//     cpv = Vector3::crossproduct( p2 - p1, p3 - p1 )
		//     triangle normal vector = cpv.normalize (or cpv / cpv.length)
		//     triangle area = cpv.length * 0.5
		// So resv resolves to ( cpv / cpv.length * cpv.length * 0.5 ) -> ( cpv * 0.5 ).
		// And since the 0.5 multiplier does not affect the ratio of the result weights, we can drop it too, simplifying all this to just ( cpv ).
		auto resv = Vector3::crossproduct( p2 - p1, p3 - p1 );

		for ( int i = 0; i < 3; i++, modeAxes += 2 ) {
			auto axisv = resv[i];
			if ( axisv > 0.0f ) {
				outWeights[ modeAxes[0] ] += axisv;
				if ( isDoubleSided )
					outWeights[ modeAxes[1] ] += axisv;
			} else if ( axisv < 0.0f ) {
				outWeights[ modeAxes[1] ] -= axisv;
				if ( isDoubleSided )
					outWeights[ modeAxes[0] ] -= axisv;
			}
		}
	};

	for ( const Triangle & t : triangles )
		processTri( t, modeAxes );
	for ( const Triangle & t : stripTriangles )
		processTri( t, modeAxes );
}


constexpr GLfloat BIG_VERTEX_SIZE = 8.5f;
constexpr GLfloat SMALL_VERTEX_SIZE = 5.5f;
constexpr GLfloat WIREFRAME_LINE_WIDTH = 1.0f;
constexpr GLfloat VECTOR_LINE_WIDTH = 1.5f;
constexpr float VECTOR_SCALE_DIV = 20.0f;
constexpr float VECTOR_MIN_SCALE = 0.5f; // 1.0f;
constexpr float VECTOR_MAX_SCALE = 25.0f;
const Color4 BOUND_SPHERE_COLOR( 1, 1, 1, 0.4f );
const Color4 BOUND_SPHERE_CENTER_COLOR( 1, 1, 1, 1 );

void Shape::drawShapes( NodeList * secondPass, bool presort )
{
	if ( numVerts <= 0 || isHidden() )
		return;

	if ( !scene->hasOption(Scene::ShowMarkers) && isEditorMarker() )
		return;

	// BSOrderedNode
	// TODO (Gavrant): I don't understand the purpose of this. Also, in the old code BSShape did not do this.
	presorted |= presort;

	// Draw translucent meshes in second pass
	if ( secondPass && drawInSecondPass ) {
		secondPass->add( this );
		return;
	}

	// TODO: Option to hide Refraction and other post effects

	// rigid mesh? then pass the transformation on to the gl layer
	if ( transformRigid ) {
		glPushMatrix();
		glMultMatrix( viewTrans() );
	}

	// Render polygon fill slightly behind alpha transparency, and alpha transparency - behind wireframe
	glEnable( GL_POLYGON_OFFSET_FILL );
	if ( drawInSecondPass )
		glPolygonOffset( 0.5f, 1.0f );
	else
		glPolygonOffset( 1.0f, 2.0f );

	glEnableClientState( GL_VERTEX_ARRAY );
	glVertexPointer( 3, GL_FLOAT, 0, transVerts.constData() );

	if ( !Node::SELECTING) { // Normal rendering
		if ( transNorms.count() > 0 ) {
			glEnableClientState( GL_NORMAL_ARRAY );
			glNormalPointer( GL_FLOAT, 0, transNorms.constData() );
		}

		if ( transColors.count() ) {
			glEnableClientState( GL_COLOR_ARRAY );
			glColorPointer( 4, GL_FLOAT, 0, transColors.constData() );
		} else {
			glColor( Color3( 1.0f, 1.0f, 1.0f ) );
		}

		if ( sRGB )
			glEnable( GL_FRAMEBUFFER_SRGB );
		else
			glDisable( GL_FRAMEBUFFER_SRGB );

		shader = scene->renderer->setupProgram( this, shader );
	} else { // Selection rendering
		if ( scene->isSelModeObject() ) {
			int s_nodeId = ID2COLORKEY( nodeId );
			glColor4ubv( (GLubyte *)&s_nodeId );
		} else
			glColor4f( 0, 0, 0, 1 );

		glDisable( GL_LIGHTING );
		glDisable( GL_FRAMEBUFFER_SRGB );
	}

	if ( isDoubleSided )
		glDisable( GL_CULL_FACE );
	else
		glEnable( GL_CULL_FACE );

	// Draw triangles and strips
	int lodLevel = isLOD ? scene->lodLevel : -1;
	if ( lodLevel >= 0 && lodLevel < lodLevels.count() ) {
		glDrawTriangles( lodLevels[lodLevel] );
	} else {
		glDrawTriangles( triangles );
	}

	glDrawTriangles( stripTriangles );

	// Post-drawing triangles and strips
	if ( !Node::SELECTING )
		scene->renderer->stopProgram();

	glDisableClientState( GL_VERTEX_ARRAY );
	glDisableClientState( GL_NORMAL_ARRAY );
	glDisableClientState( GL_COLOR_ARRAY );

	glDisable( GL_POLYGON_OFFSET_FILL );

	if ( Node::SELECTING && scene->isSelModeVertex() ) {
		glPointSize( BIG_VERTEX_SIZE );

		glBegin( GL_POINTS );

		auto pVerts = transVerts.data();
		for ( int i = 0; i < numVerts; i++, pVerts++ ) {
			int id = ID2COLORKEY( (shapeNumber << 16) + i );
			glColor4ubv( (GLubyte *) &id );
			glVertex( pVerts );
		}

		glEnd();
	}

	if ( transformRigid )
		glPopMatrix();
}

void Shape::drawSelection() const
{
	// TODO (Gavrant): move glDisable GL_FRAMEBUFFER_SRGB to drawShapes?
	glDisable( GL_FRAMEBUFFER_SRGB );

	if ( scene->hasOption(Scene::ShowNodes) )
		Node::drawSelection();

	if ( isHidden() )
		return;

	auto nif = NifModel::fromValidIndex( iBlock );
	if ( !nif )
		return;

	drawSelectionMode = DrawSelectionMode::NO;

	auto selectedField = nif->field( scene->currentIndex );
	auto selectedBlock = selectedField.block();
	bool dataSelected;
	if ( selectedBlock ) {
		QModelIndex iSelBlock = selectedBlock.toIndex();
		dataSelected = ( iSelBlock == iBlock || iSelBlock == iData || iSelBlock == iSkin || iSelBlock == iSkinData || iSelBlock == iSkinPart || iSelBlock == iExtraData );
	} else {
		dataSelected  = false;
	}

	if ( dataSelected && selectedField != selectedBlock ) {
		int selectedLevel = selectedField.ancestorLevel( selectedBlock );

		for ( auto pSelection : selections ) {
			if ( pSelection->block != selectedBlock || pSelection->level > selectedLevel )
				continue;
			int iSubLevel = selectedField.ancestorLevel( pSelection->rootField );
			if ( iSubLevel >= 0 && pSelection->process( selectedField, iSubLevel ) )
				return;
		}
	}

	// Fallback
	if ( scene->isSelModeVertex() ) {
		drawSelection_vertices( VertexSelectionType::VERTICES );
	} else if ( dataSelected && scene->isSelModeObject() ) {
		drawSelection_triangles();
	}
}

QModelIndex Shape::vertexAt( int vertexIndex ) const
{
	if ( mainVertexRoot ) {
		auto resField = mainVertexRoot.child( vertexIndex );
		if ( resField.hasStrType("BSVertexData", "BSVertexDataSSE") ) {
			auto pointField = resField.child("Vertex");
			if ( pointField )
				resField = pointField;
		}

		return resField.toIndex();
	}

	return QModelIndex();
}

template <typename T> static inline void normalizeVectorSize(QVector<T> & v, int nRequiredSize, bool hasVertexData)
{
	if ( hasVertexData ) {
		if ( v.count() < nRequiredSize )
			v.resize( nRequiredSize );
	} else {
		v.clear();
	}
}

static void validateTriangles( QVector<Triangle> & tris, QVector<int> & triMap, int numVerts )
{
	// Validate triangle data
	int nTotalTris = tris.count();
	int nValidTris = 0;
	if ( nTotalTris > 0 ) {
		triMap.fill( -1, nTotalTris );
		for ( int i = 0; i < nTotalTris; i++ ) {
			const auto & t = tris[i];
			if ( t[0] < numVerts && t[1] < numVerts && t[2] < numVerts )
				triMap[i] = nValidTris++;
		}

		if ( nValidTris < nTotalTris ) {
			if ( nValidTris > 0 ) {
				for ( int i = nTotalTris - 1; i >= 0; i-- ) {
					if ( triMap[i] < 0 )
						tris.remove(i);
				}
			} else {
				tris.clear();
			}
		}
	}
}

void Shape::updateData( const NifModel * nif )
{
	needUpdateData = false;

	if ( !nif ) {
		clear();
		return;
	}

	needUpdateBounds = true; // Force update bounds
	resetBlockData();

	updateDataImpl( nif );

	numVerts = verts.count();

	// Validate vertex data
	normalizeVectorSize( norms, numVerts, hasVertexNormals );
	normalizeVectorSize( tangents, numVerts, hasVertexTangents );
	normalizeVectorSize( bitangents, numVerts, hasVertexBitangents );

	for ( auto & uvset : coords )
		normalizeVectorSize( uvset, numVerts, hasVertexUVs );

	// Validate triangles
	validateTriangles( triangles, triangleMap, numVerts );
	validateTriangles( stripTriangles, stripMap, numVerts );

	// Update selections
	for ( auto pSelection : selections )
		pSelection->postUpdate();

	// Sort selections from the highest level to the lowest, 
	// so the deeper a selection is within its block, the closer to the start the selection would be.
	std::sort( selections.begin(), selections.end(), []( ShapeSelectionBase * a, ShapeSelectionBase * b ) { return a->level > b->level; });

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
			if ( shaderType == QStringLiteral("BSLightingShaderProperty") )
				bslsp = bssp->cast<BSLightingShaderProperty>();
			else if ( shaderType == QStringLiteral("BSEffectShaderProperty") )
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

	// TODO: trigger update data if any of the shape's bones nodes are updated (or its sceleton root?)
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

TriangleRange * Shape::addTriangles( NifFieldConst rangeRoot, const QVector<Triangle> & tris )
{
	int iStart = triangles.count();
	triangles << tris;
	return addTriangleRange( rangeRoot, TriangleRange::FLAG_ARRAY, iStart );
}

StripRange * Shape::addStrip( NifFieldConst stripPointsRoot, const QVector<Triangle> & stripTris, NifFieldConst vertexMapField )
{
	int iStart = stripTriangles.count();
	stripTriangles << stripTris;
	return addStripRange( stripPointsRoot, TriangleRange::FLAG_ARRAY | TriangleRange::FLAG_HIGHLIGHT, iStart, vertexMapField );
}

StripRange * Shape::addStrips( NifFieldConst stripsRoot, NifSkopeFlagsType rangeFlags )
{
	if ( stripsRoot ) {
		int iStart = stripTriangles.count();
		for ( auto pointsRoot : stripsRoot.iter() )
			addStrip( pointsRoot, triangulateStrip( pointsRoot.array<TriVertexIndex>() ), NifFieldConst() );
		return addStripRange( stripsRoot, rangeFlags, iStart );
	}

	return nullptr;
}

void Shape::initSkinBones( NifFieldConst nodeMapRoot, NifFieldConst nodeListRoot, NifFieldConst block )
{
	reportCountMismatch( nodeMapRoot, nodeListRoot, block );
	int nTotalBones = std::max( nodeMapRoot.childCount(), nodeListRoot.childCount() );
	bones.reserve( nTotalBones );
	Node * root = findParent( skeletonRoot );
	for ( int bind = 0; bind < nTotalBones; bind++ ) {
		auto boneNodeLink = nodeMapRoot.child(bind).link();
		const Node * boneNode = ( root && boneNodeLink >= 0 ) ? root->findChild( boneNodeLink ) : nullptr;
		bones << SkinBone( nodeListRoot.child(bind), boneNode );
	}

	addBoneSelection( nodeMapRoot, nullptr );
	addBoneSelection( nodeListRoot, nullptr );
}

void Shape::applySkinningTransforms( const Transform & skinTransform )
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

	for ( SkinBone & bone : bones ) {
		Transform t = bone.localTransform( skinTransform, skeletonRoot ) * bone.transform;

		for ( const VertexWeight & vw : bone.vertexWeights ) {
			transVerts[vw.vertex] += t * verts[vw.vertex] * vw.weight;
			if ( hasVertexNormals )
				transNorms[vw.vertex] += t.rotation * norms[vw.vertex] * vw.weight;
			if ( hasVertexTangents )
				transTangents[vw.vertex] += t.rotation * tangents[vw.vertex] * vw.weight;
			if ( hasVertexBitangents )
				transBitangents[vw.vertex] += t.rotation * bitangents[vw.vertex] * vw.weight;
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

	iData = iExtraData = QModelIndex();

	hasVertexNormals    = false;
	hasVertexTangents   = false;
	hasVertexBitangents = false;
	hasVertexUVs        = false;
	hasVertexColors     = false;

	sRGB = false;
	
	isLOD = false;
	lodLevels.clear(); // No need for qDeleteAll here, selections cleanup will take care of it

	// Vertex data
	verts.clear();
	norms.clear();
	colors.clear();
	coords.clear();
	tangents.clear();
	bitangents.clear();

	mainVertexRoot = NifFieldConst();

	// Triangle data
	triangles.clear();
	triangleMap.clear();

	// Strip data
	stripTriangles.clear();
	stripMap.clear();

	// Skinning data
	isSkinned = false;
	iSkin = iSkinData = iSkinPart = QModelIndex();

	// Skeleton data
	skeletonRoot = 0;
	skeletonTrans = Transform();

	bones.clear();

	qDeleteAll( selections );
	selections.clear();
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

void Shape::initLodData( NifFieldConst block )
{
	static constexpr int NUM_LODS = 3;
	static const QString FIELD_NAMES[NUM_LODS] = {
		QStringLiteral("LOD0 Size"),
		QStringLiteral("LOD1 Size"),
		QStringLiteral("LOD2 Size")
	};

	isLOD = true;

	lodLevels.resize( NUM_LODS );
	const NifSkopeFlagsType RANGE_FLAGS = TriangleRange::FLAG_HIGHLIGHT;
	for ( int iLod = 0, iLodStart = 0; iLod < NUM_LODS; iLod++ ) {
		auto lodField = block[FIELD_NAMES[iLod]];
		auto nLodSize = lodField.value<uint>();
		// TODO: report if iStart + nLodSize exceeds nTriangles
		int iLodEnd = iLodStart + nLodSize;

		if ( iLodStart != 0 ) {
			// One range for rendering actual triangles...
			lodLevels[iLod] = addTriangleRange( NifFieldConst(), RANGE_FLAGS, 0, iLodEnd ); 
			// Another range for tracking selection...
			addTriangleRange( lodField, RANGE_FLAGS, iLodStart, nLodSize );
		} else {
			// Micro-optimization: one single range both for rendering actual triangles and for tracking selection...
			lodLevels[iLod] = addTriangleRange( lodField, RANGE_FLAGS, 0, iLodEnd ); 
		}

		iLodStart = iLodEnd;
	}
	// TODO: report if the lods do not cover all triangles
}


// Shape: drawSelection helpers

void Shape::drawSelection_begin( DrawSelectionMode newMode ) const
{
	if ( newMode == drawSelectionMode )
		return;

	auto getUseViewTrans = [this]( DrawSelectionMode drawMode ) -> bool {
		if ( drawMode == DrawSelectionMode::NO )
			return false;
		if ( drawMode == DrawSelectionMode::BOUND_SPHERE )
			return true;
		return transformRigid;
		};

	bool oldUseViewTrans = getUseViewTrans( drawSelectionMode );
	bool newUseViewTrans = getUseViewTrans( newMode );
	if ( oldUseViewTrans != newUseViewTrans ) {
		if ( newUseViewTrans ) {
			glPushMatrix();
			glMultMatrix( viewTrans() );
		} else {
			glPopMatrix();
		}
	}

	// Cleanup of the previous selection mode
	switch( drawSelectionMode )
	{
	case DrawSelectionMode::NO: // First call of drawSelection_begin
		glDisable( GL_LIGHTING );
		glDisable( GL_COLOR_MATERIAL );
		glDisable( GL_TEXTURE_2D );
		glDisable( GL_NORMALIZE );
		glEnable( GL_DEPTH_TEST );
		glDepthFunc( GL_LEQUAL );
		glDepthMask( GL_FALSE );
		glEnable( GL_BLEND );
		glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
		glDisable( GL_ALPHA_TEST );
		glDisable( GL_CULL_FACE );
		break;
	case DrawSelectionMode::WIREFRAME:
		glDisableClientState( GL_VERTEX_ARRAY );
		glDisable( GL_POLYGON_OFFSET_FILL );
		break;
	case DrawSelectionMode::VERTICES:
		glDisableClientState( GL_VERTEX_ARRAY );
		break;
	}

	// Init the new selection mode
	switch ( newMode )
	{
	case DrawSelectionMode::VERTICES:
		glPolygonMode( GL_FRONT_AND_BACK, GL_POINT ); // ???
		glPointSize( scene->isSelModeVertex() ? BIG_VERTEX_SIZE : SMALL_VERTEX_SIZE );
		glEnableClientState( GL_VERTEX_ARRAY );
		glVertexPointer( 3, GL_FLOAT, 0, transVerts.constData() );
		break;
	case DrawSelectionMode::VECTORS:
		glPolygonMode( GL_FRONT_AND_BACK, GL_LINE ); // ???
		glLineWidth( VECTOR_LINE_WIDTH );
		break;
	case DrawSelectionMode::WIREFRAME:
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( 0.03f, 0.03f );
		glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
		glLineWidth( WIREFRAME_LINE_WIDTH );
		glEnableClientState( GL_VERTEX_ARRAY );
		glVertexPointer( 3, GL_FLOAT, 0, transVerts.constData() );
		break;
	case DrawSelectionMode::BOUND_SPHERE:
		glPointSize( BIG_VERTEX_SIZE );
		glPolygonMode( GL_FRONT_AND_BACK, GL_LINE ); // ???
		glLineWidth( WIREFRAME_LINE_WIDTH );
		break;
	}

	drawSelectionMode = newMode;
}

static inline void drawSingleSelection_begin( const Color4 & color )
{
	// Semi-transparent highlight color + no depth test (always visible)
	glColor4f( color.red(), color.green(), color.blue(), color.alpha() * 0.5f );
	glDepthFunc( GL_ALWAYS );
}

static inline void drawSingleSelection_end()
{
	glDepthFunc( GL_LEQUAL );
}

void Shape::drawSelection_triangles() const
{
	drawSelection_begin( DrawSelectionMode::WIREFRAME );

	glNormalColor();
	glDrawTriangles( triangles );
	glDrawTriangles( stripTriangles );

	drawSelection_end();
}

void Shape::drawSelection_triangles( const TriangleRange * range ) const
{
	if ( range->realLength > 0 ) {
		drawSelection_begin( DrawSelectionMode::WIREFRAME );

		glNormalColor();
		glDrawTriangles( range );

		drawSelection_end();
	}
}

void Shape::drawSelection_trianglesHighlighted( const TriangleRange * range ) const
{
	if ( range->realLength > 0 ) {
		drawSelection_begin( DrawSelectionMode::WIREFRAME );

		glNormalColor();
		const auto & rangeTris = range->triangles();
		int iRangeEnd = range->realEnd();
		const TriangleRange * pParent = range->parentRange;
		if ( pParent ) {
			glDrawTriangles( rangeTris, pParent->realStart, range->realStart - pParent->realStart );
			glDrawTriangles( rangeTris, iRangeEnd, pParent->realEnd() - iRangeEnd );
		} else {
			glDrawTriangles( rangeTris, 0, range->realStart );
			glDrawTriangles( rangeTris, iRangeEnd, rangeTris.count() - iRangeEnd );
			glDrawTriangles( range->otherTriangles() );
		}

		glHighlightColor();
		glDrawTriangles( range );

		drawSelection_end();
	}
}

void Shape::drawSelection_triangles( const TriangleRange * partition, int iSelectedTri ) const
{
	if ( partition->realLength > 0 ) {
		drawSelection_begin( DrawSelectionMode::WIREFRAME );

		glNormalColor();
		glDrawTriangles( partition );

		if ( iSelectedTri >= partition->realStart && iSelectedTri < partition->realEnd() ) {
			glHighlightColor();
			glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
			// First pass: draw the triangle with the normal highlight color (and with cull face for single-sided shapes).
			if ( !isDoubleSided ) {
				glEnable( GL_CULL_FACE );
				glDrawTriangles( partition->triangles(), iSelectedTri, 1 );
				glDisable( GL_CULL_FACE );
			} else {
				glDrawTriangles( partition->triangles(), iSelectedTri, 1 );
			}
			// Second pass: draw the triangle with the semitransparent highlight color, w/o cull face and with no depth test.
			// This makes the selected triangle noticeable even if it is facing away from the camera or is obstructed by other triangles.
			drawSingleSelection_begin( Color4(cfg.highlight) );
			glDrawTriangles( partition->triangles(), iSelectedTri, 1 );
			drawSingleSelection_end();
		}

		drawSelection_end();
	}
}

bool Shape::drawSelection_vectors_init( VertexSelectionType type, DrawVectorsData & outData ) const
{
	if ( scene->isSelModeObject() ) {
		outData.drawNormals    = ( type == VertexSelectionType::NORMALS ) && hasVertexNormals;
		outData.drawTangents   = ( type == VertexSelectionType::TANGENTS || type == VertexSelectionType::EXTRA_TANGENTS ) && hasVertexTangents;
		outData.drawBitangents = ( type == VertexSelectionType::BITANGENTS || type == VertexSelectionType::EXTRA_TANGENTS ) && hasVertexBitangents;

		if ( outData.drawNormals || outData.drawTangents || outData.drawBitangents ) {
			outData.vectorScale = std::clamp( bounds().radius / VECTOR_SCALE_DIV, VECTOR_MIN_SCALE, VECTOR_MAX_SCALE );
			return true;
		}
	}

	return false;
}

void Shape::drawSelection_vectors( int iStart, int nLength, const DrawVectorsData & drawData ) const
{
	if ( nLength > 0 ) {
		glBegin( GL_LINES );

		auto drawVectors = [this, drawData, iStart, nLength]( const QVector<Vector3> & vectors ) {
			auto pVertices = transVerts.constData() + iStart;
			auto pVectors = vectors.constData() + iStart;
			for ( int i = 0; i < nLength; i++, pVertices++, pVectors++ ) {
				glVertex( pVertices );
				glVertex( (*pVertices) + (*pVectors) * drawData.vectorScale );
			}
		};

		if ( drawData.drawNormals )
			drawVectors( transNorms );
		if ( drawData.drawTangents )
			drawVectors( transTangents );
		if ( drawData.drawBitangents )
			drawVectors( transBitangents );

		glEnd();
	}
}

void Shape::drawSelection_vertices( VertexSelectionType type ) const
{
	if ( numVerts > 0 ) {
		drawSelection_begin( DrawSelectionMode::VERTICES );

		glNormalColor();
		glDrawArrays( GL_POINTS, 0, numVerts );

		DrawVectorsData drawData;
		if ( drawSelection_vectors_init( type, drawData ) ) {
			drawSelection_begin( DrawSelectionMode::VECTORS );
			drawSelection_vectors( 0, numVerts, drawData );
		}

		drawSelection_end();
	}
}

void Shape::drawSelection_vertices( VertexSelectionType type, int iSelectedVertex ) const
{
	if ( iSelectedVertex >= 0 && iSelectedVertex < numVerts ) {
		int iNextVertex = iSelectedVertex + 1;
		DrawVectorsData drawData;
		bool bDrawVectors = drawSelection_vectors_init( type, drawData );

		drawSelection_begin( DrawSelectionMode::VERTICES );
		glNormalColor();
		if ( iSelectedVertex > 0 )
			glDrawArrays( GL_POINTS, 0, iSelectedVertex );
		if ( iNextVertex < numVerts )
			glDrawArrays( GL_POINTS, iNextVertex, numVerts - iNextVertex );

		if ( bDrawVectors ) {
			drawSelection_begin( DrawSelectionMode::VECTORS );
			drawSelection_vectors( 0, iSelectedVertex, drawData );
			drawSelection_vectors( iNextVertex, numVerts - iNextVertex, drawData );
		}

		drawSelection_begin( DrawSelectionMode::VERTICES );
		glHighlightColor();
		glDrawArrays( GL_POINTS, iSelectedVertex, 1 );
		drawSingleSelection_begin( Color4(cfg.highlight) );
		glDrawArrays( GL_POINTS, iSelectedVertex, 1 );
		drawSingleSelection_end();

		if ( bDrawVectors ) {
			drawSelection_begin( DrawSelectionMode::VECTORS );
			glHighlightColor();
			drawSelection_vectors( iSelectedVertex, 1, drawData );
		}

		drawSelection_end();
	} else {
		drawSelection_vertices( type );
	}
}

void Shape::drawSelection_sphere( const BoundSphere & sphere, const Transform & transform, bool hightlightCenter ) const
{
	auto drawCenter = [sphere, transform]( const Color4 & color ) {
		Vector3 vc = transform * sphere.center;
		
		glColor( color );
		glBegin( GL_POINTS );
		glVertex( vc );
		glEnd();

		drawSingleSelection_begin( color );
		glBegin( GL_POINTS );
		glVertex( vc );
		glEnd();
		drawSingleSelection_end();
	};

	if ( sphere.radius > 0.01f ) {
		glColor( BOUND_SPHERE_COLOR );
		drawSphereNew( sphere.center, sphere.radius, 12, transform );
	} else if ( !hightlightCenter ) {
		drawCenter( BOUND_SPHERE_CENTER_COLOR );
	}

	if ( hightlightCenter ) {
		drawCenter( Color4(cfg.highlight) );
	}
}

void Shape::drawSelection_boundSphere( const BoundSphereSelection * selSphere, bool hightlightCenter ) const
{
	drawSelection_begin( DrawSelectionMode::WIREFRAME );
	glNormalColor();
	glDrawTriangles( triangles );
	glDrawTriangles( stripTriangles );

	drawSelection_begin( DrawSelectionMode::BOUND_SPHERE );
	if ( selSphere->absoluteTransform ) {
		glPopMatrix();

		glPushMatrix();
		glMultMatrix( scene->view * selSphere->transform );
		drawSelection_sphere( selSphere->sphere, Transform(), hightlightCenter );
		// glPopMatrix(); // leave calling glPopMatrix to drawSelection_end()
	} else {
		drawSelection_sphere( selSphere->sphere, selSphere->transform, hightlightCenter );
	}
	drawSelection_end();
}

void Shape::drawSelection_bone( const BoneSelection * selection, int iSelectedBone, bool drawBoundSphere, bool hightlightSphereCenter ) const
{
	if ( iSelectedBone < 0 || iSelectedBone >= bones.count() )
		return;
	const SkinBone & bone = bones[iSelectedBone];

	// Bones' triangles
	const TriangleRange * range = selection->triRange;
	int nTotalTris = range  ? range->realLength : ( triangles.count() + stripTriangles.count() );
	if ( nTotalTris > 0 ) {
		QVector<bool> boneVerticesMap( numVerts );
		for ( const auto & vw : bone.vertexWeights) {
			if ( vw.weight > 0.0f )
				boneVerticesMap[vw.vertex] = true;
		}

		QVector<Triangle> boneTriangles;
		boneTriangles.reserve( nTotalTris );
		QVector<Triangle> otherTriangles;
		otherTriangles.reserve( nTotalTris );

		auto regTri = [boneVerticesMap, &boneTriangles, &otherTriangles]( const Triangle & t ) {
			if ( boneVerticesMap[t.v1()] && boneVerticesMap[t.v2()] && boneVerticesMap[t.v3()] )
				boneTriangles << t;
			else
				otherTriangles << t;
			};
		if ( range ) {
			const auto & tris = range->triangles();
			for ( int tind = range->realStart, iEnd = range->realEnd(); tind < iEnd; tind++ )
				regTri( tris[tind] );
		} else {
			for ( const auto & t : triangles )
				regTri( t );
			for ( const auto & t : stripTriangles )
				regTri( t );
		}

		drawSelection_begin( DrawSelectionMode::WIREFRAME );
		glNormalColor();
		glDrawTriangles( otherTriangles );
		glHighlightColor();
		glDrawTriangles( boneTriangles );
	}

	// Bound sphere
	if ( drawBoundSphere ) {
		Transform boneT = bone.localTransform( skeletonTrans, skeletonRoot ); // * bone.transform
		drawSelection_begin( DrawSelectionMode::BOUND_SPHERE );
		drawSelection_sphere( bone.boundSphere, boneT, hightlightSphereCenter );
	}

	drawSelection_end();
}


// ShapeSelectionBase class

ShapeSelectionBase::ShapeSelectionBase( Shape * _shape, NifFieldConst _rootField, NifFieldConst _mapField )
	: shape( _shape ), rootField( _rootField ), block( _rootField.block() ), mapField( _mapField )
{
	level = rootField.ancestorLevel( block );
	_shape->selections.append( this );
}

int ShapeSelectionBase::remapIndex( int i ) const
{
	if ( !mapField )
		return i;

	auto mapEntry = mapField.child( i );
	if ( mapEntry )
		return mapEntry.value<int>();
	return -1;
}


// VertexSelection class

bool VertexSelection::process( NifFieldConst selectedField, int iSubLevel ) const
{
	switch( type )
	{
	case VertexSelectionType::VERTICES:
	case VertexSelectionType::NORMALS:
	case VertexSelectionType::TANGENTS:
	case VertexSelectionType::BITANGENTS:
	case VertexSelectionType::BS_VERTEX_DATA:
		if ( iSubLevel == 0 ) {
			shape->drawSelection_vertices( type );
			return true;
		} else { // iSubLevel > 0
			int iVertex = remapIndex( selectedField.ancestorAt( iSubLevel - 1 ).row() );
			VertexSelectionType drawType = type;

			if ( type == VertexSelectionType::BS_VERTEX_DATA && iSubLevel == 2 ) {
				if ( selectedField.hasName("Normal") )
					drawType = VertexSelectionType::NORMALS;
				else if ( selectedField.hasName("Tangent") )
					drawType = VertexSelectionType::TANGENTS;
				else if ( selectedField.hasName("Bitangent X", "Bitangent Y", "Bitangent Z") )
					drawType = VertexSelectionType::BITANGENTS;
			}

			shape->drawSelection_vertices( drawType, iVertex );
			return true;
		}
		break;
	case VertexSelectionType::EXTRA_TANGENTS:
		shape->drawSelection_vertices( type );
		return true;
	case VertexSelectionType::VERTEX_ROOT:
		if ( iSubLevel == 0 ) {
			shape->drawSelection_vertices( type );
			return true;
		}
		break;
	}

	return false;
}


// TriangleRange class

const QVector<Triangle> & TriangleRange::triangles() const
{
	return shape->triangles;
}

const QVector<int> & TriangleRange::triangleMap() const
{
	return shape->triangleMap;
}

const QVector<Triangle> & TriangleRange::otherTriangles() const
{
	return shape->stripTriangles;
}

void TriangleRange::postUpdate()
{
	const auto & triMap = triangleMap();

	int nTotalTris = triMap.count();
	int nValidTris = triangles().count();

	int iFirst = std::max( start, 0 );
	int iLast = ( nValidTris > 0 ) ? ( std::min( start + length, nTotalTris ) - 1 ) : -1;

	if ( nValidTris < nTotalTris ) {
		while ( iFirst <= iLast && triMap[iFirst] < 0 ) {
			iFirst++;
		}
		while ( iLast >= iFirst && triMap[iLast] < 0 ) {
			iLast--;
		}
	}

	if ( iFirst <= iLast ) {
		realStart  = triMap[iFirst];
		realLength = triMap[iLast] - realStart + 1;
	} else {
		realStart  = 0; // Whatever...
		realLength = 0;
	}
}

bool TriangleRange::process( NifFieldConst selectedField, int iSubLevel ) const
{
	if ( shape->scene->isSelModeObject() ) {
		if ( isArray() && iSubLevel == 1 ) {
			int iSelectedTri = triangleMap().value( start + selectedField.row(), -1 );
			shape->drawSelection_triangles( this, iSelectedTri );
			return true;
		} else if ( iSubLevel == 0 || isDeep() ) {
			if ( isHighlight() )
				shape->drawSelection_trianglesHighlighted( this );
			else
				shape->drawSelection_triangles( this );
			return true;
		}
	}

	return false;
}


// StripRange class

const QVector<Triangle> & StripRange::triangles() const
{
	return shape->stripTriangles;
}

const QVector<int> & StripRange::triangleMap() const
{
	return shape->stripMap;
}

const QVector<Triangle> & StripRange::otherTriangles() const
{
	return shape->triangles;
}

bool StripRange::process( NifFieldConst selectedField, int iSubLevel ) const
{
	if ( isArray() && iSubLevel == 1 ) {
		int iVertex = remapIndex( selectedField.value<int>() );
		shape->drawSelection_vertices( VertexSelectionType::VERTICES, iVertex );
		return true;
	} else if ( iSubLevel == 0 || isDeep() ) {
		if ( shape->scene->isSelModeObject() ) {
			if ( isHighlight() )
				shape->drawSelection_trianglesHighlighted( this );
			else
				shape->drawSelection_triangles( this );
			return true;
		}
	}

	return false;
}


// BoundSphereSelection class

bool BoundSphereSelection::process( NifFieldConst selectedField, int iSubLevel ) const
{
	if ( shape->scene->isSelModeObject() ) {
		bool highlightCenter = ( iSubLevel == 1 ) && selectedField.hasName("Center");
		shape->drawSelection_boundSphere( this, highlightCenter );
		return true;
	}

	return false;
}


// BoneSelection class

bool BoneSelection::process( NifFieldConst selectedField, int iSubLevel ) const
{
	if ( shape->scene->isSelModeObject() ) {
		if ( iSubLevel == 0 ) {
			if ( triRange )
				shape->drawSelection_triangles( triRange );
			else
				shape->drawSelection_triangles();
			return true;
		} else { // iSubLevel > 0
			int iBone = remapIndex( selectedField.ancestorAt( iSubLevel - 1 ).row() );
			bool drawBoundSphere = ( iSubLevel >= 2 && selectedField.ancestorAt( iSubLevel - 2 ).hasName("Bounding Sphere") );
			bool highlightSphereCenter = drawBoundSphere && ( iSubLevel == 3 ) && selectedField.hasName("Center");
			shape->drawSelection_bone( this, iBone, drawBoundSphere, highlightSphereCenter );
			return true;
		}
	}

	return false;
}
