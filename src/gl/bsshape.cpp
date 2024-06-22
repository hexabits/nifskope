#include "bsshape.h"

#include "gl/glnode.h"
#include "gl/glscene.h"
#include "gl/renderer.h"
#include "io/material.h"
#include "model/nifmodel.h"


void BSShape::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	Shape::updateImpl( nif, index );

	if ( index == iBlock ) {
		isLOD = nif->isNiBlock( iBlock, "BSMeshLODTriShape" );
		if ( isLOD )
			emit nif->lodSliderChanged(true);
	}
}

void BSShape::updateDataImpl( const NifModel * nif )
{
	NifFieldConst block = nif->block(iBlock);

	isSkinned = block.child("Vertex Desc").value<BSVertexDesc>().HasFlag(VertexAttribute::VA_SKINNING);
	isDynamic = block.inherits("BSDynamicTriShape");

	dataBound = BoundSphere(block);

	// Is the shape skinned?
	NifFieldConst skinBlock, skinDataBlock, skinPartBlock;
	if ( isSkinned ) {
		QString skinInstName, skinDataName;
		if ( nif->getBSVersion() >= 130 ) {
			skinInstName = "BSSkin::Instance";
			skinDataName = "BSSkin::BoneData";
		} else {
			skinInstName = "NiSkinInstance";
			skinDataName = "NiSkinData";
		}

		skinBlock = block.child("Skin").linkBlock(skinInstName);
		if ( skinBlock ) {
			iSkin = skinBlock.toIndex(); // ???
			skinDataBlock = skinBlock.child("Data").linkBlock(skinDataName);
			iSkinData = skinDataBlock.toIndex(); // ???
			if ( nif->getBSVersion() == 100 ) {
				skinPartBlock = skinBlock.child("Skin Partition").linkBlock("NiSkinPartition");
				iSkinPart = skinPartBlock.toIndex(); // ???
			}
		}
	}

	// Fill vertex data
	NifFieldConst vertexData;
	if ( skinPartBlock ) {
		// For skinned geometry, the vertex data is stored in the NiSkinPartition
		// The triangles are split up among the partitions
		vertexData = skinPartBlock.child("Vertex Data");
		int dataSize = skinPartBlock.child("Data Size").value<int>();
		int vertexSize = skinPartBlock.child("Vertex Size").value<int>();
		if ( vertexData && dataSize > 0 && vertexSize > 0 )
			numVerts = dataSize / vertexSize;
	} else {
		vertexData = block.child("Vertex Data");
		numVerts = vertexData.childCount();
	}
	iData = vertexData.toIndex(); // ???

	TexCoords coordset; // For compatibility with coords list

	QVector<Vector4> dynVerts;
	if ( isDynamic ) { // TODO: What if it is "dynamic" AND has NiSkinPartition?
		auto dynVertsRoot = block.child("Vertices");
		reportCountMismatch( vertexData, numVerts, dynVertsRoot, dynVertsRoot.childCount(), block );
		dynVerts = dynVertsRoot.array<Vector4>();
		numVerts = std::min( numVerts, dynVerts.count() );
	}

	if ( numVerts > 0 ) {
		// Pre-cache num. indices of all needed vertex fields to avoid looking them by string name for every vertex in the shape again and again.
		auto firstVertex = vertexData[0];

		int iVertexField     = firstVertex.child("Vertex").row();
		int iNormalField     = firstVertex.child("Normal").row();
		int iTangentField    = firstVertex.child("Tangent").row();
		int iBitangentXField = firstVertex.child("Bitangent X").row();
		int iBitangentYField = firstVertex.child("Bitangent Y").row();
		int iBitangentZField = firstVertex.child("Bitangent Z").row();
		int iUVField         = firstVertex.child("UV").row();
		int iColorField      = firstVertex.child("Vertex Colors").row();

		hasVertexNormals    = ( iNormalField >= 0 );
		hasVertexTangents   = ( iTangentField >= 0 );
		hasVertexBitangents = ( ( iBitangentXField >= 0 || isDynamic ) && ( iBitangentYField >= 0 ) && ( iBitangentZField >= 0 ) );
		hasVertexUVs        = ( iUVField >= 0 );
		hasVertexColors     = ( iColorField >= 0 );

		verts.reserve(numVerts);
		if ( hasVertexNormals )
			norms.reserve(numVerts);
		if ( hasVertexTangents )
			tangents.reserve(numVerts);
		if ( hasVertexBitangents )
			bitangents.reserve(numVerts);
		if ( hasVertexUVs )
			coordset.reserve(numVerts);
		if ( hasVertexColors )
			colors.reserve(numVerts);

		for ( int i = 0; i < numVerts; i++ ) {
			float bitX;
			auto vdata = vertexData[i];

			if ( isDynamic ) {
				auto & dynv = dynVerts.at(i);
				verts << Vector3( dynv );
				bitX = dynv[3];
			} else {
				verts << ( iVertexField >= 0 ? vdata[iVertexField].value<Vector3>() : Vector3() );
				bitX = ( iBitangentXField >= 0 ) ? vdata[iBitangentXField].value<float>() : 0.0f;
			}

			if ( hasVertexNormals )
				norms << vdata[iNormalField].value<ByteVector3>();
			if ( hasVertexTangents )
				tangents << vdata[iTangentField].value<ByteVector3>();
			if ( hasVertexBitangents )
				bitangents << Vector3( bitX, vdata[iBitangentYField].value<float>(), vdata[iBitangentZField].value<float>() );
			if ( hasVertexUVs )
				coordset << vdata[iUVField].value<HalfVector2>();
			if ( hasVertexColors )
				colors << vdata[iColorField].value<ByteColor4>();
		}
	}

	// Add coords as the first set of QList
	coords.append( coordset );

	// Fill triangle data
	if ( skinPartBlock ) {
		for ( auto p : skinPartBlock.child("Partitions").iter() )
			addTriangleRange( p.child("Triangles") );
	} else {
		addTriangleRange( block.child("Triangles") );
	}

	// Fill skeleton data
	if ( skinBlock ) {
		// skeletonRoot = skinBlock.child("Skeleton Root").link(); 
		skeletonRoot = 0; // Always 0

		if ( nif->getBSVersion() < 130 )
			skeletonTrans = Transform( skinDataBlock );

		initSkinBones( skinBlock.child("Bones"), skinDataBlock.child("Bone List"), block );

		// Read vertex weights from vertex data
		int nBones = bones.count();
		if ( nBones > 0 && numVerts > 0 ) {
			auto firstVertex = vertexData[0];
			int iIndicesField = firstVertex["Bone Indices"].row();
			int iWeightsField = firstVertex["Bone Weights"].row();

			if ( iIndicesField >= 0 && iWeightsField >= 0 ) {
				const int WEIGHTS_PER_VERTEX = 4;
				for ( int vind = 0; vind < numVerts; vind++ ) {
					auto vdata = vertexData[vind];
					auto vbones = vdata[iIndicesField];
					auto vweights = vdata[iWeightsField];
					if ( vbones.childCount() < WEIGHTS_PER_VERTEX || vweights.childCount() < WEIGHTS_PER_VERTEX )
						continue;

					for ( int wind = 0; wind < WEIGHTS_PER_VERTEX; wind++ ) {
						float w = vweights[wind].value<float>();
						if ( w <= 0.0f )
							continue;
						int bind = vbones[wind].value<int>();
						if ( bind >= nBones || bind < 0 ) {
							vbones[wind].reportError( tr("Invalid bone index %1").arg(bind) );
							continue;
						}
						bones[bind].vertexWeights << VertexWeight( vind, w );
					}
				}
			}
		}
	}
}

QModelIndex BSShape::vertexAt( int idx ) const
{
	auto nif = NifModel::fromIndex( iBlock );
	if ( !nif )
		return QModelIndex();

	// Vertices are on NiSkinPartition in version 100
	auto blk = iBlock;
	if ( iSkinPart.isValid() ) {
		if ( isDynamic )
			return nif->getIndex( blk, "Vertices" ).child( idx, 0 );

		blk = iSkinPart;
	}

	return nif->getIndex( nif->getIndex( blk, "Vertex Data" ).child( idx, 0 ), "Vertex" );
}

void BSShape::transformShapes()
{
	if ( isHidden() )
		return;

	auto nif = NifModel::fromValidIndex( iBlock );
	if ( !nif ) {
		clear();
		return;
	}

	Node::transformShapes();

	if ( isSkinned && bones.count() && scene->hasOption(Scene::DoSkinning) ) {
		applySkinningTransforms( scene->view );
	} else {
		applyRigidTransforms();
	}

	transColors = colors;
	if ( nif->getBSVersion() < 130 && bslsp && !bslsp->hasSF1(ShaderFlags::SLSF1_Vertex_Alpha) ) {
		for ( auto & c : transColors )
			c.setAlpha(1.0f);
	}
}

void BSShape::drawShapes( NodeList * secondPass, bool presort )
{
	if ( isHidden() )
		return;

	// TODO: Only run this if BSXFlags has "EditorMarkers present" flag
	if ( !scene->hasOption(Scene::ShowMarkers) && name.contains( "EditorMarker" ) )
		return;

	// Draw translucent meshes in second pass
	if ( secondPass && drawInSecondPass ) {
		secondPass->add( this );
		return;
	}

	glPointSize( 8.5 );

	auto nif = NifModel::fromIndex( iBlock );

	if ( Node::SELECTING ) {
		if ( scene->isSelModeObject() ) {
			int s_nodeId = ID2COLORKEY( nodeId );
			glColor4ubv( (GLubyte *)&s_nodeId );
		} else {
			glColor4f( 0, 0, 0, 1 );
		}
	}

	if ( transformRigid ) {
		glPushMatrix();
		glMultMatrix( viewTrans() );
	}

	// Render polygon fill slightly behind alpha transparency and wireframe
	glEnable( GL_POLYGON_OFFSET_FILL );
	if ( drawInSecondPass )
		glPolygonOffset( 0.5f, 1.0f );
	else
		glPolygonOffset( 1.0f, 2.0f );

	glEnableClientState( GL_VERTEX_ARRAY );
	glVertexPointer( 3, GL_FLOAT, 0, transVerts.constData() );

	if ( !Node::SELECTING ) {
		glEnableClientState( GL_NORMAL_ARRAY );
		glNormalPointer( GL_FLOAT, 0, transNorms.constData() );

		bool doVCs = ( bssp && bssp->hasSF2(ShaderFlags::SLSF2_Vertex_Colors) );
		// Always do vertex colors for FO4 if colors present
		if ( nif->getBSVersion() >= 130 && hasVertexColors && colors.count() )
			doVCs = true;

		if ( transColors.count() && scene->hasOption(Scene::DoVertexColors) && doVCs ) {
			glEnableClientState( GL_COLOR_ARRAY );
			glColorPointer( 4, GL_FLOAT, 0, transColors.constData() );
		} else if ( nif->getBSVersion() < 130 && !hasVertexColors && (bslsp && bslsp->hasVertexColors) ) {
			// Correctly blacken the mesh if SLSF2_Vertex_Colors is still on
			//	yet "Has Vertex Colors" is not.
			glColor( Color3( 0.0f, 0.0f, 0.0f ) );
		} else {
			glColor( Color3( 1.0f, 1.0f, 1.0f ) );
		}
	}

	if ( !Node::SELECTING ) {
		if ( nif->getBSVersion() >= 151 )
			glEnable( GL_FRAMEBUFFER_SRGB );
		else
			glDisable( GL_FRAMEBUFFER_SRGB );
		shader = scene->renderer->setupProgram( this, shader );
	
	} else {
		if ( nif->getBSVersion() >= 151 )
			glDisable( GL_FRAMEBUFFER_SRGB );
	}
	
	if ( isDoubleSided ) {
		glCullFace( GL_FRONT );
		glDrawElements( GL_TRIANGLES, triangles.count() * 3, GL_UNSIGNED_SHORT, triangles.constData() );
		glCullFace( GL_BACK );
	}

	if ( !isLOD ) {
		glDrawElements( GL_TRIANGLES, triangles.count() * 3, GL_UNSIGNED_SHORT, triangles.constData() );
	} else if ( triangles.count() ) {
		auto lod0 = nif->get<uint>( iBlock, "LOD0 Size" );
		auto lod1 = nif->get<uint>( iBlock, "LOD1 Size" );
		auto lod2 = nif->get<uint>( iBlock, "LOD2 Size" );

		auto lod0tris = triangles.mid( 0, lod0 );
		auto lod1tris = triangles.mid( lod0, lod1 );
		auto lod2tris = triangles.mid( lod0 + lod1, lod2 );

		// If Level2, render all
		// If Level1, also render Level0
		switch ( scene->lodLevel ) {
		case Scene::Level0:
			if ( lod2tris.count() )
				glDrawElements( GL_TRIANGLES, lod2tris.count() * 3, GL_UNSIGNED_SHORT, lod2tris.constData() );
		case Scene::Level1:
			if ( lod1tris.count() )
				glDrawElements( GL_TRIANGLES, lod1tris.count() * 3, GL_UNSIGNED_SHORT, lod1tris.constData() );
		case Scene::Level2:
		default:
			if ( lod0tris.count() )
				glDrawElements( GL_TRIANGLES, lod0tris.count() * 3, GL_UNSIGNED_SHORT, lod0tris.constData() );
			break;
		}
	}

	if ( !Node::SELECTING )
		scene->renderer->stopProgram();

	glDisableClientState( GL_VERTEX_ARRAY );
	glDisableClientState( GL_NORMAL_ARRAY );
	glDisableClientState( GL_COLOR_ARRAY );

	glDisable( GL_POLYGON_OFFSET_FILL );

	if ( scene->isSelModeVertex() ) {
		drawVerts();
	}

	if ( transformRigid )
		glPopMatrix();
}

void BSShape::drawVerts() const
{
	glDisable( GL_LIGHTING );
	glNormalColor();

	glBegin( GL_POINTS );

	for ( int i = 0; i < numVerts; i++ ) {
		if ( Node::SELECTING ) {
			int id = ID2COLORKEY( ( shapeNumber << 16 ) + i );
			glColor4ubv( (GLubyte *)&id );
		}
		glVertex( transVerts.value( i ) );
	}

	auto nif = NifModel::fromIndex( iBlock );
	if ( !nif )
		return;

	// Vertices are on NiSkinPartition in version 100
	bool selected = iBlock == scene->currentBlock;
	if ( iSkinPart.isValid() ) {
		selected |= iSkinPart == scene->currentBlock;
		selected |= isDynamic;
	}


	// Highlight selected vertex
	if ( !Node::SELECTING && selected ) {
		auto idx = scene->currentIndex;
		auto n = idx.data( Qt::DisplayRole ).toString();
		if ( n == "Vertex" || n == "Vertices" ) {
			glHighlightColor();
			glVertex( transVerts.value( idx.parent().row() ) );
		}
	}

	glEnd();
}

void BSShape::drawSelection() const
{
	glDisable(GL_FRAMEBUFFER_SRGB);
	if ( scene->hasOption(Scene::ShowNodes) )
		Node::drawSelection();

	if ( isHidden() || !scene->isSelModeObject() )
		return;

	auto idx = scene->currentIndex;
	auto blk = scene->currentBlock;

	// Is the current block extra data
	bool extraData = false;

	auto nif = NifModel::fromValidIndex(blk);
	if ( !nif )
		return;

	// Set current block name and detect if extra data
	auto blockName = nif->itemName( blk );
	if ( blockName.startsWith( "BSPackedCombined" ) )
		extraData = true;

	// Don't do anything if this block is not the current block
	//	or if there is not extra data
	if ( blk != iBlock && blk != iSkin && blk != iSkinData && blk != iSkinPart && !extraData )
		return;

	// Name of this index
	auto n = idx.data( NifSkopeDisplayRole ).toString();
	// Name of this index's parent
	auto p = idx.parent().data( NifSkopeDisplayRole ).toString();
	// Parent index
	auto pBlock = nif->getBlockIndex( nif->getParent( blk ) );

	auto push = [this] ( const Transform & t ) {	
		if ( transformRigid ) {
			glPushMatrix();
			glMultMatrix( t );
		}
	};

	auto pop = [this] () {
		if ( transformRigid )
			glPopMatrix();
	};

	push( viewTrans() );

	glDepthFunc( GL_LEQUAL );

	glDisable( GL_LIGHTING );
	glDisable( GL_COLOR_MATERIAL );
	glDisable( GL_TEXTURE_2D );
	glDisable( GL_NORMALIZE );
	glEnable( GL_DEPTH_TEST );
	glDepthMask( GL_FALSE );
	glEnable( GL_BLEND );
	glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	glDisable( GL_ALPHA_TEST );

	glDisable( GL_CULL_FACE );

	// TODO: User Settings
	GLfloat lineWidth = 1.5;
	GLfloat pointSize = 5.0;

	glLineWidth( lineWidth );
	glPointSize( pointSize );

	glNormalColor();

	glEnable( GL_POLYGON_OFFSET_FILL );
	glPolygonOffset( -1.0f, -2.0f );

	float normalScale = bounds().radius / 20;
	normalScale /= 2.0f;

	if ( normalScale < 0.1f )
		normalScale = 0.1f;

	

	// Draw All Verts lambda
	auto allv = [this]( float size ) {
		glPointSize( size );
		glBegin( GL_POINTS );

		for ( int j = 0; j < transVerts.count(); j++ )
			glVertex( transVerts.value( j ) );

		glEnd();
	};

	if ( n == "Bounding Sphere" && !extraData ) {
		auto sph = BoundSphere( nif, idx );
		if ( sph.radius > 0.0 ) {
			glColor4f( 1, 1, 1, 0.33f );
			drawSphereSimple( sph.center, sph.radius, 72 );
		}
	}
	
	if ( blockName.startsWith( "BSPackedCombined" ) && pBlock == iBlock ) {
		QVector<QModelIndex> idxs;
		if ( n == "Bounding Sphere" ) {
			idxs += idx;
		} else if ( n.startsWith( "BSPackedCombined" ) ) {
			auto data = nif->getIndex( idx, "Object Data" );
			int dataCt = nif->rowCount( data );

			for ( int i = 0; i < dataCt; i++ ) {
				auto d = data.child( i, 0 );

				auto c = nif->getIndex( d, "Combined" );
				int cCt = nif->rowCount( c );

				for ( int j = 0; j < cCt; j++ ) {
					idxs += nif->getIndex( c.child( j, 0 ), "Bounding Sphere" );
				}
			}
		}

		if ( !idxs.count() ) {
			glPopMatrix();
			return;
		}

		Vector3 pTrans = nif->get<Vector3>( pBlock.child( 1, 0 ), "Translation" );
		auto iBSphere = nif->getIndex( pBlock, "Bounding Sphere" );
		Vector3 pbvC = nif->get<Vector3>( iBSphere.child( 0, 2 ) );
		float pbvR = nif->get<float>( iBSphere.child( 1, 2 ) );

		if ( pbvR > 0.0 ) {
			glColor4f( 0, 1, 0, 0.33f );
			drawSphereSimple( pbvC, pbvR, 72 );
		}

		glPopMatrix();

		for ( auto i : idxs ) {
			// Transform compound
			auto iTrans = i.parent().child( 1, 0 );
			Matrix mat = nif->get<Matrix>( iTrans, "Rotation" );
			//auto trans = nif->get<Vector3>( iTrans, "Translation" );
			float scale = nif->get<float>( iTrans, "Scale" );

			Vector3 bvC = nif->get<Vector3>( i, "Center" );
			float bvR = nif->get<float>( i, "Radius" );

			Transform t;
			t.rotation = mat.inverted();
			t.translation = bvC;
			t.scale = scale;

			glPushMatrix();
			glMultMatrix( scene->view * t );

			if ( bvR > 0.0 ) {
				glColor4f( 1, 1, 1, 0.33f );
				drawSphereSimple( Vector3( 0, 0, 0 ), bvR, 72 );
			}

			glPopMatrix();
		}

		glPushMatrix();
		glMultMatrix( viewTrans() );
	}

	if ( n == "Vertex Data" || n == "Vertex" || n == "Vertices" ) {
		allv( 5.0 );

		int s = -1;
		if ( (n == "Vertex Data" && p == "Vertex Data")
			 || (n == "Vertices" && p == "Vertices") ) {
			s = idx.row();
		} else if ( n == "Vertex" ) {
			s = idx.parent().row();
		}

		if ( s >= 0 ) {
			glPointSize( 10 );
			glDepthFunc( GL_ALWAYS );
			glHighlightColor();
			glBegin( GL_POINTS );
			glVertex( transVerts.value( s ) );
			glEnd();
		}
	} 
	
	glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );

	// Draw Lines lambda
	auto lines = [this, &normalScale, &allv, &lineWidth]( const QVector<Vector3> & v ) {
		allv( 7.0 );

		int s = scene->currentIndex.parent().row();
		glBegin( GL_LINES );

		for ( int j = 0; j < transVerts.count() && j < v.count(); j++ ) {
			glVertex( transVerts.value( j ) );
			glVertex( transVerts.value( j ) + v.value( j ) * normalScale * 2 );
			glVertex( transVerts.value( j ) );
			glVertex( transVerts.value( j ) - v.value( j ) * normalScale / 2 );
		}

		glEnd();

		if ( s >= 0 ) {
			glDepthFunc( GL_ALWAYS );
			glHighlightColor();
			glLineWidth( 3.0 );
			glBegin( GL_LINES );
			glVertex( transVerts.value( s ) );
			glVertex( transVerts.value( s ) + v.value( s ) * normalScale * 2 );
			glVertex( transVerts.value( s ) );
			glVertex( transVerts.value( s ) - v.value( s ) * normalScale / 2 );
			glEnd();
			glLineWidth( lineWidth );
		}
	};
	
	// Draw Normals
	if ( n.contains( "Normal" ) ) {
		lines( transNorms );
	}

	// Draw Tangents
	if ( n.contains( "Tangent" ) ) {
		lines( transTangents );
	}

	// Draw Triangles
	if ( n == "Triangles" ) {
		int s = scene->currentIndex.row();
		if ( s >= 0 ) {
			glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
			glHighlightColor();

			Triangle tri = triangles.value( s );
			glBegin( GL_TRIANGLES );
			glVertex( transVerts.value( tri.v1() ) );
			glVertex( transVerts.value( tri.v2() ) );
			glVertex( transVerts.value( tri.v3() ) );
			glEnd();
			glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
		}
	}

	// Draw Segments/Subsegments
	if ( n == "Segment" || n == "Sub Segment" || n == "Num Primitives" ) {
		auto sidx = idx;
		int s;

		QVector<QColor> cols = { { 255, 0, 0, 128 }, { 0, 255, 0, 128 }, { 0, 0, 255, 128 }, { 255, 255, 0, 128 },
								{ 0, 255, 255, 128 }, { 255, 0, 255, 128 }, { 255, 255, 255, 128 } 
		};

		glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );

		auto type = idx.sibling( idx.row(), 1 ).data( Qt::DisplayRole ).toString();

		bool isSegmentArray = (n == "Segment" && type == "BSGeometrySegmentData" && nif->isArray( idx ));
		bool isSegmentItem = (n == "Segment" && type == "BSGeometrySegmentData" && !nif->isArray( idx ));
		bool isSubSegArray = (n == "Sub Segment" && nif->isArray( idx ));

		int off = 0;
		int cnt = 0;
		int numRec = 0;

		int o = 0;
		if ( isSegmentItem || isSegmentArray ) {
			o = 3; // Offset 3 rows for < 130 BSGeometrySegmentData
		} else if ( isSubSegArray ) {
			o = -3; // Look 3 rows above for Sub Seg Array info
		}

		int maxTris = triangles.count();

		int loopNum = 1;
		if ( isSegmentArray )
			loopNum = nif->rowCount( idx );

		for ( int l = 0; l < loopNum; l++ ) {

			if ( n != "Num Primitives" && !isSubSegArray && !isSegmentArray ) {
				sidx = idx.child( 1, 0 );
			} else if ( isSegmentArray ) {
				sidx = idx.child( l, 0 ).child( 1, 0 );
			}
			s = sidx.row() + o;

			off = sidx.sibling( s - 1, 2 ).data().toInt() / 3;
			cnt = sidx.sibling( s, 2 ).data().toInt();
			numRec = sidx.sibling( s + 2, 2 ).data().toInt();

			auto recs = sidx.sibling( s + 3, 0 );
			for ( int i = 0; i < numRec; i++ ) {
				auto subrec = recs.child( i, 0 );
				int o = 0;
				if ( subrec.data( Qt::DisplayRole ).toString() != "Sub Segment" )
					o = 3; // Offset 3 rows for < 130 BSGeometrySegmentData

				auto suboff = subrec.child( o, 2 ).data().toInt() / 3;
				auto subcnt = subrec.child( o + 1, 2 ).data().toInt();

				for ( int j = suboff; j < subcnt + suboff; j++ ) {
					if ( j >= maxTris )
						continue;

					glColor( Color4( cols.value( i % 7 ) ) );
					Triangle tri = triangles[j];
					glBegin( GL_TRIANGLES );
					glVertex( transVerts.value( tri.v1() ) );
					glVertex( transVerts.value( tri.v2() ) );
					glVertex( transVerts.value( tri.v3() ) );
					glEnd();
				}
			}

			// Sub-segmentless Segments
			if ( numRec == 0 && cnt > 0 ) {
				glColor( Color4( cols.value( (idx.row() + l) % 7 ) ) );

				for ( int i = off; i < cnt + off; i++ ) {
					if ( i >= maxTris )
						continue;
			
					Triangle tri = triangles[i];
					glBegin( GL_TRIANGLES );
					glVertex( transVerts.value( tri.v1() ) );
					glVertex( transVerts.value( tri.v2() ) );
					glVertex( transVerts.value( tri.v3() ) );
					glEnd();
				}
			}
		}

		pop();
		return;
	}

	// Draw all bones' bounding spheres
	if ( n == "NiSkinData" || n == "BSSkin::BoneData" ) {
		// Get shape block
		if ( nif->getBlockIndex( nif->getParent( nif->getParent( blk ) ) ) == iBlock ) {
			auto iBones = nif->getIndex( blk, "Bone List" );
			int ct = nif->rowCount( iBones );

			for ( int i = 0; i < ct; i++ ) {
				auto b = iBones.child( i, 0 );
				boneSphere( nif, b );
			}
		}
		pop();
		return;
	}

	// Draw bone bounding sphere
	if ( n == "Bone List" ) {
		if ( nif->isArray( idx ) ) {
			for ( int i = 0; i < nif->rowCount( idx ); i++ )
				boneSphere( nif, idx.child( i, 0 ) );
		} else {
			boneSphere( nif, idx );
		}
	}

	// General wireframe
	if ( blk == iBlock && idx != iData && p != "Vertex Data" && p != "Vertices" ) {
		glLineWidth( 1.6f );
		glNormalColor();
		for ( const Triangle& tri : triangles ) {
			glBegin( GL_TRIANGLES );
			glVertex( transVerts.value( tri.v1() ) );
			glVertex( transVerts.value( tri.v2() ) );
			glVertex( transVerts.value( tri.v3() ) );
			glEnd();
		}
	}

	glDisable( GL_POLYGON_OFFSET_FILL );

	pop();
}

BoundSphere BSShape::bounds() const
{
	if ( needUpdateBounds ) {
		needUpdateBounds = false;
		if ( verts.count() ) {
			boundSphere = BoundSphere( verts );
		} else {
			boundSphere = dataBound;
		}
	}

	return worldTrans() * boundSphere;
}
